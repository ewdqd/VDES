#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <sqlite3.h>

// 服务端数据库管理器：文件传输元数据、日志缓存、会话记录
class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool open(const std::string& path = "vdes_data.db");
    void close();

    // ========== 文件传输记录 ==========
    struct FileRecord {
        std::string file_id;
        uint32_t upload_ship_id;
        uint32_t target_ship_id;
        std::string file_name;
        uint64_t file_size;
        std::string md5;
        uint32_t chunk_size;
        uint32_t total_chunks;
        uint32_t received_chunks;
        std::string status;       // pending / transferring / completed / failed
        std::string storage_path;
        int64_t create_time;
        int64_t complete_time;
    };

    bool insertFileRecord(const FileRecord& record);
    bool updateFileProgress(const std::string& file_id, uint32_t received_chunks);
    bool updateFileStatus(const std::string& file_id, const std::string& status,
        const std::string& storage_path = "");
    bool getFileRecord(const std::string& file_id, FileRecord& record);
    bool getPendingFiles(std::vector<FileRecord>& records);
    bool deleteFileRecord(const std::string& file_id);

    // ========== 分片进度表 ==========
    bool insertChunkProgress(const std::string& file_id, uint32_t chunk_index);
    bool isChunkReceived(const std::string& file_id, uint32_t chunk_index);
    bool getReceivedChunks(const std::string& file_id, std::vector<uint32_t>& chunks);
    bool clearChunkProgress(const std::string& file_id);

    // ========== 日志表 ==========
    bool insertLog(const std::string& msg, int slot, uint32_t shipId,
        const std::string& direction, const std::string& msgType,
        const std::string& summary, const std::string& detail);
    bool getLogs(int limit, std::vector<std::string>& logs);

    // ========== 离线消息表（待推送） ==========
    bool insertPendingMessage(uint32_t from_ship_id, uint32_t to_ship_id,
        const std::string& content, int64_t timestamp);
    bool getPendingMessages(uint32_t ship_id,
        std::vector<std::pair<uint32_t, std::string>>& messages);
    bool clearPendingMessages(uint32_t ship_id);

private:
    bool createTables();
    bool execSQL(const std::string& sql);
    sqlite3* m_db;
    std::mutex m_mutex;
};

#endif