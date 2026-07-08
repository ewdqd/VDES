#ifndef PAGINGSTATEMACHINE_H
#define PAGINGSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "vdes_global.h"

class PagingStateMachine : public VdesStateMachine {
public:
    enum class InternalState { Idle, SendingResponse };
    explicit PagingStateMachine();
    ~PagingStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    InternalState internalState() const { return m_state; }

    // 回调
    std::function<void(InternalState)> stateChanged;
    void onPagingReceived(uint32_t shipId);  // 可被外部调用

private:
    void sendPagingResponse(uint8_t sat_id, uint8_t down_cqi, int currentSlot);

    InternalState m_state;
    uint8_t m_pendingSat;
    uint8_t m_pendingCqi;
};

#endif