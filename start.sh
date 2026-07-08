#!/bin/bash
# VDES 企业级通信系统 - 环境配置与启动脚本
# 用途：设置Qt6环境变量并启动三个项目

export QT_BASE_DIR=/home/px4/Qt/6.8.3/gcc_64
export PATH=$QT_BASE_DIR/bin:$PATH
export LD_LIBRARY_PATH=$QT_BASE_DIR/lib:$LD_LIBRARY_PATH
export QT_PLUGIN_PATH=$QT_BASE_DIR/plugins
export QML2_IMPORT_PATH=$QT_BASE_DIR/qml

VDES_ROOT=/home/px4/VDES

echo "========================================="
echo " VDES 企业级通信系统"
echo " Qt6.8.3 环境已加载"
echo "========================================="
echo ""
echo "可用命令："
echo "  ./start.sh service    - 启动 VdesService（服务端）"
echo "  ./start.sh client     - 启动 VdesClient（客户端）"
echo "  ./start.sh satellite  - 启动 SatelliteSim（卫星模拟器）"
echo "  ./start.sh all        - 启动全部三个服务"
echo ""

start_service() {
    echo ">>> 启动 VdesService (TCP:12345, UDP:9090) ..."
    cd $VDES_ROOT/VdesService && ./vdes_service &
    echo $! > /tmp/vdes_service.pid
    echo "PID: $(cat /tmp/vdes_service.pid)"
}

start_client() {
    echo ">>> 启动 VdesClient ..."
    cd $VDES_ROOT/VdesClient/build && ./VdesClient &
    echo $! > /tmp/vdes_client.pid
    echo "PID: $(cat /tmp/vdes_client.pid)"
}

start_satellite() {
    echo ">>> 启动 SatelliteSim (UDP:8080) ..."
    cd $VDES_ROOT/SatelliteSim/SatelliteSim/build && ./SatelliteSim &
    echo $! > /tmp/vdes_satellite.pid
    echo "PID: $(cat /tmp/vdes_satellite.pid)"
}

stop_all() {
    echo ">>> 停止所有 VDES 进程 ..."
    for pidfile in /tmp/vdes_service.pid /tmp/vdes_client.pid /tmp/vdes_satellite.pid; do
        if [ -f "$pidfile" ]; then
            kill $(cat $pidfile) 2>/dev/null
            rm -f $pidfile
        fi
    done
    pkill -f "vdes_service" 2>/dev/null
    pkill -f "VdesClient" 2>/dev/null
    pkill -f "SatelliteSim" 2>/dev/null
    echo "已停止"
}

case "${1:-}" in
    service)   start_service ;;
    client)    start_client ;;
    satellite) start_satellite ;;
    all)
        start_service
        sleep 1
        start_satellite
        sleep 1
        start_client
        ;;
    stop)      stop_all ;;
    *)
        echo "启动全部服务..."
        start_service
        sleep 1
        start_satellite
        sleep 1
        start_client
        ;;
esac

echo ""
echo "启动顺序建议:"
echo "  1) VdesService   (先启动，监听 TCP:12345 + UDP:9090)"
echo "  2) SatelliteSim  (启动后连接 UDP:8080→9090)"
echo "  3) VdesClient    (连接到服务端 TCP:12345)"
echo ""
echo "查看日志: tail -f /tmp/vdes_*.log"
echo "========================================="
