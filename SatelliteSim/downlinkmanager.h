// DownlinkManager.h
#ifndef DOWNLINKMANAGER_H
#define DOWNLINKMANAGER_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QByteArray>
#include <QSet>
#include <functional>

// 下行分片头结构（与船舶端一致）
#pragma pack(push, 1)
struct DownlinkFragmentHeader {
    uint8_t type;           // 30=起始, 31=中间, 32=结束
    uint16_t payloadSize;   // 载荷大小
    uint32_t sourceId;      // 源ID（卫星）
    uint8_t satId;          // 卫星ID
    uint8_t sessionId;      // 会话ID
    uint32_t destId;        // 目的ID（船舶）
    uint16_t fragmentNum;   // 片段号
};
#pragma pack(pop)

// 下行链路确认消息（类型29）— 符合 ITU-R M.2092 规格
// 格式: type(1) + satId(1) + shipRadioId(4) + downlinkCqi(1) + ackNackMask(3) = 10B
#pragma pack(push, 1)
struct DownlinkAck {
    uint8_t type;           // 29
    uint8_t satId;          // 卫星ID
    uint32_t stationId;     // 船舶电台ID
    uint8_t downlinkCqi;    // 下行链路CQI
    uint8_t ackMask0;       // ACK/NACK 掩膜0
    uint8_t ackMask1;       // ACK/NACK 掩膜1
    uint8_t ackMask2;       // ACK/NACK 掩膜2
};
#pragma pack(pop)
static_assert(sizeof(DownlinkAck) == 10, "DownlinkAck must be 10 bytes per spec");

// 下行会话结构
struct DownlinkSession {
    int shipId;                     // 船舶ID
    uint8_t sessionId;              // 会话ID
    int allocatedLogicChannel;      // 分配的逻辑信道(0-5)
    uint8_t allocatedPhyChannel;    // 物理信道(0x0A-0x0F)
    QByteArray dataToSend;          // 待发送的数据
    int totalFragments;             // 总分片数
    int nextFragmentToSend;         // 下一个要发送的片段号
    int currentFragmentSent;        // 当前已发送的片段号
    QTimer *sendTimer;              // 发送定时器
    int retryCount;                 // 重试次数
    bool waitingForAck;             // 是否等待ACK
    bool completed;                 // 是否完成
};

class DownlinkManager : public QObject
{
    Q_OBJECT
public:
    explicit DownlinkManager(QObject *parent = nullptr);
    ~DownlinkManager();

    // 设置UDP发送函数（由Simulator注入）
    void setUdpSender(std::function<void(const QByteArray&, uint8_t, uint16_t, uint16_t)> sender) {
        m_udpSender = sender;
    }

    // 设置获取当前时隙的函数
    void setCurrentSlotGetter(std::function<int()> getter) {
        m_currentSlotGetter = getter;
    }

    // 发送下行寻址消息（自动分片）
    // 返回true表示会话创建成功，false表示失败（已有会话或无DC信道）
    bool sendDownlinkMessage(int shipId, const QByteArray &data,
                             uint8_t sessionId, int logicChannel);

    // 处理接收到的下行ACK
    void handleDownlinkAck(const QByteArray &data);

    // DC信道管理
    int allocateDcChannel();         // 返回分配的DC信道(0-5)，无可用时返回-1
    void releaseDcChannel(int logicChannel);

    // 获取DC信道占用状态
    bool isDcChannelOccupied(int logicChannel) const {
        return m_occupiedDcChannels.contains(logicChannel);
    }

    // 获取会话状态
    bool hasSession(int shipId) const { return m_sessions.contains(shipId); }

signals:
    void logMessage(const QString &text, const QString &color = "black");
    void sessionComplete(int shipId, uint8_t sessionId);
    void sessionFailed(int shipId, uint8_t sessionId, const QString &reason);

private slots:
    void onSendTimeout(int shipId);

private:
    void sendFragment(int shipId, int fragmentNum);
    QByteArray buildFragmentPacket(int shipId, int fragmentNum, bool isStart, bool isEnd);
    void cleanupSession(int shipId);

    // 会话管理
    QMap<int, DownlinkSession> m_sessions;  // key = shipId
    QSet<int> m_occupiedDcChannels;         // 0-5

    // 卫星配置
    uint8_t m_satId;
    uint32_t m_satSourceId;

    // 分片配置
    static constexpr int MAX_PAYLOAD_SIZE = 100;   // 每个分片最大载荷
    static constexpr int MAX_RETRY = 3;            // 最大重试次数
    static constexpr int SEND_TIMEOUT_MS = 5000;   // 发送超时时间(ms)

    // 外部依赖
    std::function<void(const QByteArray&, uint8_t, uint16_t, uint16_t)> m_udpSender;
    std::function<int()> m_currentSlotGetter;
};

#endif // DOWNLINKMANAGER_H