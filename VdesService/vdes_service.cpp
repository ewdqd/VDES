#include "vdes_service.h"
#include "client_session.h"
#include "udp_receiver_worker.h"
#include "fragmentmanager.h"
#include "globallistener.h"
#include "slotscheduler.h"
#include "broadcaststatemachine.h"
#include "downlinkaddressedstatemachine.h"
#include "downlinkshortackstatemachine.h"
#include "downlinkshortnoackstatemachine.h"
#include "pagingstatemachine.h"
#include "vdes_global.h"
#include "db_connection_pool.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

using namespace std;

// 辅助函数
static string bytesToHex(const vector<uint8_t>& data, size_t maxLen = 256) {
    stringstream ss;
    size_t len = min(data.size(), maxLen);
    for (size_t i = 0; i < len; ++i) {
        ss << hex << setw(2) << setfill('0') << (int)data[i];
        if ((i + 1) % 32 == 0 && i + 1 < len) ss << "\n";
        else if (i + 1 < len) ss << " ";
    }
    if (data.size() > maxLen) ss << " ... (truncated)";
    return ss.str();
}

VdesService::VdesService()
    : m_running(false)
    , m_tcpListenSocket(-1)
    , m_fragmentMgr(nullptr)
    , m_slotScheduler(nullptr)
    , m_globalListener(nullptr)
    , m_broadcastSM(nullptr)
    , m_pagingSM(nullptr)
    , m_fileMgr(&m_db)
    , m_currentSlot(0)
{
    vdes_global_init();
    initGlobalComponents();
    initStateMachines();
    initDatabase();
    initZooKeeper();
}

VdesService::~VdesService() { stop(); }

void VdesService::initGlobalComponents() {
    g_udpSocket = create_udp_socket();
    m_fragmentMgr = new FragmentManager();
    m_slotScheduler = new SlotScheduler();
    m_globalListener = new GlobalListener(m_fragmentMgr);
    m_slotScheduler->setListener(m_globalListener);
}

