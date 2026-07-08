#include "Simulator.h"
#include "satellite_db.h"
#include <QRandomGenerator>
#include <QDebug>
#include <QThread>
#include <QDateTime>
#include <QMutex>

#define SAT_ID 99

Simulator::Simulator(QObject *parent)
    : QObject(parent),
    m_nextSessionId(1),
    m_pendingResourceAlloc(false),
    m_pendingShipId(0),
    m_pendingSessionId(0),
    m_pendingAscSlot(0),
    m_shipIp("127.0.0.1"),
    m_shipPort(9090),
    m_satId(SAT_ID),
    m_satSourceId(0x12345678)
{
    m_scheduler = new SlotScheduler(this);
    m_ship = new Ship(1, this);

    connect(m_scheduler, &SlotScheduler::slotUpdated,
            this, &Simulator::onSlotUpdate);
    connect(m_scheduler, &SlotScheduler::packetReceived,
            m_ship, &Ship::handleSatMessage);
    connect(m_scheduler, &SlotScheduler::logMessage,
            this, &Simulator::logAppend);
    connect(m_ship, &Ship::stateUpdate, this, &Simulator::stateUpdate);
    connect(m_ship, &Ship::logMessage, this, &Simulator::logAppend);

    m_udpSocket = new QUdpSocket(this);
    if (m_udpSocket->bind(QHostAddress("127.0.0.1"), 8080)) {
        qDebug() << "✅ 卫星上行UDP端口8080绑定成功";
    } else {
        qDebug() << "❌ 卫星上行UDP端口8080绑定失败：" << m_udpSocket->errorString();
    }
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &Simulator::readUdpData);

    // 修复：连接时隙同步帧信号到 UDP 发送（否则 VdesService 永远收不到时隙更新）
    connect(m_scheduler, &SlotScheduler::slotReportFrame,
            this, [this](const QByteArray &frame) {
        if (m_downlinkSocket) {
            m_downlinkSocket->writeDatagram(frame, QHostAddress(m_shipIp), m_shipPort);
        }
    });

    m_downlinkSocket = new QUdpSocket(this);
    qDebug() << "卫星端UDP目标:" << m_shipIp << ":" << m_shipPort;

    m_downlinkManager = new DownlinkManager(this);
    m_downlinkManager->setUdpSender([this](const QByteArray& data, uint8_t phyChannel,
                                           uint16_t lcStartSlot, uint16_t linkId) {
        sendUdpMessage(data, phyChannel, lcStartSlot, linkId);
    });
    m_downlinkManager->setCurrentSlotGetter([this]() {
        return getCurrentSlot();
    });
    connect(m_downlinkManager, &DownlinkManager::logMessage,
            this, &Simulator::logAppend);

    // 初始化文件传输处理器
    m_fileTransferHandler = new FileTransferHandler(this);
    m_fileTransferHandler->setSendCallback([this](int shipId, uint8_t sessionId,
                                                   const QByteArray &data,
                                                   uint32_t fragmentNum, bool isLast) {
        // 通过 DownlinkManager 发送文件分片
        int logicChannel = m_downlinkManager->allocateDcChannel();
        if (logicChannel >= 0) {
            // 构建分片数据（带文件传输标记）
            QByteArray fragmentData;
            fragmentData.append(reinterpret_cast<const char*>(&sessionId), 1);
            fragmentData.append(reinterpret_cast<const char*>(&fragmentNum), 4);
            fragmentData.append(data);
            m_downlinkManager->sendDownlinkMessage(shipId, fragmentData, sessionId, logicChannel);
        }
    });
    connect(m_fileTransferHandler, &FileTransferHandler::logMessage,
            this, &Simulator::logAppend);
    connect(m_fileTransferHandler, &FileTransferHandler::sendProgress,
            this, &Simulator::fileSendProgress);
    connect(m_fileTransferHandler, &FileTransferHandler::sendComplete,
            this, &Simulator::fileSendComplete);
    connect(m_fileTransferHandler, &FileTransferHandler::receiveProgress,
            this, &Simulator::fileReceiveProgress);
    connect(m_fileTransferHandler, &FileTransferHandler::receiveComplete,
            this, &Simulator::fileReceiveComplete);

    // 初始化线程池
    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(QThread::idealThreadCount());

    // 初始化卫星数据库（文件传输跟踪、日志持久化）
    if (SatelliteDB::instance().init("satellite_data.db")) {
        qDebug() << "卫星数据库初始化成功";
        SatelliteDB::instance().insertLog("卫星模拟器启动", "info");
    } else {
        qDebug() << "卫星数据库初始化失败";
    }

    // 配置线程池参数（最大并行度）
    m_threadPool->setMaxThreadCount(qMax(QThread::idealThreadCount(), 4));
    m_threadPool->setExpiryTimeout(30000);  // 30秒空闲线程回收

    m_resourceAllocTimer = new QTimer(this);
    m_resourceAllocTimer->setSingleShot(true);
    connect(m_resourceAllocTimer, &QTimer::timeout, this, &Simulator::onResourceAllocTimeout);

    m_scheduler->start();

    emit logAppend("=== 卫星已启动，支持上行寻址和下行寻址完整流程 ===", "blue");
    emit logAppend(QString("下行UDP目标: %1:%2").arg(m_shipIp).arg(m_shipPort), "red");
}

Simulator::~Simulator()
{
    if (m_scheduler) {
        m_scheduler->stop();
    }
    for (auto &session : m_uplinkSessions) {
        if (session.timeoutTimer) {
            session.timeoutTimer->stop();
            delete session.timeoutTimer;
        }
    }
    m_uplinkSessions.clear();
}

void Simulator::setSlotReportDestination(const QString& ip, quint16 port)
{
    if (m_scheduler) {
        m_scheduler->setSlotReportDestination(ip, port);
    }
}

void Simulator::onSlotUpdate(int slot, ChannelType channel)
{
    processSlot(slot, channel);
    QString chName;
    switch (channel) {
    case BBSC: chName = "BBSC"; break;
    case ASC:  chName = "ASC";  break;
    case RAC:  chName = "RAC";  break;
    case DC:   chName = "DC";   break;
    default:   chName = "IDLE";
    }
    emit slotUpdate(slot, chName);
}

void Simulator::processSlot(int slot, ChannelType channel)
{
    if (channel == BBSC) {
        sendSbbFragments(slot);
    } else if (channel == ASC) {
        sendAscMessages(slot);
    }
}

