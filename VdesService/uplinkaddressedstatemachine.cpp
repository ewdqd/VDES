#include "uplinkaddressedstatemachine.h"
#include "vdes_global.h"
#include "socket_utils.h"
#include <cstring>
#include <iostream>
#include <chrono>
#include <algorithm>

using namespace std::chrono_literals;

extern SatelliteBulletinBoard g_currentSBB;
extern ASC_MAC ASC_MAC_msg;
extern GL_event_flag GL_event_control;

UplinkAddressedStateMachine::UplinkAddressedStateMachine()
    : VdesStateMachine(MessageType::UplinkAddressed)
    , m_state(InternalState::Idle)
    , m_satelliteID(0), m_mainNetID(0), m_roamNetID(0), m_uplinkMaxLength(1024)
    , m_mediaAccessPriority(0), m_racMsgAccessLimit(3), m_networkStatus(0), m_arqTimeoutLimit(3)
    , m_totalFragments(0), m_nextFragment(0), m_assignedLinkId(0)
    , m_assignedLogicChannel(0), m_sessionId(0), m_uplinkCqi(0)
    , m_linkIdChanged(false), m_retryCount15min(0)
    , m_specifiedRacSlot(-1), m_useSpecifiedRacSlot(false), m_missedRacSlot(false), m_racRetryCount(0)
    , m_myMmsi(MY_MMSI) {
    initStateMachine();
    updateConfigFromGlobals();
}

UplinkAddressedStateMachine::~UplinkAddressedStateMachine() {}

void UplinkAddressedStateMachine::initStateMachine() {
    onEnterIdleState();
}

void UplinkAddressedStateMachine::updateConfigFromGlobals() {
    m_satelliteID = g_currentSBB.satelliteID;
    m_mainNetID = g_currentSBB.mainNetID;
    m_roamNetID = g_currentSBB.roamNetID;
    m_uplinkMaxLength = g_currentSBB.uplinkMaxLength;
    m_mediaAccessPriority = ASC_MAC_msg.mediaAccessPriority;
    m_racMsgAccessLimit = ASC_MAC_msg.racMsgAccessLimit;
    m_networkStatus = ASC_MAC_msg.networkStatus;
    m_arqTimeoutLimit = ASC_MAC_msg.arqTimeoutLimit;
    if (m_arqTimeoutLimit == 0) m_arqTimeoutLimit = 3;
}

bool UplinkAddressedStateMachine::validateRacSlotForRequest(int slot) const {
    return (slot >= 630 && slot <= 808) ||
        (slot >= 1350 && slot <= 1528) ||
        (slot >= 2070 && slot <= 2248);
}

void UplinkAddressedStateMachine::sendLongMessage(const std::vector<uint8_t>& data, uint32_t destId) {
    if (m_state != InternalState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return;
    }
    m_messageData = data;
    m_destId = destId;
    m_retryCount15min = 0;
    onEnterResReqState();
}

void UplinkAddressedStateMachine::sendLongMessageWithSpecifiedSlot(int currentSlot, int specifiedRacSlot,
    const std::vector<uint8_t>& data, uint32_t destId) {
    if (m_state != InternalState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return;
    }
    if (!validateRacSlotForRequest(specifiedRacSlot)) {
        if (sendFinished) sendFinished(false, "指定时隙不是RAC时隙");
        return;
    }
    if (currentSlot > specifiedRacSlot) {
        if (sendFinished) sendFinished(false, "指定时隙已错过");
        return;
    }
    m_messageData = data;
    m_destId = destId;
    m_specifiedRacSlot = specifiedRacSlot;
    m_useSpecifiedRacSlot = true;
    m_missedRacSlot = false;
    m_racRetryCount = 0;
    onEnterResReqState();
}

void UplinkAddressedStateMachine::onEnterIdleState() {
    m_state = InternalState::Idle;
    if (stateChanged) stateChanged(m_state);
    clearSession();
    m_useSpecifiedRacSlot = false;
    m_specifiedRacSlot = -1;
    m_missedRacSlot = false;
    m_racRetryCount = 0;
    updateConfigFromGlobals();
}

void UplinkAddressedStateMachine::onEnterResReqState() {
    m_state = InternalState::ResReq;
    if (stateChanged) stateChanged(m_state);
}

void UplinkAddressedStateMachine::onEnterASCState() {
    m_state = InternalState::ASC;
    if (stateChanged) stateChanged(m_state);
    m_ascTimer.start(15000ms, [this]() { onAscTimeout(); }, true);
}

void UplinkAddressedStateMachine::onEnterSendState() {
    m_state = InternalState::Send;
    if (stateChanged) stateChanged(m_state);
}

