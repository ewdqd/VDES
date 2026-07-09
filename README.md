# VDES SatelliteSim — VHF Data Exchange System 卫星通信仿真系统

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.8.3-green.svg)](https://www.qt.io/)
[![Protobuf](https://img.shields.io/badge/Protobuf-3-blueviolet.svg)](https://protobuf.dev/)

> **VDES (VHF Data Exchange System)** 卫星通信仿真系统，基于 **ITU-R M.2092** 标准实现。包含卫星模拟器、地面网关服务、船载客户端三重架构，完整模拟 VDE-SAT 上下行通信链路与 TDMA 时隙调度协议。

---

## 系统架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        VDES SatelliteSim                         │
│                      卫星通信仿真系统                              │
└──────────────────────────────────────────────────────────────────┘

  ┌──────────────┐    UDP:8080→9090    ┌─────────────────┐
  │ SatelliteSim │ ◄────────────────── │  VdesService     │
  │  卫星模拟器   │                     │  地面网关服务      │
  │  (UDP:8080)  │ ──────────────────► │  (UDP:9090)      │
  └──────────────┘    SBB/时隙/消息      │  TCP:12345       │
                                         └────────┬────────┘
                                                  │ TCP:12345
                                                  ▼
                                         ┌─────────────────┐
                                         │  VdesClient      │
                                         │  船载客户端       │
                                         │  (船舶仿真 GUI)   │
                                         └─────────────────┘
```

### 三层架构

| 层级 | 项目 | 技术栈 | 职责 |
|------|------|--------|------|
| **卫星层** | `SatelliteSim/` | Qt 6.8 (Core/GUI/Network/SQL), C++17 | 卫星 TDMA 时隙调度、SBB 广播、上下行消息处理、寻呼、文件传输 |
| **网关层** | `VdesService/` | 纯 C++17, 原生 Socket, Protobuf, SQLite3, ZooKeeper | 卫星-船舶消息中继、协议状态机、会话管理、集群发现、文件分发 |
| **终端层** | `VdesClient/` | Qt 6.8 (Core/GUI/Network/Multimedia/SQL), Protobuf, ZooKeeper | 多船仿真 GUI、消息收发、文件上传下载、媒体播放、时隙可视化 |

---

## 目录结构

```
VDES/
├── start.sh                  # 一键启动脚本（设置 Qt6 环境并启动全部服务）
├── .gitignore
├── README.md
│
├── SatelliteSim/             # ── 卫星模拟器 ──
│   ├── SatelliteSim.pro      #   QMake 项目文件
│   ├── main.cpp              #   入口 (QApplication)
│   ├── mainwidget.*          #   主界面：日志、时隙/信道展示、发送控制
│   ├── simulator.*           #   卫星仿真核心引擎
│   ├── ship.*                #   船舶状态机（接收、重组）
│   ├── slotscheduler.*       #   TDMA 时隙调度器 (2250 时隙/帧)
│   ├── downlinkmanager.*     #   下行链路管理 & ACK 处理
│   ├── filetransferhandler.* #   文件传输处理
│   └── satellite_db.*        #   SQLite 数据库（传输跟踪 & 日志）
│
├── VdesService/              # ── 网关服务 ──
│   ├── Makefile              #   自定义 Makefile（无 Qt 依赖）
│   ├── vdes_service.*        #   核心服务编排
│   ├── globallistener.*      #   SBB/MAC/寻呼/资源分配 解析器
│   ├── client_session.*      #   每客户端 TCP 会话
│   ├── vdesstatemachine.*    #   状态机基类
│   ├── uplinkaddressedstatemachine.*     # 上行寻址消息 SM
│   ├── uplinkshortackstatemachine.*      # 上行短消息 ACK SM
│   ├── uplinkshortnoackstatemachine.*    # 上行短消息 No-ACK SM
│   ├── downlinkaddressedstatemachine.*   # 下行寻址消息 SM
│   ├── downlinkshortackstatemachine.*    # 下行短消息 ACK SM
│   ├── downlinkshortnoackstatemachine.*  # 下行短消息 No-ACK SM
│   ├── broadcaststatemachine.*           # 广播接收 SM
│   ├── pagingstatemachine.*              # 寻呼状态机
│   ├── fragmentmanager.*     #   分片重组（广播/寻址）
│   ├── file_transfer_manager.* # 服务端文件传输
│   ├── database_manager.*    #   服务端数据库
│   ├── db_connection_pool.*  #   SQLite 连接池
│   ├── thread_pool.*         #   C++17 线程池
│   ├── socket_utils.*        #   跨平台 Socket 抽象
│   ├── timer.*               #   基于线程的定时器
│   ├── zk_client.*           #   ZooKeeper 集群客户端
│   └── vdes_crc.h            #   CRC-16 / CRC-32（ITU-R M.2092）
│
└── VdesClient/               # ── 船载客户端 ──
    ├── VdesClient.pro        #   QMake 项目文件
    ├── main.cpp              #   入口
    ├── widget.*              #   多船舶标签页主界面
    ├── tcp_client.*          #   TCP 客户端
    ├── ship_connection.*     #   每船舶独立线程连接管理
    ├── vdes_global.*         #   全局常量和数据结构
    ├── file_transfer_client.* # 客户端文件上传/下载（分片、MD5、SHA256、断点续传）
    ├── localdb.*             #   本地 SQLite 数据库
    ├── slotprogresscards.*   #   时隙进度卡片可视化
    ├── media_player.*        #   媒体播放器（QMediaPlayer）
    ├── media_player_widget.* #   播放器 GUI
    ├── zk_service_discovery.* # ZooKeeper 服务发现
    └── vdes_messages.proto   #   Protobuf 协议定义
```

---

## TDMA 时隙结构

系统基于 **2250 时隙/帧** 的 TDMA 结构，包含四类信道：

| 信道类型 | 用途 | 说明 |
|----------|------|------|
| **BBSC** | 卫星公告板 (Bulletin Board) | 卫星身份、频率规划、时隙配置（6 个 SBB 分片） |
| **ASC** | 接入信道 (Access) | 终端请求资源分配 |
| **RAC** | 随机接入信道 (Random Access) | 短消息、ACK |
| **DC** | 数据信道 (Data Channel) | 寻址消息、文件传输 |

> 可参考 `VdesService/vdes_cluster.json` 配置集群节点信息。

---

## 依赖项

| 依赖 | 版本要求 | 用途 | 所需项目 |
|------|---------|------|---------|
| **Qt** | ≥ 6.8.3 | GUI、网络、多媒体、SQL、并发 | SatelliteSim, VdesClient |
| **Protocol Buffers** | ≥ 3.x | 消息序列化 | VdesService, VdesClient |
| **SQLite3** | ≥ 3.x | 数据库存储 | 全部三个项目 |
| **ZooKeeper C Client** | ≥ 3.5 (可选) | 服务发现 / 集群 | VdesService, VdesClient |
| **FFmpeg** (libavcodec 等) | ≥ 4.x (Windows 可选) | 媒体编解码 | VdesClient (Win) |
| **pthreads** | - | 多线程 | VdesService (Linux) |

### 安装依赖 (Ubuntu / Debian)

```bash
# Qt 6.8.3（建议从 qt.io 官方安装器安装）
# 如使用系统包管理器：
sudo apt install qt6-base-dev qt6-multimedia-dev libqt6sql6-sqlite

# Protocol Buffers
sudo apt install protobuf-compiler libprotobuf-dev

# SQLite3
sudo apt install libsqlite3-dev

# ZooKeeper（可选）
sudo apt install libzookeeper-mt-dev
```

---

## 构建

### 1. SatelliteSim（卫星模拟器）

```bash
cd SatelliteSim
qmake6 SatelliteSim.pro
make -j$(nproc)
# 输出: build/SatelliteSim
```

### 2. VdesService（网关服务）

```bash
cd VdesService
make -j$(nproc)
# 输出: vdes_service
```

### 3. VdesClient（船载客户端）

```bash
cd VdesClient
qmake6 VdesClient.pro
make -j$(nproc)
# 输出: build/VdesClient
```

---

## 启动

使用项目根目录的 `start.sh` 一键启动：

```bash
./start.sh              # 启动全部服务
./start.sh all          # 同上

./start.sh service      # 仅启动 VdesService（TCP:12345, UDP:9090）
./start.sh satellite    # 仅启动 SatelliteSim（UDP:8080）
./start.sh client       # 仅启动 VdesClient
./start.sh stop         # 停止所有服务
```

### 启动顺序

```
① VdesService   → 先启动，监听 TCP:12345 + UDP:9090
② SatelliteSim  → 启动后连接 UDP:8080 → 9090
③ VdesClient    → 连接到服务端 TCP:12345
```

> 日志输出: `tail -f /tmp/vdes_*.log`

---

## 通信协议

### UDP 卫星链路

| 链路 ID | 类型 | CRC |
|---------|------|-----|
| 20 | 资源请求 | CRC-16 |
| 30 / 31 / 32 | 寻址消息 | CRC-32 |
| 其他 | 广播 / 短消息 / 文件 | CRC-32 |

### TCP 客户端协议（Protobuf）

定义文件: `VdesClient/vdes_messages.proto`

- `ClientRequest` — 客户端请求（消息发送、文件操作、状态查询）
- `ServerPush` — 服务端推送（时隙状态、消息到达、文件进度、日志）

### 文件传输

- 分片传输 + MD5 / SHA256 完整性校验
- 支持断点续传
- 服务端自动推送到目标客户端

---

## 集群模式

VdesService 可选集成 ZooKeeper 实现水平扩展：

1. 安装 ZooKeeper C 客户端
2. 配置 `VdesService/vdes_cluster.json`（节点 IP、端口、心跳间隔）
3. VdesClient 通过 ZooKeeper 自动发现可用服务节点

```json
{
  "nodes": [
    { "host": "192.168.1.100", "port": 12345, "role": "leader" }
  ],
  "heartbeat_interval_ms": 3000
}
```

---

## 开发环境

| 工具 | 版本 |
|------|------|
| 编译器 | GCC ≥ 9 / MSVC 2019+ (支持 C++17) |
| Qt | 6.8.3 |
| CMake / qmake | Qt 自带 qmake6 |
| Protobuf | ≥ 3.x |
| IDE | Qt Creator / VS 2022 / VS Code |

### Windows 构建

VdesService 提供 Visual Studio 解决方案 `VdesService.sln`。依赖通过 vcpkg 管理：

```powershell
vcpkg install protobuf ffmpeg abseil --triplet x64-windows
```

---

## 标准参考

- **ITU-R M.2092** — VHF Data Exchange System (VDES) technical characteristics
- 帧结构定义参考: `帧结构v2.0_20260206(1).docx`（项目同级目录）

---

## License

本项目源代码仅供学习参考。如需商业使用，请联系作者。
