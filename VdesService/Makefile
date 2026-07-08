# VdesService Makefile for Linux
# Enterprise-grade VDES Communication Service
# Dependencies: protobuf, sqlite3, zookeeper (optional)

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread
LDFLAGS  := -pthread

# Detect protobuf
PROTOBUF_CFLAGS := $(shell pkg-config --cflags protobuf 2>/dev/null || echo "-I/usr/local/include")
PROTOBUF_LIBS   := $(shell pkg-config --libs protobuf 2>/dev/null || echo "-lprotobuf")

# SQLite3
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null || echo "")
SQLITE_LIBS   := $(shell pkg-config --libs sqlite3 2>/dev/null || echo "-lsqlite3")

# ZooKeeper (manual detection, no .pc file on Ubuntu)
ZK_H := /usr/include/zookeeper/zookeeper.h
ZK_SO := /usr/lib/x86_64-linux-gnu/libzookeeper_mt.so
ifneq ($(wildcard $(ZK_H)),)
    ZK_CFLAGS := -I/usr/include/zookeeper
    ZK_LIBS   := -lzookeeper_mt
    ZK_ENABLED := 1
else
    ZK_CFLAGS := -DZK_DISABLED
    ZK_LIBS   :=
    ZK_ENABLED := 0
endif

INCLUDES  := -I. $(PROTOBUF_CFLAGS) $(SQLITE_CFLAGS) $(ZK_CFLAGS)
LIBS      := $(PROTOBUF_LIBS) $(SQLITE_LIBS) $(ZK_LIBS) $(LDFLAGS)
TARGET    := vdes_service

SOURCES := \
    broadcaststatemachine.cpp \
    client_session.cpp \
    database_manager.cpp \
    db_connection_pool.cpp \
    downlinkaddressedstatemachine.cpp \
    downlinkshortackstatemachine.cpp \
    downlinkshortnoackstatemachine.cpp \
    file_transfer_manager.cpp \
    fragmentmanager.cpp \
    globallistener.cpp \
    main.cpp \
    pagingstatemachine.cpp \
    slotscheduler.cpp \
    socket_utils.cpp \
    thread_pool.cpp \
    timer.cpp \
    uplinkaddressedstatemachine.cpp \
    uplinkshortackstatemachine.cpp \
    uplinkshortnoackstatemachine.cpp \
    vdesstatemachine.cpp \
    vdes_global.cpp \
    vdes_messages.pb.cc \
    vdes_service.cpp

ifeq ($(ZK_ENABLED),1)
    SOURCES += zk_client.cpp
    $(info ZooKeeper FOUND - building with ZooKeeper support)
else
    $(info ZooKeeper not found - building WITHOUT ZooKeeper support)
endif

OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all clean proto distclean

all: $(TARGET)

vdes_messages.pb.cc vdes_messages.pb.h: ../VdesClient/vdes_messages.proto
	protoc --cpp_out=. -I../VdesClient $<

%.o: %.cpp vdes_messages.pb.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ $(LIBS) -o $@
	@echo "=== VdesService build complete ==="

clean:
	rm -f $(OBJECTS) $(TARGET)

distclean: clean
	rm -f vdes_messages.pb.cc vdes_messages.pb.h
