#define NOMINMAX
#include "uplinkshortnoackstatemachine.h"
#include "vdes_global.h"
#include "socket_utils.h"
#include <cstring>
#include <iostream>
#include <chrono>

using namespace std::chrono_literals;

extern Modu_2send_short Modu_data_Tx_noack[MES_BUF_MAX];
extern Modu_2send_short Modu_data_Tx_noack_id[MES_BUF_MAX];
extern GL_event_flag GL_event_control;
extern ASC_MAC ASC_MAC_msg;

UplinkShortNoAckStateMachine::UplinkShortNoAckStateMachine()
    : VdesStateMachine(MessageType::UplinkShortNoAck)
    , m_currentState(ShortMsgState::Idle)
    , m_retryCount(0)
    , m_hasDestId(true)
    , m_currentMsgIndex(-1)
    , m_racLimit(3)
    , m_randomInterval(12)
    , m_mediaAccessPriority(0)
    , m_networkStatus(0)
    , m_racOffset(0)
    , m_racSlotCount(0)
    , m_specifiedSlot(-1)
    , m_useSpecifiedSlot(false)
    , m_missedThisCycle(false)
    , m_retryCountForSlot(0)
    , m_myMmsi(MY_MMSI) {
    ASC_MAC_msg.racMsgAccessLimit = m_racLimit;
    GL_event_control.random_interval = m_randomInterval;
    GL_event_control.rac_start_slot = 630;

    m_fifteenMinTimer.start(15000ms, [this]() { onFifteenMinuteTimeout(); }, false);
    initStateMachine();
}

UplinkShortNoAckStateMachine::~UplinkShortNoAckStateMachine() {}

void UplinkShortNoAckStateMachine::initStateMachine() {
    // 初始化状态为 Idle，通过回调切换状态
    onEnterIdleState();
}

void UplinkShortNoAckStateMachine::onEnterIdleState() {
    m_currentState = ShortMsgState::Idle;
    if (stateChanged) stateChanged(m_currentState);
}

void UplinkShortNoAckStateMachine::onEnterWaitState() {
    m_currentState = ShortMsgState::Wait;
    if (stateChanged) stateChanged(m_currentState);
    if (m_mediaAccessPriority == 0 && m_networkStatus == 0 && GL_event_control.rac_message_cnt < m_racLimit) {
        onEnterSendState();
        std::cout << "UplinkShortNoAck: MAC允许，立即进入发送态" << std::endl;
    }
    else {
        m_waitTimer.start(15 * 60 * 1000ms, [this]() { onWaitTimeout(); }, true); // singleShot = true
        std::cout << "UplinkShortNoAck: MAC不允许，等待15分钟" << std::endl;
    }
}

void UplinkShortNoAckStateMachine::onEnterSendState() {
    m_currentState = ShortMsgState::Send;
    if (stateChanged) stateChanged(m_currentState);
    m_racOffset = generateRacOffset();
    m_racSlotCount = 0;
}

void UplinkShortNoAckStateMachine::onEnterAbnormalState() {
    m_currentState = ShortMsgState::Abnormal;
    if (stateChanged) stateChanged(m_currentState);
    m_abnormalTimer.start(2000ms, [this]() { onAbnormalTimeout(); }, true);
}

void UplinkShortNoAckStateMachine::onWaitTimeout() {
    if (m_currentState == ShortMsgState::Wait) {
        onEnterAbnormalState();
    }
}

void UplinkShortNoAckStateMachine::onAbnormalTimeout() {
    if (m_currentState == ShortMsgState::Abnormal) {
        if (m_retryCount < MAX_RETRY_COUNT) {
            m_retryCount++;
            onEnterWaitState();
        }
        else {
            onEnterIdleState();
            if (sendFinished) sendFinished(false, "重试次数达上限");
        }
    }
}

void UplinkShortNoAckStateMachine::onFifteenMinuteTimeout() {
    GL_event_control.rac_message_cnt = 0;
}

void UplinkShortNoAckStateMachine::onMacFrameUpdated() {
    m_racLimit = ASC_MAC_msg.racMsgAccessLimit;
    m_randomInterval = ASC_MAC_msg.randomSelectInterval;
    m_mediaAccessPriority = ASC_MAC_msg.mediaAccessPriority;
    m_networkStatus = ASC_MAC_msg.networkStatus;
    if (m_currentState == ShortMsgState::Wait) {
        m_waitTimer.stop();
        onEnterSendState();
    }
}

