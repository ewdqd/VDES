#include "zk_client.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>

using namespace std;

// 全局 watcher 回调
void ZkClient::globalWatcher(zhandle_t* zh, int type, int state, const char* path, void* context) {
    ZkClient* self = reinterpret_cast<ZkClient*>(context);
    if (!self) return;

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            cout << "[ZkClient] Connected to ZooKeeper" << endl;
            self->m_connected = true;
        }
        else if (state == ZOO_EXPIRED_SESSION_STATE) {
            cout << "[ZkClient] Session expired" << endl;
            self->m_connected = false;
        }
        else /* ZOO_NOTCONNECTED_STATE or older */ {
            cout << "[ZkClient] Disconnected" << endl;
            self->m_connected = false;
        }
    }
    else if (type == ZOO_CHILD_EVENT) {
        // 子节点变化，触发重新选举
        if (self->onLeaderChanged) self->onLeaderChanged();
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false), m_isLeader(false), m_running(false) {}

ZkClient::~ZkClient() {
    disconnect();
}

bool ZkClient::connect(const std::string& hosts, int sessionTimeout) {
    if (m_zhandle) disconnect();

    m_zhandle = zookeeper_init(hosts.c_str(), globalWatcher, sessionTimeout, nullptr, this, 0);
    if (!m_zhandle) {
        cerr << "[ZkClient] Failed to initialize ZooKeeper" << endl;
        return false;
    }

    // 等待连接成功（最多 5 秒）
    for (int i = 0; i < 50 && !m_connected; ++i) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    if (!m_connected) {
        cerr << "[ZkClient] Connection timeout" << endl;
        return false;
    }

    // 确保根路径存在
    if (!createPathRecursive("/vdes", false)) {
        cerr << "[ZkClient] Failed to create root path" << endl;
        return false;
    }
    if (!createPathRecursive("/vdes/services", false)) {
        cerr << "[ZkClient] Failed to create services path" << endl;
        return false;
    }
    if (!createPathRecursive("/vdes/config", false)) {
        cerr << "[ZkClient] Failed to create config path" << endl;
        return false;
    }

    m_running = true;
    m_electionThread = thread(&ZkClient::leaderElectionLoop, this);
    return true;
}

