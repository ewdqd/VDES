#include "file_transfer_manager.h"
#include "database_manager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <fstream>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

using namespace std;
namespace fs = std::filesystem;

static string generateUUID() {
    static random_device rd;
    static mt19937_64 gen(rd());
    static uniform_int_distribution<uint64_t> dis;
    stringstream ss;
    ss << hex << setfill('0')
        << chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count()
        << "_" << dis(gen);
    return ss.str();
}

FileTransferManager::FileTransferManager(DatabaseManager* db)
    : m_db(db), m_storageDir("./vdes_files") {
    error_code ec;
    fs::create_directories(m_storageDir, ec);
    if (ec) {
        cerr << "[FileTransferManager] Failed to create storage dir: " << ec.message() << endl;
    }
}

FileTransferManager::~FileTransferManager() {
    lock_guard<mutex> lock(m_mutex);
    for (auto& [id, session] : m_writeSessions) {
        if (session.stream.is_open()) session.stream.close();
    }
}

string FileTransferManager::generateFileId() {
    return generateUUID();
}

string FileTransferManager::createUploadSession(uint32_t uploadShipId, uint32_t targetShipId,
    const string& fileName, uint64_t fileSize,
    const string& md5, uint32_t chunkSize) {
    string fileId = generateFileId();
    uint32_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;

    DatabaseManager::FileRecord rec;
    rec.file_id = fileId;
    rec.upload_ship_id = uploadShipId;
    rec.target_ship_id = targetShipId;
    rec.file_name = fileName;
    rec.file_size = fileSize;
    rec.md5 = md5;
    rec.chunk_size = chunkSize;
    rec.total_chunks = totalChunks;
    rec.received_chunks = 0;
    rec.status = "transferring";
    rec.create_time = chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    rec.complete_time = 0;

    if (!m_db->insertFileRecord(rec)) {
        cerr << "[FileTransferManager] Failed to insert file record: " << fileId << endl;
        return "";
    }

    string tempPath = m_storageDir + "/" + fileId + ".tmp";
    string finalPath = m_storageDir + "/" + fileId + "_" + fileName;

    lock_guard<mutex> lock(m_mutex);
    WriteSession ws;
    ws.fileId = fileId;
    ws.tempPath = tempPath;
    ws.finalPath = finalPath;
    ws.expectedSize = fileSize;
    ws.totalChunks = totalChunks;
    ws.expectedMD5 = md5;
    ws.stream.open(tempPath, ios::binary | ios::out);
    if (!ws.stream.is_open()) {
        cerr << "[FileTransferManager] Failed to open temp file: " << tempPath << endl;
        return "";
    }
    m_writeSessions[fileId] = std::move(ws);

    cout << "[FileTransferManager] Created upload session: " << fileId
        << " for file " << fileName << " (" << fileSize << " bytes, "
        << totalChunks << " chunks)" << endl;
    return fileId;
}

bool FileTransferManager::receiveChunk(const string& fileId, uint32_t chunkIndex,
    const vector<uint8_t>& data, bool isLast,
    bool& isComplete, string& errorMsg) {
    isComplete = false;
    lock_guard<mutex> lock(m_mutex);

    auto it = m_writeSessions.find(fileId);
    if (it == m_writeSessions.end()) {
        DatabaseManager::FileRecord rec;
        if (!m_db->getFileRecord(fileId, rec)) {
            errorMsg = "File session not found: " + fileId;
            return false;
        }
        string tempPath = m_storageDir + "/" + fileId + ".tmp";
        WriteSession ws;
        ws.fileId = fileId;
        ws.tempPath = tempPath;
        ws.finalPath = m_storageDir + "/" + fileId + "_" + rec.file_name;
        ws.expectedSize = rec.file_size;
        ws.totalChunks = rec.total_chunks;
        ws.expectedMD5 = rec.md5;
        ws.stream.open(tempPath, ios::binary | ios::app);
        if (!ws.stream.is_open()) {
            errorMsg = "Cannot reopen temp file for resume: " + tempPath;
            return false;
        }
        auto [insertedIt, _] = m_writeSessions.emplace(fileId, std::move(ws));
        it = insertedIt;
    }

    WriteSession& session = it->second;

    // 检查重复分片
    if (m_db->isChunkReceived(fileId, chunkIndex)) {
        cout << "[FileTransferManager] Duplicate chunk " << chunkIndex
            << " for " << fileId << ", skipping" << endl;
        if (isLast) {
            isComplete = true;
        }
        return true;
    }

    // 顺序写入（假定分片按顺序到达，且无需偏移计算）
    session.stream.seekp(0, ios::end);
    session.stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    session.stream.flush();

    // 记录分片进度
    m_db->insertChunkProgress(fileId, chunkIndex);
    m_db->updateFileProgress(fileId, chunkIndex + 1);

    cout << "[FileTransferManager] Received chunk " << chunkIndex
        << " (" << data.size() << " bytes) for " << fileId
        << (isLast ? " [LAST]" : "") << endl;

    if (isLast) {
        session.stream.close();

        DatabaseManager::FileRecord rec;
        if (m_db->getFileRecord(fileId, rec)) {
            string actualMD5 = computeFileMD5(session.tempPath);
            if (!rec.md5.empty() && rec.md5 != "unknown" && actualMD5 != rec.md5) {
                errorMsg = "MD5 mismatch: expected " + rec.md5 + " got " + actualMD5;
                cerr << "[FileTransferManager] " << errorMsg << endl;
                m_db->updateFileStatus(fileId, "failed");
                m_db->clearChunkProgress(fileId);
                m_writeSessions.erase(it);
                return false;
            }

            error_code ec;
            fs::rename(session.tempPath, session.finalPath, ec);
            if (ec) {
                fs::remove(session.finalPath, ec);
                fs::rename(session.tempPath, session.finalPath, ec);
            }
            m_db->updateFileStatus(fileId, "completed", session.finalPath);
            m_db->clearChunkProgress(fileId);
            cout << "[FileTransferManager] File completed: " << session.finalPath
                << " MD5: " << actualMD5 << endl;
        }

        m_writeSessions.erase(it);
        isComplete = true;
    }

    return true;
}

