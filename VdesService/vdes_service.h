#ifndef VDES_SERVICE_H
#define VDES_SERVICE_H

#include "socket_utils.h"
#include "database_manager.h"
#include "file_transfer_manager.h"
#include "vdes_crc.h"
#ifndef ZK_DISABLED
#include "zk_client.h"
#endif
#include "slotscheduler.h"
#include "globallistener.h"
#include "thread_pool.h"
#include "uplinkshortackstatemachine.h"
#include "uplinkshortnoackstatemachine.h"
#include "uplinkaddressedstatemachine.h"
#include "downlinkaddressedstatemachine.h"
#include "downlinkshortackstatemachine.h"
#include "downlinkshortnoackstatemachine.h"
#include "broadcaststatemachine.h"
#include "pagingstatemachine.h"
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <atomic>
#include <functional>
#include <iomanip>
#include <sstream>

class ClientSession;

// 船舶状态机集合
struct ShipStateMachines {
    uint32_t shipId;
    UplinkShortAckStateMachine* uplinkShortAck;
    UplinkShortNoAckStateMachine* uplinkShortNoAck;
    UplinkAddressedStateMachine* uplinkAddressed;
    DownlinkAddressedStateMachine* downlinkAddressed;
    DownlinkShortAckStateMachine* downlinkShortAck;
    DownlinkShortNoAckStateMachine* downlinkShortNoAck;

    ShipStateMachines() : shipId(0),
        uplinkShortAck(nullptr), uplinkShortNoAck(nullptr), uplinkAddressed(nullptr),
        downlinkAddressed(nullptr), downlinkShortAck(nullptr), downlinkShortNoAck(nullptr) {}
};

class VdesService {
public:
    VdesService();
    ~VdesService();

    bool start();
    void stop();

    void sendShortAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data);
    void sendShortNoAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi,
        uint8_t data, bool hasDest, const std::vector<uint8_t>& payload);
    void sendAddressed(int specifiedSlot, uint32_t destId, uint32_t myMmsi,
        const std::vector<uint8_t>& data);
    void sendBoardConfig(int shipIndex, const std::vector<uint8_t>& configData);

    // 会话管理
    void onSessionDisconnected(ClientSession* session);
    void removeSession(ClientSession* session);

    // 管理器访问
    FileTransferManager& getFileTransferManager() { return m_fileMgr; }
    DatabaseManager& getDatabaseManager() { return m_db; }

    // 船舶状态机管理
    ShipStateMachines* getOrCreateShipMachines(uint32_t shipId);
    void removeShipMachines(uint32_t shipId);

    // 线程池
    ThreadPool& getThreadPool() { return m_threadPool; }

private:
    void initDatabase();
    void initStateMachines();
    void initZooKeeper();
    void initGlobalComponents();
    void cleanupStateMachines();
    void tcpServerThread();
    void udpReceiveThread();
    void processUdpPacket(const std::vector<uint8_t>& rawMsg, int currentSlot);

    bool m_running;
    socket_t m_tcpListenSocket;
    std::thread m_tcpThread;
    std::thread m_udpThread;
    std::mutex m_sessionsMutex;
    std::vector<ClientSession*> m_sessions;
    std::map<uint32_t, ClientSession*> m_shipToSession;

    FragmentManager* m_fragmentMgr;
    SlotScheduler* m_slotScheduler;
    GlobalListener* m_globalListener;
    BroadcastStateMachine* m_broadcastSM;
    PagingStateMachine* m_pagingSM;

    std::vector<VdesStateMachine*> m_stateMachines;
    std::map<uint32_t, UplinkShortAckStateMachine*> m_uplinkShortAckMap;
    std::map<uint32_t, UplinkShortNoAckStateMachine*> m_uplinkShortNoAckMap;
    std::map<uint32_t, UplinkAddressedStateMachine*> m_uplinkAddressedMap;

    // 多船状态机
    std::mutex m_shipMachinesMutex;
    std::map<uint32_t, ShipStateMachines*> m_shipMachinesMap;
    ThreadPool m_threadPool;

    DatabaseManager m_db;
    FileTransferManager m_fileMgr;
#ifndef ZK_DISABLED
    ZkClient m_zk;
#endif

    std::atomic<int> m_currentSlot;
};

#endif // VDES_SERVICE_H
