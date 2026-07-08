#include "filetransferhandler.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include <QTimer>
#include <QCryptographicHash>
#include <QSet>

FileTransferHandler::FileTransferHandler(QObject *parent)
    : QObject(parent), m_nextSessionId(1), m_chunkSize(64 * 1024) // 64KB
{
}

FileTransferHandler::~FileTransferHandler()
{
    // 清理发送会话
    for (auto it = m_sendSessions.begin(); it != m_sendSessions.end(); ++it) {
        SendSession *session = it.value();
        if (session->file) {
            if (session->file->isOpen()) session->file->close();
            delete session->file;
        }
        delete session;
    }
    m_sendSessions.clear();

    // 清理接收会话
    for (auto it = m_recvSessions.begin(); it != m_recvSessions.end(); ++it) {
        RecvSession *session = it.value();
        if (session->file) {
            if (session->file->isOpen()) session->file->close();
            delete session->file;
        }
        delete session;
    }
    m_recvSessions.clear();
}

QString FileTransferHandler::generateTransferId(int shipId)
{
    return QString("sat_%1_%2_%3")
        .arg(shipId)
        .arg(QDateTime::currentSecsSinceEpoch())
        .arg(QRandomGenerator::global()->bounded(10000));
}

QString FileTransferHandler::startSendFile(int shipId, const QString &filePath)
{
    QFileInfo info(filePath);
    if (!info.exists()) {
        emit logMessage(QString("文件传输: 文件不存在 %1").arg(filePath), "red");
        return QString();
    }

    // 先计算 MD5（在打开文件之前，用独立句柄）
    QString md5 = computeFileMD5(filePath);
    QString transferId = generateTransferId(shipId);

    // 若已有同一 shipId 的会话，先清理
    if (m_sendSessions.contains(shipId)) {
        SendSession *old = m_sendSessions[shipId];
        if (old->file) {
            if (old->file->isOpen()) old->file->close();
            delete old->file;
        }
        delete old;
        m_sendSessions.remove(shipId);
    }

    // 分配新会话（堆上分配，避免拷贝问题）
    SendSession *session = new SendSession;
    session->shipId = shipId;
    session->sessionId = m_nextSessionId++;
    session->fileName = info.fileName();
    session->filePath = filePath;
    session->file = new QFile(filePath);
    if (!session->file->open(QIODevice::ReadOnly)) {
        emit logMessage(QString("文件传输: 无法打开文件 %1").arg(filePath), "red");
        delete session->file;
        delete session;
        return QString();
    }
    session->totalSize = info.size();
    session->chunkSize = m_chunkSize;
    session->totalChunks = (session->totalSize + m_chunkSize - 1) / m_chunkSize;
    session->sentChunks = 0;
    session->transferId = transferId;
    session->md5 = md5;

    m_sendSessions[shipId] = session;

    // 记录到卫星数据库
    SatelliteDB::instance().addTransferRecord(
        transferId, shipId, session->fileName, session->totalSize,
        md5, m_chunkSize, "download");

    emit logMessage(QString("文件传输: 开始发送文件 %1 到船舶 %2 (大小=%3 分片=%4 MD5=%5)")
                        .arg(session->fileName).arg(shipId)
                        .arg(session->totalSize).arg(session->totalChunks).arg(md5),
                    "blue");

    // 立即发送第一个分片
    sendNextChunk(shipId);
    return transferId;
}

void FileTransferHandler::sendNextChunk(int shipId)
{
    if (!m_sendSessions.contains(shipId)) return;
    SendSession *session = m_sendSessions[shipId];

    if (session->sentChunks >= session->totalChunks) {
        // 发送完成
        session->file->close();
        // 更新数据库
        SatelliteDB::instance().completeTransfer(session->transferId, session->filePath, session->md5);
        emit sendComplete(session->fileName, shipId, true);
        emit logMessage(QString("文件传输: 发送完成 %1 -> 船舶 %2")
                            .arg(session->fileName).arg(shipId), "green");
        delete session->file;
        delete session;
        m_sendSessions.remove(shipId);
        return;
    }

    qint64 pos = session->sentChunks * session->chunkSize;
    session->file->seek(pos);
    QByteArray data = session->file->read(session->chunkSize);
    bool isLast = (session->sentChunks + 1 >= session->totalChunks);

    if (m_sendCallback) {
        m_sendCallback(shipId, session->sessionId, data, session->sentChunks, isLast);
    }

    session->sentChunks++;
    int percent = static_cast<int>(session->sentChunks * 100 / session->totalChunks);
    emit sendProgress(session->fileName, shipId, percent);

    // 继续发送下一个分片（通过 QTimer 延迟避免阻塞）
    QTimer::singleShot(100, this, [this, shipId]() { sendNextChunk(shipId); });
}