vector<uint32_t> FileTransferManager::getMissingChunks(const string& fileId) {
    DatabaseManager::FileRecord rec;
    if (!m_db->getFileRecord(fileId, rec)) return {};

    vector<uint32_t> received;
    m_db->getReceivedChunks(fileId, received);

    vector<uint32_t> missing;
    for (uint32_t i = 0; i < rec.total_chunks; ++i) {
        if (find(received.begin(), received.end(), i) == received.end()) {
            missing.push_back(i);
        }
    }
    return missing;
}

void FileTransferManager::cancelTransfer(const string& fileId) {
    lock_guard<mutex> lock(m_mutex);
    auto it = m_writeSessions.find(fileId);
    if (it != m_writeSessions.end()) {
        if (it->second.stream.is_open()) it->second.stream.close();
        error_code ec;
        fs::remove(it->second.tempPath, ec);
        m_writeSessions.erase(it);
    }
    m_db->updateFileStatus(fileId, "failed");
    m_db->clearChunkProgress(fileId);
}

string FileTransferManager::getStoragePath(const string& fileId) {
    DatabaseManager::FileRecord rec;
    if (m_db->getFileRecord(fileId, rec)) return rec.storage_path;
    return "";
}

string FileTransferManager::computeMD5(const vector<uint8_t>& data) {
    uint64_t hash = 0;
    for (uint8_t b : data) {
        hash = hash * 31 + b;
    }
    stringstream ss;
    ss << hex << setfill('0') << setw(16) << hash;
    return ss.str();
}

string FileTransferManager::computeFileMD5(const string& filePath) {
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) return "unknown";
    uint64_t hash = 0;
    char buf[8192];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        for (streamsize i = 0; i < file.gcount(); ++i) {
            hash = hash * 31 + static_cast<uint8_t>(buf[i]);
        }
    }
    stringstream ss;
    ss << hex << setfill('0') << setw(16) << hash;
    return ss.str();
}

void FileTransferManager::pushFileToClient(uint32_t targetShipId, const string& fileId) {
    if (!m_pushCallback) {
        cerr << "[FileTransferManager] No push callback set, cannot push file" << endl;
        return;
    }

    DatabaseManager::FileRecord rec;
    if (!m_db->getFileRecord(fileId, rec) || rec.status != "completed") {
        cerr << "[FileTransferManager] File not ready for push: " << fileId << endl;
        return;
    }

    ifstream file(rec.storage_path, ios::binary);
    if (!file.is_open()) {
        cerr << "[FileTransferManager] Cannot open file for push: " << rec.storage_path << endl;
        return;
    }

    cout << "[FileTransferManager] Pushing file " << rec.file_name
        << " to ship " << targetShipId << endl;

    uint32_t chunkIndex = 0;
    vector<uint8_t> buffer(rec.chunk_size);
    while (file.read(reinterpret_cast<char*>(buffer.data()), rec.chunk_size) || file.gcount() > 0) {
        buffer.resize(file.gcount());
        bool isLast = (chunkIndex == rec.total_chunks - 1);
        m_pushCallback(targetShipId, fileId, rec.file_name, rec.file_size,
            rec.md5, rec.chunk_size, rec.total_chunks,
            chunkIndex, buffer, isLast);
        chunkIndex++;
    }
    file.close();
}