void VdesService::initStateMachines() {
    // 通用日志推送回调（推送到所有TCP客户端）
    auto globalLogCb = [this](const string& msg) {
        cout << "[SM:global] " << msg << endl;
        m_db.insertLog(msg, m_currentSlot.load(), 0, "SM", "StateMachine", msg, "");
        lock_guard<mutex> lock(m_sessionsMutex);
        for (auto* session : m_sessions) {
            if (session) {
                session->pushLogEntry(
                    to_string(chrono::duration_cast<chrono::milliseconds>(
                        chrono::system_clock::now().time_since_epoch()).count()),
                    m_currentSlot.load(), 0, "SM", "StateMachine", msg, msg);
            }
        }
    };

    // 广播状态机（全局共享）
    m_broadcastSM = new BroadcastStateMachine(m_fragmentMgr);
    m_broadcastSM->broadcastDataReceived = [this](const vector<uint8_t>& data, uint32_t srcId) {
        string hexData = bytesToHex(data, 200);
        cout << "[Broadcast] Data received from " << srcId << ": " << data.size() << " bytes" << endl;

        // 推送给所有已连接客户端
        lock_guard<mutex> lock(m_sessionsMutex);
        for (auto* session : m_sessions) {
            if (session) {
                // 通过日志推送广播数据（简化为日志条目）
                session->pushLogEntry(
                    to_string(chrono::duration_cast<chrono::milliseconds>(
                        chrono::system_clock::now().time_since_epoch()).count()),
                    m_currentSlot.load(), 0, "Rx", "Broadcast",
                    "Broadcast data: " + to_string(data.size()) + " bytes", hexData);
            }
        }
    };
    // 设置全局状态机的日志回调
    m_broadcastSM->logMessage = globalLogCb;
    m_stateMachines.push_back(m_broadcastSM);
    m_slotScheduler->addStateMachine(m_broadcastSM);

    // 连接广播资源分配回调（从 GlobalListener → BroadcastStateMachine）
    if (m_globalListener) {
        m_globalListener->broadcastResourceAllocationReceived = [this]() {
            cout << "[VdesService] Broadcast resource allocation received, notifying BroadcastSM" << endl;
            if (m_broadcastSM) {
                m_broadcastSM->onBroadcastResourceAllocationReceived();
            }
            // 推送日志到客户端
            lock_guard<mutex> lock(m_sessionsMutex);
            for (auto* session : m_sessions) {
                if (session) {
                    session->pushLogEntry(
                        to_string(chrono::duration_cast<chrono::milliseconds>(
                            chrono::system_clock::now().time_since_epoch()).count()),
                        m_currentSlot.load(), 0, "Rx", "Broadcast",
                        "广播资源分配已接收", "");
                }
            }
        };
    }

    // 下行寻址状态机（全局共享，多船只读）
    DownlinkAddressedStateMachine* downAddrSM = new DownlinkAddressedStateMachine();
    downAddrSM->logMessage = globalLogCb;
    m_stateMachines.push_back(downAddrSM);
    m_slotScheduler->addStateMachine(downAddrSM);

    // 下行短消息ACK（全局共享）
    DownlinkShortAckStateMachine* downShortAckSM = new DownlinkShortAckStateMachine();
    downShortAckSM->logMessage = globalLogCb;
    m_stateMachines.push_back(downShortAckSM);
    m_slotScheduler->addStateMachine(downShortAckSM);

    // 下行短消息无ACK（全局共享）
    DownlinkShortNoAckStateMachine* downShortNoAckSM = new DownlinkShortNoAckStateMachine();
    downShortNoAckSM->logMessage = globalLogCb;
    m_stateMachines.push_back(downShortNoAckSM);
    m_slotScheduler->addStateMachine(downShortNoAckSM);

    // 寻呼状态机（全局共享）
    m_pagingSM = new PagingStateMachine();
    m_pagingSM->logMessage = globalLogCb;
    m_stateMachines.push_back(m_pagingSM);
    m_slotScheduler->addStateMachine(m_pagingSM);

    // 全局监听回调
    if (m_globalListener) {
        m_globalListener->myResourceAllocationReceived = [this](uint8_t logicChannel, uint8_t linkID,
            uint8_t sessionID, uint8_t uplinkCQI) {
                // 使用线程池分发到各船舶状态机
                lock_guard<mutex> lock(m_shipMachinesMutex);
                for (auto& pair : m_shipMachinesMap) {
                    ShipStateMachines* sm = pair.second;
                    if (sm && sm->uplinkAddressed) {
                        m_threadPool.enqueue([sm, logicChannel, linkID, sessionID, uplinkCQI]() {
                            sm->uplinkAddressed->onMyResourceAllocationReceived(
                                logicChannel, linkID, sessionID, uplinkCQI);
                        });
                    }
                }
                // 旧有映射兼容
                for (auto& pair : m_uplinkAddressedMap) {
                    pair.second->onMyResourceAllocationReceived(logicChannel, linkID, sessionID, uplinkCQI);
                }
            };
        m_globalListener->macFrameUpdated = [this]() {
            lock_guard<mutex> lock(m_shipMachinesMutex);
            for (auto& pair : m_shipMachinesMap) {
                ShipStateMachines* sm = pair.second;
                if (sm) {
                    m_threadPool.enqueue([sm]() {
                        if (sm->uplinkAddressed) sm->uplinkAddressed->onMacFrameUpdated();
                        if (sm->uplinkShortAck) sm->uplinkShortAck->onMacFrameUpdated();
                        if (sm->uplinkShortNoAck) sm->uplinkShortNoAck->onMacFrameUpdated();
                    });
                }
            }
            // 旧有映射兼容
            for (auto& pair : m_uplinkAddressedMap) pair.second->onMacFrameUpdated();
            for (auto& pair : m_uplinkShortAckMap) pair.second->onMacFrameUpdated();
            for (auto& pair : m_uplinkShortNoAckMap) pair.second->onMacFrameUpdated();
            };
    }

    m_slotScheduler->slotUpdateCallback = [this](int slot) {
        m_currentSlot.store(slot);
        // 分发时隙更新到所有船舶状态机
        {
            lock_guard<mutex> lock(m_shipMachinesMutex);
            for (auto& pair : m_shipMachinesMap) {
                ShipStateMachines* sm = pair.second;
                if (sm) {
                    m_threadPool.enqueue([sm, slot]() {
                        if (sm->uplinkShortAck) sm->uplinkShortAck->onSlot(slot);
                        if (sm->uplinkShortNoAck) sm->uplinkShortNoAck->onSlot(slot);
                        if (sm->uplinkAddressed) sm->uplinkAddressed->onSlot(slot);
                        if (sm->downlinkAddressed) sm->downlinkAddressed->onSlot(slot);
                        if (sm->downlinkShortAck) sm->downlinkShortAck->onSlot(slot);
                        if (sm->downlinkShortNoAck) sm->downlinkShortNoAck->onSlot(slot);
                    });
                }
            }
        }
        // 推送时隙更新到所有已连接的 TCP 客户端
        {
            lock_guard<mutex> lock(m_sessionsMutex);
            for (auto* session : m_sessions) {
                if (session) {
                    session->pushSlotUpdate(slot);
                }
            }
        }
    };
}

