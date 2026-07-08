#ifndef FILETRANSFERHANDLER_H
#define FILETRANSFERHANDLER_H

#include <QObject>
#include <QFile>
#include <QMap>
#include <QByteArray>
#include <QCryptographicHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <functional>
#include "satellite_db.h"

// 卫星端文件传输处理器
// 职责：
//   1. 接收上行文件分片（来自船舶的上行寻址消息）
//   2. 通过 DownlinkManager 向船舶发送文件
//   3. 管理文件会话和进度
class FileTransferHandler : public QObject
{
    Q_OBJECT
public:
    explicit FileTransferHandler(QObject *parent = nullptr);
    ~FileTransferHandler();

    // 开始发送文件到指定船舶
    // 返回 transferId
    QString startSendFile(int shipId, const QString &filePath);

    // 接收上行文件分片
    // 返回 true 表示分片已处理，isComplete 表示文件接收完成
    bool receiveChunk(int shipId, uint8_t sessionId, uint32_t fragmentNum,
                      const QByteArray &data, bool isLast,
                      bool &isComplete, QString &errorMsg);

    // 设置发送回调（通过 DownlinkManager 发分片）
    void setSendCallback(std::function<void(int shipId, uint8_t sessionId,
                                            const QByteArray &data,
                                            uint32_t fragmentNum, bool isLast)> cb) {
        m_sendCallback = cb;
    }

    // 获取接收文件的存储路径
    QString getReceivedFilePath(const QString &transferId) const;

signals:
    void sendProgress(const QString &fileName, int shipId, int percent);
    void sendComplete(const QString &fileName, int shipId, bool success);
    void receiveProgress(const QString &transferId, int percent);
    void receiveComplete(const QString &filePath, const QString &md5);
    void logMessage(const QString &text, const QString &color = "black");

private:
    QString generateTransferId(int shipId);
    void sendNextChunk(int shipId);
    QString computeFileMD5(const QString &filePath);

    // 发送会话（存指针避免 QFile 不可拷贝问题）
    struct SendSession {
        int shipId;
        uint8_t sessionId;
        QString fileName;
        QString filePath;
        QFile *file;           // 裸指针，手动管理生命周期
        qint64 totalSize;
        quint32 chunkSize;
        quint32 totalChunks;
        quint32 sentChunks;
        QString transferId;
        QString md5;
    };

    // 接收会话
    struct RecvSession {
        int shipId;
        uint8_t sessionId;
        QString transferId;
        QString fileName;
        QString tempPath;
        QFile *file;           // 裸指针，手动管理生命周期
        qint64 expectedSize;
        quint32 totalChunks;
        quint32 receivedChunks;
        QSet<quint32> receivedChunkSet;
        QString expectedMD5;
    };

    QMap<int, SendSession*> m_sendSessions;     // key = shipId (存指针)
    QMap<QString, RecvSession*> m_recvSessions; // key = transferId (存指针)
    uint8_t m_nextSessionId;
    quint32 m_chunkSize;

    std::function<void(int, uint8_t, const QByteArray&, uint32_t, bool)> m_sendCallback;
};

#endif // FILETRANSFERHANDLER_H
