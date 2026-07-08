// DownlinkManager.cpp
#include "DownlinkManager.h"
#include <QRandomGenerator>
#include <QDebug>

DownlinkManager::DownlinkManager(QObject *parent)
    : QObject(parent)
    , m_satId(99)
    , m_satSourceId(0x12345678)  // 卫星源ID
{
}

DownlinkManager::~DownlinkManager()
{
    for (auto &session : m_sessions) {
        if (session.sendTimer) {
            session.sendTimer->stop();
            delete session.sendTimer;
        }
    }
    m_sessions.clear();
}

int DownlinkManager::allocateDcChannel()
{
    for (int i = 0; i < 6; ++i) {
        if (!m_occupiedDcChannels.contains(i)) {
            m_occupiedDcChannels.insert(i);
            emit logMessage(QString("下行管理器: 分配DC信道 %1").arg(i), "green");
            return i;
        }
    }
    // 所有信道被占用，返回-1表示失败
    emit logMessage("下行管理器: 所有DC信道被占用，无法分配", "red");
    return -1;
}

void DownlinkManager::releaseDcChannel(int logicChannel)
{
    if (m_occupiedDcChannels.contains(logicChannel)) {
        m_occupiedDcChannels.remove(logicChannel);
        emit logMessage(QString("下行管理器: 释放DC信道 %1").arg(logicChannel), "gray");
    }
}

bool DownlinkManager::sendDownlinkMessage(int shipId, const QByteArray &data,
                                          uint8_t sessionId, int logicChannel)
{
    // 检查是否已有会话
    if (m_sessions.contains(shipId)) {
        emit logMessage(QString("下行管理器: 船舶%1已有进行中的会话，请等待完成").arg(shipId), "red");
        return false;
    }

    // 检查数据是否为空
    if (data.isEmpty()) {
        emit logMessage("下行管理器: 发送数据为空", "red");
        return false;
    }

    // 检查DC信道有效性
    if (logicChannel < 0 || logicChannel >= 6) {
        emit logMessage(QString("下行管理器: 无效的逻辑信道 %1").arg(logicChannel), "red");
        return false;
    }

    // 计算分片数
    int totalFragments = (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;

    // 物理信道映射
    static const uint8_t phyChannelMap[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    uint8_t phyChannel = phyChannelMap[logicChannel];

    // 创建定时器
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, shipId]() {
        onSendTimeout(shipId);
    });

    // 创建会话
    DownlinkSession session;
    session.shipId = shipId;
    session.sessionId = sessionId;
    session.allocatedLogicChannel = logicChannel;
    session.allocatedPhyChannel = phyChannel;
    session.dataToSend = data;
    session.totalFragments = totalFragments;
    session.nextFragmentToSend = 0;
    session.currentFragmentSent = -1;
    session.sendTimer = timer;
    session.retryCount = 0;
    session.waitingForAck = false;
    session.completed = false;

    m_sessions[shipId] = session;

    emit logMessage(QString("下行管理器: 开始发送下行消息 船舶=%1 会话=%2 总分片=%3 逻辑信道=%4 物理信道=0x%5")
                        .arg(shipId).arg(sessionId).arg(totalFragments)
                        .arg(logicChannel).arg(phyChannel, 2, 16, QChar('0')), "blue");

    // 立即发送第一个分片
    sendFragment(shipId, 0);
    return true;
}

QByteArray DownlinkManager::buildFragmentPacket(int shipId, int fragmentNum, bool isStart, bool isEnd)
{
    if (!m_sessions.contains(shipId)) {
        return QByteArray();
    }

    DownlinkSession &session = m_sessions[shipId];

    // 计算分片数据
    int startOffset = fragmentNum * MAX_PAYLOAD_SIZE;
    int dataSize = qMin(MAX_PAYLOAD_SIZE, session.dataToSend.size() - startOffset);
    QByteArray fragmentData = session.dataToSend.mid(startOffset, dataSize);

    // 确定类型
    uint8_t type;
    if (isStart) {
        type = 30;  // 起始片段
    } else if (isEnd) {
        type = 32;  // 结束片段
    } else {
        type = 31;  // 中间片段
    }

    // 构建分片头
    DownlinkFragmentHeader header;
    header.type = type;
    header.payloadSize = static_cast<uint16_t>(dataSize);
    header.sourceId = m_satSourceId;
    header.satId = m_satId;
    header.sessionId = session.sessionId;
    header.destId = static_cast<uint32_t>(shipId);
    header.fragmentNum = static_cast<uint16_t>(fragmentNum);

    // 打包
    QByteArray packet;
    packet.append(reinterpret_cast<const char*>(&header), sizeof(header));
    packet.append(fragmentData);

    return packet;
}