void VdesService::initDatabase() {
    m_db.open("vdes_data.db");

    // 初始化数据库连接池（支持高并发访问）
    if (DBConnectionPool::instance().initialize("vdes_data.db", 4)) {
        cout << "[VdesService] Database connection pool initialized (4 connections)" << endl;
    } else {
        cerr << "[VdesService] Failed to initialize database connection pool" << endl;
    }
}

void VdesService::initZooKeeper() {
#ifndef ZK_DISABLED
    // 使用真实 ZooKeeper 服务发现
    if (m_zk.connect("127.0.0.1:2181")) {
        if (m_zk.registerService("127.0.0.1", 12345)) {
            cout << "[VdesService] Registered with ZooKeeper" << endl;
        }
        else {
            cout << "[VdesService] Failed to register service with ZooKeeper" << endl;
        }
    }
    else {
        cout << "[VdesService] Failed to connect to ZooKeeper, running standalone" << endl;
    }
#else
    cout << "[VdesService] ZooKeeper disabled, running standalone" << endl;
#endif
}

void VdesService::cleanupStateMachines() {
    // 清理全局状态机
    for (auto sm : m_stateMachines) delete sm;
    m_stateMachines.clear();

    // 清理多船状态机
    lock_guard<mutex> lock(m_shipMachinesMutex);
    for (auto& pair : m_shipMachinesMap) {
        ShipStateMachines* sm = pair.second;
        if (sm) {
            delete sm->uplinkShortAck;
            delete sm->uplinkShortNoAck;
            delete sm->uplinkAddressed;
            delete sm->downlinkAddressed;
            delete sm->downlinkShortAck;
            delete sm->downlinkShortNoAck;
            delete sm;
        }
    }
    m_shipMachinesMap.clear();

    // 清理旧有映射
    for (auto& pair : m_uplinkShortAckMap) delete pair.second;
    m_uplinkShortAckMap.clear();
    for (auto& pair : m_uplinkShortNoAckMap) delete pair.second;
    m_uplinkShortNoAckMap.clear();
    for (auto& pair : m_uplinkAddressedMap) delete pair.second;
    m_uplinkAddressedMap.clear();

    delete m_fragmentMgr;
    delete m_slotScheduler;
    delete m_globalListener;
}

