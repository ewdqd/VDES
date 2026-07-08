// uplinkshortackstatemachine.h
#ifndef UPLINKSHORTACKSTATEMACHINE_H
#define UPLINKSHORTACKSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "vdes_global.h"
#include "timer.h"
#include <vector>
#include <cstdint>

enum class ShortMsgAckState { Idle, Wait, Send, Abnormal };

class UplinkShortAckStateMachine : public VdesStateMachine {
public:
    explicit UplinkShortAckStateMachine();
    ~UplinkShortAckStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    void setMyMmsi(uint32_t mmsi);
    bool sendMessage(int currentSlot, int specifiedSlot, uint16_t linkId, const Modu_2send_short& msg);
    void onMacFrameUpdated();

    ShortMsgAckState currentState() const { return m_state; }
    std::function<void(ShortMsgAckState)> stateChanged;
    std::function<void(bool, const std::string&)> sendFinished;
    std::function<void(const std::string&)> errorOccurred;

private:
    void changeState(ShortMsgAckState newState);
    void sendCurrentMessage(int currentSlot);
    void clearPending();
    void parseUplinkAck(const uint8_t* data, int len);
    void onAckTimeout();
    void onAbnormalTimeout();

    ShortMsgAckState m_state;
    int m_specifiedSlot;
    uint8_t m_specifiedLinkId;
    Modu_2send_short m_pendingMsg;
    bool m_hasPendingMsg;
    uint32_t m_myMmsi;

    Timer m_ackTimer;
    Timer m_abnormalTimer;
    int m_retryCount;
    static constexpr int MAX_RETRY = 3;

    bool m_macFrameReceived;
    bool m_macFrameAllowed;
    int m_racLimit;
    int m_randomInterval;
    int m_mediaAccessPriority;
    int m_networkStatus;
};

#endif