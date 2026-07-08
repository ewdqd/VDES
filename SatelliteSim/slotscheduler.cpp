#include "SlotScheduler.h"
#include <QUdpSocket>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>  // 添加这一行
SlotScheduler::SlotScheduler(QObject *parent)
    : QObject(parent)
    , m_currentSlot(0)
    , m_lastSlotTimeMs(0)
    , m_slotTimer(nullptr)
    , m_reportSocket(nullptr)
    , m_reportIp("127.0.0.1")
    , m_reportPort(9000)  // 改为船舶端监听的端口
    , m_reportEnabled(false)
{
}

SlotScheduler::~SlotScheduler()
{
    stop();
    if (m_reportSocket) {
        m_reportSocket->close();
        delete m_reportSocket;
    }
}

// ========== 时隙上报目标设置 ==========
void SlotScheduler::setSlotReportDestination(const QString& ip, quint16 port)
{
    m_reportIp = ip;
    m_reportPort = port;
    m_reportEnabled = true;

    if (!m_reportSocket) {
        m_reportSocket = new QUdpSocket(this);
    }

    emit logMessage(QString("时隙上报目标已设置: %1:%2").arg(ip).arg(port), "green");
}

// ========== 启动时隙调度器 ==========
void SlotScheduler::start()
{
    if (m_slotTimer) {
        stop();
    }

    initSlotTiming();
    emit logMessage("时隙调度器已启动", "blue");
}

// ========== 停止时隙调度器 ==========
void SlotScheduler::stop()
{
    if (m_slotTimer) {
        m_slotTimer->stop();
        delete m_slotTimer;
        m_slotTimer = nullptr;
    }
}

// ========== 基于UTC时间的时隙初始化 ==========
void SlotScheduler::initSlotTiming()
{
    // 获取当前UTC时间
    QDateTime utcNow = QDateTime::currentDateTimeUtc();
    qint64 nowMs = utcNow.toMSecsSinceEpoch();

    // 计算当前时隙：每个时隙 = 80/3 ≈ 26.6666667 毫秒
    // 时隙号 = (当前毫秒数 * 3 / 80) % 2250
    qint64 slotNumber = (nowMs * 3) / 80;
    m_currentSlot = slotNumber % 2250;

    // 获取当前时隙的信道类型
    m_currentChannel = getCurrentChannel(m_currentSlot);

    // 记录开始时间
    m_lastSlotTimeMs = nowMs;

    // 计算到下一个时隙边界的毫秒数
    qint64 currentSlotMs = (m_currentSlot * 80) / 3;
    qint64 nextSlotMs = ((m_currentSlot + 1) * 80) / 3;
    qint64 currentTimeMs = nowMs % (2250 * 80 / 3);
    qint64 delayToNextSlot = nextSlotMs - currentTimeMs;

    if (delayToNextSlot < 1) delayToNextSlot = 1;

    emit logMessage(QString("UTC时隙初始化: 当前时隙=%1, 信道=%2, 距下次时隙边界=%3ms, UTC=%4")
                        .arg(m_currentSlot)
                        .arg(m_currentChannel == BBSC ? "BBSC" :
                                 m_currentChannel == ASC ? "ASC" :
                                 m_currentChannel == RAC ? "RAC" : "DC")
                        .arg(delayToNextSlot)
                        .arg(utcNow.toString("hh:mm:ss.zzz")), "cyan");

    // 立即触发第一次时隙更新，让界面显示当前时隙
    emit slotUpdated(m_currentSlot, m_currentChannel);

    // 发送时隙上报（大端格式，与船舶端匹配）
    if (m_reportEnabled) {
        sendSlotReport();
    }

    // 创建定时器
    m_slotTimer = new QTimer(this);
    m_slotTimer->setTimerType(Qt::PreciseTimer);
    connect(m_slotTimer, &QTimer::timeout, this, &SlotScheduler::onSlotTimeout);

    // 延时到下一个时隙边界后启动精确的时隙更新
    QTimer::singleShot(delayToNextSlot, this, [this]() {
        // 启动时隙更新定时器，每10ms触发一次用于检查
        m_slotTimer->start(10);
        // 立即更新一次时隙
        updateSlotByRealTime();
    });
}