bool VdesService::start() {
    if (!init_sockets()) return false;
    g_udpSocket = create_udp_socket();
    if (!bind_socket(g_udpSocket, "0.0.0.0", 9090)) {
        cerr << "UDP bind failed" << endl;
        return false;
    }
    m_udpThread = thread(&VdesService::udpReceiveThread, this);

    m_tcpListenSocket = create_tcp_socket();
    if (!bind_socket(m_tcpListenSocket, "0.0.0.0", 12345)) {
        cerr << "TCP bind failed" << endl;
        return false;
    }
    if (!listen_tcp(m_tcpListenSocket, 5)) {
        cerr << "TCP listen failed" << endl;
        return false;
    }
    cout << "TCP server listening on 0.0.0.0:12345" << endl;
    m_running = true;
    m_tcpThread = thread(&VdesService::tcpServerThread, this);
    cout << "VDES Service started (ZooKeeper enabled)" << endl;
    return true;
}

void VdesService::stop() {
    m_running = false;
    if (m_tcpListenSocket != -1) close_socket(m_tcpListenSocket);
    if (g_udpSocket != -1) close_socket(g_udpSocket);

    if (m_tcpThread.joinable()) m_tcpThread.join();
    if (m_udpThread.joinable()) m_udpThread.join();

    // 安全删除所有会话
    vector<ClientSession*> sessionsToDelete;
    {
        lock_guard<mutex> lock(m_sessionsMutex);
        sessionsToDelete.swap(m_sessions);
    }
    for (ClientSession* s : sessionsToDelete) {
        if (s) {
            thread([s]() { delete s; }).detach();
        }
    }

    cleanupStateMachines();
    cleanup_sockets();
    m_db.close();
    DBConnectionPool::instance().shutdown();
}

void VdesService::tcpServerThread() {
    while (m_running) {
        string client_ip;
        uint16_t client_port;
        socket_t client_sock = accept_tcp(m_tcpListenSocket, client_ip, client_port);
        if (client_sock != -1) {
            ClientSession* session = new ClientSession(client_sock, this);
            lock_guard<mutex> lock(m_sessionsMutex);
            m_sessions.push_back(session);
            cout << "New client connected: " << client_ip << ":" << client_port
                << " (total sessions: " << m_sessions.size() << ")" << endl;
        }
    }
}

void VdesService::udpReceiveThread() {
    vector<uint8_t> buffer(65536);
    while (m_running) {
        string src_ip;
        uint16_t src_port;
        int ret = recv_udp(g_udpSocket, buffer.data(), buffer.size(), src_ip, src_port);
        if (ret <= 0) continue;

        // 时隙同步帧
        if (ret >= 5 && buffer[0] == 0x55 && buffer[1] == 0xAA && buffer[2] == 0xFF) {
            int slot = (buffer[3] << 8) | buffer[4];
            m_slotScheduler->updateSlotFromBoard(slot);
            continue;
        }

        // 标准板卡数据帧
        if (ret < 34 || buffer[0] != 0xEB || buffer[1] != 0x90 || buffer[2] != 0xF9) {
            continue;
        }

        uint16_t msgLen = (static_cast<uint16_t>(buffer[6]) << 8) | static_cast<uint16_t>(buffer[7]);
        if (msgLen < 26) continue;
        int rawLen = static_cast<int>(msgLen) - 26;
        if (rawLen <= 0) continue;
        const int PAYLOAD_OFFSET = 34;
        if (static_cast<int>(buffer.size()) < PAYLOAD_OFFSET + rawLen) continue;

        vector<uint8_t> rawMsg(buffer.begin() + PAYLOAD_OFFSET,
            buffer.begin() + PAYLOAD_OFFSET + rawLen);
        int currentSlot = m_slotScheduler->currentSlot();

        // 使用线程池分发数据包处理，支持多船并行
        m_threadPool.enqueue([this, rawMsg = std::move(rawMsg), currentSlot]() {
            processUdpPacket(rawMsg, currentSlot);
        });
    }
}

