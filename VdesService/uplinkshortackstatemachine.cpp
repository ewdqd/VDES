// uplinkshortackstatemachine.cpp
#define NOMINMAX
#include "uplinkshortackstatemachine.h"
#include "vdes_global.h"
#include "socket_utils.h"
#include <cstring>
#include <iostream>
#include <chrono>

using namespace std::chrono_literals;

UplinkShortAckStateMachine::UplinkShortAckStateMachine()
    : VdesStateMachine(MessageType::UplinkShortAck)
    , m_state(ShortMsgAckState::Idle)
    , m_specifiedSlot(-1)
    , m_specifiedLinkId(0)
    , m_hasPendingMsg(false)
    , m_myMmsi(MY_MMSI)
    , m_retryCount(0)
    , m_macFrameReceived(false)
    , m_macFrameAllowed(false)
    , m_racLimit(3)
    , m_randomInterval(12)
    , m_mediaAccessPriority(0)
    , m_networkStatus(0) {
}

UplinkShortAckStateMachine::~UplinkShortAckStateMachine() {}

void UplinkShortAckStateMachine::setMyMmsi(uint32_t mmsi) {
    m_myMmsi = mmsi;
    if (m_state != ShortMsgAckState::Idle) {
        clearPending();
        changeState(ShortMsgAckState::Idle);
    }
}

bool UplinkShortAckStateMachine::sendMessage(int currentSlot, int specifiedSlot, uint16_t linkId, const Modu_2send_short& msg) {
    if (m_state != ShortMsgAckState::Idle) {
        if (errorOccurred) errorOccurred("状态机非空闲");
        return false;
    }
    if (linkId != 20) {
        if (errorOccurred) errorOccurred("Link ID必须为20");
        return false;
    }
    auto isRacSlot = [](int slot) {
        return (slot >= 630 && slot <= 808) || (slot >= 1350 && slot <= 1528) || (slot >= 2070 && slot <= 2248);
        };
    if (!isRacSlot(specifiedSlot)) {
        if (errorOccurred) errorOccurred("指定时隙不是RAC时隙");
        return false;
    }
    if (currentSlot > specifiedSlot) {
        if (errorOccurred) errorOccurred("指定时隙已错过");
        return false;
    }
    m_specifiedSlot = specifiedSlot;
    m_specifiedLinkId = static_cast<uint8_t>(linkId);
    m_pendingMsg = msg;
    m_hasPendingMsg = true;
    m_retryCount = 0;
    changeState(ShortMsgAckState::Wait);
    return true;
}

void UplinkShortAckStateMachine::onSlot(int currentSlot) {
    if (m_state != ShortMsgAckState::Wait) return;
    if (currentSlot == m_specifiedSlot - 2) {
        if (m_macFrameAllowed) {
            sendCurrentMessage(m_specifiedSlot);
            changeState(ShortMsgAckState::Send);
            m_ackTimer.start(5000ms, [this]() { onAckTimeout(); }, true);
        }
        else {
            if (errorOccurred) errorOccurred("MAC不允许发送");
        }
    }
}

void UplinkShortAckStateMachine::handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) {
    if (packet.empty()) return;
    uint8_t type = packet[0];
    if (type == 34 && m_state == ShortMsgAckState::Send) {
        parseUplinkAck(packet.data(), packet.size());
    }
}

void UplinkShortAckStateMachine::onMacFrameUpdated() {
    m_macFrameReceived = true;
    m_macFrameAllowed = (m_mediaAccessPriority == 0) && (m_networkStatus == 0) &&
        (GL_event_control.rac_message_cnt < m_racLimit);
}

void UplinkShortAckStateMachine::sendCurrentMessage(int currentSlot) {
    if (!m_hasPendingMsg) return;
    UplinkShortMessageAck msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = 33;
    msg.src_ID[0] = (m_myMmsi >> 24) & 0xFF;
    msg.src_ID[1] = (m_myMmsi >> 16) & 0xFF;
    msg.src_ID[2] = (m_myMmsi >> 8) & 0xFF;
    msg.src_ID[3] = m_myMmsi & 0xFF;
    uint32_t dest = m_pendingMsg.ship_id;
    msg.stationID[0] = (dest >> 24) & 0xFF;
    msg.stationID[1] = (dest >> 16) & 0xFF;
    msg.stationID[2] = (dest >> 8) & 0xFF;
    msg.stationID[3] = dest & 0xFF;
    msg.data = m_pendingMsg.data;
    sendToBoard(PHY_CH_A, currentSlot, m_specifiedLinkId,
        reinterpret_cast<uint8_t*>(&msg), sizeof(msg), "UplinkShortAck");
}

void UplinkShortAckStateMachine::parseUplinkAck(const uint8_t* data, int len) {
    if (len < 9) return;
    if (data[0] != 34) return;
    uint32_t shipId = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    if (shipId == m_myMmsi) {
        uint8_t nack = data[8];
        if (nack == 0) {
            m_ackTimer.stop();
            if (sendFinished) sendFinished(true, "ACK确认");
            clearPending();
            changeState(ShortMsgAckState::Idle);
        }
        else {
            // 重试逻辑
            m_retryCount++;
            if (m_retryCount < MAX_RETRY) {
                changeState(ShortMsgAckState::Wait);
            }
            else {
                if (sendFinished) sendFinished(false, "ACK指示失败");
                clearPending();
                changeState(ShortMsgAckState::Abnormal);
            }
        }
    }
}

void UplinkShortAckStateMachine::onAckTimeout() {
    if (m_state == ShortMsgAckState::Send) {
        if (++m_retryCount < MAX_RETRY) {
            changeState(ShortMsgAckState::Wait);
        }
        else {
            if (sendFinished) sendFinished(false, "ACK超时");
            clearPending();
            changeState(ShortMsgAckState::Abnormal);
        }
    }
}

void UplinkShortAckStateMachine::onAbnormalTimeout() {
    if (m_state == ShortMsgAckState::Abnormal) {
        clearPending();
        changeState(ShortMsgAckState::Idle);
    }
}

void UplinkShortAckStateMachine::changeState(ShortMsgAckState newState) {
    if (m_state == newState) return;
    m_state = newState;
    if (stateChanged) stateChanged(m_state);
    if (newState == ShortMsgAckState::Abnormal) {
        m_abnormalTimer.start(2000ms, [this]() { onAbnormalTimeout(); }, true);
    }
    else if (newState == ShortMsgAckState::Idle) {
        m_ackTimer.stop();
        m_abnormalTimer.stop();
    }
}

void UplinkShortAckStateMachine::clearPending() {
    m_hasPendingMsg = false;
    m_specifiedSlot = -1;
    m_retryCount = 0;
}