// ---------------------- SBB 片段构建 ----------------------
QByteArray Simulator::buildSbbFragment1()
{
    QByteArray data;
    data.append((char)1);
    data.append((char)m_satId);
    data.append((char)0x01);
    data.append((char)0x00);
    uint16_t sbbVersion = 0x0001;
    data.append((char)((sbbVersion >> 8) & 0xFF));
    data.append((char)(sbbVersion & 0xFF));
    QDateTime epoch2000(QDate(2000, 1, 1), QTime(0, 0, 0), Qt::UTC);
    qint64 secondsSince2000 = epoch2000.secsTo(QDateTime::currentDateTimeUtc());
    uint32_t startTime = static_cast<uint32_t>(secondsSince2000);
    data.append((char)((startTime >> 24) & 0xFF));
    data.append((char)((startTime >> 16) & 0xFF));
    data.append((char)((startTime >> 8) & 0xFF));
    data.append((char)(startTime & 0xFF));
    uint16_t validityDuration = 1440;
    data.append((char)((validityDuration >> 8) & 0xFF));
    data.append((char)(validityDuration & 0xFF));
    uint8_t serviceCapabilities = (1 << 4) | 0x00;
    data.append((char)serviceCapabilities);
    uint16_t backupFreq = 0x0000;
    data.append((char)((backupFreq >> 8) & 0xFF));
    data.append((char)(backupFreq & 0xFF));
    uint16_t maxUplinkSize = 64;
    data.append((char)((maxUplinkSize >> 8) & 0xFF));
    data.append((char)(maxUplinkSize & 0xFF));
    data.append((char)0x00);
    uint16_t totalSbbSize = 280;
    data.append((char)((totalSbbSize >> 8) & 0xFF));
    data.append((char)(totalSbbSize & 0xFF));
    for (int i = 0; i < 6; i++) data.append((char)0x00);
    emit logAppend(QString("SBB片段1: 卫星ID=%1 版本=%2 有效帧数=%3 最大上行=%4kB 总大小=%5字节")
                       .arg(m_satId).arg(sbbVersion).arg(validityDuration)
                       .arg(maxUplinkSize).arg(totalSbbSize), "black");
    return data;
}

QByteArray Simulator::buildSbbFragment2()
{
    QByteArray data;
    data.append((char)2);
    uint16_t dlFreqA = 2226;
    data.append((char)((dlFreqA >> 8) & 0xFF));
    data.append((char)(dlFreqA & 0xFF));
    uint16_t ulFreqA = 1226;
    data.append((char)((ulFreqA >> 8) & 0xFF));
    data.append((char)(ulFreqA & 0xFF));
    uint8_t bandwidthA = (1 << 4) | 1;
    data.append((char)bandwidthA);
    uint8_t slotValuesA[12] = {6, 6, 2, 6, 6, 6, 6, 2, 2, 12, 0, 0};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesA[i*2] << 4) | slotValuesA[i*2+1];
        data.append((char)packed);
    }
    uint8_t functionsA[12] = {0, 1, 4, 4, 4, 4, 4, 4, 2, 3, 5, 5};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsA[i*2] << 4) | functionsA[i*2+1];
        data.append((char)packed);
    }
    uint16_t dlFreqB = 2227;
    data.append((char)((dlFreqB >> 8) & 0xFF));
    data.append((char)(dlFreqB & 0xFF));
    uint16_t ulFreqB = 1227;
    data.append((char)((ulFreqB >> 8) & 0xFF));
    data.append((char)(ulFreqB & 0xFF));
    uint8_t bandwidthB = (1 << 4) | 1;
    data.append((char)bandwidthB);
    uint8_t slotValuesB[12] = {6, 6, 2, 6, 6, 6, 6, 2, 2, 12, 0, 0};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesB[i*2] << 4) | slotValuesB[i*2+1];
        data.append((char)packed);
    }
    uint8_t functionsB[12] = {0, 1, 4, 4, 4, 4, 4, 4, 2, 3, 5, 5};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsB[i*2] << 4) | functionsB[i*2+1];
        data.append((char)packed);
    }
    emit logAppend(QString("SBB片段2: 频段A DL=%1 UL=%2 | 频段B DL=%3 UL=%4")
                       .arg(dlFreqA).arg(ulFreqA).arg(dlFreqB).arg(ulFreqB), "black");
    return data;
}

QByteArray Simulator::buildSbbFragment3()
{
    QByteArray data;
    data.append((char)3);
    uint16_t dlFreqC = 2284;
    data.append((char)((dlFreqC >> 8) & 0xFF));
    data.append((char)(dlFreqC & 0xFF));
    uint16_t ulFreqC = 1225;
    data.append((char)((ulFreqC >> 8) & 0xFF));
    data.append((char)(ulFreqC & 0xFF));
    uint8_t bandwidthC = (2 << 4) | 1;
    data.append((char)bandwidthC);
    uint8_t slotValuesC[12] = {6, 6, 2, 6, 6, 6, 6, 2, 2, 12, 0, 0};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesC[i*2] << 4) | slotValuesC[i*2+1];
        data.append((char)packed);
    }
    uint8_t functionsC[12] = {5, 5, 4, 4, 4, 4, 4, 4, 2, 3, 5, 5};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsC[i*2] << 4) | functionsC[i*2+1];
        data.append((char)packed);
    }
    uint16_t dlFreqD = 1284;
    data.append((char)((dlFreqD >> 8) & 0xFF));
    data.append((char)(dlFreqD & 0xFF));
    uint16_t ulFreqD = 2225;
    data.append((char)((ulFreqD >> 8) & 0xFF));
    data.append((char)(ulFreqD & 0xFF));
    uint8_t bandwidthD = (2 << 4) | 1;
    data.append((char)bandwidthD);
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesC[i*2] << 4) | slotValuesC[i*2+1];
        data.append((char)packed);
    }
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsC[i*2] << 4) | functionsC[i*2+1];
        data.append((char)packed);
    }
    emit logAppend(QString("SBB片段3: 频段C DL=%1 UL=%2 | 频段D DL=%3 UL=%4")
                       .arg(dlFreqC).arg(ulFreqC).arg(dlFreqD).arg(ulFreqD), "black");
    return data;
}

