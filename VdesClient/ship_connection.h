#ifndef SHIP_CONNECTION_H
#define SHIP_CONNECTION_H

#include <QObject>
#include <QTcpSocket>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include "vdes_messages.pb.h"

// 单船舶TCP连接（运行于独立子线程）
class ShipConnectionWorker : public QObject {
    Q_OBJECT
public:
    explicit ShipConnectionWorker(const QString &host, quint16 port,
                                  uint32_t shipId, QObject *parent = nullptr);
    ~ShipConnectionWorker();

    uint32_t shipId() const { return m_shipId; }
    bool isConnected() const;

signals:
    void connected(uint32_t shipId);
    void disconnected(uint32_t shipId);
    void errorOccurred(uint32_t shipId, const QString &errorString);

    void slotUpdateReceived(uint32_t shipId, int slot);
    void logReceived(uint32_t shipId, const QString &time, int slot,
                     const QString &direction, const QString &msgType,
                     const QString &summary, const QString &detail);
    void broadcastDataReceived(uint32_t shipId, const QByteArray &data, uint32_t sourceId);
    void fileInfoReceived(uint32_t shipId, const QString &fileId, const QString &fileName,
                          qint64 totalSize, const QString &md5);
    void fileChunkReceived(uint32_t shipId, const QString &fileId, int chunkIndex,
                           const QByteArray &data, bool isLast);
    void fileTransferStatusReceived(uint32_t shipId, const QString &transferId,
                                    uint32_t completedChunks, uint32_t totalChunks,
                                    const QString &status, const QString &errorMsg);

public slots:
    void connectToServer();
    void disconnectFromServer();

    void sendShortAck(int specifiedSlot, uint32_t destId, uint8_t data);
    void sendShortNoAck(int specifiedSlot, uint32_t destId, uint8_t data,
                        bool hasDest, const QByteArray &payload);
    void sendAddressed(int specifiedSlot, uint32_t destId, const QByteArray &data);
    void sendConfig(int shipIndex, const QByteArray &configData);
    void sendFileTransferRequest(uint32_t destShipId, const QString &fileName,
                                  quint64 fileSize, const QString &md5, uint32_t chunkSize);
    void sendFileChunk(const QString &transferId, uint32_t chunkIndex,
                       const QByteArray &data, bool isLast);
    void sendResumeRequest(const QString &transferId, uint32_t lastReceivedChunk);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    void sendRequest(const vdes::ClientRequest &req);

    QTcpSocket *m_socket;
    QString m_host;
    quint16 m_port;
    uint32_t m_shipId;
    QByteArray m_buffer;
};

// 船舶连接管理器
class ShipConnectionManager : public QObject {
    Q_OBJECT
public:
    explicit ShipConnectionManager(const QString &host, quint16 port,
                                   QObject *parent = nullptr);
    ~ShipConnectionManager();

    bool ensureConnection(uint32_t shipId);
    void removeConnection(uint32_t shipId);
    ShipConnectionWorker* getConnection(uint32_t shipId);
    void connectAll();
    void disconnectAll();
    bool isConnected(uint32_t shipId) const;
    int activeConnections() const { return m_connections.size(); }

signals:
    void anySlotUpdate(uint32_t shipId, int slot);
    void anyLogReceived(uint32_t shipId, const QString &time, int slot,
                        const QString &direction, const QString &msgType,
                        const QString &summary, const QString &detail);
    void anyBroadcastData(uint32_t shipId, const QByteArray &data, uint32_t sourceId);
    void anyFileInfo(uint32_t shipId, const QString &fileId, const QString &fileName,
                     qint64 totalSize, const QString &md5);
    void anyFileChunk(uint32_t shipId, const QString &fileId, int chunkIndex,
                      const QByteArray &data, bool isLast);
    void anyFileStatus(uint32_t shipId, const QString &transferId, uint32_t completed,
                       uint32_t total, const QString &status, const QString &error);
    void anyConnectionChanged(uint32_t shipId, bool connected);

private:
    ShipConnectionWorker* createWorker(uint32_t shipId);
    QString m_host;
    quint16 m_port;
    QMap<uint32_t, ShipConnectionWorker*> m_connections;
    QMap<uint32_t, QThread*> m_threads;
};

#endif // SHIP_CONNECTION_H
