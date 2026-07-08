// vdesstatemachine.h
#ifndef VDESSTATEMACHINE_H
#define VDESSTATEMACHINE_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

enum class MessageType {
    Broadcast,
    DownlinkAddressed,
    DownlinkShortAck,
    DownlinkShortNoAck,
    UplinkAddressed,
    UplinkShortAck,
    UplinkShortNoAck,
    Paging
};

class VdesStateMachine {
public:
    explicit VdesStateMachine(MessageType type);
    virtual ~VdesStateMachine() = default;

    virtual void onSlot(int currentSlot) = 0;
    virtual void handlePacket(const std::vector<uint8_t>& packet, int slot, int channel) = 0;

    MessageType type() const { return m_type; }

    // 回调函数替代 Qt 信号
    std::function<void(const std::string&)> logMessage;
    std::function<void(const std::vector<uint8_t>&, uint32_t)> dataReceived;
    std::function<void(const std::string&, uint32_t)> downlinkMessageComplete;
    std::function<void(int, MessageType, const std::string&)> slotActivity;

protected:
    MessageType m_type;
};

#endif