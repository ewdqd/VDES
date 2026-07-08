#include "client_session.h"
#include "vdes_service.h"
#include "vdes_crc.h"
#include <iostream>
#include <thread>

using namespace std;

ClientSession::ClientSession(socket_t sock, VdesService* service)
    : m_socket(sock), m_service(service), m_running(true) {
    m_readThread = std::thread(&ClientSession::readLoop, this);
}

ClientSession::~ClientSession() {
    m_running = false;
    try {
        if (m_readThread.joinable()) {
            if (m_readThread.get_id() != std::this_thread::get_id()) {
                m_readThread.join();
            }
            else {
                m_readThread.detach();
            }
        }
    }
    catch (...) {
    }
    close_socket(m_socket);
}

void ClientSession::readLoop() {
    std::vector<uint8_t> lenBuf(2);
    while (m_running) {
        ssize_t ret = recv_all(m_socket, lenBuf.data(), 2);
        if (ret != 2) break;
        uint16_t msgLen = (lenBuf[0] << 8) | lenBuf[1];
        if (msgLen > 65535) break;
        std::vector<uint8_t> msgBuf(msgLen);
        ret = recv_all(m_socket, msgBuf.data(), msgLen);
        if (ret != (ssize_t)msgLen) break;
        vdes::ClientRequest req;
        if (req.ParseFromArray(msgBuf.data(), msgLen)) {
            if (req.has_send_short_ack()) {
                auto& r = req.send_short_ack();
                if (m_shipId == 0) m_shipId = r.my_mmsi();
                m_service->sendShortAck(r.specified_slot(), r.dest_id(), r.my_mmsi(), (uint8_t)r.data());
            }
            else if (req.has_send_short_noack()) {
                auto& r = req.send_short_noack();
                if (m_shipId == 0) m_shipId = r.my_mmsi();
                std::vector<uint8_t> payload(r.payload().begin(), r.payload().end());
                m_service->sendShortNoAck(r.specified_slot(), r.dest_id(), r.my_mmsi(), (uint8_t)r.data(),
                    r.has_dest(), payload);
            }
            else if (req.has_send_addressed()) {
                auto& r = req.send_addressed();
                if (m_shipId == 0) m_shipId = r.my_mmsi();
                std::vector<uint8_t> data(r.data().begin(), r.data().end());
                // 添加CRC校验后发送
                m_service->sendAddressed(r.specified_slot(), r.dest_id(), r.my_mmsi(), data);
            }
            else if (req.has_send_config()) {
                auto& r = req.send_config();
                std::vector<uint8_t> configData(r.config_data().begin(), r.config_data().end());
                m_service->sendBoardConfig(r.ship_index(), configData);
            }
            else if (req.has_file_transfer_req()) {
                handleFileTransferRequest(req.file_transfer_req());
            }
            else if (req.has_file_chunk()) {
                handleFileChunk(req.file_chunk());
            }
            else if (req.has_resume_req()) {
                handleResumeRequest(req.resume_req());
            }
        }
        else {
            cerr << "ClientSession: parse failed" << endl;
        }
    }

    if (m_service) {
        m_service->onSessionDisconnected(this);
    }
}

// ========== 文件传输处理 ==========
void ClientSession::handleFileTransferRequest(const vdes::FileTransferRequest& req) {
    if (!m_service) return;

    uint32_t uploadShipId = m_shipId;
    uint32_t targetShipId = req.dest_ship_id();
    string fileName = req.file_name();
    uint64_t fileSize = req.file_size();
    string md5 = req.md5();
    uint32_t chunkSize = req.chunk_size() > 0 ? req.chunk_size() : 65536;

    string fileId = m_service->getFileTransferManager().createUploadSession(
        uploadShipId, targetShipId, fileName, fileSize, md5, chunkSize);

    if (fileId.empty()) {
        cerr << "[ClientSession] Failed to create upload session for " << fileName << endl;
        return;
    }

    cout << "[ClientSession] File transfer session created: " << fileId
         << " for " << fileName << " from ship " << uploadShipId
         << " to ship " << targetShipId << endl;
}

