// downlinkaddressedstatemachine.cpp
#include "downlinkaddressedstatemachine.h"
#include "socket_utils.h"
#include <cstring>
#include <iostream>
#include <sstream>

DownlinkAddressedStateMachine::DownlinkAddressedStateMachine()
    : VdesStateMachine(MessageType::DownlinkAddressed)
    , m_curState(Idle)
    , m_slotNum(0)
    , m_shipId(MY_MMSI)
    , m_resourceAllocated(false)
    , m_logicChannel(0)
    , m_linkID(0)
    , m_sessionID(0)
    , m_uplinkCQI(0)
    , m_isEndFragment(false)
    , m_fragmentSeq(0)
    , m_idleSlotLogged(false) {
}

DownlinkAddressedStateMachine::~DownlinkAddressedStateMachine() {}

void DownlinkAddressedStateMachine::onSlot(int currentSlot) {
    m_slotNum = currentSlot;
    if (slotActivity) slotActivity(currentSlot, m_type, "onSlot");
    if (m_curState == Idle && !m_idleSlotLogged) {
        if (logMessage) logMessage("时隙更新 | 状态: 空闲");
        m_idleSlotLogged = true;
    }
}

void DownlinkAddressedStateMachine::handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) {
    if (packet.empty()) {
        if (logMessage) logMessage("错误：收到空数据包");
        changeState(Abnormal);
        return;
    }
    uint8_t msgType = packet[0];
    if (msgType != 30 && msgType != 31 && msgType != 32) return;

    if (!m_resourceAllocated) {
        if (logMessage) logMessage("错误：未收到资源分配，丢弃数据分片");
        changeState(Abnormal);
        return;
    }
    if (m_curState == Idle) changeState(Receiving);
    handleDataFragment(packet);
}

void DownlinkAddressedStateMachine::onResourceAllocated(uint8_t logicChannel, uint8_t linkID,
    uint8_t sessionID, uint8_t uplinkCQI) {
    m_resourceAllocated = true;
    m_logicChannel = logicChannel;
    m_linkID = linkID;
    m_sessionID = sessionID;
    m_uplinkCQI = uplinkCQI;
    if (logMessage) {
        std::ostringstream oss;
        oss << "收到资源分配 | 逻辑信道:" << (int)logicChannel << " 链路ID:" << (int)linkID
            << " 会话ID:" << (int)sessionID << " CQI:" << (int)uplinkCQI;
        logMessage(oss.str());
    }
}

void DownlinkAddressedStateMachine::handleDataFragment(const std::vector<uint8_t>& packet) {
    uint8_t fragType = packet[0];
    std::vector<uint8_t> payload(packet.begin() + 1, packet.end());
    switch (fragType) {
    case 30:
        resetReceiveState();
        m_receivedData = payload;
        m_fragmentSeq = 0;
        if (logMessage) logMessage("起始分片");
        break;
    case 31:
        if (m_receivedData.empty()) {
            if (logMessage) logMessage("错误：未收到起始分片，丢弃续传包");
            changeState(Abnormal);
            return;
        }
        m_receivedData.insert(m_receivedData.end(), payload.begin(), payload.end());
        m_fragmentSeq++;
        break;
    case 32:
        if (m_receivedData.empty()) {
            if (logMessage) logMessage("错误：未收到起始分片，丢弃结束包");
            changeState(Abnormal);
            return;
        }
        m_receivedData.insert(m_receivedData.end(), payload.begin(), payload.end());
        m_isEndFragment = true;
        m_fragmentSeq++;
        if (logMessage) logMessage("结束分片，子帧完整");
        sendAckToSatellite();
        if (m_isEndFragment) {
            if (dataComplete) dataComplete(m_receivedData);
            changeState(Idle);
        }
        break;
    default:
        changeState(Abnormal);
        break;
    }
}

void DownlinkAddressedStateMachine::sendAckToSatellite() {
    std::vector<uint8_t> ackFrame;
    ackFrame.push_back(29);
    ackFrame.push_back((m_fragmentSeq >> 8) & 0xFF);
    ackFrame.push_back(m_fragmentSeq & 0xFF);
    ackFrame.push_back(0);
    uint32_t shipId = m_shipId;
    ackFrame.push_back((shipId >> 24) & 0xFF);
    ackFrame.push_back((shipId >> 16) & 0xFF);
    ackFrame.push_back((shipId >> 8) & 0xFF);
    ackFrame.push_back(shipId & 0xFF);
    // 发送 UDP
    send_udp(create_udp_socket(), ackFrame.data(), ackFrame.size(), ACK_TARGET_IP, ACK_TARGET_PORT);
    if (logMessage) logMessage("ACK(29)已发送");
}

void DownlinkAddressedStateMachine::resetReceiveState() {
    m_receivedData.clear();
    m_isEndFragment = false;
    m_fragmentSeq = 0;
}

void DownlinkAddressedStateMachine::changeState(InternalState newState) {
    if (m_curState == newState) return;
    m_curState = newState;
    if (stateChanged) stateChanged(newState);
    if (newState == Idle) {
        resetReceiveState();
        m_resourceAllocated = false;
        m_idleSlotLogged = false;
    }
    else if (newState == Abnormal) {
        resetReceiveState();
        m_resourceAllocated = false;
    }
}