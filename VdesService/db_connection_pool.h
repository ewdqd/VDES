#ifndef DB_CONNECTION_POOL_H
#define DB_CONNECTION_POOL_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <sqlite3.h>

// 数据库连接池：管理多个 SQLite 连接，支持高并发读写
class DBConnectionPool {
public:
    static DBConnectionPool& instance();

    bool initialize(const std::string& dbPath, int poolSize = 4);
    void shutdown();

    // 获取/归还连接
    struct Connection {
        sqlite3* db;
        Connection(sqlite3* d) : db(d) {}
        ~Connection() { if (db) sqlite3_close(db); }
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
    };

    std::shared_ptr<Connection> acquire();
    void release(std::shared_ptr<Connection> conn);

    // 统计
    int available() const { std::lock_guard<std::mutex> lock(m_mutex); return (int)m_available.size(); }
    int size() const { return m_poolSize; }

private:
    DBConnectionPool() = default;
    ~DBConnectionPool() { shutdown(); }
    DBConnectionPool(const DBConnectionPool&) = delete;
    DBConnectionPool& operator=(const DBConnectionPool&) = delete;

    sqlite3* createConnection(const std::string& dbPath);

    mutable std::mutex m_mutex;
    std::queue<sqlite3*> m_available;
    std::string m_dbPath;
    int m_poolSize = 4;
    bool m_initialized = false;
};

#endif // DB_CONNECTION_POOL_H
