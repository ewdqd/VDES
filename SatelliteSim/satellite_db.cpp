#include "satellite_db.h"
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QDir>

SatelliteDB& SatelliteDB::instance()
{
    static SatelliteDB db;
    return db;
}

SatelliteDB::SatelliteDB(QObject *parent) : QObject(parent)
{
}

SatelliteDB::~SatelliteDB()
{
    if (m_db.isOpen())
        m_db.close();
}

bool SatelliteDB::init(const QString &dbPath)
{
    if (m_db.isOpen()) {
        m_db.close();
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", "satellite_connection");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "[SatelliteDB] Failed to open database:" << m_db.lastError().text();
        return false;
    }

    qDebug() << "[SatelliteDB] Database opened:" << dbPath;
    return createTables();
}

bool SatelliteDB::createTables()
{
    QSqlQuery query(m_db);

    // 文件传输记录表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS file_transfers ("
        "transfer_id TEXT PRIMARY KEY,"
        "ship_id INTEGER,"
        "file_name TEXT,"
        "file_size INTEGER,"
        "md5 TEXT,"
        "chunk_size INTEGER DEFAULT 65536,"
        "total_chunks INTEGER,"
        "received_chunks INTEGER DEFAULT 0,"
        "direction TEXT,"
        "status TEXT DEFAULT 'transferring',"
        "storage_path TEXT,"
        "actual_md5 TEXT,"
        "error_msg TEXT,"
        "create_time INTEGER,"
        "complete_time INTEGER"
        ")")) {
        qWarning() << "[SatelliteDB] Failed to create file_transfers table:" << query.lastError().text();
        return false;
    }

    // 分片接收记录表（用于断点续传）
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS received_chunks ("
        "transfer_id TEXT,"
        "chunk_index INTEGER,"
        "PRIMARY KEY (transfer_id, chunk_index)"
        ")")) {
        qWarning() << "[SatelliteDB] Failed to create received_chunks table:" << query.lastError().text();
        return false;
    }

    // 日志表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS satellite_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp TEXT,"
        "type TEXT,"
        "message TEXT"
        ")")) {
        qWarning() << "[SatelliteDB] Failed to create logs table:" << query.lastError().text();
        return false;
    }

    return true;
}

bool SatelliteDB::addTransferRecord(const QString &transferId, int shipId,
                                     const QString &fileName, quint64 fileSize,
                                     const QString &md5, quint32 chunkSize,
                                     const QString &direction)
{
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT OR REPLACE INTO file_transfers "
        "(transfer_id, ship_id, file_name, file_size, md5, chunk_size, total_chunks, "
        " received_chunks, direction, status, create_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, 'transferring', ?)");

    quint32 totalChunks = (fileSize + chunkSize - 1) / chunkSize;
    if (totalChunks == 0) totalChunks = 1;

    query.addBindValue(transferId);
    query.addBindValue(shipId);
    query.addBindValue(fileName);
    query.addBindValue(fileSize);
    query.addBindValue(md5);
    query.addBindValue(chunkSize);
    query.addBindValue(totalChunks);
    query.addBindValue(direction);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "[SatelliteDB] addTransferRecord failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool SatelliteDB::updateTransferProgress(const QString &transferId, quint32 receivedChunks)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE file_transfers SET received_chunks = ? WHERE transfer_id = ?");
    query.addBindValue(receivedChunks);
    query.addBindValue(transferId);
    return query.exec();
}

bool SatelliteDB::completeTransfer(const QString &transferId, const QString &finalPath,
                                    const QString &actualMD5)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE file_transfers SET status = 'completed', storage_path = ?, "
                  "actual_md5 = ?, complete_time = ? WHERE transfer_id = ?");
    query.addBindValue(finalPath);
    query.addBindValue(actualMD5);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(transferId);
    return query.exec();
}

bool SatelliteDB::failTransfer(const QString &transferId, const QString &errorMsg)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE file_transfers SET status = 'failed', error_msg = ?, "
                  "complete_time = ? WHERE transfer_id = ?");
    query.addBindValue(errorMsg);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(transferId);
    return query.exec();
}

bool SatelliteDB::getTransferInfo(const QString &transferId, QVariantMap &info)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM file_transfers WHERE transfer_id = ?");
    query.addBindValue(transferId);
    if (query.exec() && query.next()) {
        info["transfer_id"] = query.value("transfer_id");
        info["ship_id"] = query.value("ship_id");
        info["file_name"] = query.value("file_name");
        info["file_size"] = query.value("file_size");
        info["md5"] = query.value("md5");
        info["chunk_size"] = query.value("chunk_size");
        info["total_chunks"] = query.value("total_chunks");
        info["received_chunks"] = query.value("received_chunks");
        info["direction"] = query.value("direction");
        info["status"] = query.value("status");
        info["storage_path"] = query.value("storage_path");
        return true;
    }
    return false;
}

bool SatelliteDB::getPendingTransfers(QList<QVariantMap> &transfers)
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT * FROM file_transfers WHERE status = 'transferring'")) {
        return false;
    }
    while (query.next()) {
        QVariantMap info;
        info["transfer_id"] = query.value("transfer_id");
        info["ship_id"] = query.value("ship_id");
        info["file_name"] = query.value("file_name");
        info["file_size"] = query.value("file_size");
        info["status"] = query.value("status");
        info["received_chunks"] = query.value("received_chunks");
        info["total_chunks"] = query.value("total_chunks");
        transfers.append(info);
    }
    return true;
}

bool SatelliteDB::getReceivedChunks(const QString &transferId, QList<quint32> &chunks)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT chunk_index FROM received_chunks WHERE transfer_id = ? ORDER BY chunk_index");
    query.addBindValue(transferId);
    if (query.exec()) {
        while (query.next()) {
            chunks.append(query.value(0).toUInt());
        }
        return true;
    }
    return false;
}

bool SatelliteDB::addReceivedChunk(const QString &transferId, quint32 chunkIndex)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT OR IGNORE INTO received_chunks (transfer_id, chunk_index) VALUES (?, ?)");
    query.addBindValue(transferId);
    query.addBindValue(chunkIndex);
    return query.exec();
}

bool SatelliteDB::isChunkReceived(const QString &transferId, quint32 chunkIndex)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM received_chunks WHERE transfer_id = ? AND chunk_index = ?");
    query.addBindValue(transferId);
    query.addBindValue(chunkIndex);
    return query.exec() && query.next();
}

bool SatelliteDB::insertLog(const QString &msg, const QString &type)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO satellite_logs (timestamp, type, message) VALUES (?, ?, ?)");
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(type);
    query.addBindValue(msg);
    return query.exec();
}

QList<QVariantMap> SatelliteDB::getRecentLogs(int limit)
{
    QList<QVariantMap> logs;
    QSqlQuery query(m_db);
    query.prepare("SELECT timestamp, type, message FROM satellite_logs ORDER BY id DESC LIMIT ?");
    query.addBindValue(limit);
    if (query.exec()) {
        while (query.next()) {
            QVariantMap log;
            log["timestamp"] = query.value(0);
            log["type"] = query.value(1);
            log["message"] = query.value(2);
            logs.append(log);
        }
    }
    return logs;
}
