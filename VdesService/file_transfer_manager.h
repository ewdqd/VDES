#ifndef FILE_TRANSFER_MANAGER_H
#define FILE_TRANSFER_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <functional>
#include <fstream>

class DatabaseManager;
class ClientSession;

// 服务端文件传输管理器
class FileTransferManager {
public:
    FileTransferManager(DatabaseManager* db);
    ~FileTransferManager();

    // 客户端请求发起文件上传
    std::string createUploadSession(uint32_t uploadShipId, uint32_t targetShipId,
        const std::string& fileName, uint64_t fileSize,
        const std::string& md5, uint32_t chunkSize);

    // 接收文件分片
    bool receiveChunk(const std::string& fileId, uint32_t chunkIndex,
        const std::vector<uint8_t>& data, bool isLast,
        bool& isComplete, std::string& errorMsg);

    // 客户端请求续传：返回缺失的分片索引列表
    std::vector<uint32_t> getMissingChunks(const std::string& fileId);

    // 删除传输会话
    void cancelTransfer(const std::string& fileId);

    // 获取文件存储路径
    std::string getStoragePath(const std::string& fileId);

    // 设置推送回调
    using PushCallback = std::function<void(uint32_t targetShipId,
        const std::string& fileId,
        const std::string& fileName,
        uint64_t totalSize,
        const std::string& md5,
        uint32_t chunkSize,
        uint32_t totalChunks,
        uint32_t chunkIndex,
        const std::vector<uint8_t>& data,
        bool isLast)>;
    void setPushCallback(PushCallback cb) { m_pushCallback = cb; }

    // 开始推送文件给目标客户端
    void pushFileToClient(uint32_t targetShipId, const std::string& fileId);

private:
    std::string generateFileId();
    std::string computeMD5(const std::vector<uint8_t>& data);
    std::string computeFileMD5(const std::string& filePath);

    DatabaseManager* m_db;
    PushCallback m_pushCallback;

    struct WriteSession {
        std::string fileId;
        std::string tempPath;
        std::string finalPath;
        std::ofstream stream;
        uint64_t expectedSize;
        uint32_t totalChunks;
        std::string expectedMD5;
    };
    std::map<std::string, WriteSession> m_writeSessions;
    std::mutex m_mutex;

    std::string m_storageDir;
};

#endif