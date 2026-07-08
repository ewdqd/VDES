#include "ship_connection.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>

// ==================== ShipConnectionWorker ====================
ShipConnectionWorker::ShipConnectionWorker(const QString &host, quint16 port,
                                            uint32_t shipId, QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
    , m_host(host)
    , m_port(port)
    , m_shipId(shipId)
{
}

ShipConnectionWorker::~ShipConnectionWorker()
{
    if (m_socket) {
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->disconnectFromHost();
        }
        delete m_socket;
    }
}

bool ShipConnectionWorker::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void ShipConnectionWorker::connectToServer()
{
    if (m_socket) {
        delete m_socket;
    }

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &ShipConnectionWorker::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ShipConnectionWorker::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ShipConnectionWorker::onReadyRead);
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &ShipConnectionWorker::onErrorOccurred);

    qDebug() << "[ShipConn:" << m_shipId << "] Connecting to" << m_host << ":" << m_port;
    m_socket->connectToHost(m_host, m_port);
}

void ShipConnectionWorker::disconnectFromServer()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void ShipConnectionWorker::onConnected()
{
    qDebug() << "[ShipConn:" << m_shipId << "] Connected to server";
    emit connected(m_shipId);
}

void ShipConnectionWorker::onDisconnected()
{
    qDebug() << "[ShipConn:" << m_shipId << "] Disconnected from server";
    emit disconnected(m_shipId);
}

void ShipConnectionWorker::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errStr = m_socket ? m_socket->errorString() : "Unknown error";
    qDebug() << "[ShipConn:" << m_shipId << "] Socket error:" << errStr;
    emit errorOccurred(m_shipId, errStr);
}

void ShipConnectionWorker::onReadyRead()
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
        if (push.ParseFromArray(m_buffer.constData() + 2, msgLen)) {
            // 时隙更新
            if (push.has_slot_update()) {
                emit slotUpdateReceived(m_shipId, push.slot_update().slot());
            }
            // 日志
            if (push.has_log_entry()) {
                auto &log = push.log_entry();
                emit logReceived(m_shipId,
                                 QString::number(log.timestamp()), log.slot(),
                                 QString::fromStdString(log.direction()),
                                 QString::fromStdString(log.msg_type()),
                                 QString::fromStdString(log.summary()),
                                 QString::fromStdString(log.detail()));
            }
            // 广播
            if (push.push_case() == vdes::ServerPush::kBroadcastData) {
                const auto& bd = push.broadcast_data();
                emit broadcastDataReceived(m_shipId,
                    QByteArray(bd.data(), bd.size()), 0);
            }
            // 文件信息
            if (push.has_file_info()) {
                auto &info = push.file_info();
                emit fileInfoReceived(m_shipId,
                    QString::fromStdString(info.file_id()),
                    QString::fromStdString(info.file_name()),
                    info.total_size(), QString::fromStdString(info.md5()));
            }
            // 文件分片
            if (push.has_file_chunk()) {
                auto &chunk = push.file_chunk();
                emit fileChunkReceived(m_shipId,
                    QString::fromStdString(chunk.file_id()), chunk.chunk_index(),
                    QByteArray(chunk.data().data(), chunk.data().size()), chunk.is_last());
            }
            // 文件状态
            if (push.has_file_status()) {
                auto &fs = push.file_status();
                emit fileTransferStatusReceived(m_shipId,
                    QString::fromStdString(fs.transfer_id()),
                    fs.completed_chunks(), fs.total_chunks(),
                    QString::fromStdString(fs.status()),
                    QString::fromStdString(fs.error_msg()));
            }

            m_buffer.remove(0, 2 + msgLen);
        } else {
            qWarning() << "[ShipConn:" << m_shipId << "] Parse failed, clearing buffer";
            m_buffer.clear();
            break;
        }
    }
}

// ====== 发送方法 ======
void ShipConnectionWorker::sendRequest(const vdes::ClientRequest &req)
{
    if (!isConnected()) {
        qWarning() << "[ShipConn:" << m_shipId << "] Not connected, cannot send";
        return;
    }

    std::string data;
    req.SerializeToString(&data);
    uint16_t len = static_cast<uint16_t>(data.size());
    char header[2];
    header[0] = (len >> 8) & 0xFF;
    header[1] = len & 0xFF;

    m_socket->write(header, 2);
    m_socket->write(data.c_str(), data.size());
}

void ShipConnectionWorker::sendShortAck(int specifiedSlot, uint32_t destId, uint8_t data)
{
    vdes::ClientRequest req;
    auto *ack = req.mutable_send_short_ack();
    ack->set_specified_slot(specifiedSlot);
    ack->set_dest_id(destId);
    ack->set_my_mmsi(m_shipId);
    ack->set_data(data);
    sendRequest(req);
}

void ShipConnectionWorker::sendShortNoAck(int specifiedSlot, uint32_t destId,
                                           uint8_t data, bool hasDest,
                                           const QByteArray &payload)
{
    vdes::ClientRequest req;
    auto *noack = req.mutable_send_short_noack();
    noack->set_specified_slot(specifiedSlot);
    noack->set_dest_id(destId);
    noack->set_my_mmsi(m_shipId);
    noack->set_data(data);
    noack->set_has_dest(hasDest);
    noack->set_payload(payload.constData(), payload.size());
    sendRequest(req);
}

