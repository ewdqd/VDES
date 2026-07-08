#ifndef CLIENT_SESSION_H
#define CLIENT_SESSION_H

#include "socket_utils.h"
#include "vdes_messages.pb.h"
#include <vector>
#include <thread>
#include <atomic>
#include <string>
#include <cstdint>
#include <map>

class VdesService;

class ClientSession {
public:
    ClientSession(socket_t sock, VdesService* service);
    ~ClientSession();

    void start();

    // 推送接口
    void pushLogEntry(const std::string& time, int slot, uint32_t shipId,
        const std::string& direction, const std::string& msgType,
        const std::string& summary, const std::string& detail);
    void pushSlotUpdate(int slot);
    void pushFileInfo(const std::string& fileId, const std::string& fileName,
        int64_t totalSize, const std::string& md5,
        int32_t chunkSize, int32_t totalChunks);
    void pushFileChunk(const std::string& fileId, int32_t chunkIndex,
        const std::vector<uint8_t>& data, bool isLast);
    void pushFileStatus(const std::string& transferId, uint32_t completedChunks,
        uint32_t totalChunks, const std::string& status,
        const std::string& errorMsg);

    uint32_t getShipId() const { return m_shipId; }
    void setShipId(uint32_t shipId) { m_shipId = shipId; }

private:
    void readLoop();
    void sendPush(const vdes::ServerPush& push);

    void handleFileTransferRequest(const vdes::FileTransferRequest& req);
    void handleFileChunk(const vdes::FileChunk& chunk);
    void handleResumeRequest(const vdes::ResumeRequest& req);

    socket_t m_socket;
    VdesService* m_service;
    std::thread m_readThread;
    std::atomic<bool> m_running;
    std::vector<uint8_t> m_buffer;
    uint32_t m_shipId;
};

#endif
