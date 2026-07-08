#ifndef LOCALDB_H
#define LOCALDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariantMap>

// 本地数据库管理：文件传输断点记录、日志缓存
class LocalDB : public QObject
{
    Q_OBJECT
public:
    static LocalDB& instance();
    bool init();

    // 文件传输记录
    bool addTransferRecord(const QString &transferId, const QString &fileName,
                           quint64 fileSize, const QString &md5, quint32 chunkSize);
    bool updateTransferProgress(const QString &transferId, quint32 completedChunks);
    bool getTransferProgress(const QString &transferId, quint32 &completedChunks, quint32 &totalChunks);
    bool removeTransferRecord(const QString &transferId);

    // 日志缓存（可选）
    void cacheLog(const QString &time, int slot, quint32 shipId,
                  const QString &direction, const QString &msgType,
                  const QString &summary, const QString &detail);
    QList<QVariantMap> getCachedLogs(int limit = 100);
    void clearCachedLogs();

private:
    explicit LocalDB(QObject *parent = nullptr);
    ~LocalDB();
    QSqlDatabase m_db;
};

#endif // LOCALDB_H