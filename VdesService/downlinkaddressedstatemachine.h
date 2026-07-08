// downlinkaddressedstatemachine.h
#ifndef DOWNLINKADDRESSEDSTATEMACHINE_H
#define DOWNLINKADDRESSEDSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "vdes_global.h"
#include <vector>
#include <cstdint>

class DownlinkAddressedStateMachine : public VdesStateMachine {
public:
    enum InternalState { Idle, Receiving, SendingAck, Abnormal };
    explicit DownlinkAddressedStateMachine();
    ~DownlinkAddressedStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;
    void onResourceAllocated(uint8_t logicChannel, uint8_t linkID, uint8_t sessionID, uint8_t uplinkCQI);

    InternalState currentState() const { return m_curState; }
    std::function<void(InternalState)> stateChanged;
    std::function<void(const std::string&)> logMessage;
    std::function<void(const std::vector<uint8_t>&)> dataComplete;

private:
    void handleDataFragment(const std::vector<uint8_t>& packet);
    void sendAckToSatellite();
    void resetReceiveState();
    void changeState(InternalState newState);

    InternalState m_curState;
    int m_slotNum;
    uint32_t m_shipId;

    bool m_resourceAllocated;
    uint8_t m_logicChannel;
    uint8_t m_linkID;
    uint8_t m_sessionID;
    uint8_t m_uplinkCQI;

    std::vector<uint8_t> m_receivedData;
    bool m_isEndFragment;
    int m_fragmentSeq;
    bool m_idleSlotLogged;

    const std::string ACK_TARGET_IP = "127.0.0.1";
    const uint16_t ACK_TARGET_PORT = 9091;
};

#endif