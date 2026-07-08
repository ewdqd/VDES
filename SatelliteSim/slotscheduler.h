#ifndef SLOTSCHEDULER_H
#define SLOTSCHEDULER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QUdpSocket>
#include <functional>

enum ChannelType {
    BBSC,
    ASC,
    RAC,
    DC,
    IDLE
};

struct Message {
    int typeId;
    int shipId;
    int sessionId;
    QByteArray payload;
};

class SlotScheduler : public QObject
{
    Q_OBJECT
public:
    explicit SlotScheduler(QObject *parent = nullptr);
    ~SlotScheduler();

    // 获取当前时隙对应的信道类型
    ChannelType getCurrentChannel(int slot);

    // 获取当前时隙数 (0-2249)
    int getCurrentSlot() const { return m_currentSlot; }

    // 注入数据包
    void injectPacket(const Message& msg);

    // 设置时隙上报目标（UDP）
    void setSlotReportDestination(const QString& ip, quint16 port);

    // 启动时隙调度器
    void start();

    // 停止时隙调度器
    void stop();

signals:
    void slotUpdated(int slot, ChannelType channel);
    void packetReceived(Message msg);
    void slotReportFrame(const QByteArray& frame);
    void logMessage(const QString& text, const QString& color = "black");

private slots:
    void onSlotTimeout();

private:
    // 时隙相关
    void initSlotTiming();           // 初始化时隙定时（基于UTC）
    void advanceSlot();              // 前进一个时隙
    void updateSlotByRealTime();     // 基于真实时间更新时隙
    QByteArray buildSlotReportFrame(int slot);  // 构建时隙上报帧
    void sendSlotReport();           // 发送时隙上报

    // 信道映射
    ChannelType m_currentChannel;
    int m_currentSlot;               // 当前时隙数 (0-2249)

    // 定时器
    QTimer* m_slotTimer;
    qint64 m_lastSlotTimeMs;

    // UDP上报
    QUdpSocket* m_reportSocket;
    QString m_reportIp;
    quint16 m_reportPort;
    bool m_reportEnabled;
};

#endif // SLOTSCHEDULER_H