void UplinkAddressedStateMachine::onEnterWaitState() {
    m_state = InternalState::Wait;
    if (stateChanged) stateChanged(m_state);
    m_waitTimer.start(15000ms, [this]() { onWaitTimeout(); }, true);
}

void UplinkAddressedStateMachine::onEnterAbnormalState() {
    m_state = InternalState::Abnormal;
    if (stateChanged) stateChanged(m_state);
    m_abnormalTimer.start(std::chrono::milliseconds(2000),
        [this]() { clearSession(); onEnterIdleState(); if (sendFinished) sendFinished(false, "异常"); }, true);
}

void UplinkAddressedStateMachine::onEnterWaitAckState() {
    m_state = InternalState::WaitAck;
    if (stateChanged) stateChanged(m_state);
    m_ackWaitTimer.start(std::chrono::milliseconds(ACK_WAIT_TIMEOUT_MS), [this]() { onAckWaitTimeout(); }, true);
}

void UplinkAddressedStateMachine::onAscTimeout() {
    if (m_state == InternalState::ASC) {
        m_retryCount15min++;
        if (m_retryCount15min < MAX_RETRY_15MIN) {
            onEnterResReqState();
        }
        else {
            onEnterWaitState();
        }
    }
}

void UplinkAddressedStateMachine::onWaitTimeout() {
    if (m_state == InternalState::Wait) {
        m_retryCount15min = 0;
        onEnterIdleState();
    }
}

void UplinkAddressedStateMachine::onAckWaitTimeout() {
    if (m_state == InternalState::WaitAck) {
        bool anyExceed = false;
        for (auto& frag : m_fragments) {
            if (!frag.acked) {
                frag.retryCount++;
                if (frag.retryCount > m_arqTimeoutLimit) anyExceed = true;
            }
        }
        if (anyExceed) {
            clearSession();
            onEnterIdleState();
            if (sendFinished) sendFinished(false, "ACK超时，重传次数超限");
            return;
        }
        for (auto& frag : m_fragments) frag.acked = false;
        m_nextFragment = 0;
        onEnterSendState();
    }
}

void UplinkAddressedStateMachine::sendResourceRequest(int currentSlot) {
    ResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.type = 20;
    uint32_t src = m_myMmsi;
    req.ship_id[0] = (src >> 24) & 0xFF;
    req.ship_id[1] = (src >> 16) & 0xFF;
    req.ship_id[2] = (src >> 8) & 0xFF;
    req.ship_id[3] = src & 0xFF;
    req.sat_id = SAT_ID;
    req.priority_size = 180;
    req.terminal_cap = 16;
    req.downlink_cqi = 214;
    req.reserved = 0;
    sendToBoard(0x0A, currentSlot, 20, reinterpret_cast<uint8_t*>(&req), sizeof(req), "UplinkAddressed");
}

void UplinkAddressedStateMachine::onMyResourceAllocationReceived(uint8_t logicChannel, uint8_t linkID,
    uint8_t sessionID, uint8_t uplinkCQI) {
    if (m_state != InternalState::ASC || m_totalFragments > 0) return;
    if (m_messageData.empty()) {
        onEnterIdleState();
        return;
    }
    m_assignedLogicChannel = logicChannel;
    if (m_linkIdChanged) updateLinkId(linkID);
    else m_assignedLinkId = linkID;
    m_sessionId = sessionID;
    m_uplinkCqi = uplinkCQI;
    m_ascTimer.stop();
    startSendingFragments();
    onEnterSendState();
}

void UplinkAddressedStateMachine::startSendingFragments() {
    m_fragments = splitMessage();
    m_totalFragments = (int)m_fragments.size();
    m_nextFragment = 0;
    for (auto& f : m_fragments) {
        f.retryCount = 0;
        f.acked = false;
    }
}

