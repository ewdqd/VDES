#include "slotscheduler.h"
#include "globallistener.h"
#include "vdesstatemachine.h"
#include "vdes_global.h"
#include <iostream>

SlotScheduler::SlotScheduler() : m_currentSlot(0), m_lastSlot(-1), m_listener(nullptr) {
    loadSlotMap();
}

SlotScheduler::~SlotScheduler() {}

void SlotScheduler::addStateMachine(VdesStateMachine* sm) {
    if (sm) m_machines.push_back(sm);
}

void SlotScheduler::start() {
    std::cout << "SlotScheduler: 时隙由板卡通过UDP帧驱动" << std::endl;
}

void SlotScheduler::setListener(GlobalListener* listener) {
    m_listener = listener;
}

void SlotScheduler::injectPacket(const std::vector<uint8_t>& packet, int slot, int channel) {
    if (m_listener) {
        m_listener->handlePacket(packet, slot, channel);
    }
    for (auto sm : m_machines) {
        sm->handlePacket(packet, slot, channel);
    }
}

void SlotScheduler::updateSlotFromBoard(int slot) {
    if (slot < 0 || slot >= SLOT_TOTAL) {
        std::cerr << "SlotScheduler: 无效的板卡时隙 " << slot << std::endl;
        return;
    }
    if (slot != m_lastSlot) {
        m_currentSlot = slot;
        m_lastSlot = slot;
        setGlobalCurrentSlot(slot);
        for (auto sm : m_machines) {
            sm->onSlot(m_currentSlot);
        }
        if (slotUpdateCallback) slotUpdateCallback(slot);
    }
}

void SlotScheduler::loadSlotMap() {
    slot_resize();  // 已在 vdes_global 中定义
    std::cout << "SlotScheduler: 时隙映射已加载" << std::endl;
}