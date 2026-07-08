#include "Ship.h"
#include "Simulator.h"   // 需要访问 Simulator::sendUplinkAck
#include <QDebug>

Ship::Ship(int id, QObject *parent)
    : QObject(parent), m_shipId(id), m_recvState(RECV_IDLE), m_expectedFragmentIndex(0)
{
    m_currentState = STATE_UPLINK_ADDR;
    emit stateUpdate("上行寻址");

    m_recvTimeoutTimer = new QTimer(this);
    m_recvTimeoutTimer->setSingleShot(true);
    connect(m_recvTimeoutTimer, &QTimer::timeout, this, &Ship::onReceiveTimeout);
}

void Ship::onSlot(int slot, ChannelType channel)
{
    Q_UNUSED(slot);
    Q_UNUSED(channel);
}

void Ship::handleSatMessage(const Message& msg)
{
    // 处理资源分配帧（广播/单播）
    if (msg.typeId == 12) {
        if (msg.shipId == 0) {
            m_currentState = STATE_BROADCAST;
            emit stateUpdate("广播");
            // 进入广播接收模式
            resetReceiver();
            m_recvState = RECV_BROADCAST;
            emit logMessage("进入广播接收模式，等待分片数据", "green");
            m_recvTimeoutTimer->start(5000); // 5秒超时
        } else if (msg.shipId == 1) {
            m_currentState = STATE_DOWNLINK_ADDR;
            emit stateUpdate("下行寻址");
            // 进入单播接收模式
            resetReceiver();
            m_recvState = RECV_UNICAST;
            emit logMessage("进入下行寻址接收模式，等待分片数据", "orange");
            m_recvTimeoutTimer->start(5000);
        }
        return;
    }

    // 处理下行分片消息 (40:起始, 41:连续, 42:结束)
    if (msg.typeId == 40 || msg.typeId == 41 || msg.typeId == 42) {
        handleDownlinkFragment(msg);
        return;
    }

    // 原有其他消息类型处理
    if (msg.typeId == 11) {
        m_currentState = STATE_PAGING;
        emit stateUpdate("寻呼");
    }
    if (msg.typeId == 14) {
        m_currentState = STATE_DOWNLINK_SMS_ACK;
        emit stateUpdate("下行短消息(带ACK)");
    }
    if (msg.typeId == 16) {
        m_currentState = STATE_DOWNLINK_SMS_NOACK;
        emit stateUpdate("下行短消息(无ACK)");
    }
}

void Ship::handleDownlinkFragment(const Message& msg)
{
    if (m_recvState == RECV_IDLE) {
        emit logMessage("收到下行分片但未处于接收状态，忽略", "red");
        return;
    }

    int fragmentIndex = -1;
    QByteArray fragmentData;

    if (msg.typeId == 40) {        // 起始片段
        fragmentIndex = 0;
        m_recvBuffer.clear();
        m_expectedFragmentIndex = 1;
        fragmentData = msg.payload;
        m_recvBuffer.append(fragmentData);
        emit logMessage(QString("收到起始分片 (索引0)，长度=%1").arg(fragmentData.size()), "blue");
        m_recvTimeoutTimer->start(5000);
    }
    else if (msg.typeId == 41) {   // 连续片段
        if (msg.payload.size() < 1) {
            emit logMessage("连续分片载荷为空", "red");
            resetReceiver();
            emit stateUpdate("执行过程异常");
            return;
        }
        fragmentIndex = (unsigned char)msg.payload[0];
        fragmentData = msg.payload.mid(1);
        if (fragmentIndex == m_expectedFragmentIndex) {
            m_recvBuffer.append(fragmentData);
            m_expectedFragmentIndex++;
            emit logMessage(QString("收到连续分片 (索引%1)，累计长度=%2").arg(fragmentIndex).arg(m_recvBuffer.size()), "blue");
            m_recvTimeoutTimer->start(5000);
        } else {
            emit logMessage(QString("分片序号错误：期望%1，收到%2，接收异常").arg(m_expectedFragmentIndex).arg(fragmentIndex), "red");
            resetReceiver();
            emit stateUpdate("执行过程异常");
        }
    }
    else if (msg.typeId == 42) {   // 结束片段
        if (msg.payload.size() < 1) {
            emit logMessage("结束分片载荷为空", "red");
            resetReceiver();
            emit stateUpdate("执行过程异常");
            return;
        }
        fragmentIndex = (unsigned char)msg.payload[0];
        fragmentData = msg.payload.mid(1);
        if (fragmentIndex == m_expectedFragmentIndex) {
            m_recvBuffer.append(fragmentData);
            emit logMessage(QString("收到结束分片，总数据长度=%1").arg(m_recvBuffer.size()), "green");

            // 根据接收模式决定是否回复 ACK
            if (m_recvState == RECV_UNICAST) {
                // 单播模式：通过父对象查找 Simulator 并发送 ACK (type=13)
                Simulator *sim = qobject_cast<Simulator*>(parent());
                if (sim) {
                    sim->sendUplinkAck(m_shipId);
                    emit logMessage("单播模式：已回复 ACK (type=13)", "purple");
                } else {
                    emit logMessage("错误：无法找到 Simulator 实例", "red");
                }
            } else {
                emit logMessage("广播模式：不回复 ACK", "gray");
            }

            // 接收完成，重置状态
            resetReceiver();
            emit stateUpdate("接收完成本的数据");
        } else {
            emit logMessage(QString("结束分片序号错误，接收异常"), "red");
            resetReceiver();
            emit stateUpdate("执行过程异常");
        }
        m_recvTimeoutTimer->stop();
    }
}

void Ship::resetReceiver()
{
    m_recvState = RECV_IDLE;
    m_recvBuffer.clear();
    m_expectedFragmentIndex = 0;
    if (m_recvTimeoutTimer->isActive())
        m_recvTimeoutTimer->stop();
}

void Ship::onReceiveTimeout()
{
    emit logMessage("分片接收超时，未收到完整消息", "red");
    resetReceiver();
    emit stateUpdate("执行过程异常");
}