void UplinkAddressedStateMachine::sendNextFragment(int currentSlot) {
    while (m_nextFragment < m_totalFragments && m_fragments.begin()->acked) {
        m_fragments.pop_front();
        m_nextFragment--;
    }
    if (m_fragments.empty()) {
        onEnterWaitAckState();
        return;
    }
    FragmentInfo& frag = m_fragments.front();
    std::vector<uint8_t> fragmentData;
    fragmentData.reserve(15 + frag.dataSize);
    uint8_t type;
    bool isLast = (frag.fragmentNum == m_totalFragments - 1);
    if (isLast) type = 32;
    else if (frag.fragmentNum == 0) type = 30;
    else type = 31;
    fragmentData.push_back(type);
    uint16_t paySize = frag.dataSize;
    fragmentData.push_back((paySize >> 8) & 0xFF);
    fragmentData.push_back(paySize & 0xFF);
    uint32_t src = m_myMmsi;
    fragmentData.push_back((src >> 24) & 0xFF);
    fragmentData.push_back((src >> 16) & 0xFF);
    fragmentData.push_back((src >> 8) & 0xFF);
    fragmentData.push_back(src & 0xFF);
    fragmentData.push_back(SAT_ID);
    fragmentData.push_back(m_sessionId);
    uint32_t dest = m_destId;
    fragmentData.push_back((dest >> 24) & 0xFF);
    fragmentData.push_back((dest >> 16) & 0xFF);
    fragmentData.push_back((dest >> 8) & 0xFF);
    fragmentData.push_back(dest & 0xFF);
    if (type == 30) {
        uint16_t remaining = m_totalFragments - 1;
        fragmentData.push_back((remaining >> 8) & 0xFF);
        fragmentData.push_back(remaining & 0xFF);
    }
    else {
        uint16_t fragNum = frag.fragmentNum;
        fragmentData.push_back((fragNum >> 8) & 0xFF);
        fragmentData.push_back(fragNum & 0xFF);
    }
    fragmentData.insert(fragmentData.end(), m_messageData.begin() + frag.dataStart,
        m_messageData.begin() + frag.dataStart + frag.dataSize);
    static const uint8_t phyChannelMap[] = { 0x0A,0x0B,0x0C,0x0D,0x0E,0x0F };
    uint8_t phyChannel = (m_assignedLogicChannel < 6) ? phyChannelMap[m_assignedLogicChannel] : 0x0A;
    sendToBoard(phyChannel, currentSlot, m_assignedLinkId, fragmentData.data(), fragmentData.size(), "UplinkAddressed");
    m_fragments.pop_front();
    if (isLast) onEnterWaitAckState();
}

void UplinkAddressedStateMachine::parseUplinkAck(const uint8_t* data, int len) {
    if (len < 10 + 25) return;
    if (data[0] != 13) return;
    uint32_t shipId = (data[2] << 24) | (data[3] << 16) | (data[4] << 8) | data[5];
    if (shipId != MY_MMSI) return;
    uint8_t sessionId = data[6];
    uint8_t resourceRealloc = data[7];
    uint8_t adaptCtrl = data[9];
    const uint8_t* mask = data + 10;
    if (sessionId != m_sessionId) return;
    if (m_ackWaitTimer.isRunning()) m_ackWaitTimer.stop();
    int maskBytes = (m_totalFragments + 7) / 8;
    if (maskBytes > 25) maskBytes = 25;
    bool allAcked = true;
    std::list<int> lostFragments;
    int idx = 0;
    for (auto it = m_fragments.begin(); it != m_fragments.end(); ++it, ++idx) {
        int byteIdx = idx / 8;
        int bitIdx = idx % 8;
        if (byteIdx >= maskBytes) break;
        int bitPos = 7 - bitIdx;
        bool bitSet = (mask[byteIdx] & (1 << bitPos)) != 0;
        if (bitSet) {
            it->acked = true;
        }
        else {
            allAcked = false;
            lostFragments.push_back(idx);
            it->acked = false;
        }
    }
    if (resourceRealloc == 0) {
        if (allAcked) {
            clearSession();
            onEnterIdleState();
            if (sendFinished) sendFinished(true, "发送成功");
        }
        else {
            clearSession();
            onEnterIdleState();
            if (sendFinished) sendFinished(false, "资源分配被取消");
        }
        return;
    }
    bool needRetransmit = false;
    for (int lostIdx : lostFragments) {
        auto it = m_fragments.begin();
        std::advance(it, lostIdx);
        it->retryCount++;
        if (it->retryCount > m_arqTimeoutLimit) {
            clearSession();
            onEnterIdleState();
            if (sendFinished) sendFinished(false, "重传次数超限");
            return;
        }
        needRetransmit = true;
    }
    if (needRetransmit) {
        resetForRetransmission();
        onEnterSendState();
    }
}

void UplinkAddressedStateMachine::updateLinkId(uint8_t newLinkId) {
    if (newLinkId != m_assignedLinkId) {
        m_assignedLinkId = newLinkId;
        m_linkIdChanged = false;
        for (auto& f : m_fragments) {
            f.acked = false;
            f.retryCount = 0;
        }
    }
}

void UplinkAddressedStateMachine::resetForRetransmission() {
    m_nextFragment = 0;
}

