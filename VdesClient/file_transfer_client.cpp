#include "file_transfer_client.h"
#include "localdb.h"
#include "vdes_messages.pb.h"
#include <QFileInfo>
#include <QDebug>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>

FileTransferClient::FileTransferClient(TcpClient *tcpClient, QObject *parent)
    : QObject(parent)
    , m_tcpClient(tcpClient)
    , m_currentFile()
    , m_currentSession(nullptr)
    , m_chunkSize(64 * 1024)
    , m_uploadTimer(new QTimer(this))
    , m_uploading(false)
{
    m_uploadTimer->setSingleShot(false);
    connect(m_uploadTimer, &QTimer::timeout, this, &FileTransferClient::onUploadTimer);
}

FileTransferClient::~FileTransferClient()
{
    m_uploadTimer->stop();

    // 清理下载会话
    for (auto it = m_downloadSessions.begin(); it != m_downloadSessions.end(); ++it) {
        delete it.value();
    }
    m_downloadSessions.clear();

    for (auto it = m_downloadFiles.begin(); it != m_downloadFiles.end(); ++it) {
        if (it.value()) {
            if (it.value()->isOpen()) it.value()->close();
            delete it.value();
        }
    }
    m_downloadFiles.clear();

    if (m_currentSession) {
        delete m_currentSession;
    }
}

QString FileTransferClient::computeMD5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "unknown";
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    file.close();
    return hash.result().toHex();
}

QString FileTransferClient::computeSHA256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "unknown";
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();
    return hash.result().toHex();
}

bool FileTransferClient::verifyIntegrity(const QString &filePath,
                                          const QString &expectedMD5,
                                          const QString &expectedSHA256)
{
    if (expectedMD5 != "unknown" && !expectedMD5.isEmpty()) {
        QString actualMD5 = computeMD5(filePath);
        if (actualMD5 != expectedMD5) {
            qWarning() << "[FileTransfer] MD5 mismatch: expected" << expectedMD5 << "got" << actualMD5;
            return false;
        }
    }
    if (expectedSHA256 != "unknown" && !expectedSHA256.isEmpty()) {
        QString actualSHA256 = computeSHA256(filePath);
        if (actualSHA256 != expectedSHA256) {
            qWarning() << "[FileTransfer] SHA256 mismatch: expected" << expectedSHA256 << "got" << actualSHA256;
            return false;
        }
    }
    return true;
}

void FileTransferClient::uploadFile(quint32 destShipId, const QString &filePath)
{
    if (m_uploading) {
        emit uploadComplete(filePath, false, "已有上传任务在进行");
        return;
    }
    startUpload(destShipId, filePath);
}

void FileTransferClient::startUpload(quint32 destShipId, const QString &filePath)
{
    QFileInfo info(filePath);
    if (!info.exists()) {
        emit uploadComplete(filePath, false, "文件不存在");
        return;
    }

    // 计算 MD5 和 SHA-256
    QString md5 = computeMD5(filePath);
    QString sha256 = computeSHA256(filePath);
    QString fileName = info.fileName();
    quint64 fileSize = info.size();

    // 分配传输会话
    if (m_currentSession) delete m_currentSession;
    m_currentSession = new TransferSession();

    QString transferId = QString("%1_%2_%3_%4")
                             .arg(m_tcpClient->myMmsi())
                             .arg(destShipId)
                             .arg(QDateTime::currentSecsSinceEpoch())
                             .arg(QRandomGenerator::global()->bounded(10000));

    m_currentSession->transferId = transferId;
    m_currentSession->fileName = fileName;
    m_currentSession->filePath = filePath;
    m_currentSession->fileSize = fileSize;
    m_currentSession->md5 = md5;
    m_currentSession->sha256 = sha256;
    m_currentSession->chunkSize = m_chunkSize;
    m_currentSession->totalChunks = (fileSize + m_chunkSize - 1) / m_chunkSize;
    m_currentSession->transferredChunks = 0;
    m_currentSession->destShipId = destShipId;
    m_currentSession->direction = TransferDirection::Upload;
    m_currentSession->active = true;

    // 存储到本地数据库（用于断点续传）
    LocalDB::instance().addTransferRecord(transferId, fileName, fileSize, md5, m_chunkSize);

    // 发送文件传输请求
    vdes::ClientRequest req;
    auto *ftReq = req.mutable_file_transfer_req();
    ftReq->set_dest_ship_id(destShipId);
    ftReq->set_file_name(fileName.toStdString());
    ftReq->set_file_size(fileSize);
    ftReq->set_md5(md5.toStdString());
    ftReq->set_chunk_size(m_chunkSize);
    m_tcpClient->sendRequest(req);

    // 打开文件
    m_currentFile.reset(new QFile(filePath));
    if (!m_currentFile->open(QIODevice::ReadOnly)) {
        emit uploadComplete(fileName, false, "无法打开文件");
        m_currentFile.reset();
        return;
    }

    m_uploading = true;
    m_uploadTimer->start(50);

    qDebug() << "[FileTransfer] Upload started:" << fileName
             << "size:" << fileSize << "chunks:" << m_currentSession->totalChunks
             << "MD5:" << md5 << "SHA256:" << sha256;
}