QByteArray Simulator::buildSbbFragment4()
{
    QByteArray data;
    data.append((char)4);
    uint16_t dlFreqE = 2225;
    data.append((char)((dlFreqE >> 8) & 0xFF));
    data.append((char)(dlFreqE & 0xFF));
    uint16_t ulFreqE = 1224;
    data.append((char)((ulFreqE >> 8) & 0xFF));
    data.append((char)(ulFreqE & 0xFF));
    uint8_t bandwidthE = (3 << 4) | 1;
    data.append((char)bandwidthE);
    uint8_t slotValuesE[12] = {6, 6, 2, 6, 6, 6, 6, 2, 2, 12, 0, 0};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesE[i*2] << 4) | slotValuesE[i*2+1];
        data.append((char)packed);
    }
    uint8_t functionsE[12] = {5, 5, 4, 4, 4, 4, 4, 4, 2, 3, 5, 5};
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsE[i*2] << 4) | functionsE[i*2+1];
        data.append((char)packed);
    }
    uint16_t dlFreqF = 1225;
    data.append((char)((dlFreqF >> 8) & 0xFF));
    data.append((char)(dlFreqF & 0xFF));
    uint16_t ulFreqF = 2224;
    data.append((char)((ulFreqF >> 8) & 0xFF));
    data.append((char)(ulFreqF & 0xFF));
    uint8_t bandwidthF = (3 << 4) | 1;
    data.append((char)bandwidthF);
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (slotValuesE[i*2] << 4) | slotValuesE[i*2+1];
        data.append((char)packed);
    }
    for (int i = 0; i < 6; i++) {
        uint8_t packed = (functionsE[i*2] << 4) | functionsE[i*2+1];
        data.append((char)packed);
    }
    emit logAppend(QString("SBB片段4: 频段E DL=%1 UL=%2 | 频段F DL=%3 UL=%4")
                       .arg(dlFreqE).arg(ulFreqE).arg(dlFreqF).arg(ulFreqF), "black");
    return data;
}

QByteArray Simulator::buildSbbFragment5()
{
    QByteArray data;
    data.append((char)5);
    for (int i = 0; i < 32; i++) data.append((char)((m_satId + i) & 0xFF));
    emit logAppend("SBB片段5: 数字签名部分1 (32字节)", "black");
    return data;
}

QByteArray Simulator::buildSbbFragment6()
{
    QByteArray data;
    data.append((char)6);
    for (int i = 0; i < 32; i++) data.append((char)((m_satId + 32 + i) & 0xFF));
    emit logAppend("SBB片段6: 数字签名部分2 (32字节)", "black");
    return data;
}

void Simulator::sendSbbFragments(int slot)
{
    if (slot >= 0 && slot <= 5) {
        QByteArray sbbData;
        switch (slot) {
        case 0: sbbData = buildSbbFragment1(); break;
        case 1: sbbData = buildSbbFragment2(); break;
        case 2: sbbData = buildSbbFragment3(); break;
        case 3: sbbData = buildSbbFragment4(); break;
        case 4: sbbData = buildSbbFragment5(); break;
        case 5: sbbData = buildSbbFragment6(); break;
        default: return;
        }
        uint8_t phyChannel = 0x0A;
        uint16_t linkId = 20;
        sendUdpMessage(sbbData, phyChannel, m_scheduler->getCurrentSlot() + 2, linkId);
        emit logAppend(QString("BBSC:%1 → SBB%2 (UDP发送, 大小=%3字节)")
                           .arg(slot).arg(slot+1).arg(sbbData.size()), "darkgreen");
        qDebug() << "SBB" << (slot+1) << "UDP发送, 大小=" << sbbData.size();
    }
}

// ---------------------- ASC 消息 ----------------------
void Simulator::sendAscMessages(int slot)
{
    if (slot == 90 || slot == 810 || slot == 1530) {
        sendMacFrame();
    }
    if (slot == 100 || slot == 820 || slot == 1540) {
        sendPaging();
    }
    if (slot == 120 || slot == 840 || slot == 1560) {
        sendDownlinkSms();
    }
    if (slot == 150 || slot == 870 || slot == 1590) {
        qDebug() << "ASC时隙" << slot << "触发广播资源分配";
        sendBroadcastResourceAllocation();
        QTimer::singleShot(2000, this, [this]() {
            QByteArray testMessage = "这是广播消息测试，你好啊666";
            qDebug() << "发送广播消息，内容:" << testMessage;
            sendBroadcastMessage(testMessage);
        });
    }
}

bool Simulator::isAscSlot(int slot) const
{
    return (slot >= 90 && slot <= 179) ||
           (slot >= 810 && slot <= 899) ||
           (slot >= 1530 && slot <= 1619);
}

void Simulator::sendMacFrame()
{
    QByteArray payload;
    payload.append((char)10);
    payload.append((char)0x00);
    payload.append((char)0x0B);
    payload.append((char)m_satId);
    payload.append((char)0x01);
    payload.append((char)0x00);
    payload.append((char)0x00);
    payload.append((char)12);
    payload.append((char)3);
    payload.append((char)0x00);
    payload.append((char)10);
    payload.append((char)0x00);
    payload.append((char)0x01);
    uint8_t phyChannel = 0x0A;
    sendUdpMessage(payload, phyChannel, m_scheduler->getCurrentSlot() + 2, 20);
    emit logAppend("ASC → MAC帧(10) (UDP发送)", "purple");
}

void Simulator::sendPaging()
{
    QByteArray payload;
    payload.append((char)11);
    payload.append((char)0x00);
    payload.append((char)0x04);
    uint32_t shipMmsi = 200000002;
    payload.append((char)((shipMmsi >> 24) & 0xFF));
    payload.append((char)((shipMmsi >> 16) & 0xFF));
    payload.append((char)((shipMmsi >> 8) & 0xFF));
    payload.append((char)(shipMmsi & 0xFF));
    uint8_t phyChannel = 0x0A;
    sendUdpMessage(payload, phyChannel, m_scheduler->getCurrentSlot() + 2, 20);
    emit logAppend("ASC → 寻呼(11) (UDP发送)", "blue");
}

