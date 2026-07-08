QT       += core gui network concurrent sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# 启用线程池优化（多核并行处理UDP收包和下行发送）
DEFINES += SATELLITE_THREAD_POOL_SIZE=8

SOURCES += \
    downlinkmanager.cpp \
    filetransferhandler.cpp \
    main.cpp \
    mainwidget.cpp \
    satellite_db.cpp \
    ship.cpp \
    simulator.cpp \
    slotscheduler.cpp

HEADERS += \
    downlinkmanager.h \
    filetransferhandler.h \
    mainwidget.h \
    satellite_db.h \
    ship.h \
    simulator.h \
    slotscheduler.h

FORMS +=

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