void FileTransferClient::sendNextChunk()
{
    if (!m_uploading || !m_currentSession || !m_currentSession->active) {
        return;
    }

    if (m_currentFile.isNull() || !m_currentFile->isOpen()) {
        return;
    }

    qint64 pos = static_cast<qint64>(m_currentSession->transferredChunks) * m_chunkSize;
    if (!m_currentFile->seek(pos)) {
        m_uploadTimer->stop();
        m_uploading = false;
        m_currentSession->active = false;
        emit uploadComplete(m_currentSession->fileName, false, "文件寻址失败");
        LocalDB::instance().removeTransferRecord(m_currentSession->transferId);
        m_currentFile.reset();
        return;
    }

    QByteArray data = m_currentFile->read(m_chunkSize);
    if (data.isEmpty()) {
        // 文件结束
        m_uploadTimer->stop();
        m_uploading = false;
        m_currentSession->active = false;
        m_currentFile.reset();

        emit uploadComplete(m_currentSession->fileName, true, "");
        LocalDB::instance().removeTransferRecord(m_currentSession->transferId);

        if (m_currentSession) {
            delete m_currentSession;
            m_currentSession = nullptr;
        }
        return;
    }

    vdes::ClientRequest req;
    auto *chunk = req.mutable_file_chunk();
    chunk->set_file_id(m_currentSession->transferId.toStdString());
    chunk->set_chunk_index(m_currentSession->transferredChunks);
    chunk->set_data(data.constData(), data.size());
    chunk->set_is_last((m_currentSession->transferredChunks + 1) >= m_currentSession->totalChunks);
    m_tcpClient->sendRequest(req);

    m_currentSession->transferredChunks++;
    LocalDB::instance().updateTransferProgress(m_currentSession->transferId,
                                                m_currentSession->transferredChunks);

    emit uploadProgress(m_currentSession->fileName, m_currentSession->percent());
}

void FileTransferClient::onUploadTimer()
{
    sendNextChunk();
}

void FileTransferClient::resumeUpload(const QString &transferId)
{
    quint32 completedChunks = 0;
    quint32 totalChunks = 0;
    if (!LocalDB::instance().getTransferProgress(transferId, completedChunks, totalChunks)) {
        emit uploadComplete(transferId, false, "未找到断点续传记录");
        return;
    }

    if (m_currentSession) {
        m_currentSession->transferredChunks = completedChunks;
        m_currentFile->seek(completedChunks * m_chunkSize);
        m_uploading = true;
        m_uploadTimer->start(50);

        qDebug() << "[FileTransfer] Resumed upload:" << transferId
                 << "from chunk" << completedChunks;
    }
}

