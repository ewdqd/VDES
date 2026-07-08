// downlinkshortnoackstatemachine.h
#ifndef DOWNLINKSHORTNOACKSTATEMACHINE_H
#define DOWNLINKSHORTNOACKSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "vdes_global.h"

class DownlinkShortNoAckStateMachine : public VdesStateMachine {
public:
    enum class InternalState { Idle, Receive, Abnormal };
    explicit DownlinkShortNoAckStateMachine();
    ~DownlinkShortNoAckStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    InternalState internalState() const { return m_state; }

    // 回调
    std::function<void(InternalState)> stateChanged;
    std::function<void(const std::string&, uint32_t, uint8_t)> msg16Parsed;

private:
    bool isValidSrcId(uint32_t srcId) const;
    bool isValidSatId(uint8_t satId) const;

    InternalState m_state;
    uint32_t m_srcId;
    uint8_t m_satId;
    uint32_t m_shipId;
    std::vector<uint8_t> m_payload;
};

#endif