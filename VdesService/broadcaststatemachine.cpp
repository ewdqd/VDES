#include "broadcaststatemachine.h"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

BroadcastStateMachine::BroadcastStateMachine(FragmentManager* fragmentMgr)
    : VdesStateMachine(MessageType::Broadcast)
    , m_fragmentMgr(fragmentMgr)
    , m_currentState(Idle)
    , m_workingTimer()
    , m_hasReceivedStart(false)
    , m_hasReceivedContinue(false)
    , m_hasReceivedEnd(false)
{
    if (m_fragmentMgr) {
        m_fragmentMgr->onStart = [this]() { onStartReceived(); };
        m_fragmentMgr->onContinue = [this]() { onContinueReceived(); };
        m_fragmentMgr->onEnd = [this]() { onEndReceived(); };
        m_fragmentMgr->onReady = [this]() { onReadyReceived(); };
    }
}

BroadcastStateMachine::~BroadcastStateMachine() {
    m_workingTimer.stop();
}

void BroadcastStateMachine::onSlot(int currentSlot) {
    // 广播状态机不依赖于特定时隙触发行为
    // 时隙变化时检查是否超时
    if (m_currentState == Working) {
        // workingTimer 会自动超时，无需额外操作
    }
}

void BroadcastStateMachine::handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) {
    if (packet.empty()) return;

    uint8_t type = packet[0];
    // 广播资源分配（类型12）
    if (type == 12 && packet.size() >= 4) {
        uint8_t subType = packet[3];
        if (subType == 0x20) { // 广播资源分配的subType判断
            onBroadcastResourceAllocationReceived();
            return;
        }
    }

    // 广播分片（类型30/31/32）
    if ((type == 30 || type == 31 || type == 32) && m_fragmentMgr) {
        if (packet.size() < 15) return;

        BaseFragment frag;
        frag.type = type;
        frag.fieldSize = (static_cast<uint16_t>(packet[1]) << 8) | packet[2];
        frag.srcRadioId = (static_cast<uint32_t>(packet[3]) << 24) |
                          (static_cast<uint32_t>(packet[4]) << 16) |
                          (static_cast<uint32_t>(packet[5]) << 8)  |
                          static_cast<uint32_t>(packet[6]);
        frag.satelliteId = packet[7];
        frag.sessionId = packet[8];
        frag.destRadioId = (static_cast<uint32_t>(packet[9]) << 24) |
                           (static_cast<uint32_t>(packet[10]) << 16) |
                           (static_cast<uint32_t>(packet[11]) << 8)  |
                           static_cast<uint32_t>(packet[12]);
        frag.fragmentNum = (static_cast<uint16_t>(packet[13]) << 8) | packet[14];

        if (packet.size() > 15) {
            frag.payload.assign(packet.begin() + 15, packet.end());
        }

        if (type == 30) {
            m_fragmentMgr->setStartFragment(frag);
        } else if (type == 31) {
            m_fragmentMgr->addContinueFragment(frag);
        } else if (type == 32) {
            m_fragmentMgr->setEndFragment(frag);
        }

        if (logMessage) {
            ostringstream oss;
            oss << "[BroadcastSM] Received fragment type=" << (int)type
                << " fragNum=" << frag.fragmentNum
                << " src=" << frag.srcRadioId
                << " size=" << frag.payload.size();
            logMessage(oss.str());
        }
    }
}

void BroadcastStateMachine::onBroadcastResourceAllocationReceived() {
    if (m_currentState == Idle) {
        switchState(Working);
        m_workingTimer.start(std::chrono::milliseconds(WORK_TIMEOUT_MS),
                             [this]() { onTimerTimeout(); }, true);
        m_hasReceivedStart = false;
        m_hasReceivedContinue = false;
        m_hasReceivedEnd = false;

        if (logMessage) {
            logMessage("[BroadcastSM] Broadcast resource allocation received, entering Working state");
        }
    }
}

void BroadcastStateMachine::onReadyReceived() {
    // FragmentManager 资源就绪回调
}

void BroadcastStateMachine::onStartReceived() {
    m_hasReceivedStart = true;
    if (logMessage) {
        logMessage("[BroadcastSM] Start fragment received");
    }
}

void BroadcastStateMachine::onContinueReceived() {
    m_hasReceivedContinue = true;
    if (logMessage) {
        logMessage("[BroadcastSM] Continue fragment received");
    }
}

void BroadcastStateMachine::onEndReceived() {
    m_hasReceivedEnd = true;

    if (logMessage) {
        logMessage("[BroadcastSM] End fragment received, assembling data...");
    }

    assembleAndShowData();
}

void BroadcastStateMachine::onTimerTimeout() {
    if (m_currentState == Working) {
        if (logMessage) {
            logMessage("[BroadcastSM] Working timeout reached");
        }

        // 如果已经收到结束分片，仍然尝试组装
        if (m_hasReceivedEnd) {
            assembleAndShowData();
        } else {
            switchState(Abnormal);
            if (logMessage) {
                logMessage("[BroadcastSM] Timeout without end fragment, entering Abnormal state");
            }
        }
    }
}

void BroadcastStateMachine::switchState(State newState) {
    State oldState = m_currentState;
    m_currentState = newState;

    if (stateChanged) {
        stateChanged(newState);
    }

    if (logMessage) {
        ostringstream oss;
        oss << "[BroadcastSM] State: " << oldState << " -> " << newState;
        logMessage(oss.str());
    }
}

void BroadcastStateMachine::assembleAndShowData() {
    if (!m_fragmentMgr) {
        if (logMessage) logMessage("[BroadcastSM] No fragment manager, cannot assemble");
        return;
    }

    BaseFragment startFrag = m_fragmentMgr->getStartFragment();
    vector<BaseFragment> continueFrags = m_fragmentMgr->getContinueFragments();
    BaseFragment endFrag = m_fragmentMgr->getEndFragment();

    // 组装完整数据
    vector<uint8_t> assembledData;
    assembledData.insert(assembledData.end(), startFrag.payload.begin(), startFrag.payload.end());
    for (auto& cf : continueFrags) {
        assembledData.insert(assembledData.end(), cf.payload.begin(), cf.payload.end());
    }
    assembledData.insert(assembledData.end(), endFrag.payload.begin(), endFrag.payload.end());

    uint32_t sourceId = startFrag.srcRadioId;

    if (logMessage) {
        ostringstream oss;
        oss << "[BroadcastSM] Broadcast data assembled: " << assembledData.size() << " bytes from source " << sourceId;
        logMessage(oss.str());
    }

    // 发送回调
    if (broadcastDataReceived) {
        broadcastDataReceived(assembledData, sourceId);
    }
    if (dataReceived) {
        dataReceived(assembledData, sourceId);
    }

    // 重置状态
    m_workingTimer.stop();
    m_fragmentMgr->clearContinueFragments();
    switchState(Idle);
}