void VdesService::processUdpPacket(const std::vector<uint8_t>& rawMsg, int currentSlot) {
    if (rawMsg.empty()) return;
    uint8_t type = rawMsg[0];
    (void)type; (void)currentSlot;

    // 自动推进时隙并推送给客户端
    int slot = (m_currentSlot.fetch_add(1) + 1) % 2250;
    {
        lock_guard<mutex> lock(m_sessionsMutex);
        for (auto* session : m_sessions) {
            if (session) session->pushSlotUpdate(slot);
        }
    }

    // 全局监听器统一处理
    if (m_slotScheduler) {
        m_slotScheduler->injectPacket(rawMsg, slot, 0);
    }

    // 提取源船舶ID（根据不同类型的偏移量不同）
    uint32_t shipId = 0;
    if ((type == 20 || type == 21 || type == 23 || type == 33) && rawMsg.size() >= 5) {
        // 资源请求(20): 字节1-4
        // 寻呼响应(21): 字节1-4
        // 上行短消息无ACK(23): 字节1-4
        // 上行短消息ACK(33): 字节1-4
        shipId = (static_cast<uint32_t>(rawMsg[1]) << 24) |
                 (static_cast<uint32_t>(rawMsg[2]) << 16) |
                 (static_cast<uint32_t>(rawMsg[3]) << 8)  |
                 static_cast<uint32_t>(rawMsg[4]);
    } else if ((type == 30 || type == 31 || type == 32) && rawMsg.size() >= 7) {
        // 分片消息(30/31/32): 字节3-6为源ID
        shipId = (static_cast<uint32_t>(rawMsg[3]) << 24) |
                 (static_cast<uint32_t>(rawMsg[4]) << 16) |
                 (static_cast<uint32_t>(rawMsg[5]) << 8)  |
                 static_cast<uint32_t>(rawMsg[6]);
    }

    // 如果提取到有效船舶ID，确保其有独立状态机
    if (shipId != 0 && m_shipMachinesMap.find(shipId) == m_shipMachinesMap.end()) {
        // 懒创建：不阻塞当前线程
        getOrCreateShipMachines(shipId);
    }

    // 日志记录
    ostringstream oss;
    oss << "UDP packet type=" << (int)type << " ship=" << shipId
        << " slot=" << currentSlot << " size=" << rawMsg.size();
    cout << "[VdesService] " << oss.str() << endl;
}

// ========== 会话管理（线程安全删除） ==========
void VdesService::onSessionDisconnected(ClientSession* session) {
    if (!session) return;

    bool removed = false;
    {
        lock_guard<mutex> lock(m_sessionsMutex);
        auto it = find(m_sessions.begin(), m_sessions.end(), session);
        if (it != m_sessions.end()) {
            m_sessions.erase(it);
            removed = true;
        }
        // 从船舶映射中移除
        for (auto sit = m_shipToSession.begin(); sit != m_shipToSession.end(); ++sit) {
            if (sit->second == session) {
                m_shipToSession.erase(sit);
                break;
            }
        }
    }

    if (removed) {
        // 延迟删除，确保 readLoop 线程完全退出
        thread([session]() {
            this_thread::sleep_for(chrono::milliseconds(100));
            delete session;
            }).detach();
    }
}

void VdesService::removeSession(ClientSession* session) {
    onSessionDisconnected(session);
}

