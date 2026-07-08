#ifndef ZK_SERVICE_DISCOVERY_H
#define ZK_SERVICE_DISCOVERY_H

#include <QObject>
#include <QString>
#include <QHostAddress>

#ifndef ZK_DISABLED
#define THREADED
#include <zookeeper.h>
#endif

class ZkServiceDiscovery : public QObject
{
    Q_OBJECT
public:
    static ZkServiceDiscovery& instance();

    // 初始化，连接 ZooKeeper
    bool init(const QString &zkHosts = "127.0.0.1:2181");

    // 获取可用的服务端地址（IP:Port）
    QPair<QHostAddress, quint16> getBestServer();

    // 更新服务列表（由 ZK watch 触发）
    void refreshServers();

signals:
    void serversChanged();

private:
    ZkServiceDiscovery();
    ~ZkServiceDiscovery();

#ifndef ZK_DISABLED
    zhandle_t *m_zhandle;
#else
    void *m_zhandle;
#endif
};

#endif // ZK_SERVICE_DISCOVERY_H
