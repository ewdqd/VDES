// downlinkshortnoackstatemachine.cpp
#include "downlinkshortnoackstatemachine.h"
#include <iostream>
#include <sstream>
#include <iomanip>

DownlinkShortNoAckStateMachine::DownlinkShortNoAckStateMachine()
    : VdesStateMachine(MessageType::DownlinkShortNoAck)
    , m_state(InternalState::Idle), m_srcId(0), m_satId(0), m_shipId(0) {
}

DownlinkShortNoAckStateMachine::~DownlinkShortNoAckStateMachine() {}

void DownlinkShortNoAckStateMachine::onSlot(int currentSlot) {
    if (m_state == InternalState::Abnormal) {
        m_state = InternalState::Idle;
        if (stateChanged) stateChanged(m_state);
    }
}

void DownlinkShortNoAckStateMachine::handlePacket(const std::vector<uint8_t>& packet, int, int) {
    if (packet.empty()) {
        m_state = InternalState::Abnormal;
        if (stateChanged) stateChanged(m_state);
        return;
    }
    const uint8_t* data = packet.data();
    if (data[0] != 16) return;  // 只处理类型16
    if (packet.size() < 12) {
        m_state = InternalState::Abnormal;
        if (stateChanged) stateChanged(m_state);
        return;
    }
    // uint16_t len = (data[1] << 8) | data[2]; // length field, parsed but unused
    m_satId = data[3];
    m_srcId = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    m_shipId = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
    int payloadStart = 12;
    int payloadLen = (int)packet.size() - payloadStart;
    m_payload.assign(data + payloadStart, data + payloadStart + payloadLen);
    if (!isValidSrcId(m_srcId) || !isValidSatId(m_satId)) {
        m_state = InternalState::Abnormal;
        if (stateChanged) stateChanged(m_state);
        m_srcId = 0; m_satId = 0;
        return;
    }
    m_state = InternalState::Receive;
    if (stateChanged) stateChanged(m_state);
    // 生成显示字符串
    std::stringstream ss;
    ss << "下行短消息(无ACK) 卫星ID:" << (int)m_satId
        << " 信源ID:" << std::hex << m_srcId << std::dec
        << " 船舶ID:" << std::hex << m_shipId << std::dec
        << " 载荷:";
    for (auto c : m_payload) ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    if (msg16Parsed) msg16Parsed(ss.str(), m_srcId, m_satId);
    m_state = InternalState::Idle;
    if (stateChanged) stateChanged(m_state);
    m_payload.clear();
}

bool DownlinkShortNoAckStateMachine::isValidSrcId(uint32_t srcId) const {
    return srcId >= 100000000 && srcId <= 999999999;
}

bool DownlinkShortNoAckStateMachine::isValidSatId(uint8_t satId) const {
    return satId > 0;
}