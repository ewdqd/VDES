#include "tcp_client.h"
#include <QDebug>
#include <QDateTime>

TcpClient::TcpClient(const QString &host, quint16 port, QObject *parent)
    : QObject(parent), m_host(host), m_port(port), m_myMmsi(0)
{
    qDebug() << "[TcpClient] Constructor, host=" << host << "port=" << port;
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpClient::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            [this](QAbstractSocket::SocketError err) {
                QString errStr = m_socket->errorString();
                qDebug() << "[TcpClient] Socket error:" << err << errStr;
                emit errorOccurred(errStr);
            });
}

void TcpClient::connectToServer()
{
    m_socket->connectToHost(m_host, m_port);
}

void TcpClient::onConnected()
{
    qDebug() << "[TcpClient] Connected to server!";
    emit connected();
}

// ---------- 原有发送方法 ----------
void TcpClient::sendShortAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data)
{
    vdes::ClientRequest req;
    auto *ack = req.mutable_send_short_ack();
    ack->set_specified_slot(specifiedSlot);
    ack->set_dest_id(destId);
    ack->set_my_mmsi(myMmsi);
    ack->set_data(data);
    sendRequest(req);
}

void TcpClient::sendShortNoAck(int specifiedSlot, uint32_t destId, uint32_t myMmsi, uint8_t data, bool hasDest, const QByteArray &payload)
{
    vdes::ClientRequest req;
    auto *noack = req.mutable_send_short_noack();
    noack->set_specified_slot(specifiedSlot);
    noack->set_dest_id(destId);
    noack->set_my_mmsi(myMmsi);
    noack->set_data(data);
    noack->set_has_dest(hasDest);
    noack->set_payload(payload.data(), payload.size());
    sendRequest(req);
}

void TcpClient::sendAddressed(int specifiedSlot, uint32_t destId, uint32_t myMmsi, const QByteArray &data)
{
    vdes::ClientRequest req;
    auto *add = req.mutable_send_addressed();
    add->set_specified_slot(specifiedSlot);
    add->set_dest_id(destId);
    add->set_my_mmsi(myMmsi);
    add->set_data(data.data(), data.size());
    sendRequest(req);
}

void TcpClient::sendConfig(int shipIndex, const QByteArray &configData)
{
    vdes::ClientRequest req;
    auto *cfg = req.mutable_send_config();
    cfg->set_ship_index(shipIndex);
    cfg->set_config_data(configData.data(), configData.size());
    sendRequest(req);
}

// ---------- 新增发送方法 ----------
void TcpClient::sendFileTransferRequest(uint32_t destShipId, const QString &fileName,
                                        quint64 fileSize, const QString &md5, uint32_t chunkSize)
{
    vdes::ClientRequest req;
    auto *ftReq = req.mutable_file_transfer_req();
    ftReq->set_dest_ship_id(destShipId);
    ftReq->set_file_name(fileName.toStdString());
    ftReq->set_file_size(fileSize);
    ftReq->set_md5(md5.toStdString());
    ftReq->set_chunk_size(chunkSize);
    sendRequest(req);
}

void TcpClient::sendFileChunk(const QString &transferId, uint32_t chunkIndex,
                              const QByteArray &data, bool isLast)
{
    vdes::ClientRequest req;
    auto *chunk = req.mutable_file_chunk();
    chunk->set_file_id(transferId.toStdString());
    chunk->set_chunk_index(chunkIndex);
    chunk->set_data(data.data(), data.size());
    chunk->set_is_last(isLast);
    sendRequest(req);
}

void TcpClient::sendResumeRequest(const QString &transferId, uint32_t lastReceivedChunk)
{
    vdes::ClientRequest req;
    auto *resume = req.mutable_resume_req();
    resume->set_transfer_id(transferId.toStdString());
    resume->set_last_received_chunk(lastReceivedChunk);
    sendRequest(req);
}


// ---------- 公共发送方法 ----------
void TcpClient::sendRequest(const vdes::ClientRequest &req)
{
    if (!isConnected()) {
        qWarning() << "[TcpClient] sendRequest called but socket not connected";
        emit errorOccurred("发送失败：socket 未连接");
        return;
    }
    std::string data;
    req.SerializeToString(&data);
    uint16_t len = static_cast<uint16_t>(data.size());
    char header[2];
    header[0] = (len >> 8) & 0xFF;
    header[1] = len & 0xFF;
    qint64 written = m_socket->write(header, 2);
    if (written != 2) {
        emit errorOccurred("发送长度头失败");
        return;
    }
    written = m_socket->write(data.c_str(), data.size());
    if (written != static_cast<qint64>(data.size())) {
        emit errorOccurred("发送 protobuf 数据不完整");
        return;
    }
}

// ---------- 接收解析 ----------
void TcpClient::onReadyRead()
{
    if (!m_socket) return;

    m_buffer.append(m_socket->readAll());

    while (true) {
        if (m_buffer.size() < 2) break;

        uint16_t msgLen = (static_cast<uint8_t>(m_buffer[0]) << 8) |
                          static_cast<uint8_t>(m_buffer[1]);

        if (msgLen == 0) {
            m_buffer.clear();
            break;
        }

        if (m_buffer.size() < 2 + msgLen) break;

        vdes::ServerPush push;
        if (push.ParseFromArray(m_buffer.data() + 2, msgLen)) {
            if (push.has_slot_update()) {
                emit slotUpdate(push.slot_update().slot());
            }
            if (push.has_log_entry()) {
                auto &log = push.log_entry();
                emit logReceived(QString::number(log.timestamp()), log.slot(), log.ship_id(),
                                 QString::fromStdString(log.direction()), QString::fromStdString(log.msg_type()),
                                 QString::fromStdString(log.summary()), QString::fromStdString(log.detail()));
            }
            if (push.push_case() == vdes::ServerPush::kBroadcastData) {
                emit broadcastDataReceived(QByteArray(push.broadcast_data().c_str(), push.broadcast_data().size()), 0);
            }
            if (push.has_file_info()) {
                auto &info = push.file_info();
                emit fileInfoReceived(QString::fromStdString(info.file_id()), QString::fromStdString(info.file_name()),
                                      info.total_size(), QString::fromStdString(info.md5()));
            }
            if (push.has_file_chunk()) {
                auto &chunk = push.file_chunk();
                emit fileChunkReceived(QString::fromStdString(chunk.file_id()), chunk.chunk_index(),
                                       QByteArray(chunk.data().c_str(), chunk.data().size()), chunk.is_last());
            }
            if (push.push_case() == vdes::ServerPush::kFileStatus) {
                auto &fs = push.file_status();
                emit fileTransferStatus(QString::fromStdString(fs.transfer_id()),
                                        fs.completed_chunks(), fs.total_chunks(),
                                        QString::fromStdString(fs.status()),
                                        QString::fromStdString(fs.error_msg()));
            }

            m_buffer.remove(0, 2 + msgLen);
        } else {
            qWarning() << "[TcpClient] parse failed, clearing buffer";
            m_buffer.clear();
            break;
        }
    }
}