void ClientSession::handleFileChunk(const vdes::FileChunk& chunk) {
    if (!m_service) return;

    string fileId = chunk.file_id();
    uint32_t chunkIndex = chunk.chunk_index();
    vector<uint8_t> data(chunk.data().begin(), chunk.data().end());
    bool isLast = chunk.is_last();

    bool isComplete = false;
    string errorMsg;

    // 对文件分片进行 CRC-32 校验（文件传输使用 Link ID 22/24，CRC-32）
    if (!VdesCRC::verify(data, 22)) {
        cerr << "[ClientSession] CRC32 check failed for chunk " << chunkIndex
             << " of " << fileId << endl;
        pushFileStatus(fileId, chunkIndex, 0, "failed", "CRC32 mismatch");
        return;
    }
    // 去除末尾4字节CRC再传入
    vector<uint8_t> payload(data.begin(), data.end() - 4);

    if (m_service->getFileTransferManager().receiveChunk(fileId, chunkIndex, payload, isLast, isComplete, errorMsg)) {
        if (isComplete) {
            cout << "[ClientSession] File transfer complete: " << fileId << endl;
            m_service->getFileTransferManager().pushFileToClient(0, fileId);
            pushFileStatus(fileId, 0, 0, "completed", "");
        }
    } else {
        cerr << "[ClientSession] File chunk error for " << fileId
             << " chunk " << chunkIndex << ": " << errorMsg << endl;
        pushFileStatus(fileId, chunkIndex, 0, "failed", errorMsg);
    }
}

void ClientSession::handleResumeRequest(const vdes::ResumeRequest& req) {
    if (!m_service) return;

    string transferId = req.transfer_id();
    uint32_t lastReceivedChunk = req.last_received_chunk();

    auto missingChunks = m_service->getFileTransferManager().getMissingChunks(transferId);

    cout << "[ClientSession] Resume request for " << transferId
         << " lastReceived=" << lastReceivedChunk
         << " missing=" << missingChunks.size() << endl;

    string statusMsg = "resuming";
    if (missingChunks.empty()) statusMsg = "completed";
    pushFileStatus(transferId, lastReceivedChunk,
                   lastReceivedChunk + (uint32_t)missingChunks.size(),
                   statusMsg, "");
}

void ClientSession::pushLogEntry(const std::string& time, int slot, uint32_t shipId,
    const std::string& direction, const std::string& msgType,
    const std::string& summary, const std::string& detail) {
    vdes::ServerPush push;
    auto* log = push.mutable_log_entry();
    log->set_timestamp(std::stoll(time));
    log->set_slot(slot);
    log->set_ship_id(shipId);
    log->set_direction(direction);
    log->set_msg_type(msgType);
    log->set_summary(summary);
    log->set_detail(detail);
    sendPush(push);
}

void ClientSession::pushSlotUpdate(int slot) {
    vdes::ServerPush push;
    push.mutable_slot_update()->set_slot(slot);
    sendPush(push);
}

void ClientSession::sendPush(const vdes::ServerPush& push) {
    string data;
    if (!push.SerializeToString(&data)) return;
    uint16_t len = static_cast<uint16_t>(data.size());
    vector<uint8_t> buffer;
    buffer.push_back((len >> 8) & 0xFF);
    buffer.push_back(len & 0xFF);
    buffer.insert(buffer.end(), data.begin(), data.end());
    send_all(m_socket, buffer.data(), buffer.size());
}

void ClientSession::pushFileInfo(const std::string& fileId, const std::string& fileName,
    int64_t totalSize, const std::string& md5,
    int32_t chunkSize, int32_t totalChunks) {
    vdes::ServerPush push;
    auto* info = push.mutable_file_info();
    info->set_file_id(fileId);
    info->set_file_name(fileName);
    info->set_total_size(totalSize);
    info->set_md5(md5);
    info->set_chunk_size(chunkSize);
    info->set_total_chunks(totalChunks);
    sendPush(push);
}

void ClientSession::pushFileChunk(const std::string& fileId, int32_t chunkIndex,
    const std::vector<uint8_t>& data, bool isLast) {
    vdes::ServerPush push;
    auto* chunk = push.mutable_file_chunk();
    chunk->set_file_id(fileId);
    chunk->set_chunk_index(chunkIndex);
    chunk->set_data(data.data(), data.size());
    chunk->set_is_last(isLast);
    sendPush(push);
}

void ClientSession::pushFileStatus(const std::string& transferId, uint32_t completedChunks,
    uint32_t totalChunks, const std::string& status, const std::string& errorMsg) {
    vdes::ServerPush push;
    auto* fs = push.mutable_file_status();
    fs->set_transfer_id(transferId);
    fs->set_completed_chunks(completedChunks);
    fs->set_total_chunks(totalChunks);
    fs->set_status(status);
    fs->set_error_msg(errorMsg);
    sendPush(push);
}