void DownlinkManager::sendFragment(int shipId, int fragmentNum)
{
    if (!m_sessions.contains(shipId)) {
        return;
    }

    DownlinkSession &session = m_sessions[shipId];

    bool isStart = (fragmentNum == 0);
    bool isEnd = (fragmentNum == session.totalFragments - 1);

    QByteArray packet = buildFragmentPacket(shipId, fragmentNum, isStart, isEnd);
    if (packet.isEmpty()) {
        emit logMessage(QString("下行管理器: 构建分片失败 船舶=%1 片段号=%2").arg(shipId).arg(fragmentNum), "red");
        cleanupSession(shipId);
        return;
    }

    // 获取当前时隙
    int currentSlot = m_currentSlotGetter ? m_currentSlotGetter() : 0;
    int sendSlot = currentSlot + 2;

    // 通过UDP发送
    if (m_udpSender) {
        m_udpSender(packet, session.allocatedPhyChannel, sendSlot, 20);

        QString typeStr;
        if (isStart) typeStr = "起始";
        else if (isEnd) typeStr = "结束";
        else typeStr = "中间";

        emit logMessage(QString("下行管理器: 发送%1分片 船舶=%2 片段号=%3/%4 大小=%5字节 物理信道=0x%6 时隙=%7")
                            .arg(typeStr).arg(shipId).arg(fragmentNum + 1)
                            .arg(session.totalFragments).arg(packet.size())
                            .arg(session.allocatedPhyChannel, 2, 16, QChar('0'))
                            .arg(sendSlot), "green");
    }

    session.currentFragmentSent = fragmentNum;
    session.nextFragmentToSend = fragmentNum + 1;
    session.waitingForAck = isEnd;  // 只有最后一个分片需要等待ACK
    session.sendTimer->start(SEND_TIMEOUT_MS);
}

void DownlinkManager::handleDownlinkAck(const QByteArray &data)
{
    if (data.size() < static_cast<int>(sizeof(DownlinkAck))) {
        emit logMessage(QString("下行管理器: ACK消息长度不足，期望%1实际%2")
                            .arg(sizeof(DownlinkAck)).arg(data.size()), "red");
        return;
    }

    const DownlinkAck *ack = reinterpret_cast<const DownlinkAck*>(data.constData());
    if (ack->type != 29) {
        return;  // 不是下行ACK
    }

    int shipId = static_cast<int>(ack->stationId);
    uint8_t cqi = ack->downlinkCqi;

    // 解析ACK/NACK掩膜：24位位图，每位对应一个分片
    uint32_t ackMask = (static_cast<uint32_t>(ack->ackMask0) << 16) |
                       (static_cast<uint32_t>(ack->ackMask1) << 8)  |
                       static_cast<uint32_t>(ack->ackMask2);

    emit logMessage(QString("下行管理器: 收到下行ACK 船舶=%1 CQI=%2 掩膜=0x%3")
                        .arg(shipId).arg(cqi).arg(ackMask, 6, 16, QChar('0')), "purple");

    if (!m_sessions.contains(shipId)) {
        emit logMessage(QString("下行管理器: 未找到船舶%1的会话").arg(shipId), "red");
        return;
    }

    DownlinkSession &session = m_sessions[shipId];

    // 检查所有分片是否都已确认（掩膜全1）
    uint32_t expectedMask = (1 << session.totalFragments) - 1;
    if ((ackMask & expectedMask) == expectedMask) {
        // 所有分片已确认 → 发送完成
        session.sendTimer->stop();
        emit logMessage(QString("下行管理器: 下行消息发送完成 船舶=%1 会话=%2 总数据=%3字节")
                            .arg(shipId).arg(session.sessionId).arg(session.dataToSend.size()), "green");
        emit sessionComplete(shipId, session.sessionId);
        cleanupSession(shipId);
    } else {
        // 部分分片未确认 → 重传缺失分片
        emit logMessage(QString("下行管理器: 船舶%1 部分分片未确认，重传中...").arg(shipId), "blue");
        for (int i = 0; i < session.totalFragments; ++i) {
            if (!(ackMask & (1 << i))) {
                session.retryCount++;
                if (session.retryCount <= MAX_RETRY) {
                    sendFragment(shipId, i);
                } else {
                    emit logMessage(QString("下行管理器: 分片%1重试次数用尽，会话终止").arg(i), "red");
                    emit sessionFailed(shipId, session.sessionId, QString("分片%1重试次数用尽").arg(i));
                    cleanupSession(shipId);
                }
                break;
            }
        }
    }
}

void DownlinkManager::onSendTimeout(int shipId)
{
    if (!m_sessions.contains(shipId)) {
        return;
    }

    DownlinkSession &session = m_sessions[shipId];
    session.retryCount++;

    if (session.retryCount <= MAX_RETRY) {
        emit logMessage(QString("下行管理器: 发送超时，重试 %1/%2 船舶=%3 片段号=%4")
                            .arg(session.retryCount).arg(MAX_RETRY).arg(shipId)
                            .arg(session.currentFragmentSent), "blue");

        // 重发当前分片
        sendFragment(shipId, session.currentFragmentSent);
    } else {
        emit logMessage(QString("下行管理器: 发送超时，重试次数用尽 船舶=%1 会话=%2")
                            .arg(shipId).arg(session.sessionId), "red");
        emit sessionFailed(shipId, session.sessionId, "发送超时，重试次数用尽");
        cleanupSession(shipId);
    }
}

void DownlinkManager::cleanupSession(int shipId)
{
    if (!m_sessions.contains(shipId)) {
        return;
    }

    DownlinkSession &session = m_sessions[shipId];

    // 释放DC信道
    releaseDcChannel(session.allocatedLogicChannel);

    // 停止并删除定时器
    if (session.sendTimer) {
        session.sendTimer->stop();
        delete session.sendTimer;
    }

    // 移除会话
    m_sessions.remove(shipId);
}