// ========== 多船并行状态机管理 ==========
ShipStateMachines* VdesService::getOrCreateShipMachines(uint32_t shipId) {
    if (shipId == 0) return nullptr;

    lock_guard<mutex> lock(m_shipMachinesMutex);

    auto it = m_shipMachinesMap.find(shipId);
    if (it != m_shipMachinesMap.end()) {
        return it->second;
    }

    // 为新船舶创建独立的状态机集合
    ShipStateMachines* sm = new ShipStateMachines();
    sm->shipId = shipId;

    // 创建上行状态机
    sm->uplinkShortAck = new UplinkShortAckStateMachine();
    sm->uplinkShortNoAck = new UplinkShortNoAckStateMachine();
    sm->uplinkAddressed = new UplinkAddressedStateMachine();

    // 创建下行状态机
    sm->downlinkAddressed = new DownlinkAddressedStateMachine();
    sm->downlinkShortAck = new DownlinkShortAckStateMachine();
    sm->downlinkShortNoAck = new DownlinkShortNoAckStateMachine();

    // 设置日志回调（同时推送到所有TCP客户端）
    auto logCb = [this, shipId](const string& msg) {
        cout << "[SM:" << shipId << "] " << msg << endl;
        m_db.insertLog(msg, m_currentSlot.load(), shipId, "SM", "StateMachine", msg, "");

        // 推送到所有已连接客户端
        lock_guard<mutex> lock(m_sessionsMutex);
        for (auto* session : m_sessions) {
            if (session) {
                session->pushLogEntry(
                    to_string(chrono::duration_cast<chrono::milliseconds>(
                        chrono::system_clock::now().time_since_epoch()).count()),
                    m_currentSlot.load(), shipId, "SM", "StateMachine", msg, msg);
            }
        }
    };

    sm->uplinkShortAck->logMessage = logCb;
    sm->uplinkShortNoAck->logMessage = logCb;
    sm->uplinkAddressed->logMessage = logCb;
    sm->downlinkAddressed->logMessage = logCb;
    sm->downlinkShortAck->logMessage = logCb;
    sm->downlinkShortNoAck->logMessage = logCb;

    // 设置数据接收回调
    sm->uplinkAddressed->dataReceived = [this, shipId](const vector<uint8_t>& data, uint32_t srcId) {
        cout << "[SM:" << shipId << "] Addressed data received from " << srcId
             << ": " << data.size() << " bytes" << endl;
    };

    // 注册到调度器
    if (m_slotScheduler) {
        m_slotScheduler->addStateMachine(sm->uplinkShortAck);
        m_slotScheduler->addStateMachine(sm->uplinkShortNoAck);
        m_slotScheduler->addStateMachine(sm->uplinkAddressed);
        m_slotScheduler->addStateMachine(sm->downlinkAddressed);
        m_slotScheduler->addStateMachine(sm->downlinkShortAck);
        m_slotScheduler->addStateMachine(sm->downlinkShortNoAck);
    }

    // 保存到旧映射兼容
    m_uplinkShortAckMap[shipId] = sm->uplinkShortAck;
    m_uplinkShortNoAckMap[shipId] = sm->uplinkShortNoAck;
    m_uplinkAddressedMap[shipId] = sm->uplinkAddressed;

    m_shipMachinesMap[shipId] = sm;

    cout << "[VdesService] Created per-ship state machines for ship " << shipId << endl;
    return sm;
}

void VdesService::removeShipMachines(uint32_t shipId) {
    lock_guard<mutex> lock(m_shipMachinesMutex);

    auto it = m_shipMachinesMap.find(shipId);
    if (it == m_shipMachinesMap.end()) return;

    ShipStateMachines* sm = it->second;
    if (sm) {
        // 从调度器移除（若实现removeStateMachine）
        if (m_slotScheduler) {
            // 移除注册
        }
        delete sm->uplinkShortAck;
        delete sm->uplinkShortNoAck;
        delete sm->uplinkAddressed;
        delete sm->downlinkAddressed;
        delete sm->downlinkShortAck;
        delete sm->downlinkShortNoAck;
        delete sm;
    }

    m_shipMachinesMap.erase(it);
    m_uplinkShortAckMap.erase(shipId);
    m_uplinkShortNoAckMap.erase(shipId);
    m_uplinkAddressedMap.erase(shipId);

    cout << "[VdesService] Removed per-ship state machines for ship " << shipId << endl;
}

