#include <QApplication>
#include "widget.h"
#include "localdb.h"
#include "zk_service_discovery.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化本地数据库
    if (!LocalDB::instance().init()) {
        qWarning() << "Failed to initialize local database";
    }

    // 初始化服务发现（可选）
    ZkServiceDiscovery::instance().init();

    Widget w;
    w.show();
    return a.exec();
}