void UplinkAddressedStateMachine::clearSession() {
    m_messageData.clear();
    m_destId = 0;
    m_totalFragments = 0;
    m_nextFragment = 0;
    m_fragments.clear();
    m_assignedLinkId = 0;
    m_assignedLogicChannel = 0;
    m_sessionId = 0;
    m_uplinkCqi = 0;
    m_linkIdChanged = false;
    m_ackWaitTimer.stop();
    m_ascTimer.stop();
    m_waitTimer.stop();
}

bool UplinkAddressedStateMachine::isSlotInDcChannel(int slot) const {
    switch (m_assignedLogicChannel) {
    case 0: return (slot >= 180 && slot <= 209) || (slot >= 900 && slot <= 929) || (slot >= 1620 && slot <= 1649);
    case 1: return (slot >= 210 && slot <= 299) || (slot >= 930 && slot <= 1019) || (slot >= 1650 && slot <= 1739);
    case 2: return (slot >= 300 && slot <= 389) || (slot >= 1020 && slot <= 1109) || (slot >= 1740 && slot <= 1829);
    case 3: return (slot >= 390 && slot <= 479) || (slot >= 1110 && slot <= 1199) || (slot >= 1830 && slot <= 1919);
    case 4: return (slot >= 480 && slot <= 569) || (slot >= 1200 && slot <= 1289) || (slot >= 1920 && slot <= 2009);
    case 5: return (slot >= 570 && slot <= 599) || (slot >= 1290 && slot <= 1319) || (slot >= 2010 && slot <= 2039);
    default: return false;
    }
}

bool UplinkAddressedStateMachine::isRacSlot(int slot) const {
    return (slot >= 630 && slot <= 808) || (slot >= 1350 && slot <= 1528) || (slot >= 2070 && slot <= 2248);
}

std::list<FragmentInfo> UplinkAddressedStateMachine::splitMessage() const {
    std::list<FragmentInfo> list;
    int totalSize = (int)m_messageData.size();
    int maxPayload = 100;
    int numFrags = (totalSize + maxPayload - 1) / maxPayload;
    for (int i = 0; i < numFrags; ++i) {
        FragmentInfo info;
        info.fragmentNum = i;
        info.dataStart = i * maxPayload;
        // 使用括号避免 Windows.h 中的 min 宏冲突
        info.dataSize = (std::min)(maxPayload, totalSize - info.dataStart);
        info.isLast = (i == numFrags - 1);
        info.retryCount = 0;
        info.acked = false;
        list.push_back(info);
    }
    return list;
}

void UplinkAddressedStateMachine::onSlot(int currentSlot) {
    if (m_state == InternalState::Send && isSlotInDcChannel(currentSlot)) {
        sendNextFragment(currentSlot);
    }
    else if (m_state == InternalState::ResReq) {
        if (m_useSpecifiedRacSlot) {
            if (currentSlot == m_specifiedRacSlot - 2) {
                if (isRacSlot(currentSlot)) {
                    sendResourceRequest(m_specifiedRacSlot);
                    onEnterASCState();
                    m_useSpecifiedRacSlot = false;
                }
                else {
                    if (sendFinished) sendFinished(false, "指定时隙不是RAC时隙");
                    onEnterIdleState();
                }
                return;
            }
            if (currentSlot > m_specifiedRacSlot - 2 && !m_missedRacSlot) {
                m_missedRacSlot = true;
                m_racRetryCount++;
                if (m_racRetryCount < MAX_RAC_RETRY) {
                    if (sendFinished) sendFinished(false, "时隙已错过，重试");
                }
                else {
                    if (sendFinished) sendFinished(false, "连续错过指定时隙");
                    m_useSpecifiedRacSlot = false;
                    onEnterIdleState();
                }
            }
            if (currentSlot < m_specifiedRacSlot - 2 && m_missedRacSlot) {
                m_missedRacSlot = false;
            }
        }
        else {
            if (isRacSlot(currentSlot)) {
                sendResourceRequest(currentSlot);
                onEnterASCState();
            }
        }
    }
}

void UplinkAddressedStateMachine::handlePacket(const std::vector<uint8_t>& packet, int, int) {
    if (packet.empty()) return;
    uint8_t type = packet[0];
    if ((m_state == InternalState::Send || m_state == InternalState::WaitAck) && type == 13) {
        parseUplinkAck(packet.data(), (int)packet.size());
    }
    else if (m_state == InternalState::Idle && (type == 1 || type == 10)) {
        updateConfigFromGlobals();
    }
}

void UplinkAddressedStateMachine::onMacFrameUpdated() {
    m_arqTimeoutLimit = ASC_MAC_msg.arqTimeoutLimit;
    if (m_arqTimeoutLimit == 0) m_arqTimeoutLimit = 3;
}