// ========== 发送接口（完整实现） ==========
void VdesService::sendShortAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data) {
    cout << "sendShortAck: slot=" << specifiedSlot << " dest=" << destId
         << " my=" << myMmsi << " data=" << (int)data << endl;

    // 构建上行短消息ACK帧（类型33）
    vector<uint8_t> frame;
    frame.push_back(33);                    // 消息类型
    frame.push_back((myMmsi >> 24) & 0xFF); // 源ID
    frame.push_back((myMmsi >> 16) & 0xFF);
    frame.push_back((myMmsi >> 8) & 0xFF);
    frame.push_back(myMmsi & 0xFF);
    frame.push_back((destId >> 24) & 0xFF); // 目的ID
    frame.push_back((destId >> 16) & 0xFF);
    frame.push_back((destId >> 8) & 0xFF);
    frame.push_back(destId & 0xFF);
    frame.push_back(data);                  // 数据载荷

    // 通过全局监听器或直接转发到板卡
    int currentSlot = m_slotScheduler ? m_slotScheduler->currentSlot() : 0;
    int sendSlot = (specifiedSlot >= 0) ? specifiedSlot : (currentSlot + 2);

    // 使用全局 sendToBoard 函数发送到板卡
    uint8_t phyChannel = 0x0A;
    sendToBoard(phyChannel, sendSlot, 20, frame.data(), (int)frame.size(), "sendShortAck");

    // 日志记录
    m_db.insertLog("SendShortAck", currentSlot, myMmsi, "Tx", "UplinkShortAck",
                   "sendShortAck dispatched", "Dest=" + to_string(destId) + " data=" + to_string((int)data));
}

void VdesService::sendShortNoAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi,
    uint8_t data, bool hasDest, const vector<uint8_t>& payload) {
    cout << "sendShortNoAck: slot=" << specifiedSlot << " dest=" << destId
         << " my=" << myMmsi << " data=" << (int)data << " hasDest=" << hasDest
         << " payloadSize=" << payload.size() << endl;

    // 构建上行短消息无ACK帧（类型23-28）
    uint8_t msgType = hasDest ? 23 : 24;
    vector<uint8_t> frame;
    frame.push_back(msgType);
    frame.push_back((myMmsi >> 24) & 0xFF);
    frame.push_back((myMmsi >> 16) & 0xFF);
    frame.push_back((myMmsi >> 8) & 0xFF);
    frame.push_back(myMmsi & 0xFF);

    if (hasDest) {
        frame.push_back((destId >> 24) & 0xFF);
        frame.push_back((destId >> 16) & 0xFF);
        frame.push_back((destId >> 8) & 0xFF);
        frame.push_back(destId & 0xFF);
        frame.push_back(data);
    } else {
        // 无目的ID：使用payload填入5字节
        for (int i = 0; i < 5 && i < (int)payload.size(); ++i) {
            frame.push_back(payload[i]);
        }
        while (frame.size() < 4 + 5) frame.push_back(0);
    }

    int currentSlot = m_slotScheduler ? m_slotScheduler->currentSlot() : 0;
    int sendSlot = (specifiedSlot >= 0) ? specifiedSlot : (currentSlot + 2);

    uint8_t phyChannel = 0x0A;
    sendToBoard(phyChannel, sendSlot, 20, frame.data(), (int)frame.size(), "sendShortNoAck");

    m_db.insertLog("SendShortNoAck", currentSlot, myMmsi, "Tx", "UplinkShortNoAck",
                   "sendShortNoAck dispatched", "Dest=" + to_string(destId) + " type=" + to_string(msgType));
}