bool UplinkShortNoAckStateMachine::validateRacSlot(int slot) const {
    return (slot >= 630 && slot <= 808) ||
        (slot >= 1350 && slot <= 1528) ||
        (slot >= 2070 && slot <= 2248);
}

bool UplinkShortNoAckStateMachine::sendMessageWithSpecifiedSlot(int currentSlot, int specifiedSlot, uint16_t linkId, const Modu_2send_short& msg) {
    if (m_currentState != ShortMsgState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return false;
    }
    if (linkId != 20) {
        if (sendFinished) sendFinished(false, "Link ID必须为20");
        return false;
    }
    if (!validateRacSlot(specifiedSlot)) {
        if (sendFinished) sendFinished(false, "指定时隙不是RAC时隙");
        return false;
    }
    if (currentSlot > specifiedSlot) {
        if (sendFinished) sendFinished(false, "指定时隙已错过");
        return false;
    }
    for (int i = 0; i < MES_BUF_MAX; ++i) {
        if (Modu_data_Tx_noack[i].state == 0) {
            Modu_data_Tx_noack[i] = msg;
            m_currentMsgIndex = i;
            m_hasDestId = true;
            break;
        }
    }
    m_specifiedSlot = specifiedSlot;
    m_useSpecifiedSlot = true;
    m_retryCountForSlot = 0;
    m_missedThisCycle = false;
    onEnterWaitState();
    return true;
}

bool UplinkShortNoAckStateMachine::sendMessageNoDestWithSpecifiedSlot(int currentSlot, int specifiedSlot, uint16_t linkId, uint8_t type, const std::vector<uint8_t>& data) {
    if (m_currentState != ShortMsgState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return false;
    }
    if (linkId != 20) {
        if (sendFinished) sendFinished(false, "Link ID必须为20");
        return false;
    }
    if (!validateRacSlot(specifiedSlot)) {
        if (sendFinished) sendFinished(false, "指定时隙不是RAC时隙");
        return false;
    }
    if (currentSlot > specifiedSlot) {
        if (sendFinished) sendFinished(false, "指定时隙已错过");
        return false;
    }
    for (int i = 0; i < MES_BUF_MAX; ++i) {
        if (Modu_data_Tx_noack_id[i].state == 0) {
            Modu_data_Tx_noack_id[i].type = type;
            Modu_data_Tx_noack_id[i].ship_id = 0;
            int copyLen = std::min((int)data.size(), SHORT_MES_MAX);
            memcpy(Modu_data_Tx_noack_id[i].message, data.data(), copyLen);
            Modu_data_Tx_noack_id[i].lenth = copyLen;
            Modu_data_Tx_noack_id[i].state = MSG_STATE_FULL;
            m_currentMsgIndex = i;
            m_hasDestId = false;
            break;
        }
    }
    m_specifiedSlot = specifiedSlot;
    m_useSpecifiedSlot = true;
    m_retryCountForSlot = 0;
    m_missedThisCycle = false;
    onEnterWaitState();
    return true;
}

void UplinkShortNoAckStateMachine::addMessage(const Modu_2send_short& msg) {
    if (m_currentState != ShortMsgState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return;
    }
    for (int i = 0; i < MES_BUF_MAX; ++i) {
        if (Modu_data_Tx_noack[i].state == 0) {
            Modu_data_Tx_noack[i] = msg;
            m_currentMsgIndex = i;
            m_hasDestId = true;
            break;
        }
    }
    onEnterWaitState();
}

void UplinkShortNoAckStateMachine::addMessageNoDest(uint8_t type, const std::vector<uint8_t>& data) {
    if (m_currentState != ShortMsgState::Idle) {
        if (sendFinished) sendFinished(false, "状态机非空闲");
        return;
    }
    for (int i = 0; i < MES_BUF_MAX; ++i) {
        if (Modu_data_Tx_noack_id[i].state == 0) {
            Modu_data_Tx_noack_id[i].type = type;
            Modu_data_Tx_noack_id[i].ship_id = 0;
            int copyLen = std::min((int)data.size(), SHORT_MES_MAX);
            memcpy(Modu_data_Tx_noack_id[i].message, data.data(), copyLen);
            Modu_data_Tx_noack_id[i].lenth = copyLen;
            Modu_data_Tx_noack_id[i].state = MSG_STATE_FULL;
            m_currentMsgIndex = i;
            m_hasDestId = false;
            break;
        }
    }
    onEnterWaitState();
}