void Simulator::sendDownlinkSms()
{
    bool withAck = QRandomGenerator::global()->bounded(2) == 0;
    int type = withAck ? 14 : 16;
    QByteArray payload;
    payload.append((char)m_satId);
    uint32_t srcId = m_satSourceId;
    payload.append((char)((srcId >> 24) & 0xFF));
    payload.append((char)((srcId >> 16) & 0xFF));
    payload.append((char)((srcId >> 8) & 0xFF));
    payload.append((char)(srcId & 0xFF));
    uint32_t destId = 200000002;
    payload.append((char)((destId >> 24) & 0xFF));
    payload.append((char)((destId >> 16) & 0xFF));
    payload.append((char)((destId >> 8) & 0xFF));
    payload.append((char)(destId & 0xFF));
    QString msgText = QString("来自卫星的测试消息 (类型%1)").arg(type);
    QByteArray msgData = msgText.toUtf8();
    payload.append(msgData);
    uint16_t payloadSize = 1 + 4 + 4 + msgData.size();
    QByteArray message;
    message.append((char)type);
    message.append((char)((payloadSize >> 8) & 0xFF));
    message.append((char)(payloadSize & 0xFF));
    message.append(payload);
    uint8_t phyChannel = 0x0A;
    sendUdpMessage(message, phyChannel, m_scheduler->getCurrentSlot() + 2, 20);
    QString typeStr = (type == 14) ? "带ACK" : "无ACK";
    emit logAppend(QString("ASC → 下行短消息(%1) 类型%2 (UDP发送, 大小=%3字节)")
                       .arg(typeStr).arg(type).arg(message.size()), "darkblue");
    // 记录到界面表格
    recordMessage("下行短消息(" + typeStr + ")",
                  QString("目的船舶%1 数据=%2").arg(destId).arg(QString(msgData.toHex(' '))),
                  QString("原始消息: %1").arg(msgText));
}

// ---------------------- 下行消息发送（供界面调用） ----------------------
void Simulator::sendDownlinkToShip(int shipId, const QByteArray& data)
{
    uint8_t sessionId = m_nextSessionId++;
    int logicChannel = m_downlinkManager->allocateDcChannel();
    if (logicChannel < 0) {
        emit logAppend("下行寻址失败：无可用DC信道", "red");
        return;
    }
    m_downlinkManager->sendDownlinkMessage(shipId, data, sessionId, logicChannel);
    // 记录到界面表格
    QString summary = QString("发给船舶%1，长度%2字节").arg(shipId).arg(data.size());
    QString detail = QString("内容: %1").arg(QString::fromUtf8(data));
    recordMessage("下行寻址", summary, detail);
}

void Simulator::sendDownlinkShortAck(int shipId, uint8_t data)
{
    QByteArray payload;
    payload.append((char)m_satId);
    uint32_t srcId = m_satSourceId;
    payload.append((char)((srcId >> 24) & 0xFF));
    payload.append((char)((srcId >> 16) & 0xFF));
    payload.append((char)((srcId >> 8) & 0xFF));
    payload.append((char)(srcId & 0xFF));
    payload.append((char)((shipId >> 24) & 0xFF));
    payload.append((char)((shipId >> 16) & 0xFF));
    payload.append((char)((shipId >> 8) & 0xFF));
    payload.append((char)(shipId & 0xFF));
    payload.append((char)data);
    uint16_t payloadSize = 1 + 4 + 4 + 1;
    QByteArray msg;
    msg.append((char)14);
    msg.append((char)((payloadSize >> 8) & 0xFF));
    msg.append((char)(payloadSize & 0xFF));
    msg.append(payload);
    int sendSlot = m_scheduler->getCurrentSlot() + 2;
    sendUdpMessage(msg, 0x0A, sendSlot, 20);
    emit logAppend(QString("下行短消息(ACK) 已发送到船舶%1，数据=%2").arg(shipId).arg(data), "green");
    recordMessage("下行短消息(ACK)",
                  QString("目的船舶%1 数据=%2").arg(shipId).arg(data),
                  QString("原始数据: 0x%1").arg(data, 2, 16, QChar('0')));
}

void Simulator::sendDownlinkShortNoAck(int shipId, uint8_t data)
{
    QByteArray payload;
    payload.append((char)m_satId);
    uint32_t srcId = m_satSourceId;
    payload.append((char)((srcId >> 24) & 0xFF));
    payload.append((char)((srcId >> 16) & 0xFF));
    payload.append((char)((srcId >> 8) & 0xFF));
    payload.append((char)(srcId & 0xFF));
    payload.append((char)((shipId >> 24) & 0xFF));
    payload.append((char)((shipId >> 16) & 0xFF));
    payload.append((char)((shipId >> 8) & 0xFF));
    payload.append((char)(shipId & 0xFF));
    payload.append((char)data);
    uint16_t payloadSize = 1 + 4 + 4 + 1;
    QByteArray msg;
    msg.append((char)16);
    msg.append((char)((payloadSize >> 8) & 0xFF));
    msg.append((char)(payloadSize & 0xFF));
    msg.append(payload);
    int sendSlot = m_scheduler->getCurrentSlot() + 2;
    sendUdpMessage(msg, 0x0A, sendSlot, 20);
    emit logAppend(QString("下行短消息(无ACK) 已发送到船舶%1，数据=%2").arg(shipId).arg(data), "green");
    recordMessage("下行短消息(无ACK)",
                  QString("目的船舶%1 数据=%2").arg(shipId).arg(data),
                  QString("原始数据: 0x%1").arg(data, 2, 16, QChar('0')));
}

// ---------------------- 广播消息 ----------------------
void Simulator::sendBroadcastResourceAllocation()
{
    qDebug() << "=== sendBroadcastResourceAllocation 被调用 ===";
    QByteArray payload;
    payload.append((char)12);
    payload.append((char)0x00);
    payload.append((char)0x20);
    for (int i = 0; i < 4; i++) {
        payload.append((char)0x00);
        payload.append((char)0x00);
        payload.append((char)0x00);
        payload.append((char)0x00);
        payload.append((char)i);
        payload.append((char)(20 + i));
        static uint8_t sessionId = 1;
        payload.append((char)sessionId);
        sessionId++;
        payload.append((char)100);
    }
    uint8_t phyChannel = 0x0A;
    int currentSlot = m_scheduler->getCurrentSlot();
    int sendSlot = currentSlot + 2;
    sendUdpMessage(payload, phyChannel, sendSlot, 20);
    emit logAppend(QString("【广播】发送广播资源分配 时隙=%1 大小=%2字节 ")
                       .arg(sendSlot).arg(payload.size()), "magenta");
    recordMessage("广播资源分配", "广播资源分配消息", "船舶ID全0，广播资源分配");
}

