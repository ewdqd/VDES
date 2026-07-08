#include "zk_service_discovery.h"
#include <QDebug>
#include <QStringList>

#ifndef ZK_DISABLED

// 全局 Watcher 回调函数
void watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx) {
    Q_UNUSED(zh);
    Q_UNUSED(path);
    Q_UNUSED(watcherCtx);
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            qDebug() << "ZooKeeper 连接成功!";
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            qDebug() << "ZooKeeper 会话已过期!";
        } else /* ZOO_NOTCONNECTED_STATE */ {
            qDebug() << "ZooKeeper 未连接!";
        }
    }
}

ZkServiceDiscovery& ZkServiceDiscovery::instance() {
    static ZkServiceDiscovery zk;
    return zk;
}

ZkServiceDiscovery::ZkServiceDiscovery() : m_zhandle(nullptr) {
}

ZkServiceDiscovery::~ZkServiceDiscovery() {
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
}

bool ZkServiceDiscovery::init(const QString &zkHosts) {
    m_zhandle = zookeeper_init(zkHosts.toStdString().c_str(), watcher, 10000, nullptr, nullptr, 0);
    if (m_zhandle != nullptr) {
        qDebug() << "ZooKeeper 初始化成功，正在尝试连接...";
        return true;
    }
    qDebug() << "ZooKeeper 初始化失败!";
    return false;
}

QPair<QHostAddress, quint16> ZkServiceDiscovery::getBestServer() {
    if (m_zhandle == nullptr) {
        qWarning() << "ZooKeeper 句柄无效，返回默认地址";
        return qMakePair(QHostAddress("127.0.0.1"), 12345);
    }

    const char* rootPath = "/vdes/services";
    int rootExists = zoo_exists(m_zhandle, rootPath, 0, nullptr);
    if (rootExists != ZOK) {
        if (zoo_create(m_zhandle, rootPath, "", 0, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0) == ZOK) {
            qDebug() << "成功创建根节点:" << rootPath;
        } else {
            qDebug() << "创建根节点失败或节点已存在";
        }
    }

    struct String_vector children;
    if (zoo_get_children(m_zhandle, rootPath, 0, &children) != ZOK) {
        qWarning() << "获取子节点列表失败";
        return qMakePair(QHostAddress("127.0.0.1"), 12345);
    }

    if (children.count == 0) {
        deallocate_String_vector(&children);
        qWarning() << "没有找到可用的服务端，使用默认地址";
        return qMakePair(QHostAddress("127.0.0.1"), 12345);
    }

    QString bestServerPath = QString("%1/%2").arg(rootPath).arg(children.data[0]);
    char buffer[1024];
    int buffer_len = sizeof(buffer);
    int ret = zoo_get(m_zhandle, bestServerPath.toStdString().c_str(), 0, buffer, &buffer_len, nullptr);
    deallocate_String_vector(&children);

    if (ret == ZOK) {
        QString data = QString::fromUtf8(buffer, buffer_len);
        QStringList parts = data.split(":");
        if (parts.size() == 2) {
            QHostAddress ip(parts[0]);
            quint16 port = parts[1].toUShort();
            qDebug() << "从 ZooKeeper 获取到服务端地址:" << ip.toString() << ":" << port;
            return qMakePair(ip, port);
        } else {
            qWarning() << "节点数据格式不正确:" << data;
        }
    } else {
        qWarning() << "读取节点数据失败";
    }

    return qMakePair(QHostAddress("127.0.0.1"), 12345);
}

void ZkServiceDiscovery::refreshServers() {
    // 可在此实现动态刷新逻辑
}

#else // ZK_DISABLED

ZkServiceDiscovery& ZkServiceDiscovery::instance() {
    static ZkServiceDiscovery zk;
    return zk;
}

ZkServiceDiscovery::ZkServiceDiscovery() : m_zhandle(nullptr) {}
ZkServiceDiscovery::~ZkServiceDiscovery() {}

bool ZkServiceDiscovery::init(const QString &zkHosts) {
    Q_UNUSED(zkHosts);
    qDebug() << "ZooKeeper 已禁用，使用默认连接";
    return false;
}

QPair<QHostAddress, quint16> ZkServiceDiscovery::getBestServer() {
    return qMakePair(QHostAddress("127.0.0.1"), 12345);
}

void ZkServiceDiscovery::refreshServers() {}

#endif
