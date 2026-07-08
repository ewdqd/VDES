#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include "vdes_messages.pb.h"

class TcpClient : public QObject
{
    Q_OBJECT
public:
    explicit TcpClient(const QString &host, quint16 port, QObject *parent = nullptr);
    void connectToServer();
    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }

    // 原有发送方法
    void sendShortAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data);
    void sendShortNoAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data, bool hasDest, const QByteArray &payload);
    void sendAddressed(int specifiedSlot, uint32_t destId, uint32_t myMmsi, const QByteArray &data);
    void sendConfig(int shipIndex, const QByteArray &configData);

    // 新增文件传输、实时消息相关发送方法
    void sendFileTransferRequest(uint32_t destShipId, const QString &fileName,
                                 quint64 fileSize, const QString &md5, uint32_t chunkSize);
    void sendFileChunk(const QString &transferId, uint32_t chunkIndex,
                       const QByteArray &data, bool isLast);
    void sendResumeRequest(const QString &transferId, uint32_t lastReceivedChunk);
    void sendPeerMessage(uint32_t toShipId, const QString &content, quint64 timestamp);

    // 公共的序列化发送方法（原为 private，现改为 public 供内部和友元使用）
    void sendRequest(const vdes::ClientRequest &req);

    // 本船 ID 管理
    uint32_t myMmsi() const { return m_myMmsi; }
    void setMyMmsi(uint32_t mmsi) { m_myMmsi = mmsi; }

signals:
    void connected();
    void disconnected();
    void slotUpdate(int slot);
    void logReceived(const QString &time, int slot, uint32_t shipId, const QString &direction,
                     const QString &msgType, const QString &summary, const QString &detail);
    void broadcastDataReceived(const QByteArray &data, uint32_t sourceId);
    void fileInfoReceived(const QString &fileId, const QString &fileName, qint64 totalSize, const QString &md5);
    void fileChunkReceived(const QString &fileId, int chunkIndex, const QByteArray &data, bool isLast);
    void errorOccurred(const QString &errorString);

    // 新增信号
    void fileTransferStatus(const QString &transferId, uint32_t completedChunks,
                            uint32_t totalChunks, const QString &status, const QString &errorMsg);

private slots:
    void onReadyRead();
    void onConnected();

private:
    QTcpSocket *m_socket;
    QString m_host;
    quint16 m_port;
    QByteArray m_buffer;
    uint32_t m_myMmsi;   // 本船 MMSI
};

#endif // TCP_CLIENT_H