void ShipConnectionWorker::sendAddressed(int specifiedSlot, uint32_t destId,
                                          const QByteArray &data)
{
    vdes::ClientRequest req;
    auto *add = req.mutable_send_addressed();
    add->set_specified_slot(specifiedSlot);
    add->set_dest_id(destId);
    add->set_my_mmsi(m_shipId);
    add->set_data(data.constData(), data.size());
    sendRequest(req);
}

void ShipConnectionWorker::sendConfig(int shipIndex, const QByteArray &configData)
{
    vdes::ClientRequest req;
    auto *cfg = req.mutable_send_config();
    cfg->set_ship_index(shipIndex);
    cfg->set_config_data(configData.constData(), configData.size());
    sendRequest(req);
}

void ShipConnectionWorker::sendFileTransferRequest(uint32_t destShipId,
                                                    const QString &fileName,
                                                    quint64 fileSize,
                                                    const QString &md5,
                                                    uint32_t chunkSize)
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

void ShipConnectionWorker::sendFileChunk(const QString &transferId, uint32_t chunkIndex,
                                          const QByteArray &data, bool isLast)
{
    vdes::ClientRequest req;
    auto *chunk = req.mutable_file_chunk();
    chunk->set_file_id(transferId.toStdString());
    chunk->set_chunk_index(chunkIndex);
    chunk->set_data(data.constData(), data.size());
    chunk->set_is_last(isLast);
    sendRequest(req);
}

void ShipConnectionWorker::sendResumeRequest(const QString &transferId,
                                              uint32_t lastReceivedChunk)
{
    vdes::ClientRequest req;
    auto *resume = req.mutable_resume_req();
    resume->set_transfer_id(transferId.toStdString());
    resume->set_last_received_chunk(lastReceivedChunk);
    sendRequest(req);
}


// ==================== ShipConnectionManager ====================
ShipConnectionManager::ShipConnectionManager(const QString &host, quint16 port,
                                             QObject *parent)
    : QObject(parent), m_host(host), m_port(port)
{
}

ShipConnectionManager::~ShipConnectionManager()
{
    disconnectAll();
}

ShipConnectionWorker* ShipConnectionManager::createWorker(uint32_t shipId)
{
    ShipConnectionWorker *worker = new ShipConnectionWorker(m_host, m_port, shipId);
    QThread *thread = new QThread(this);

    // 将worker移动到子线程
    worker->moveToThread(thread);

    // 线程启动时自动连接
    connect(thread, &QThread::started, worker, &ShipConnectionWorker::connectToServer);

    // 转发信号（跨线程自动队列连接）
    connect(worker, &ShipConnectionWorker::connected, this, [this](uint32_t sid) {
        emit anyConnectionChanged(sid, true);
    });
    connect(worker, &ShipConnectionWorker::disconnected, this, [this](uint32_t sid) {
        emit anyConnectionChanged(sid, false);
    });
    connect(worker, &ShipConnectionWorker::slotUpdateReceived, this, &ShipConnectionManager::anySlotUpdate);
    connect(worker, &ShipConnectionWorker::logReceived, this, &ShipConnectionManager::anyLogReceived);
    connect(worker, &ShipConnectionWorker::broadcastDataReceived, this, &ShipConnectionManager::anyBroadcastData);
    connect(worker, &ShipConnectionWorker::fileInfoReceived, this, &ShipConnectionManager::anyFileInfo);
    connect(worker, &ShipConnectionWorker::fileChunkReceived, this, &ShipConnectionManager::anyFileChunk);
    connect(worker, &ShipConnectionWorker::fileTransferStatusReceived, this, &ShipConnectionManager::anyFileStatus);

    // 清理连接
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    m_connections[shipId] = worker;
    m_threads[shipId] = thread;

    return worker;
}

bool ShipConnectionManager::ensureConnection(uint32_t shipId)
{
    if (m_connections.contains(shipId)) {
        return m_threads[shipId]->isRunning();
    }

    createWorker(shipId);
    m_threads[shipId]->start();

    qDebug() << "[ShipConnMgr] Started connection thread for ship" << shipId;
    return true;
}

void ShipConnectionManager::removeConnection(uint32_t shipId)
{
    if (!m_connections.contains(shipId)) return;

    ShipConnectionWorker *worker = m_connections[shipId];
    QThread *thread = m_threads[shipId];

    // 断开信号
    worker->disconnect();

    // 停止线程
    thread->quit();
    thread->wait(3000);

    m_connections.remove(shipId);
    m_threads.remove(shipId);

    qDebug() << "[ShipConnMgr] Removed connection for ship" << shipId;
}

ShipConnectionWorker* ShipConnectionManager::getConnection(uint32_t shipId)
{
    return m_connections.value(shipId, nullptr);
}

void ShipConnectionManager::connectAll()
{
    // 默认创建8个船舶连接（与实际船舶面板数匹配）
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t shipId = 200000001 + i;
        ensureConnection(shipId);
    }
}

void ShipConnectionManager::disconnectAll()
{
    QList<uint32_t> shipIds = m_connections.keys();
    for (uint32_t shipId : shipIds) {
        removeConnection(shipId);
    }
}

bool ShipConnectionManager::isConnected(uint32_t shipId) const
{
    auto it = m_connections.find(shipId);
    return it != m_connections.end() && it.value()->isConnected();
}
