#ifndef DOWNLINKSHORTACKSTATEMACHINE_H
#define DOWNLINKSHORTACKSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "vdes_global.h"

class DownlinkShortAckStateMachine : public VdesStateMachine {
public:
    enum class InternalState { Idle, SendingAck };
    explicit DownlinkShortAckStateMachine();
    ~DownlinkShortAckStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    InternalState internalState() const { return m_state; }

    // 回调
    std::function<void(InternalState)> stateChanged;
    std::function<void(int, uint32_t, uint8_t, const std::string&)> downlinkShortMessageReceived;
    std::function<void(bool, uint32_t, uint8_t)> ackSendResult;

private:
    void sendAck(uint32_t src_id, uint8_t sat_id, int currentSlot);
    bool isValidSrcId(uint32_t srcId) const;
    bool isValidSatId(uint8_t satId) const;
    bool isForMe(uint32_t destId) const { return destId == MY_MMSI; }

    InternalState m_state;
    uint32_t m_pendingSrc;
    uint8_t m_pendingSat;
    uint32_t m_pendingDest;
};

#endif