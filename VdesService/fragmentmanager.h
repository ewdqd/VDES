#ifndef FRAGMENTMANAGER_H
#define FRAGMENTMANAGER_H

#include <vector>
#include <cstdint>
#include <functional>   // 必须包含

struct ResourceAllocationFragment {
    uint8_t type;
    uint16_t payloadSize;
    uint32_t shipRadioId1;
    uint8_t logicalChannel1;
    uint8_t linkId1;
    uint8_t sessionId1;
    uint8_t ulCqi1;
    uint32_t shipRadioId2;
    uint8_t logicalChannel2;
    uint8_t linkId2;
    uint8_t sessionId2;
    uint8_t ulCqi2;
    uint32_t shipRadioId3;
    uint8_t logicalChannel3;
    uint8_t linkId3;
    uint8_t sessionId3;
    uint8_t ulCqi3;
    uint32_t shipRadioId4;
    uint8_t logicalChannel4;
    uint8_t linkId4;
    uint8_t sessionId4;
    uint8_t ulCqi4;
};

struct BaseFragment {
    uint8_t type;
    uint16_t fieldSize;
    uint32_t srcRadioId;
    uint8_t satelliteId;
    uint8_t sessionId;
    uint32_t destRadioId;
    uint16_t fragmentNum;
    std::vector<uint8_t> payload;
};

class FragmentManager {
public:
    FragmentManager();
    void setResourceAllocationFragment(const ResourceAllocationFragment& fragment);
    void setStartFragment(const BaseFragment& fragment);
    void addContinueFragment(const BaseFragment& fragment);
    void setEndFragment(const BaseFragment& fragment);
    void clearContinueFragments();

    // 回调（替代 Qt 信号）
    std::function<void()> onReady;
    std::function<void()> onStart;
    std::function<void()> onContinue;
    std::function<void()> onEnd;

    BaseFragment getStartFragment() const { return m_startFragment; }
    std::vector<BaseFragment> getContinueFragments() const { return m_continueFragments; }
    BaseFragment getEndFragment() const { return m_endFragment; }
    ResourceAllocationFragment getResourceFragment() const { return m_resourceFragment; }

private:
    ResourceAllocationFragment m_resourceFragment;
    BaseFragment m_startFragment;
    std::vector<BaseFragment> m_continueFragments;
    BaseFragment m_endFragment;
};

#endif