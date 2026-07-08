#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QMap>
#include <QSet>
#include <QThreadPool>
#include "SlotScheduler.h"
#include "Ship.h"
#include "DownlinkManager.h"
#include "filetransferhandler.h"

class Simulator : public QObject
{
    Q_OBJECT
public:
    explicit Simulator(QObject *parent = nullptr);
    ~Simulator();

    // 公共接口
    DownlinkManager* getDownlinkManager() const { return m_downlinkManager; }
    FileTransferHandler* getFileTransferHandler() const { return m_fileTransferHandler; }
    QThreadPool* getThreadPool() const { return m_threadPool; }
    void sendUplinkAck(int shipId);
    void sendDownlinkAddressedMessage(int shipId, const QByteArray &data,
                                      uint8_t sessionId, int logicChannel);
    int getCurrentSlot() const { return m_scheduler ? m_scheduler->getCurrentSlot() : 0; }
    void setSlotReportDestination(const QString& ip, quint16 port);

    void sendBroadcastMessage(const QByteArray &data);
    void sendBroadcastResourceAllocation();
    void sendDownlinkToShip(int shipId, const QByteArray& data);
    void sendDownlinkShortAck(int shipId, uint8_t data);
    void sendDownlinkShortNoAck(int shipId, uint8_t data);
    void recordMessage(const QString& type, const QString& summary, const QString& detail);

    // 文件传输接口
    QString sendFileToShip(int shipId, const QString &filePath);
    void handleUplinkFileChunk(int shipId, uint8_t sessionId, uint32_t fragmentNum,
                               const QByteArray &data, bool isLast);

signals:
    void logAppend(QString text, QString color = "black");
    void slotUpdate(int slot, QString channel);
    void stateUpdate(QString state);
    void messageRecorded(const QString& type, const QString& summary, const QString& detail);
    void uplinkMessageReceived(const QString& type, const QString& summary, const QString& detail);
    // 文件传输相关信号
    void fileSendProgress(const QString &fileName, int shipId, int percent);
    void fileSendComplete(const QString &fileName, int shipId, bool success);
    void fileReceiveProgress(const QString &transferId, int percent);
    void fileReceiveComplete(const QString &filePath, const QString &md5);

private slots:
    void onSlotUpdate(int slot, ChannelType channel);
    void readUdpData();
    void onUplinkTimeout(int shipId);
    void onResourceAllocTimeout();

private:
    void processSlot(int slot, ChannelType channel);
    void sendSbbFragments(int slot);
    void handlePagingResponse(const QByteArray &rawMsg);
    QByteArray buildSbbFragment1();
    QByteArray buildSbbFragment2();
    QByteArray buildSbbFragment3();
    QByteArray buildSbbFragment4();
    QByteArray buildSbbFragment5();
    QByteArray buildSbbFragment6();
    void sendAscMessages(int slot);
    void sendMacFrame();
    void handleUplinkShortAck(const QByteArray &rawMsg);
    void handleUplinkShortNoAck(const QByteArray &rawMsg);
    void handleUplinkShortNoAckNoDest(const QByteArray &rawMsg, uint8_t type);
    void sendPaging();
    void sendResourceAlloc(int shipId, uint8_t sessionId, int currentAscSlot);
    void sendDownlinkSms();
    void sendUplinkAckWithMask(int shipId, uint8_t sessionId, const QByteArray &mask);
    void sendUplinkAddressedAck(int shipId, uint8_t sessionId, uint8_t resourceRealloc, const QByteArray &mask);
    void sendUdpMessage(const QByteArray& rawPayload, uint8_t phyChannel,
                        uint16_t lcStartSlot, uint16_t linkId);
    bool isAscSlot(int slot) const;
    void releaseDcChannel(int logicChannel);
    int allocateDcChannel();

    struct UplinkSession {
        int shipId;
        uint8_t sessionId;
        int expectedFragmentIndex;
        QByteArray receivedData;
        QTimer *timeoutTimer;
        bool active;
        int allocatedLogicChannel;
        uint8_t allocatedPhyChannel;
    };
    QMap<int, UplinkSession> m_uplinkSessions;
    uint8_t m_nextSessionId;

    bool m_pendingResourceAlloc;
    int m_pendingShipId;
    uint8_t m_pendingSessionId;
    int m_pendingAscSlot;
    QTimer *m_resourceAllocTimer;

    QSet<int> m_occupiedDcChannels;
    DownlinkManager *m_downlinkManager;
    FileTransferHandler *m_fileTransferHandler;
    QThreadPool *m_threadPool;
    SlotScheduler* m_scheduler;
    Ship* m_ship;
    QUdpSocket* m_udpSocket;
    QUdpSocket* m_downlinkSocket;
    QString m_shipIp;
    quint16 m_shipPort;
    uint8_t m_satId;
    uint32_t m_satSourceId;
};

#endif // SIMULATOR_H
