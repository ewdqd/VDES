#ifndef SATELLITE_DB_H
#define SATELLITE_DB_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QList>
#include <QVariantMap>

// 卫星端数据库管理：文件传输进度跟踪、日志持久化、断点续传支持
class SatelliteDB : public QObject
{
    Q_OBJECT
public:
    static SatelliteDB& instance();

    bool init(const QString &dbPath = "satellite_data.db");

    // 文件传输记录
    bool addTransferRecord(const QString &transferId, int shipId,
                           const QString &fileName, quint64 fileSize,
                           const QString &md5, quint32 chunkSize,
                           const QString &direction);  // "upload" or "download"

    bool updateTransferProgress(const QString &transferId, quint32 receivedChunks);
    bool completeTransfer(const QString &transferId, const QString &finalPath,
                          const QString &actualMD5);
    bool failTransfer(const QString &transferId, const QString &errorMsg);

    bool getTransferInfo(const QString &transferId, QVariantMap &info);
    bool getPendingTransfers(QList<QVariantMap> &transfers);
    bool getReceivedChunks(const QString &transferId, QList<quint32> &chunks);

    // 日志
    bool insertLog(const QString &msg, const QString &type = "info");
    QList<QVariantMap> getRecentLogs(int limit = 100);

    // 断点续传支持
    bool addReceivedChunk(const QString &transferId, quint32 chunkIndex);
    bool isChunkReceived(const QString &transferId, quint32 chunkIndex);

private:
    explicit SatelliteDB(QObject *parent = nullptr);
    ~SatelliteDB();
    bool createTables();

    QSqlDatabase m_db;
};

#endif // SATELLITE_DB_H
