#include "vdes_global.h"

QString g_boardIp = "127.0.0.1";
quint16 g_boardPort = 8080;
QString g_localIp = "127.0.0.1";
quint16 g_localPort = 9000;
QUdpSocket *g_udpSocket = nullptr;