void FileTransferClient::handleFileChunk(const QString &fileId, quint32 chunkIndex,
                                          const QByteArray &data, bool isLast)
{
    // 获取或创建下载会话
    TransferSession *session = m_downloadSessions.value(fileId, nullptr);
    if (!session) {
        // 首个分片：创建新会话
        session = new TransferSession();
        session->transferId = fileId;
        session->fileName = fileId + "_received.dat";
        session->filePath = QDir::tempPath() + "/" + session->fileName;
        session->direction = TransferDirection::Download;
        session->active = true;
        session->chunkSize = m_chunkSize;
        session->transferredChunks = 0;
        session->totalChunks = 0;
        m_downloadSessions[fileId] = session;

        QFile *file = new QFile(session->filePath);
        if (file->open(QIODevice::WriteOnly)) {
            m_downloadFiles[fileId] = file;
        } else {
            delete file;
            return;
        }

        qDebug() << "[FileTransfer] Download started:" << fileId;
    }

    QFile *file = m_downloadFiles.value(fileId, nullptr);
    if (!file || !file->isOpen()) return;

    // 检查重复分片
    if (session->receivedChunks.contains(chunkIndex)) {
        qDebug() << "[FileTransfer] Duplicate chunk" << chunkIndex << "for" << fileId;
        if (isLast) {
            session->totalChunks = chunkIndex + 1;
        }
        return;
    }

    // 写入数据
    qint64 offset = static_cast<qint64>(chunkIndex) * m_chunkSize;
    file->seek(offset);
    file->write(data);
    file->flush();

    session->receivedChunks.insert(chunkIndex);
    session->transferredChunks = session->receivedChunks.size();

    if (isLast) {
        session->totalChunks = chunkIndex + 1;
    }

    emit downloadProgress(session->fileName, session->percent());

    if (isLast) {
        file->close();

        // 检查是否需要生成最终文件名
        QString finalPath = session->filePath;

        // 验证文件完整性（MD5校验在服务端完成，客户端可选验证）
        bool integrityOk = true;
        if (!session->md5.isEmpty() && session->md5 != "unknown") {
            integrityOk = verifyIntegrity(finalPath, session->md5, session->sha256);
        }

        if (integrityOk) {
            // 检查是否为图片，发送预览信号
            QMimeDatabase mimeDb;
            QString mimeType = mimeDb.mimeTypeForFile(finalPath).name();
            if (mimeType.startsWith("image/")) {
                emit imageFileReceived(finalPath);
            }

            emit downloadComplete(finalPath, session->md5, session->sha256, true);
        } else {
            emit downloadComplete(finalPath, session->md5, session->sha256, false);
        }

        // 清理
        m_downloadFiles.remove(fileId);
        m_downloadSessions.remove(fileId);
        delete file;
        delete session;

        qDebug() << "[FileTransfer] Download complete:" << fileId;
    }
}

void FileTransferClient::handleFileStatus(const QString &transferId, quint32 completedChunks,
                                           quint32 totalChunks, const QString &status,
                                           const QString &errorMsg)
{
    if (status == "completed") {
        m_uploadTimer->stop();
        m_uploading = false;
        if (m_currentSession) {
            m_currentSession->active = false;
            emit uploadComplete(m_currentSession->fileName, true, "");
            LocalDB::instance().removeTransferRecord(transferId);
            delete m_currentSession;
            m_currentSession = nullptr;
        }
        m_currentFile.reset();
    } else if (status == "failed") {
        m_uploadTimer->stop();
        m_uploading = false;
        if (m_currentSession) {
            m_currentSession->active = false;
            emit uploadComplete(m_currentSession->fileName, false, errorMsg);
        }
        m_currentFile.reset();
    } else if (status == "transferring") {
        if (m_currentSession) {
            m_currentSession->transferredChunks = completedChunks;
            m_currentSession->totalChunks = totalChunks;
            emit uploadProgress(m_currentSession->fileName, m_currentSession->percent());
        }
    }
}

void FileTransferClient::resumeDownload(const QString &transferId)
{
    // 断点续传下载：向服务端请求缺失的分片
    // 由客户端发起 ResumeRequest
    vdes::ClientRequest req;
    auto *resume = req.mutable_resume_req();
    resume->set_transfer_id(transferId.toStdString());
    resume->set_last_received_chunk(m_downloadSessions.contains(transferId)
        ? m_downloadSessions[transferId]->transferredChunks : 0);
    m_tcpClient->sendRequest(req);
}

const TransferSession* FileTransferClient::getSession(const QString &transferId) const
{
    if (m_currentSession && m_currentSession->transferId == transferId)
        return m_currentSession;
    return m_downloadSessions.value(transferId, nullptr);
}