void Simulator::sendBroadcastMessage(const QByteArray &data)
{
    const int MAX_PAYLOAD_SIZE = 100;
    int dataFragments = (data.size() + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    for (int fragmentNum = 0; fragmentNum < dataFragments; fragmentNum++) {
        int startOffset = fragmentNum * MAX_PAYLOAD_SIZE;
        int dataSize = qMin(MAX_PAYLOAD_SIZE, data.size() - startOffset);
        QByteArray fragmentData = data.mid(startOffset, dataSize);
        uint8_t type;
        if (dataFragments == 1) {
            type = 32;  // 单分片直接发送结束片段，让状态机完成
        } else if (fragmentNum == 0) {
            type = 30;  // 起始片段
        } else if (fragmentNum == dataFragments - 1) {
            type = 32;  // 结束片段
        } else {
            type = 31;  // 中间片段
        }
        QByteArray packet;
        packet.append((char)type);
        uint16_t fieldSize = 12;
        packet.append((char)((fieldSize >> 8) & 0xFF));
        packet.append((char)(fieldSize & 0xFF));
        uint32_t srcRadioId = m_satSourceId;
        packet.append((char)((srcRadioId >> 24) & 0xFF));
        packet.append((char)((srcRadioId >> 16) & 0xFF));
        packet.append((char)((srcRadioId >> 8) & 0xFF));
        packet.append((char)(srcRadioId & 0xFF));
        packet.append((char)m_satId);
        static uint8_t sessionId = 100;
        packet.append((char)sessionId);
        packet.append((char)0x00);
        packet.append((char)0x00);
        packet.append((char)0x00);
        packet.append((char)0x00);
        uint16_t fragNum = fragmentNum;
        packet.append((char)((fragNum >> 8) & 0xFF));
        packet.append((char)(fragNum & 0xFF));
        packet.append(fragmentData);
        int currentSlot = m_scheduler->getCurrentSlot();
        int sendSlot = currentSlot + 2 + fragmentNum;
        uint8_t phyChannel = 0x0A;
        sendUdpMessage(packet, phyChannel, sendSlot, 20);
        qDebug() << "分片" << fragmentNum + 1 << "/" << dataFragments
                 << "type=" << type << "dataSize=" << dataSize;
        qDebug() << "发送成功";
    }
    // 记录广播消息发送
    recordMessage("广播", QString("广播消息，总分片数=%1").arg(dataFragments),
                  QString("内容: %1").arg(QString::fromUtf8(data)));
}

// ---------------------- UDP 发送与接收 ----------------------
void Simulator::sendUdpMessage(const QByteArray& rawPayload, uint8_t phyChannel,
                               uint16_t lcStartSlot, uint16_t linkId)
{
    Q_UNUSED(lcStartSlot);
    QByteArray frame;
    frame.append(static_cast<char>(0xEB));
    frame.append(static_cast<char>(0x90));
    frame.append(static_cast<char>(0xF9));
    frame.append(static_cast<char>(0xA5));
    static uint16_t seq = 0;
    frame.append(static_cast<char>((seq >> 8) & 0xFF));
    frame.append(static_cast<char>(seq & 0xFF));
    seq++;
    uint16_t msgLen = 26 + static_cast<uint16_t>(rawPayload.size());
    frame.append(static_cast<char>((msgLen >> 8) & 0xFF));
    frame.append(static_cast<char>(msgLen & 0xFF));
    QDateTime utcNow = QDateTime::currentDateTimeUtc();
    uint32_t nowSec = static_cast<uint32_t>(utcNow.toSecsSinceEpoch());
    frame.append(static_cast<char>((nowSec >> 24) & 0xFF));
    frame.append(static_cast<char>((nowSec >> 16) & 0xFF));
    frame.append(static_cast<char>((nowSec >> 8) & 0xFF));
    frame.append(static_cast<char>(nowSec & 0xFF));
    uint32_t nowUsec = static_cast<uint32_t>(utcNow.time().msec() * 1000);
    frame.append(static_cast<char>((nowUsec >> 24) & 0xFF));
    frame.append(static_cast<char>((nowUsec >> 16) & 0xFF));
    frame.append(static_cast<char>((nowUsec >> 8) & 0xFF));
    frame.append(static_cast<char>(nowUsec & 0xFF));
    for (int i = 0; i < 12; ++i) frame.append(static_cast<char>(0x00));
    frame.append(static_cast<char>(100));          // 功率 (1B)
    frame.append(static_cast<char>(100));          // 信噪比 (1B)
    frame.append(static_cast<char>(100));          // 上行CQI (1B)
    frame.append(static_cast<char>(phyChannel));   // 通道号 (1B)
    frame.append(static_cast<char>((linkId >> 8) & 0xFF));
    frame.append(static_cast<char>(linkId & 0xFF));
    frame.append(rawPayload);
    if (m_downlinkSocket) {
        m_downlinkSocket->writeDatagram(frame, QHostAddress("127.0.0.1"), 9090);
        emit logAppend(QString("UDP发送(板卡→上位机): 类型=0xA5 通道=0x%1 LinkID=%2 载荷大小=%3")
                           .arg(phyChannel, 2, 16, QChar('0')).arg(linkId).arg(rawPayload.size()), "gray");
    }
}

int Simulator::allocateDcChannel()
{
    for (int i = 0; i < 6; ++i) {
        if (!m_occupiedDcChannels.contains(i)) {
            m_occupiedDcChannels.insert(i);
            emit logAppend(QString("分配DC信道: %1").arg(i), "green");
            return i;
        }
    }
    int randomChannel = QRandomGenerator::global()->bounded(6);
    emit logAppend(QString("所有DC信道被占用，随机选择 %1").arg(randomChannel), "blue");
    return randomChannel;
}

void Simulator::releaseDcChannel(int logicChannel)
{
    if (m_occupiedDcChannels.contains(logicChannel)) {
        m_occupiedDcChannels.remove(logicChannel);
        emit logAppend(QString("释放DC信道 %1").arg(logicChannel), "gray");
    }
}

// ---------------------- 上行消息处理 ----------------------
void Simulator::handleUplinkShortAck(const QByteArray &rawMsg)
{
    if (rawMsg.size() < 10) {
        emit logAppend(QString("上行短消息(33): 消息长度不足，期望10字节实际%1字节").arg(rawMsg.size()), "red");
        return;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t*>(rawMsg.constData());
    uint32_t srcShipId = (static_cast<uint32_t>(data[1]) << 24) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 8)  |
                         static_cast<uint32_t>(data[4]);
    uint32_t destId = (static_cast<uint32_t>(data[5]) << 24) |
                      (static_cast<uint32_t>(data[6]) << 16) |
                      (static_cast<uint32_t>(data[7]) << 8)  |
                      static_cast<uint32_t>(data[8]);
    uint8_t msgData = data[9];
    emit logAppend(QString("【上行短消息-带ACK】源船舶=%1 目的船舶=%2 数据=%3")
                       .arg(srcShipId).arg(destId).arg(msgData), "green");
    emit uplinkMessageReceived("上行短消息(ACK)",
                               QString("源船舶%1 → 目的船舶%2").arg(srcShipId).arg(destId),
                               QString("数据=0x%1").arg(msgData, 2, 16, QChar('0')));
    // 发送ACK回复
    QByteArray ackData;
    ackData.append((char)34);
    uint16_t payloadSize = 31;
    ackData.append((char)((payloadSize >> 8) & 0xFF));
    ackData.append((char)(payloadSize & 0xFF));
    ackData.append((char)m_satId);
    ackData.append((char)(srcShipId >> 24));
    ackData.append((char)(srcShipId >> 16));
    ackData.append((char)(srcShipId >> 8));
    ackData.append((char)srcShipId);
    ackData.append((char)0x00);
    for (int i = 0; i < 5; i++) {
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
    }
    uint8_t phyChannel = 0x0A;
    int sendSlot = m_scheduler->getCurrentSlot() + 2;
    sendUdpMessage(ackData, phyChannel, sendSlot, 20);
    emit logAppend(QString("ASC → 上行短消息ACK(34) 船舶=%1 (直接回复，无会话)").arg(srcShipId), "orange");
}

void Simulator::handleUplinkShortNoAck(const QByteArray &rawMsg)
{
    if (rawMsg.size() < 10) {
        emit logAppend(QString("上行短消息(23): 消息长度不足，期望10字节实际%1字节").arg(rawMsg.size()), "red");
        return;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t*>(rawMsg.constData());
    uint32_t srcShipId = (static_cast<uint32_t>(data[1]) << 24) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 8)  |
                         static_cast<uint32_t>(data[4]);
    uint32_t destId = (static_cast<uint32_t>(data[5]) << 24) |
                      (static_cast<uint32_t>(data[6]) << 16) |
                      (static_cast<uint32_t>(data[7]) << 8)  |
                      static_cast<uint32_t>(data[8]);
    uint8_t msgData = data[9];
    emit logAppend(QString("【上行短消息-无ACK有目的ID】源船舶=%1 目的=%2 数据=%3")
                       .arg(srcShipId).arg(destId).arg(msgData), "green");
    emit uplinkMessageReceived("上行短消息(无ACK有目的)",
                               QString("源船舶%1 → 目的船舶%2").arg(srcShipId).arg(destId),
                               QString("数据=0x%1").arg(msgData, 2, 16, QChar('0')));
}

void Simulator::handleUplinkShortNoAckNoDest(const QByteArray &rawMsg, uint8_t type)
{
    if (rawMsg.size() < 10) {
        emit logAppend(QString("上行短消息(%1): 消息长度不足，期望10字节实际%2字节").arg(type).arg(rawMsg.size()), "red");
        return;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t*>(rawMsg.constData());
    uint32_t srcShipId = (static_cast<uint32_t>(data[1]) << 24) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 8)  |
                         static_cast<uint32_t>(data[4]);
    QByteArray msgData = rawMsg.mid(5, 5);
    emit logAppend(QString("【上行短消息-无ACK无目的ID】类型=%1 源船舶=%2 数据(hex)=%3")
                       .arg(type).arg(srcShipId).arg(QString(msgData.toHex(' ')).toUpper()), "green");
    emit uplinkMessageReceived(QString("上行短消息(无ACK无目的) 类型%1").arg(type),
                               QString("源船舶%1").arg(srcShipId),
                               QString("数据(hex): %1").arg(QString(msgData.toHex(' ')).toUpper()));
}

void Simulator::handlePagingResponse(const QByteArray &rawMsg)
{
    if (rawMsg.size() < 7) {
        emit logAppend(QString("寻呼响应: 消息长度不足，期望7字节实际%1字节").arg(rawMsg.size()), "red");
        return;
    }
    const uint8_t *data = reinterpret_cast<const uint8_t*>(rawMsg.constData());
    uint32_t shipId = (static_cast<uint32_t>(data[1]) << 24) |
                      (static_cast<uint32_t>(data[2]) << 16) |
                      (static_cast<uint32_t>(data[3]) << 8) |
                      static_cast<uint32_t>(data[4]);
    uint8_t terminalCap = data[5];
    uint8_t downlinkCQI = data[6];
    emit logAppend(QString("【寻呼响应】收到船舶%1的寻呼响应，终端能力=0x%2，下行CQI=%3")
                       .arg(shipId).arg(terminalCap, 2, 16, QChar('0')).arg(downlinkCQI), "green");
    emit uplinkMessageReceived("寻呼应答",
                               QString("船舶%1 响应寻呼").arg(shipId),
                               QString("终端能力=0x%1 下行CQI=%2").arg(terminalCap, 2, 16, QChar('0')).arg(downlinkCQI));
}

void Simulator::sendUplinkAck(int shipId)
{
    if (m_uplinkSessions.contains(shipId)) {
        uint8_t sessionId = m_uplinkSessions[shipId].sessionId;
        sendUplinkAckWithMask(shipId, sessionId, QByteArray());
    }
}

void Simulator::sendUplinkAckWithMask(int shipId, uint8_t sessionId, const QByteArray &mask)
{
    Q_UNUSED(mask);
    Q_UNUSED(sessionId);
    uint8_t phyChannel = 0x0A;
    if (m_uplinkSessions.contains(shipId)) {
        phyChannel = m_uplinkSessions[shipId].allocatedPhyChannel;
    }
    QByteArray ackData;
    ackData.append((char)34);
    uint16_t payloadSize = 31;
    ackData.append((char)((payloadSize >> 8) & 0xFF));
    ackData.append((char)(payloadSize & 0xFF));
    ackData.append((char)m_satId);
    ackData.append((char)(shipId >> 24));
    ackData.append((char)(shipId >> 16));
    ackData.append((char)(shipId >> 8));
    ackData.append((char)shipId);
    ackData.append((char)0x00);
    for (int i = 0; i < 5; i++) {
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
        ackData.append((char)0x00);
    }
    sendUdpMessage(ackData, phyChannel, m_scheduler->getCurrentSlot() + 2, 20);
    emit logAppend(QString("ASC → 上行短消息ACK(34) 船舶=%1 (符合规范6组格式)").arg(shipId), "orange");
}

void Simulator::sendUplinkAddressedAck(int shipId, uint8_t sessionId, uint8_t resourceRealloc, const QByteArray &mask)
{
    QByteArray ackData;
    ackData.append((char)13);
    ackData.append((char)m_satId);
    ackData.append((char)((shipId >> 24) & 0xFF));
    ackData.append((char)((shipId >> 16) & 0xFF));
    ackData.append((char)((shipId >> 8) & 0xFF));
    ackData.append((char)(shipId & 0xFF));
    ackData.append((char)sessionId);
    ackData.append((char)resourceRealloc);
    ackData.append((char)100);
    ackData.append((char)0);
    if (mask.isEmpty()) {
        ackData.append(QByteArray(25, 0));
    } else {
        QByteArray fullMask = mask;
        if (fullMask.size() < 25) fullMask.append(25 - fullMask.size(), 0);
        ackData.append(fullMask);
    }
    uint8_t phyChannel = 0x0A;
    if (m_uplinkSessions.contains(shipId)) {
        phyChannel = m_uplinkSessions[shipId].allocatedPhyChannel;
    }
    int sendSlot = m_scheduler->getCurrentSlot() + 2;
    sendUdpMessage(ackData, phyChannel, sendSlot, 20);
    emit logAppend(QString("ASC → 上行寻址ACK(13) 船舶=%1 会话=%2 resourceRealloc=%3 (UDP发送)")
                       .arg(shipId).arg(sessionId).arg(resourceRealloc), "orange");
}

void Simulator::sendResourceAlloc(int shipId, uint8_t sessionId, int currentAscSlot)
{
    int logicChannel = allocateDcChannel();
    uint16_t linkId = 20 + sessionId;
    static const uint8_t phyChannelMap[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    uint8_t phyChannel = phyChannelMap[logicChannel];
    QByteArray payload;
    payload.append((char)12);
    payload.append((char)0x00);
    payload.append((char)0x08);
    payload.append((char)(shipId >> 24));
    payload.append((char)(shipId >> 16));
    payload.append((char)(shipId >> 8));
    payload.append((char)shipId);
    payload.append((char)logicChannel);
    payload.append((char)(linkId & 0xFF));
    payload.append((char)sessionId);
    payload.append((char)100);
    sendUdpMessage(payload, phyChannel, currentAscSlot + 2, linkId);
    emit logAppend(QString("ASC → 资源分配(12) 船舶=%1 会话=%2 逻辑信道=%3 (UDP发送)")
                       .arg(shipId).arg(sessionId).arg(logicChannel), "red");
    // 更新上行会话记录的信道信息
    if (m_uplinkSessions.contains(shipId)) {
        m_uplinkSessions[shipId].allocatedLogicChannel = logicChannel;
        m_uplinkSessions[shipId].allocatedPhyChannel = phyChannel;
    }
}

void Simulator::readUdpData()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(d.data(), d.size());

        const int HEADER_SIZE = 17;
        if (d.size() < HEADER_SIZE + 1) continue;
        const uint8_t *data = reinterpret_cast<const uint8_t*>(d.constData());
        if (data[0] != 0xEB || data[1] != 0xA0 || data[2] != 0xF9) continue;
        uint16_t msgLen = (static_cast<uint16_t>(data[5]) | (static_cast<uint16_t>(data[6]) << 8));
        int rawLen = msgLen - 10;
        if (rawLen <= 0) continue;
        int payloadStart = HEADER_SIZE;
        if (d.size() < payloadStart + rawLen + 1) continue;
        uint8_t calcChecksum = 0;
        for (int i = 3; i < payloadStart + rawLen; ++i)
            calcChecksum += static_cast<uint8_t>(d[i]);
        if (calcChecksum != static_cast<uint8_t>(d[payloadStart + rawLen]))
            continue;
        QByteArray rawMsg = d.mid(payloadStart, rawLen);
        uint8_t type = static_cast<uint8_t>(rawMsg[0]);
        QString hexStr;
        for (int i = 0; i < rawMsg.size(); ++i) {
            hexStr += QString("%1 ").arg((uint8_t)rawMsg[i], 2, 16, QChar('0'));
        }
        qDebug() << "【卫星端收到上行消息】type =" << type << ", len =" << rawMsg.size() << ", hex =" << hexStr;
        emit logAppend(QString("收到的消息类型：type=%1 len=%2 hex=%3")
                           .arg(type).arg(rawMsg.size()).arg(hexStr), "darkGreen");

        if (type == 29) {
            if (m_downlinkManager) m_downlinkManager->handleDownlinkAck(rawMsg);
        } else if (type == 33) {
            handleUplinkShortAck(rawMsg);
        } else if (type == 23) {
            handleUplinkShortNoAck(rawMsg);
        } else if (type >= 24 && type <= 28) {
            handleUplinkShortNoAckNoDest(rawMsg, type);
        } else if (type == 20) {
            uint32_t shipId = 0;
            if (rawMsg.size() >= 5) {
                shipId = (static_cast<uint32_t>(static_cast<uint8_t>(rawMsg[1])) << 24) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(rawMsg[2])) << 16) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(rawMsg[3])) << 8)  |
                         static_cast<uint32_t>(static_cast<uint8_t>(rawMsg[4]));
            } else {
                shipId = 200000002;
            }
            uint8_t sessionId = m_nextSessionId++;
            UplinkSession session;
            session.shipId = shipId;
            session.sessionId = sessionId;
            session.expectedFragmentIndex = 0;
            session.active = true;
            session.allocatedLogicChannel = -1;
            session.allocatedPhyChannel = 0x0A;
            session.timeoutTimer = new QTimer(this);
            session.timeoutTimer->setSingleShot(true);
            connect(session.timeoutTimer, &QTimer::timeout, this, [this, shipId]() { onUplinkTimeout(shipId); });
            m_uplinkSessions[shipId] = session;
            m_pendingResourceAlloc = true;
            m_pendingShipId = shipId;
            m_pendingSessionId = sessionId;
            int currentSlot = m_scheduler->getCurrentSlot();
            int nextAscSlot = -1;
            for (int offset = 1; offset <= 2250; ++offset) {
                int slot = (currentSlot + offset) % 2250;
                if (isAscSlot(slot)) {
                    nextAscSlot = slot;
                    break;
                }
            }
            if (nextAscSlot == -1) nextAscSlot = 90;
            int slotsToWait = (nextAscSlot - currentSlot + 2250) % 2250;
            int delayMs = static_cast<int>(slotsToWait * 80.0 / 3.0);
            if (delayMs < 1) delayMs = 1;
            m_pendingAscSlot = nextAscSlot;
            m_resourceAllocTimer->start(delayMs);
            qDebug() << "资源分配定时器启动，将在" << delayMs << "ms后（时隙" << nextAscSlot << "）发送";
            emit uplinkMessageReceived("上行资源请求",
                                       QString("船舶%1 请求资源").arg(shipId),
                                       QString("会话ID=%1").arg(sessionId));
            emit logAppend(QString("收到资源请求(20) 船舶=%1 分配会话ID=%2，将在时隙%3发送资源分配")
                               .arg(shipId).arg(sessionId).arg(nextAscSlot), "blue");
        } else if (type == 30 || type == 31 || type == 32) {
            if (rawMsg.size() < 15) continue;
            uint32_t shipId = ((uint32_t)(uint8_t)rawMsg[3] << 24) |
                              ((uint32_t)(uint8_t)rawMsg[4] << 16) |
                              ((uint32_t)(uint8_t)rawMsg[5] << 8)  |
                              (uint8_t)rawMsg[6];
            uint8_t sessionId = (uint8_t)rawMsg[8];
            uint16_t fragNum = ((uint8_t)rawMsg[13] << 8) | (uint8_t)rawMsg[14];
            QByteArray fragData = rawMsg.mid(15);

            // 检测文件传输标记：前2字节为 0xFF 0xFE
            bool isFileChunk = (fragData.size() >= 2 &&
                                (uint8_t)fragData[0] == 0xFF &&
                                (uint8_t)fragData[1] == 0xFE);
            if (isFileChunk && m_fileTransferHandler) {
                // 文件传输：去掉标记头，剩余为文件数据
                QByteArray fileData = fragData.mid(2);
                m_threadPool->start([this, shipId, sessionId, fragNum, fileData, type]() {
                    handleUplinkFileChunk(shipId, sessionId, fragNum, fileData, (type == 32));
                });
                continue;
            }

            // 原有上行寻址消息处理
            if (!m_uplinkSessions.contains(shipId)) continue;
            UplinkSession &session = m_uplinkSessions[shipId];
            if (session.sessionId != sessionId) continue;
            session.timeoutTimer->start(10000);
            if (type == 30) {
                session.receivedData.clear();
                session.expectedFragmentIndex = 1;
                session.receivedData.append(fragData);
            } else if (type == 31 || type == 32) {
                if (fragNum != session.expectedFragmentIndex) {
                    onUplinkTimeout(shipId);
                    continue;
                }
                session.receivedData.append(fragData);
                session.expectedFragmentIndex++;
                if (type == 32) {
                    QByteArray mask(25, 0);
                    sendUplinkAddressedAck(shipId, sessionId, 0, mask);
                    if (session.allocatedLogicChannel >= 0) releaseDcChannel(session.allocatedLogicChannel);
                    session.timeoutTimer->stop();
                    m_uplinkSessions.remove(shipId);
                    // 记录收到的完整上行寻址消息
                    emit uplinkMessageReceived("上行寻址",
                                               QString("船舶%1 会话%2").arg(shipId).arg(sessionId),
                                               QString("完整数据长度=%1字节 内容预览: %2")
                                                   .arg(session.receivedData.size())
                                                   .arg(QString::fromUtf8(session.receivedData.left(100))));
                }
            }
        } else if (type == 21) {
            handlePagingResponse(rawMsg);
        }
    }
}

