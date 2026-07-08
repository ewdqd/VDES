#include "db_connection_pool.h"
#include <iostream>

using namespace std;

DBConnectionPool& DBConnectionPool::instance() {
    static DBConnectionPool pool;
    return pool;
}

bool DBConnectionPool::initialize(const std::string& dbPath, int poolSize) {
    lock_guard<mutex> lock(m_mutex);
    if (m_initialized) {
        shutdown();
    }

    m_dbPath = dbPath;
    m_poolSize = max(1, min(poolSize, 16));

    cout << "[DBConnectionPool] Initializing pool with " << m_poolSize << " connections to " << dbPath << endl;

    for (int i = 0; i < m_poolSize; ++i) {
        sqlite3* db = createConnection(dbPath);
        if (!db) {
            cerr << "[DBConnectionPool] Failed to create connection " << i << endl;
            // 回滚已创建的连接
            while (!m_available.empty()) {
                sqlite3_close(m_available.front());
                m_available.pop();
            }
            return false;
        }
        m_available.push(db);
    }

    m_initialized = true;
    cout << "[DBConnectionPool] Successfully initialized " << m_poolSize << " connections" << endl;
    return true;
}

void DBConnectionPool::shutdown() {
    lock_guard<mutex> lock(m_mutex);
    while (!m_available.empty()) {
        sqlite3* db = m_available.front();
        if (db) {
            sqlite3_close(db);
        }
        m_available.pop();
    }
    m_initialized = false;
    cout << "[DBConnectionPool] Shutdown, all connections closed" << endl;
}

shared_ptr<DBConnectionPool::Connection> DBConnectionPool::acquire() {
    sqlite3* db = nullptr;
    {
        unique_lock<mutex> lock(m_mutex);
        if (m_available.empty()) {
            // 连接池耗尽，创建一个临时连接
            cerr << "[DBConnectionPool] Pool exhausted, creating temporary connection" << endl;
            db = createConnection(m_dbPath);
            if (!db) return nullptr;
            return make_shared<Connection>(db);
        }
        db = m_available.front();
        m_available.pop();
    }
    return make_shared<Connection>(db);
}

void DBConnectionPool::release(shared_ptr<Connection> conn) {
    if (!conn || !conn->db) return;

    lock_guard<mutex> lock(m_mutex);
    // 只归还池化连接，临时连接直接关闭
    if ((int)m_available.size() < m_poolSize) {
        m_available.push(conn->db);
        // 防止析构时关闭
        conn->db = nullptr;
    } else {
        // 池已满，关闭多余连接
        sqlite3_close(conn->db);
        conn->db = nullptr;
    }
}

sqlite3* DBConnectionPool::createConnection(const std::string& dbPath) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPath.c_str(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        cerr << "[DBConnectionPool] Failed to open database: " << sqlite3_errmsg(db) << endl;
        if (db) sqlite3_close(db);
        return nullptr;
    }

    // 设置 PRAGMA 优化并发性能
    char* errMsg = nullptr;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }
    sqlite3_exec(db, "PRAGMA busy_timeout=5000", nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }
    sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }

    return db;
}
