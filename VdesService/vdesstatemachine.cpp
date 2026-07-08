// vdesstatemachine.cpp
#include "vdesstatemachine.h"

VdesStateMachine::VdesStateMachine(MessageType type) : m_type(type) {
    // 初始化所有回调，避免未定义行为（空函数对象）
    logMessage = [](const std::string&) {};
    dataReceived = [](const std::vector<uint8_t>&, uint32_t) {};
    downlinkMessageComplete = [](const std::string&, uint32_t) {};
    slotActivity = [](int, MessageType, const std::string&) {};
}