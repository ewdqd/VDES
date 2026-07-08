#ifndef SHIP_H
#define SHIP_H

#include <QObject>
#include <QTimer>
#include "SlotScheduler.h"

// 前向声明
class Simulator;

enum ShipState {
    STATE_UPLINK_ADDR,
    STATE_DOWNLINK_ADDR,
    STATE_PAGING,
    STATE_UPLINK_SMS_ACK,
    STATE_UPLINK_SMS_NOACK,
    STATE_DOWNLINK_SMS_ACK,
    STATE_DOWNLINK_SMS_NOACK,
    STATE_BROADCAST
};

enum ReceiveState {
    RECV_IDLE,          // 空闲
    RECV_BROADCAST,     // 广播接收中
    RECV_UNICAST        // 单播（下行寻址）接收中
};

class Ship : public QObject
{
    Q_OBJECT
public:
    explicit Ship(int id, QObject *parent = nullptr);
    void handleSatMessage(const Message& msg);
    void onSlot(int slot, ChannelType channel);

signals:
    void stateUpdate(QString state);
    void logMessage(QString text, QString color = "black");   // 发送日志到模拟器

private:
    void handleDownlinkFragment(const Message& msg);
    void resetReceiver();
    void onReceiveTimeout();

    int m_shipId;
    ShipState m_currentState;
    ReceiveState m_recvState;               // 当前接收状态
    QByteArray m_recvBuffer;                // 分片重组缓冲区
    int m_expectedFragmentIndex;            // 期望的下一个分片序号
    QTimer *m_recvTimeoutTimer;             // 接收超时定时器
};

#endif