void ZkClient::disconnect() {
    m_running = false;
    if (m_electionThread.joinable()) m_electionThread.join();

    if (m_zhandle) {
        // 删除临时节点
        if (!m_serviceNodePath.empty()) {
            zoo_delete(m_zhandle, m_serviceNodePath.c_str(), -1);
        }
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    m_connected = false;
}

bool ZkClient::createPathRecursive(const string& path, bool ephemeral) {
    if (!m_zhandle) return false;

    string currentPath;
    size_t pos = 1;
    while (pos < path.length()) {
        size_t next = path.find('/', pos);
        if (next == string::npos) next = path.length();
        currentPath = path.substr(0, next);
        struct Stat stat;
        if (zoo_exists(m_zhandle, currentPath.c_str(), 0, &stat) != ZOK) {
            int flags = 0;
            if (!ephemeral && currentPath != "/vdes/services" && currentPath != "/vdes/config") {
                // 持久节点
                flags = 0;
            }
            else {
                // 临时节点（仅用于服务注册）
                flags = ZOO_EPHEMERAL;
            }
            int ret = zoo_create(m_zhandle, currentPath.c_str(), "", 0, &ZOO_OPEN_ACL_UNSAFE, flags, nullptr, 0);
            if (ret != ZOK && ret != ZNODEEXISTS) {
                cerr << "[ZkClient] Failed to create path: " << currentPath << " error: " << ret << endl;
                return false;
            }
        }
        pos = next + 1;
    }
    return true;
}

bool ZkClient::setNodeData(const string& path, const string& data) {
    if (!m_zhandle) return false;
    int ret = zoo_set(m_zhandle, path.c_str(), data.c_str(), data.size(), -1);
    return ret == ZOK;
}

string ZkClient::getNodeData(const string& path) {
    if (!m_zhandle) return "";
    char buffer[1024];
    int buffer_len = sizeof(buffer);
    int ret = zoo_get(m_zhandle, path.c_str(), 0, buffer, &buffer_len, nullptr);
    if (ret == ZOK && buffer_len > 0) {
        return string(buffer, buffer_len);
    }
    return "";
}

bool ZkClient::registerService(const string& ip, uint16_t port) {
    if (!m_connected || !m_zhandle) return false;

    // 创建临时顺序节点
    string nodePath = "/vdes/services/server_";
    char createdPath[256];
    int ret = zoo_create(m_zhandle, nodePath.c_str(), "", 0, &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL | ZOO_SEQUENCE, createdPath, sizeof(createdPath));
    if (ret != ZOK) {
        cerr << "[ZkClient] Failed to create service node: " << ret << endl;
        return false;
    }
    m_serviceNodePath = createdPath;
    m_myNodeName = m_serviceNodePath.substr(m_serviceNodePath.find_last_of('/') + 1);

    // 写入服务数据（IP:Port）
    string data = ip + ":" + to_string(port);
    if (!setNodeData(m_serviceNodePath, data)) {
        cerr << "[ZkClient] Failed to set service node data" << endl;
        return false;
    }

    cout << "[ZkClient] Registered service: " << m_myNodeName << " at " << ip << ":" << port << endl;
    return true;
}

vector<ServiceInstance> ZkClient::fetchServices() {
    vector<ServiceInstance> services;
    if (!m_zhandle) return services;

    String_vector children;
    if (zoo_get_children(m_zhandle, "/vdes/services", 0, &children) != ZOK) {
        cerr << "[ZkClient] Failed to get children of /vdes/services" << endl;
        return services;
    }

    for (int i = 0; i < children.count; ++i) {
        string childPath = string("/vdes/services/") + children.data[i];
        string data = getNodeData(childPath);
        size_t colon = data.find(':');
        if (colon == string::npos) continue;

        ServiceInstance inst;
        inst.nodeName = children.data[i];
        inst.ip = data.substr(0, colon);
        inst.port = static_cast<uint16_t>(stoi(data.substr(colon + 1)));
        inst.isLeader = false; // 先设为 false，后面选举时确定
        services.push_back(inst);
    }
    deallocate_String_vector(&children);

    // 按节点名称排序（序号升序）
    sort(services.begin(), services.end(),
        [](const ServiceInstance& a, const ServiceInstance& b) { return a.nodeName < b.nodeName; });

    // 第一个节点为 leader
    if (!services.empty()) services[0].isLeader = true;

    return services;
}

vector<ServiceInstance> ZkClient::getServiceInstances() {
    lock_guard<mutex> lock(m_mutex);
    return fetchServices();
}

bool ZkClient::tryBecomeLeader() {
    auto services = fetchServices();
    if (services.empty()) {
        m_isLeader = true;
        return true;
    }
    bool isLeaderNow = (services[0].nodeName == m_myNodeName);
    if (m_isLeader != isLeaderNow) {
        m_isLeader = isLeaderNow;
        if (onLeaderChanged) onLeaderChanged();
    }
    return m_isLeader;
}

string ZkClient::getLeaderAddress() {
    auto services = fetchServices();
    for (auto& s : services) {
        if (s.isLeader) return s.ip + ":" + to_string(s.port);
    }
    return "";
}

bool ZkClient::setConfig(const string& key, const string& value) {
    string path = "/vdes/config/" + key;
    if (!createPathRecursive(path, false)) return false;
    if (!setNodeData(path, value)) return false;
    lock_guard<mutex> lock(m_mutex);
    m_configCache[key] = value;
    auto it = m_configWatchers.find(key);
    if (it != m_configWatchers.end()) it->second(key, value);
    return true;
}

string ZkClient::getConfig(const string& key, const string& defaultValue) {
    lock_guard<mutex> lock(m_mutex);
    auto it = m_configCache.find(key);
    if (it != m_configCache.end()) return it->second;
    string path = "/vdes/config/" + key;
    string value = getNodeData(path);
    if (value.empty()) return defaultValue;
    m_configCache[key] = value;
    return value;
}

bool ZkClient::watchConfig(const string& key,
    function<void(const string&, const string&)> callback) {
    lock_guard<mutex> lock(m_mutex);
    m_configWatchers[key] = callback;
    // TODO: 设置 ZooKeeper watch 以监听节点变化
    return true;
}

void ZkClient::leaderElectionLoop() {
    while (m_running && m_connected) {
        this_thread::sleep_for(chrono::seconds(ELECTION_INTERVAL_SEC));
        if (!m_running || !m_connected) break;
        tryBecomeLeader();
    }
}