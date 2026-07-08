#include "database_manager.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

using namespace std;

static int64_t now_epoch() {
    return chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

DatabaseManager::DatabaseManager() : m_db(nullptr) {}

DatabaseManager::~DatabaseManager() { close(); }

bool DatabaseManager::open(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        cerr << "[DatabaseManager] Cannot open database: " << sqlite3_errmsg(m_db) << endl;
        return false;
    }
    // 启用 WAL 模式，支持并发读
    execSQL("PRAGMA journal_mode=WAL");
    execSQL("PRAGMA synchronous=NORMAL");
    execSQL("PRAGMA busy_timeout=5000");
    return createTables();
}

void DatabaseManager::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool DatabaseManager::execSQL(const std::string& sql) {
    lock_guard<mutex> lock(m_mutex);
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        cerr << "[DatabaseManager] SQL error: " << (errMsg ? errMsg : "unknown")
             << " | SQL: " << sql.substr(0, 200) << endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool DatabaseManager::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS server_files (
            file_id TEXT PRIMARY KEY,
            upload_ship_id INTEGER,
            target_ship_id INTEGER,
            file_name TEXT,
            file_size INTEGER,
            md5 TEXT,
            chunk_size INTEGER,
            total_chunks INTEGER,
            received_chunks INTEGER DEFAULT 0,
            status TEXT DEFAULT 'pending',
            storage_path TEXT,
            create_time INTEGER,
            complete_time INTEGER
        );
        CREATE TABLE IF NOT EXISTS chunk_progress (
            file_id TEXT,
            chunk_index INTEGER,
            PRIMARY KEY (file_id, chunk_index)
        );
        CREATE TABLE IF NOT EXISTS server_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT,
            slot INTEGER,
            ship_id INTEGER,
            direction TEXT,
            msg_type TEXT,
            summary TEXT,
            detail TEXT
        );
        CREATE TABLE IF NOT EXISTS pending_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_ship_id INTEGER,
            to_ship_id INTEGER,
            content TEXT,
            msg_timestamp INTEGER
        );
    )";
    return execSQL(sql);
}