bool FileTransferHandler::receiveChunk(int shipId, uint8_t sessionId, uint32_t fragmentNum,
                                        const QByteArray &data, bool isLast,
                                        bool &isComplete, QString &errorMsg)
{
    isComplete = false;
    QString transferId = QString("%1_%2").arg(shipId).arg(sessionId);

    RecvSession *session = nullptr;
    if (m_recvSessions.contains(transferId)) {
        session = m_recvSessions[transferId];
    } else {
        // 新会话：第一个分片必须是 fragment 0
        if (fragmentNum != 0) {
            errorMsg = QString("First chunk must be fragment 0, got %1").arg(fragmentNum);
            return false;
        }
        RecvSession *newSession = new RecvSession;
        newSession->shipId = shipId;
        newSession->sessionId = sessionId;
        newSession->transferId = transferId;
        newSession->fileName = QString("uplink_%1_%2.dat").arg(shipId).arg(sessionId);
        newSession->tempPath = QDir::tempPath() + "/" + newSession->fileName;
        newSession->file = new QFile(newSession->tempPath);
        if (!newSession->file->open(QIODevice::WriteOnly)) {
            errorMsg = "Cannot create temp file: " + newSession->tempPath;
            delete newSession->file;
            delete newSession;
            return false;
        }
        newSession->receivedChunks = 0;
        newSession->totalChunks = 0; // 未知，由最后一个分片确定
        m_recvSessions[transferId] = newSession;
        session = newSession;

        // 记录到卫星数据库
        SatelliteDB::instance().addTransferRecord(
            transferId, shipId, session->fileName, 0, "", m_chunkSize, "upload");

        emit logMessage(QString("文件传输: 开始接收上行文件 船舶=%1 会话=%2")
                            .arg(shipId).arg(sessionId), "blue");
    }

    // 检查重复分片
    if (session->receivedChunkSet.contains(fragmentNum)) {
        emit logMessage(QString("文件传输: 重复分片 %1，跳过").arg(fragmentNum), "gray");
        if (isLast) isComplete = true;
        return true;
    }

    // 写入数据
    qint64 offset = static_cast<qint64>(fragmentNum) * m_chunkSize;
    session->file->seek(offset);
    session->file->write(data);
    session->file->flush();

    session->receivedChunkSet.insert(fragmentNum);
    session->receivedChunks++;

    // 记录到卫星数据库（断点续传支持）
    SatelliteDB::instance().addReceivedChunk(transferId, fragmentNum);
    SatelliteDB::instance().updateTransferProgress(transferId, session->receivedChunks);

    if (isLast) {
        session->totalChunks = fragmentNum + 1;
    }

    int percent = session->totalChunks > 0
        ? static_cast<int>(session->receivedChunks * 100 / session->totalChunks) : 0;
    emit receiveProgress(transferId, percent);

    if (isLast) {
        session->file->close();
        // 计算 MD5
        QString actualMD5 = computeFileMD5(session->tempPath);
        // 重命名为最终文件名
        QString finalPath = QString("./received_files/%1").arg(session->fileName);
        QDir().mkpath("./received_files");
        QFile::remove(finalPath);
        QFile::rename(session->tempPath, finalPath);

        qint64 finalSize = QFileInfo(finalPath).size();
        // 更新数据库为完成状态
        SatelliteDB::instance().completeTransfer(transferId, finalPath, actualMD5);
        emit receiveComplete(finalPath, actualMD5);
        emit logMessage(QString("文件传输: 接收完成 %1 (大小=%2 MD5=%3)")
                            .arg(finalPath).arg(finalSize).arg(actualMD5), "green");

        delete session->file;
        delete session;
        m_recvSessions.remove(transferId);
        isComplete = true;
    }

    return true;
}

QString FileTransferHandler::getReceivedFilePath(const QString &transferId) const
{
    return QString(); // 简单实现
}

QString FileTransferHandler::computeFileMD5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "unknown";
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    file.close();
    return hash.result().toHex();
}
