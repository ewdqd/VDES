QT       += core gui network multimedia multimediawidgets sql concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# ---------- ZooKeeper 依赖 ----------
win32 {
    DEFINES += THREADED ZOO_HAVE_SYNC_API USE_STATIC_LIB

    INCLUDEPATH += E:/apache-zookeeper-3.9.5/apache-zookeeper-3.9.5/zookeeper-client/zookeeper-client-c/include
    INCLUDEPATH += E:/apache-zookeeper-3.9.5/apache-zookeeper-3.9.5/zookeeper-client/zookeeper-client-c/src
    INCLUDEPATH += E:/apache-zookeeper-3.9.5/apache-zookeeper-3.9.5/zookeeper-client/zookeeper-client-c/generated

    LIBS += -LE:/apache-zookeeper-3.9.5/apache-zookeeper-3.9.5/zookeeper-client/zookeeper-client-c/build-static/Debug
    LIBS += -lzookeeper -lhashtable
    LIBS += -lws2_32
}

# vcpkg MSVC 安装根目录
VCPKG_ROOT = E:/qqqttt/vcpkg/vcpkg/installed/x64-windows

# ---------- FFmpeg 依赖 ----------
win32 {
    INCLUDEPATH += $$VCPKG_ROOT/include
    LIBS += -L$$VCPKG_ROOT/lib
    LIBS += -lavcodec -lavformat -lavutil -lswscale
}

# ---------- Protobuf 依赖 ----------
win32 {
    INCLUDEPATH += E:/qqqttt/vcpkg/vcpkg/installed/x64-windows/include
    LIBS += -LE:/qqqttt/vcpkg/vcpkg/installed/x64-windows/lib
    LIBS += -llibprotobuf
    LIBS += -labseil_dll
}
unix:!macx {
    INCLUDEPATH += /usr/include/zookeeper
    LIBS += -lzookeeper_mt
    CONFIG += link_pkgconfig
    PKGCONFIG += protobuf
}

SOURCES += \
    file_transfer_client.cpp \
    localdb.cpp \
    main.cpp \
    media_player.cpp \
    media_player_widget.cpp \
    ship_connection.cpp \
    slotprogresscards.cpp \
    tcp_client.cpp \
    vdes_global.cpp \
    vdes_messages.pb.cc \
    widget.cpp \
    zk_service_discovery.cpp

HEADERS += \
    file_transfer_client.h \
    localdb.h \
    media_player.h \
    media_player_widget.h \
    ship_connection.h \
    slotprogresscards.h \
    tcp_client.h \
    vdes_global.h \
    vdes_messages.pb.h \
    widget.h \
    zk_service_discovery.h

FORMS +=

DISTFILES += \
    vdes_messages.proto
