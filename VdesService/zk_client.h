#ifndef ZK_CLIENT_H
#define ZK_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <map>

#include <zookeeper.h>

// 服务实例信息
struct ServiceInstance {
    std::string nodeName;
    std::string ip;
    uint16_t port;
    bool isLeader;
};

// ZooKeeper 客户端封装（使用真实 C API）
class ZkClient {
public:
    ZkClient();
    ~ZkClient();

    // 连接到 ZooKeeper 集群
    bool connect(const std::string& hosts, int sessionTimeout = 10000);

    // 断开连接
    void disconnect();

    // 注册当前服务（创建临时有序节点）
    bool registerService(const std::string& ip, uint16_t port);

    // 获取所有服务实例
    std::vector<ServiceInstance> getServiceInstances();

    // 尝试成为 leader（基于最小序号）
    bool tryBecomeLeader();

    // 是否 leader
    bool isLeader() const { return m_isLeader; }

    // 获取当前 leader 地址
    std::string getLeaderAddress();

    // 配置中心（存储/读取配置）
    bool setConfig(const std::string& key, const std::string& value);
    std::string getConfig(const std::string& key, const std::string& defaultValue = "");
    bool watchConfig(const std::string& key,
        std::function<void(const std::string&, const std::string&)> callback);

    // 事件回调
    std::function<void()> onLeaderChanged;

private:
    // ZooKeeper 全局 watcher
    static void globalWatcher(zhandle_t* zh, int type, int state, const char* path, void* context);

    // 获取子节点列表并解析数据
    std::vector<ServiceInstance> fetchServices();

    // 向 ZooKeeper 写入节点数据
    bool setNodeData(const std::string& path, const std::string& data);

    // 读取节点数据
    std::string getNodeData(const std::string& path);

    // 创建节点（递归创建父节点）
    bool createPathRecursive(const std::string& path, bool ephemeral = true);

    zhandle_t* m_zhandle;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_isLeader;
    std::string m_serviceNodePath;   // 当前服务注册的节点路径
    std::string m_myNodeName;        // 当前节点名称

    mutable std::mutex m_mutex;
    std::map<std::string, std::function<void(const std::string&, const std::string&)>> m_configWatchers;
    std::map<std::string, std::string> m_configCache;

    std::thread m_electionThread;
    std::atomic<bool> m_running;
    static constexpr int ELECTION_INTERVAL_SEC = 3;

    void leaderElectionLoop();
};

#endif