// ========== 文件传输记录 ==========
bool DatabaseManager::insertFileRecord(const FileRecord& record) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = R"(
        INSERT OR REPLACE INTO server_files
        (file_id, upload_ship_id, target_ship_id, file_name, file_size, md5,
         chunk_size, total_chunks, received_chunks, status, storage_path,
         create_time, complete_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        cerr << "[DatabaseManager] prepare insertFileRecord: " << sqlite3_errmsg(m_db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, record.file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, record.upload_ship_id);
    sqlite3_bind_int(stmt, 3, record.target_ship_id);
    sqlite3_bind_text(stmt, 4, record.file_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, record.file_size);
    sqlite3_bind_text(stmt, 6, record.md5.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, record.chunk_size);
    sqlite3_bind_int(stmt, 8, record.total_chunks);
    sqlite3_bind_int(stmt, 9, record.received_chunks);
    sqlite3_bind_text(stmt, 10, record.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, record.storage_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, record.create_time);
    sqlite3_bind_int64(stmt, 13, record.complete_time);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::updateFileProgress(const std::string& file_id, uint32_t received_chunks) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "UPDATE server_files SET received_chunks = ? WHERE file_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, received_chunks);
    sqlite3_bind_text(stmt, 2, file_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::updateFileStatus(const std::string& file_id, const std::string& status,
                                        const std::string& storage_path) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = storage_path.empty()
        ? "UPDATE server_files SET status = ?, complete_time = ? WHERE file_id = ?"
        : "UPDATE server_files SET status = ?, storage_path = ?, complete_time = ? WHERE file_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    if (!storage_path.empty()) {
        sqlite3_bind_text(stmt, 2, storage_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now_epoch());
        sqlite3_bind_text(stmt, 4, file_id.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_int64(stmt, 2, now_epoch());
        sqlite3_bind_text(stmt, 3, file_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::getFileRecord(const std::string& file_id, FileRecord& record) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT * FROM server_files WHERE file_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        record.upload_ship_id = sqlite3_column_int(stmt, 1);
        record.target_ship_id = sqlite3_column_int(stmt, 2);
        record.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        record.file_size = sqlite3_column_int64(stmt, 4);
        record.md5 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        record.chunk_size = sqlite3_column_int(stmt, 6);
        record.total_chunks = sqlite3_column_int(stmt, 7);
        record.received_chunks = sqlite3_column_int(stmt, 8);
        record.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        const char* sp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        record.storage_path = sp ? sp : "";
        record.create_time = sqlite3_column_int64(stmt, 11);
        record.complete_time = sqlite3_column_int64(stmt, 12);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool DatabaseManager::getPendingFiles(std::vector<FileRecord>& records) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT * FROM server_files WHERE status IN ('pending','transferring')";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileRecord r;
        r.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.upload_ship_id = sqlite3_column_int(stmt, 1);
        r.target_ship_id = sqlite3_column_int(stmt, 2);
        r.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.file_size = sqlite3_column_int64(stmt, 4);
        r.md5 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        r.chunk_size = sqlite3_column_int(stmt, 6);
        r.total_chunks = sqlite3_column_int(stmt, 7);
        r.received_chunks = sqlite3_column_int(stmt, 8);
        r.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        const char* sp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        r.storage_path = sp ? sp : "";
        records.push_back(r);
    }
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::deleteFileRecord(const std::string& file_id) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "DELETE FROM server_files WHERE file_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ========== 分片进度 ==========
bool DatabaseManager::insertChunkProgress(const std::string& file_id, uint32_t chunk_index) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "INSERT OR IGNORE INTO chunk_progress (file_id, chunk_index) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chunk_index);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::isChunkReceived(const std::string& file_id, uint32_t chunk_index) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT 1 FROM chunk_progress WHERE file_id = ? AND chunk_index = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chunk_index);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool DatabaseManager::getReceivedChunks(const std::string& file_id, std::vector<uint32_t>& chunks) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT chunk_index FROM chunk_progress WHERE file_id = ? ORDER BY chunk_index";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        chunks.push_back(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::clearChunkProgress(const std::string& file_id) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "DELETE FROM chunk_progress WHERE file_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ========== 日志 ==========
bool DatabaseManager::insertLog(const std::string& msg, int slot, uint32_t shipId,
                                 const std::string& direction, const std::string& msgType,
                                 const std::string& summary, const std::string& detail) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = R"(
        INSERT INTO server_logs (timestamp, slot, ship_id, direction, msg_type, summary, detail)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, slot);
    sqlite3_bind_int(stmt, 3, shipId);
    sqlite3_bind_text(stmt, 4, direction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msgType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, detail.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::getLogs(int limit, std::vector<std::string>& logs) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT timestamp, slot, ship_id, direction, msg_type, summary, detail FROM server_logs ORDER BY id DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ostringstream oss;
        oss << sqlite3_column_text(stmt, 0) << "|"
            << sqlite3_column_int(stmt, 1) << "|"
            << sqlite3_column_int(stmt, 2) << "|"
            << sqlite3_column_text(stmt, 3) << "|"
            << sqlite3_column_text(stmt, 4) << "|"
            << sqlite3_column_text(stmt, 5) << "|"
            << sqlite3_column_text(stmt, 6);
        logs.push_back(oss.str());
    }
    sqlite3_finalize(stmt);
    return true;
}

// ========== 离线消息 ==========
bool DatabaseManager::insertPendingMessage(uint32_t from_ship_id, uint32_t to_ship_id,
                                            const std::string& content, int64_t timestamp) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = R"(
        INSERT INTO pending_messages (from_ship_id, to_ship_id, content, msg_timestamp)
        VALUES (?, ?, ?, ?)
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, from_ship_id);
    sqlite3_bind_int(stmt, 2, to_ship_id);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, timestamp);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool DatabaseManager::getPendingMessages(uint32_t ship_id,
                                          std::vector<std::pair<uint32_t, std::string>>& messages) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "SELECT from_ship_id, content FROM pending_messages WHERE to_ship_id = ? ORDER BY id";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, ship_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint32_t from = sqlite3_column_int(stmt, 0);
        string content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        messages.push_back({from, content});
    }
    sqlite3_finalize(stmt);
    return true;
}

bool DatabaseManager::clearPendingMessages(uint32_t ship_id) {
    lock_guard<mutex> lock(m_mutex);
    const char* sql = "DELETE FROM pending_messages WHERE to_ship_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, ship_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