// ========== 基于真实时间更新时隙 ==========
void SlotScheduler::updateSlotByRealTime()
{
    // 获取当前UTC时间
    QDateTime utcNow = QDateTime::currentDateTimeUtc();
    qint64 nowMs = utcNow.toMSecsSinceEpoch();

    // 计算当前应该在哪一个时隙
    qint64 expectedSlot = (nowMs * 3) / 80;
    expectedSlot = expectedSlot % 2250;

    // 如果时隙发生了变化
    if (expectedSlot != m_currentSlot) {
        // 计算需要前进的时隙数
        int slotsToAdvance = (expectedSlot - m_currentSlot + 2250) % 2250;

        // 限制一次最多前进2个时隙（避免跳变太大，同时保持业务连续性）
        if (slotsToAdvance > 2) {
            slotsToAdvance = 1;
        }

        // 前进时隙
        for (int i = 0; i < slotsToAdvance; i++) {
            m_currentSlot++;
            if (m_currentSlot >= 2250) {
                m_currentSlot = 0;
            }

            // 获取当前时隙的信道类型
            m_currentChannel = getCurrentChannel(m_currentSlot);

            // 发送时隙上报（大端格式）
            if (m_reportEnabled) {
                sendSlotReport();
            }

            // 发出时隙更新信号
            emit slotUpdated(m_currentSlot, m_currentChannel);
        }
    }
}

// ========== 时隙超时处理 ==========
void SlotScheduler::onSlotTimeout()
{
    static QElapsedTimer elapsedTimer;
    static bool firstRun = true;

    if (firstRun) {
        elapsedTimer.start();
        firstRun = false;
        return;
    }

    // 获取实际经过的时间
    qint64 elapsedMs = elapsedTimer.elapsed();

    // 重置计时器
    elapsedTimer.restart();

    // 基于真实时间更新时隙
    updateSlotByRealTime();
}

// ========== 构建时隙上报帧（大端格式，与船舶端匹配） ==========
QByteArray SlotScheduler::buildSlotReportFrame(int slot)
{
    QByteArray frame;

    // 帧头: 0x55 0xAA 0xFF (与船舶端一致)
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0xFF));

    // 时隙数: 2字节，大端格式（高字节在前，低字节在后）
    // 与船舶端解析方式匹配: (datagram[3] << 8) | datagram[4]
    frame.append(static_cast<char>((slot >> 8) & 0xFF)); // 高字节
    frame.append(static_cast<char>(slot & 0xFF));        // 低字节

    return frame;
}

// ========== 发送时隙上报 ==========
void SlotScheduler::sendSlotReport()
{
    if (!m_reportSocket) {
        return;
    }

    QByteArray frame = buildSlotReportFrame(m_currentSlot);

    qint64 sent = m_reportSocket->writeDatagram(
        frame,
        QHostAddress(m_reportIp),
        m_reportPort
        );

    if (sent == -1) {
        emit logMessage(QString("时隙上报UDP发送失败: %1").arg(m_reportSocket->errorString()), "red");
    }

    // 同时也通过信号发出，方便其他模块使用
    emit slotReportFrame(frame);
}

// ========== 获取当前时隙的信道类型 ==========
ChannelType SlotScheduler::getCurrentChannel(int slot)
{
    if (slot >= 0 && slot <= 89) return BBSC;
    if ((slot >= 90 && slot <= 179) ||
        (slot >= 810 && slot <= 899) ||
        (slot >= 1530 && slot <= 1619)) return ASC;
    if (slot >= 2070 && slot <= 2249) return RAC;
    return DC;
}

// ========== 注入数据包 ==========
void SlotScheduler::injectPacket(const Message& msg)
{
    emit packetReceived(msg);
}