void VdesService::sendAddressed(int specifiedSlot, uint32_t destId, uint32_t myMmsi,
    const vector<uint8_t>& data) {
    cout << "sendAddressed: slot=" << specifiedSlot << " dest=" << destId
         << " my=" << myMmsi << " dataSize=" << data.size() << endl;

    // 构建上行寻址消息（类型20 + 30/31/32分片）
    // 先发送资源请求（类型20）
    uint8_t sessionId = 1; // 简化：实际应从session管理获取

    vector<uint8_t> resReq;
    resReq.push_back(20);                    // 资源请求类型
    resReq.push_back((myMmsi >> 24) & 0xFF);
    resReq.push_back((myMmsi >> 16) & 0xFF);
    resReq.push_back((myMmsi >> 8) & 0xFF);
    resReq.push_back(myMmsi & 0xFF);
    resReq.push_back(1);                     // 卫星ID
    resReq.push_back((uint8_t)data.size());  // 优先级/消息长度
    resReq.push_back(1);                     // 终端能力
    resReq.push_back(10);                    // 下行链路ASC CQI
    resReq.push_back(0);                     // TDB（预留字段，符合规格10B）

    int currentSlot = m_slotScheduler ? m_slotScheduler->currentSlot() : 0;
    int sendSlot = (specifiedSlot >= 0) ? specifiedSlot : (currentSlot + 2);

    uint8_t phyChannel = 0x0A;
    sendToBoard(phyChannel, sendSlot, 20, resReq.data(), (int)resReq.size(), "sendAddressed(ResourceReq)");

    // 分片发送实际数据（类型30/31/32）
    const int CHUNK_SIZE = 100;
    size_t dataSize = data.size();
    size_t numFragments = (dataSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    if (numFragments == 0) numFragments = 1;

    for (size_t i = 0; i < numFragments; ++i) {
        uint8_t fragType;
        if (i == 0) fragType = 30;       // 起始分片
        else if (i == numFragments - 1) fragType = 32;  // 结束分片
        else fragType = 31;              // 中间分片

        size_t offset = i * CHUNK_SIZE;
        size_t len = min(CHUNK_SIZE, (int)(dataSize - offset));

        vector<uint8_t> fragFrame;
        fragFrame.push_back(fragType);
        uint16_t fieldSize = 12;
        fragFrame.push_back((fieldSize >> 8) & 0xFF);
        fragFrame.push_back(fieldSize & 0xFF);
        fragFrame.push_back((myMmsi >> 24) & 0xFF);
        fragFrame.push_back((myMmsi >> 16) & 0xFF);
        fragFrame.push_back((myMmsi >> 8) & 0xFF);
        fragFrame.push_back(myMmsi & 0xFF);
        fragFrame.push_back(1);           // 卫星ID
        fragFrame.push_back(sessionId);
        fragFrame.push_back((destId >> 24) & 0xFF);
        fragFrame.push_back((destId >> 16) & 0xFF);
        fragFrame.push_back((destId >> 8) & 0xFF);
        fragFrame.push_back(destId & 0xFF);
        uint16_t fragNum = (uint16_t)i;
        fragFrame.push_back((fragNum >> 8) & 0xFF);
        fragFrame.push_back(fragNum & 0xFF);
        fragFrame.insert(fragFrame.end(), data.begin() + offset, data.begin() + offset + len);

        sendToBoard(phyChannel, sendSlot + (int)i, 20, fragFrame.data(), (int)fragFrame.size(),
                    "sendAddressed(Fragment)");
    }

    m_db.insertLog("SendAddressed", currentSlot, myMmsi, "Tx", "UplinkAddressed",
                   "sendAddressed dispatched",
                   "Dest=" + to_string(destId) + " size=" + to_string(data.size()) +
                   " fragments=" + to_string(numFragments));
}

void VdesService::sendBoardConfig(int shipIndex, const vector<uint8_t>& configData) {
    cout << "sendBoardConfig: shipIndex=" << shipIndex << " configSize=" << configData.size() << endl;

    // 配置帧直接发送到板卡UDP端口
    int currentSlot = m_slotScheduler ? m_slotScheduler->currentSlot() : 0;
    uint8_t phyChannel = 0x0A;

    sendToBoard(phyChannel, currentSlot + 2, 20, configData.data(), (int)configData.size(), "sendBoardConfig");

    m_db.insertLog("SendBoardConfig", currentSlot, 0, "Tx", "Config",
                   "Board config sent", "ShipIndex=" + to_string(shipIndex) +
                   " size=" + to_string(configData.size()));
}