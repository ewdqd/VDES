#ifndef UPLINKSHORTNOACKSTATEMACHINE_H
#define UPLINKSHORTNOACKSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "timer.h"
#include "vdes_global.h"
#include <functional>

enum class ShortMsgState {
    Idle, Wait, Send, Abnormal
};

class UplinkShortNoAckStateMachine : public VdesStateMachine {
public:
    explicit UplinkShortNoAckStateMachine();
    ~UplinkShortNoAckStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    void addMessage(const Modu_2send_short& msg);
    void addMessageNoDest(uint8_t type, const std::vector<uint8_t>& data);
    bool sendMessageWithSpecifiedSlot(int currentSlot, int specifiedSlot, uint16_t linkId, const Modu_2send_short& msg);
    bool sendMessageNoDestWithSpecifiedSlot(int currentSlot, int specifiedSlot, uint16_t linkId, uint8_t type, const std::vector<uint8_t>& data);
    bool validateRacSlot(int slot) const;

    ShortMsgState currentState() const { return m_currentState; }
    void setMyMmsi(uint32_t mmsi) { m_myMmsi = mmsi; }

    // 回调
    std::function<void(ShortMsgState)> stateChanged;
    std::function<void(bool, const std::string&)> sendFinished;

    // 普通函数，不是 slot
    void onMacFrameUpdated();

private:
    void initStateMachine();
    void sendCurrentMessage(int currentSlot);
    bool isRacSlot(int slot);
    int generateRacOffset();

    void onEnterIdleState();
    void onEnterWaitState();
    void onEnterSendState();
    void onEnterAbnormalState();
    void onFifteenMinuteTimeout();
    void onWaitTimeout();
    void onAbnormalTimeout();

    ShortMsgState m_currentState;
    int m_retryCount;
    static constexpr int MAX_RETRY_COUNT = 3;

    bool m_hasDestId;
    int m_currentMsgIndex;

    int m_racLimit;
    int m_randomInterval;
    int m_mediaAccessPriority;
    int m_networkStatus;

    int m_racOffset;
    int m_racSlotCount;

    Timer m_fifteenMinTimer;
    Timer m_waitTimer;
    Timer m_abnormalTimer;

    int m_specifiedSlot;
    bool m_useSpecifiedSlot;
    bool m_missedThisCycle;
    int m_retryCountForSlot;
    static constexpr int MAX_SLOT_RETRY = 3;

    uint32_t m_myMmsi;
};

#endif