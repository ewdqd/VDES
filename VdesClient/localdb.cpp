#include "localdb.h"
#include <QSqlError>
#include <QDebug>
#include <QDateTime>

LocalDB& LocalDB::instance()
{
    static LocalDB db;
    return db;
}

LocalDB::LocalDB(QObject *parent) : QObject(parent)
{
}

LocalDB::~LocalDB()
{
    if (m_db.isOpen())
        m_db.close();
}

bool LocalDB::init()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("vdes_client.db");
    if (!m_db.open()) {
        qWarning() << "Failed to open local database:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query;
    // 文件传输记录表
    if (!query.exec("CREATE TABLE IF NOT EXISTS transfers ("
                    "transfer_id TEXT PRIMARY KEY,"
                    "file_name TEXT,"
                    "file_size INTEGER,"
                    "md5 TEXT,"
                    "chunk_size INTEGER,"
                    "completed_chunks INTEGER DEFAULT 0,"
                    "total_chunks INTEGER,"
                    "last_update INTEGER)")) {
        qWarning() << "Failed to create transfers table:" << query.lastError().text();
        return false;
    }

    // 日志缓存表
    if (!query.exec("CREATE TABLE IF NOT EXISTS logs ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "timestamp TEXT,"
                    "slot INTEGER,"
                    "ship_id INTEGER,"
                    "direction TEXT,"
                    "msg_type TEXT,"
                    "summary TEXT,"
                    "detail TEXT)")) {
        qWarning() << "Failed to create logs table:" << query.lastError().text();
        return false;
    }

    return true;
}

bool LocalDB::addTransferRecord(const QString &transferId, const QString &fileName,
                                quint64 fileSize, const QString &md5, quint32 chunkSize)
{
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO transfers (transfer_id, file_name, file_size, md5, chunk_size, completed_chunks, total_chunks, last_update) "
                  "VALUES (?, ?, ?, ?, ?, 0, ?, ?)");
    quint32 totalChunks = (fileSize + chunkSize - 1) / chunkSize;
    query.addBindValue(transferId);
    query.addBindValue(fileName);
    query.addBindValue(fileSize);
    query.addBindValue(md5);
    query.addBindValue(chunkSize);
    query.addBindValue(totalChunks);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    return query.exec();
}

bool LocalDB::updateTransferProgress(const QString &transferId, quint32 completedChunks)
{
    QSqlQuery query;
    query.prepare("UPDATE transfers SET completed_chunks = ?, last_update = ? WHERE transfer_id = ?");
    query.addBindValue(completedChunks);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(transferId);
    return query.exec();
}

bool LocalDB::getTransferProgress(const QString &transferId, quint32 &completedChunks, quint32 &totalChunks)
{
    QSqlQuery query;
    query.prepare("SELECT completed_chunks, total_chunks FROM transfers WHERE transfer_id = ?");
    query.addBindValue(transferId);
    if (query.exec() && query.next()) {
        completedChunks = query.value(0).toUInt();
        totalChunks = query.value(1).toUInt();
        return true;
    }
    return false;
}

bool LocalDB::removeTransferRecord(const QString &transferId)
{
    QSqlQuery query;
    query.prepare("DELETE FROM transfers WHERE transfer_id = ?");
    query.addBindValue(transferId);
    return query.exec();
}

void LocalDB::cacheLog(const QString &time, int slot, quint32 shipId,
                       const QString &direction, const QString &msgType,
                       const QString &summary, const QString &detail)
{
    QSqlQuery query;
    query.prepare("INSERT INTO logs (timestamp, slot, ship_id, direction, msg_type, summary, detail) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(time);
    query.addBindValue(slot);
    query.addBindValue(shipId);
    query.addBindValue(direction);
    query.addBindValue(msgType);
    query.addBindValue(summary);
    query.addBindValue(detail);
    query.exec();
}

QList<QVariantMap> LocalDB::getCachedLogs(int limit)
{
    QList<QVariantMap> logs;
    QSqlQuery query;
    query.prepare("SELECT timestamp, slot, ship_id, direction, msg_type, summary, detail FROM logs ORDER BY id DESC LIMIT ?");
    query.addBindValue(limit);
    if (query.exec()) {
        while (query.next()) {
            QVariantMap map;
            map["time"] = query.value(0);
            map["slot"] = query.value(1);
            map["shipId"] = query.value(2);
            map["direction"] = query.value(3);
            map["msgType"] = query.value(4);
            map["summary"] = query.value(5);
            map["detail"] = query.value(6);
            logs.append(map);
        }
    }
    return logs;
}

void LocalDB::clearCachedLogs()
{
    QSqlQuery query;
    query.exec("DELETE FROM logs");
}