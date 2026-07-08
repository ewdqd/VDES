#ifndef UPLINKADDRESSEDSTATEMACHINE_H
#define UPLINKADDRESSEDSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "timer.h"
#include "vdes_global.h"
#include <vector>
#include <list>
#include <functional>

struct FragmentInfo {
    int fragmentNum;
    int dataStart;
    int dataSize;
    bool isLast;
    int retryCount;
    bool acked;
};

class UplinkAddressedStateMachine : public VdesStateMachine {
public:
    enum class InternalState { Idle, ResReq, ASC, Send, WaitAck, Wait, Abnormal };
    explicit UplinkAddressedStateMachine();
    ~UplinkAddressedStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;

    void sendLongMessage(const std::vector<uint8_t>& data, uint32_t destId);
    void sendLongMessageWithSpecifiedSlot(int currentSlot, int specifiedRacSlot,
        const std::vector<uint8_t>& data, uint32_t destId);
    bool validateRacSlotForRequest(int slot) const;
    void setMyMmsi(uint32_t mmsi) { m_myMmsi = mmsi; }

    // 回调
    std::function<void(InternalState)> stateChanged;
    std::function<void(bool, const std::string&)> sendFinished;

    // 普通函数，不是 slot
    void onMyResourceAllocationReceived(uint8_t logicChannel, uint8_t linkID,
        uint8_t sessionID, uint8_t uplinkCQI);
    void onMacFrameUpdated();

private:
    void initStateMachine();
    void updateConfigFromGlobals();
    void sendResourceRequest(int currentSlot);
    void parseUplinkAck(const uint8_t* data, int len);
    void startSendingFragments();
    void sendNextFragment(int currentSlot);
    bool isSlotInDcChannel(int slot) const;
    bool isRacSlot(int slot) const;
    std::list<FragmentInfo> splitMessage() const;
    void clearSession();
    void updateLinkId(uint8_t newLinkId);
    void resetForRetransmission();

    void onEnterIdleState();
    void onEnterResReqState();
    void onEnterASCState();
    void onEnterSendState();
    void onEnterWaitState();
    void onEnterAbnormalState();
    void onEnterWaitAckState();
    void onAscTimeout();
    void onWaitTimeout();
    void onAckWaitTimeout();

    InternalState m_state;
    Timer m_ascTimer;
    Timer m_waitTimer;
    Timer m_ackWaitTimer;
    Timer m_abnormalTimer;

    uint8_t m_satelliteID;
    uint8_t m_mainNetID;
    uint8_t m_roamNetID;
    uint16_t m_uplinkMaxLength;
    uint8_t m_mediaAccessPriority;
    uint8_t m_racMsgAccessLimit;
    uint8_t m_networkStatus;
    uint8_t m_arqTimeoutLimit;

    std::vector<uint8_t> m_messageData;
    uint32_t m_destId;
    int m_totalFragments;
    int m_nextFragment;
    std::list<FragmentInfo> m_fragments;

    uint8_t m_assignedLinkId;
    uint8_t m_assignedLogicChannel;
    uint8_t m_sessionId;
    uint8_t m_uplinkCqi;
    bool m_linkIdChanged;

    int m_retryCount15min;
    static constexpr int MAX_RETRY_15MIN = 3;
    static constexpr int ACK_WAIT_TIMEOUT_MS = 5000;

    int m_specifiedRacSlot;
    bool m_useSpecifiedRacSlot;
    bool m_missedRacSlot;
    int m_racRetryCount;
    static constexpr int MAX_RAC_RETRY = 3;

    uint32_t m_myMmsi;
};

#endif