void Simulator::onResourceAllocTimeout()
{
    if (m_pendingResourceAlloc) {
        int currentSlot = m_scheduler->getCurrentSlot();
        int diff = (currentSlot - m_pendingAscSlot + 2250) % 2250;
        if (diff > 5 && diff < 2245) {
            qWarning() << "资源分配定时器触发但时隙偏差过大，放弃发送";
            m_pendingResourceAlloc = false;
            return;
        }
        sendResourceAlloc(m_pendingShipId, m_pendingSessionId, m_pendingAscSlot);
        m_pendingResourceAlloc = false;
    }
}

void Simulator::onUplinkTimeout(int shipId)
{
    if (m_uplinkSessions.contains(shipId)) {
        if (m_uplinkSessions[shipId].allocatedLogicChannel >= 0) {
            releaseDcChannel(m_uplinkSessions[shipId].allocatedLogicChannel);
        }
        emit logAppend(QString("船舶%1的上行接收超时，会话终止").arg(shipId), "red");
        m_uplinkSessions.remove(shipId);
    }
}

void Simulator::recordMessage(const QString& type, const QString& summary, const QString& detail)
{
    emit messageRecorded(type, summary, detail);
}

// ========== 文件传输 ==========
QString Simulator::sendFileToShip(int shipId, const QString &filePath)
{
    return m_fileTransferHandler->startSendFile(shipId, filePath);
}

void Simulator::handleUplinkFileChunk(int shipId, uint8_t sessionId, uint32_t fragmentNum,
                                       const QByteArray &data, bool isLast)
{
    bool isComplete = false;
    QString errorMsg;
    if (m_fileTransferHandler->receiveChunk(shipId, sessionId, fragmentNum, data, isLast,
                                              isComplete, errorMsg)) {
        if (isComplete) {
            emit logAppend(QString("上行文件接收完成: 船舶%1 会话%2").arg(shipId).arg(sessionId), "green");
        }
    } else {
        emit logAppend(QString("上行文件分片错误: %1").arg(errorMsg), "red");
    }
}