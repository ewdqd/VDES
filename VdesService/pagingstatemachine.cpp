#include "pagingstatemachine.h"
#include "vdes_global.h"
#include "socket_utils.h"
#include <iostream>

PagingStateMachine::PagingStateMachine()
    : VdesStateMachine(MessageType::Paging)
    , m_state(InternalState::Idle), m_pendingSat(0), m_pendingCqi(0) {
}

PagingStateMachine::~PagingStateMachine() {}

void PagingStateMachine::onSlot(int currentSlot) {
    if (m_state != InternalState::SendingResponse) return;
    auto isRacSlot = [](int slot) {
        return (slot >= 630 && slot <= 808) ||
            (slot >= 1350 && slot <= 1528) ||
            (slot >= 2070 && slot <= 2248);
        };
    if (isRacSlot(currentSlot)) {
        sendPagingResponse(m_pendingSat, m_pendingCqi, currentSlot);
        m_state = InternalState::Idle;
        if (stateChanged) stateChanged(m_state);
    }
}

void PagingStateMachine::handlePacket(const std::vector<uint8_t>& packet, int, int) {
    if (packet.size() < 3) return;
    const uint8_t* data = packet.data();
    if (data[0] != 11) return;
    int numShips = (packet.size() - 3) / 4;
    for (int i = 0; i < numShips; ++i) {
        uint32_t id = (data[3 + 4 * i] << 24) | (data[4 + 4 * i] << 16) | (data[5 + 4 * i] << 8) | data[6 + 4 * i];
        if (id == MY_MMSI) {
            m_pendingSat = SAT_ID;
            m_pendingCqi = 100;
            m_state = InternalState::SendingResponse;
            if (stateChanged) stateChanged(m_state);
            break;
        }
    }
}

void PagingStateMachine::sendPagingResponse(uint8_t sat_id, uint8_t down_cqi, int currentSlot) {
    uint8_t buffer[10];
    buffer[0] = 21;
    uint32_t src = MY_MMSI;
    buffer[1] = (src >> 24) & 0xFF;
    buffer[2] = (src >> 16) & 0xFF;
    buffer[3] = (src >> 8) & 0xFF;
    buffer[4] = src & 0xFF;
    buffer[5] = 0x10;  // 终端能力
    buffer[6] = down_cqi;
    int result = rac_pack(buffer, 7, currentSlot);
    if (result == 0) std::cout << "Paging: 寻呼响应发送成功" << std::endl;
    else std::cout << "Paging: 寻呼响应发送失败" << std::endl;
}

void PagingStateMachine::onPagingReceived(uint32_t shipId) {
    if (shipId == MY_MMSI) {
        m_pendingSat = SAT_ID;
        m_pendingCqi = 100;
        m_state = InternalState::SendingResponse;
        if (stateChanged) stateChanged(m_state);
    }
}