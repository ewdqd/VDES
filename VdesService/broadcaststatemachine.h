// broadcaststatemachine.h
#ifndef BROADCASTSTATEMACHINE_H
#define BROADCASTSTATEMACHINE_H

#include "vdesstatemachine.h"
#include "fragmentmanager.h"
#include "timer.h"
#include <vector>
#include <functional>

class BroadcastStateMachine : public VdesStateMachine {
public:
    enum State { Idle, Working, Abnormal };
    explicit BroadcastStateMachine(FragmentManager* fragmentMgr);
    ~BroadcastStateMachine() override;

    void onSlot(int currentSlot) override;
    void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) override;
    void onBroadcastResourceAllocationReceived();

    State currentState() const { return m_currentState; }
    std::function<void(State)> stateChanged;
    std::function<void(const std::vector<uint8_t>&, uint32_t)> broadcastDataReceived;

private:
    void onReadyReceived();
    void onStartReceived();
    void onContinueReceived();
    void onEndReceived();
    void onTimerTimeout();
    void switchState(State newState);
    void assembleAndShowData();

    FragmentManager* m_fragmentMgr;
    State m_currentState;
    Timer m_workingTimer;
    static constexpr int WORK_TIMEOUT_MS = 12000;
    bool m_hasReceivedStart;
    bool m_hasReceivedContinue;
    bool m_hasReceivedEnd;
};

#endif