bool UplinkShortNoAckStateMachine::isRacSlot(int slot) {
    return (slot >= 630 && slot <= 808) ||
        (slot >= 1350 && slot <= 1528) ||
        (slot >= 2070 && slot <= 2248);
}

int UplinkShortNoAckStateMachine::generateRacOffset() {
    int maxOffset = m_randomInterval * 15;
    int offset = generate_rac_offset();
    while (offset > maxOffset) offset = generate_rac_offset();
    return offset;
}

void UplinkShortNoAckStateMachine::onSlot(int currentSlot) {
    if (m_currentState != ShortMsgState::Send) return;

    if (m_useSpecifiedSlot) {
        if (currentSlot == m_specifiedSlot - 2) {
            if (m_mediaAccessPriority == 0 && m_networkStatus == 0 && GL_event_control.rac_message_cnt < m_racLimit) {
                sendCurrentMessage(m_specifiedSlot);
                m_useSpecifiedSlot = false;
                m_specifiedSlot = -1;
                onEnterIdleState();
                if (sendFinished) sendFinished(true, "消息发送成功");
            }
            else {
                if (sendFinished) sendFinished(false, "MAC不允许发送，稍后重试");
            }
            return;
        }
        if (currentSlot > m_specifiedSlot - 2 && !m_missedThisCycle) {
            m_missedThisCycle = true;
            m_retryCountForSlot++;
            if (m_retryCountForSlot < MAX_SLOT_RETRY) {
                if (sendFinished) sendFinished(false, "时隙已错过，下一分钟重试");
            }
            else {
                if (sendFinished) sendFinished(false, "连续错过指定时隙，发送失败");
                m_useSpecifiedSlot = false;
                m_specifiedSlot = -1;
                onEnterIdleState();
            }
        }
        if (currentSlot < m_specifiedSlot - 2 && m_missedThisCycle) {
            m_missedThisCycle = false;
        }
        return;
    }

    if (m_mediaAccessPriority != 0 || m_networkStatus != 0 || GL_event_control.rac_message_cnt >= m_racLimit) {
        onEnterAbnormalState();
        return;
    }
    if (!isRacSlot(currentSlot)) return;
    m_racSlotCount++;
    if (m_racSlotCount >= m_racOffset + 1) {
        sendCurrentMessage(currentSlot);
        onEnterIdleState();
        if (sendFinished) sendFinished(true, "消息发送成功");
    }
}

void UplinkShortNoAckStateMachine::sendCurrentMessage(int currentSlot) {
    uint8_t buffer[64];
    int len = 0;
    if (m_hasDestId) {
        UplinkShortMessageNoAck msg23;
        msg23.type = 23;
        uint32_t src = m_myMmsi;
        msg23.src_ID[0] = (src >> 24) & 0xFF;
        msg23.src_ID[1] = (src >> 16) & 0xFF;
        msg23.src_ID[2] = (src >> 8) & 0xFF;
        msg23.src_ID[3] = src & 0xFF;
        uint32_t dest = Modu_data_Tx_noack[m_currentMsgIndex].ship_id;
        msg23.stationID[0] = (dest >> 24) & 0xFF;
        msg23.stationID[1] = (dest >> 16) & 0xFF;
        msg23.stationID[2] = (dest >> 8) & 0xFF;
        msg23.stationID[3] = dest & 0xFF;
        msg23.data = Modu_data_Tx_noack[m_currentMsgIndex].data;
        memcpy(buffer, &msg23, sizeof(msg23));
        len = sizeof(msg23);
    }
    else {
        RAC_DownSMsgNoAckid msg24;
        msg24.type = 24;
        uint32_t src = m_myMmsi;
        msg24.sourceID[0] = (src >> 24) & 0xFF;
        msg24.sourceID[1] = (src >> 16) & 0xFF;
        msg24.sourceID[2] = (src >> 8) & 0xFF;
        msg24.sourceID[3] = src & 0xFF;
        int dataLen = std::min(Modu_data_Tx_noack_id[m_currentMsgIndex].lenth, 5);
        memcpy(msg24.data, Modu_data_Tx_noack_id[m_currentMsgIndex].message, dataLen);
        if (dataLen < 5) memset(msg24.data + dataLen, 0, 5 - dataLen);
        memcpy(buffer, &msg24, sizeof(msg24));
        len = sizeof(msg24);
    }
    sendToBoard(PHY_CH_A, currentSlot, 20, buffer, len, "UplinkShortNoAck");
}

void UplinkShortNoAckStateMachine::handlePacket(const std::vector<uint8_t>&, int, int) {
    // 上行短消息无确认不需要处理接收
}