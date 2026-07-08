#ifndef SLOTSCHEDULER_H
#define SLOTSCHEDULER_H

#include <vector>
#include <cstdint>
#include <functional>  
class VdesStateMachine;
class GlobalListener;


class SlotScheduler {
public:
    SlotScheduler();
    ~SlotScheduler();

    void addStateMachine(VdesStateMachine* sm);
    void start();
    void setListener(GlobalListener* listener);
    void injectPacket(const std::vector<uint8_t>& packet, int slot, int channel);
    void updateSlotFromBoard(int slot);
    int currentSlot() const { return m_currentSlot; }
    std::function<void(int)> slotUpdateCallback;

private:
    void loadSlotMap();

    int m_currentSlot;
    int m_lastSlot;
    std::vector<VdesStateMachine*> m_machines;
    GlobalListener* m_listener;
};

#endif