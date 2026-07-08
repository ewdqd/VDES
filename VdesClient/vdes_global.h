#ifndef VDES_GLOBAL_H
#define VDES_GLOBAL_H

#include <QString>
#include <QUdpSocket>

extern QString g_boardIp;
extern quint16 g_boardPort;
extern QString g_localIp;
extern quint16 g_localPort;
extern QUdpSocket *g_udpSocket;

#endif // VDES_GLOBAL_H