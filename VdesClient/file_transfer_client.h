#ifndef FILE_TRANSFER_CLIENT_H
#define FILE_TRANSFER_CLIENT_H

#include <QObject>
#include <QScopedPointer>
#include <QFile>
#include <QTimer>
#include <QCryptographicHash>
#include <QMap>
#include <QSet>
#include "tcp_client.h"
#include "ship_connection.h"

// 文件传输方向
enum class TransferDirection {
    Upload,
    Download
};

// 传输会话信息
struct TransferSession {
    QString transferId;
    QString fileName;
    QString filePath;
    quint64 fileSize;
    QString md5;
    QString sha256;          // SHA-256 完整性校验
    quint32 chunkSize;
    quint32 totalChunks;
    quint32 transferredChunks;
    QSet<quint32> receivedChunks;  // 下载时收到的分片索引
    quint32 destShipId;
    TransferDirection direction;
    bool active;

    int percent() const {
        if (totalChunks == 0) return 0;
        return static_cast<int>(transferredChunks * 100 / totalChunks);
    }
};

class FileTransferClient : public QObject
{
    Q_OBJECT
public:
    explicit FileTransferClient(TcpClient *tcpClient, QObject *parent = nullptr);
    ~FileTransferClient();

    // 上传文件
    void uploadFile(quint32 destShipId, const QString &filePath);

    // 处理下载分片
    void handleFileChunk(const QString &fileId, quint32 chunkIndex,
                         const QByteArray &data, bool isLast);
    void handleFileStatus(const QString &transferId, quint32 completedChunks,
                          quint32 totalChunks, const QString &status,
                          const QString &errorMsg);

    // 断点续传
    void resumeUpload(const QString &transferId);
    void resumeDownload(const QString &transferId);

    // 获取会话信息
    const TransferSession* getSession(const QString &transferId) const;

signals:
    void uploadProgress(const QString &fileName, int percent);
    void uploadComplete(const QString &fileName, bool success, const QString &error);
    void downloadProgress(const QString &fileName, int percent);
    void downloadComplete(const QString &filePath, const QString &md5,
                          const QString &sha256, bool success);

    // 图片文件预览信号
    void imageFileReceived(const QString &filePath);

private slots:
    void onUploadTimer();

private:
    void startUpload(quint32 destShipId, const QString &filePath);
    void sendNextChunk();
    QString computeMD5(const QString &filePath);
    QString computeSHA256(const QString &filePath);
    bool verifyIntegrity(const QString &filePath, const QString &expectedMD5,
                         const QString &expectedSHA256);

    TcpClient *m_tcpClient;
    QScopedPointer<QFile> m_currentFile;
    TransferSession *m_currentSession;   // 当前活跃的上传会话
    quint32 m_chunkSize;
    QTimer *m_uploadTimer;
    bool m_uploading;

    // 下载会话管理（通过transferId索引）
    QMap<QString, TransferSession*> m_downloadSessions;
    QMap<QString, QFile*> m_downloadFiles;
};

#endif // FILE_TRANSFER_CLIENT_H
