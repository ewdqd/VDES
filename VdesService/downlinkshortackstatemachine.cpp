#define _CRT_SECURE_NO_WARNINGS
#include "downlinkshortackstatemachine.h"
#include "vdes_global.h"
#include "socket_utils.h"
#include <cstring>
#include <iostream>

DownlinkShortAckStateMachine::DownlinkShortAckStateMachine()
    : VdesStateMachine(MessageType::DownlinkShortAck)
    , m_state(InternalState::Idle)
    , m_pendingSrc(0), m_pendingSat(0), m_pendingDest(0) {
}

DownlinkShortAckStateMachine::~DownlinkShortAckStateMachine() {}

void DownlinkShortAckStateMachine::onSlot(int currentSlot) {
    if (m_state != InternalState::SendingAck) return;
    auto isAscSlot = [](int slot) {
        return (slot >= 90 && slot <= 179) ||
            (slot >= 810 && slot <= 899) ||
            (slot >= 1530 && slot <= 1619);
        };
    if (isAscSlot(currentSlot)) {
        if (isValidSrcId(m_pendingSrc) && isValidSatId(m_pendingSat)) {
            sendAck(m_pendingSrc, m_pendingSat, currentSlot);
        }
        m_state = InternalState::Idle;
        if (stateChanged) stateChanged(m_state);
    }
}

void DownlinkShortAckStateMachine::handlePacket(const std::vector<uint8_t>& packet, int, int) {
    if (packet.size() < 12) return;
    const uint8_t* data = packet.data();
    if (data[0] != 14) return;  // 只处理类型14

    // uint16_t payloadSize = (data[1] << 8) | data[2];
    uint8_t satId = data[3];
    uint32_t srcId = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    uint32_t destId = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
    if (!isForMe(destId)) return;
    if (!isValidSrcId(srcId) || !isValidSatId(satId)) return;

    std::vector<uint8_t> payload(data + 12, data + packet.size());
    std::string messageText(payload.begin(), payload.end());
    if (messageText.empty()) {
        // 转为十六进制字符串
        char buf[256] = { 0 };
        for (size_t i = 0; i < payload.size() && i < 50; ++i) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%02X ", payload[i]);
            strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
        }
        messageText = buf;
    }
    if (downlinkShortMessageReceived)
        downlinkShortMessageReceived(14, srcId, satId, messageText);

    m_pendingSrc = srcId;
    m_pendingSat = satId;
    m_state = InternalState::SendingAck;
    if (stateChanged) stateChanged(m_state);
}

void DownlinkShortAckStateMachine::sendAck(uint32_t src_id, uint8_t sat_id, int currentSlot) {
    RAC_EndOfTransmit ack;
    memset(&ack, 0, sizeof(ack));
    ack.type = 36;
    uint32_t myMMSI = MY_MMSI;
    ack.stationID[0] = (myMMSI >> 24) & 0xFF;
    ack.stationID[1] = (myMMSI >> 16) & 0xFF;
    ack.stationID[2] = (myMMSI >> 8) & 0xFF;
    ack.stationID[3] = myMMSI & 0xFF;
    ack.satID = sat_id;
    ack.destID[0] = (src_id >> 24) & 0xFF;
    ack.destID[1] = (src_id >> 16) & 0xFF;
    ack.destID[2] = (src_id >> 8) & 0xFF;
    ack.destID[3] = src_id & 0xFF;
    ack.sessionID = 0;
    int result = rac_pack(reinterpret_cast<uint8_t*>(&ack), sizeof(ack) - 1, currentSlot);
    if (ackSendResult) ackSendResult(result == 0, src_id, sat_id);
}

bool DownlinkShortAckStateMachine::isValidSrcId(uint32_t srcId) const {
    return srcId >= 100000000 && srcId <= 999999999;
}

bool DownlinkShortAckStateMachine::isValidSatId(uint8_t satId) const {
    return satId > 0 && satId <= 255;
}