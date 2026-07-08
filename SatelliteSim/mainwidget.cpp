#include "MainWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QScrollBar>
#include <QMessageBox>
#include <QSplitter>

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent), m_sim(nullptr), m_log(nullptr), m_messageTable(nullptr), m_uplinkTable(nullptr)
{
    setWindowTitle("VDE-SAT 卫星通信模拟器 - 地面网关");
    setMinimumSize(1100, 700);
    setupUI();
    initSimulator();
}

void MainWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // 顶部状态栏
    QWidget* statusBar = new QWidget;
    statusBar->setStyleSheet("background: #f0f4f8; border-radius: 6px; padding: 6px;");
    QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
    m_slotLabel = new QLabel("当前时隙: 0");
    m_channelLabel = new QLabel("信道: BBSC");
    m_stateLabel = new QLabel("卫星状态: 正常");
    m_slotLabel->setStyleSheet("font-weight: bold; color: #1e40af;");
    m_channelLabel->setStyleSheet("font-weight: bold; color: #1e40af;");
    m_stateLabel->setStyleSheet("font-weight: bold; color: #10b981;");
    statusLayout->addWidget(m_slotLabel);
    statusLayout->addWidget(m_channelLabel);
    statusLayout->addWidget(m_stateLabel);
    statusLayout->addStretch();
    mainLayout->addWidget(statusBar);

    // 运行日志
    QGroupBox* logGroup = new QGroupBox("运行日志");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    m_log = new QTextBrowser;
    m_log->setStyleSheet("background-color:#f8f8f8; font-family: Consolas;");
    logLayout->addWidget(m_log);
    mainLayout->addWidget(logGroup, 1);

    // 消息表格区域 - 使用垂直分割器分开两个表格
    QSplitter* splitter = new QSplitter(Qt::Vertical);

    // 下行/混合消息表格
    QGroupBox* downMsgGroup = new QGroupBox("下行发送消息记录");
    QVBoxLayout* downLayout = new QVBoxLayout(downMsgGroup);
    m_messageTable = new QTableWidget;
    m_messageTable->setColumnCount(4);
    QStringList headers;
    headers << "时间" << "类型" << "摘要" << "详情";
    m_messageTable->setHorizontalHeaderLabels(headers);
    m_messageTable->horizontalHeader()->setStretchLastSection(true);
    m_messageTable->setAlternatingRowColors(true);
    m_messageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_messageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_messageTable, &QTableWidget::itemDoubleClicked,
            this, &MainWidget::onTableItemDoubleClicked);
    downLayout->addWidget(m_messageTable);
    splitter->addWidget(downMsgGroup);

    // 上行接收消息表格
    QGroupBox* upMsgGroup = new QGroupBox("接收的上行消息记录");
    QVBoxLayout* upLayout = new QVBoxLayout(upMsgGroup);
    m_uplinkTable = new QTableWidget;
    m_uplinkTable->setColumnCount(4);
    QStringList uplinkHeaders;
    uplinkHeaders << "时间" << "类型" << "摘要" << "详情";
    m_uplinkTable->setHorizontalHeaderLabels(uplinkHeaders);
    m_uplinkTable->horizontalHeader()->setStretchLastSection(true);
    m_uplinkTable->setAlternatingRowColors(true);
    m_uplinkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_uplinkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_uplinkTable, &QTableWidget::itemDoubleClicked,
            this, &MainWidget::onTableItemDoubleClicked);
    upLayout->addWidget(m_uplinkTable);
    splitter->addWidget(upMsgGroup);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 2);

    // 发送控制面板
    QTabWidget* sendTabs = new QTabWidget;
    sendTabs->setStyleSheet("QTabBar::tab { padding: 6px 12px; }");

    // 广播消息页
    QWidget* broadcastTab = new QWidget;
    QVBoxLayout* broadcastLayout = new QVBoxLayout(broadcastTab);
    broadcastLayout->addWidget(new QLabel("广播消息内容:"));
    m_broadcastEdit = new QLineEdit;
    m_broadcastEdit->setPlaceholderText("输入要广播的消息...");
    QPushButton* btnBroadcast = new QPushButton("发送广播消息");
    btnBroadcast->setStyleSheet("background-color: #3b82f6; color: white;");
    connect(btnBroadcast, &QPushButton::clicked, this, &MainWidget::onSendBroadcastClicked);
    broadcastLayout->addWidget(m_broadcastEdit);
    broadcastLayout->addWidget(btnBroadcast);
    broadcastLayout->addStretch();
    sendTabs->addTab(broadcastTab, "广播消息");

    // 下行寻址消息页
    QWidget* addressedTab = new QWidget;
    QGridLayout* addrLayout = new QGridLayout(addressedTab);
    addrLayout->addWidget(new QLabel("目的船舶ID:"), 0, 0);
    m_addressedShipIdEdit = new QLineEdit("200000002");
    addrLayout->addWidget(m_addressedShipIdEdit, 0, 1);
    addrLayout->addWidget(new QLabel("消息内容:"), 1, 0, 1, 2);
    m_addressedContentEdit = new QTextEdit;
    m_addressedContentEdit->setMaximumHeight(80);
    m_addressedContentEdit->setPlaceholderText("输入下行寻址消息（长文本）...");
    addrLayout->addWidget(m_addressedContentEdit, 2, 0, 1, 2);
    QPushButton* btnAddr = new QPushButton("发送下行寻址消息");
    btnAddr->setStyleSheet("background-color: #10b981; color: white;");
    connect(btnAddr, &QPushButton::clicked, this, &MainWidget::onSendAddressedClicked);
    addrLayout->addWidget(btnAddr, 3, 0, 1, 2);
    sendTabs->addTab(addressedTab, "下行寻址");

    // 短消息页
    QWidget* shortTab = new QWidget;
    QGridLayout* shortLayout = new QGridLayout(shortTab);
    shortLayout->addWidget(new QLabel("目的船舶ID:"), 0, 0);
    m_shortDestEdit = new QLineEdit("200000002");
    shortLayout->addWidget(m_shortDestEdit, 0, 1);
    shortLayout->addWidget(new QLabel("数据字节(0-255):"), 1, 0);
    m_shortDataEdit = new QLineEdit("119");
    shortLayout->addWidget(m_shortDataEdit, 1, 1);
    shortLayout->addWidget(new QLabel("消息类型:"), 2, 0);
    m_shortTypeCombo = new QComboBox;
    m_shortTypeCombo->addItems({"带ACK (type=14)", "无ACK (type=16)"});
    shortLayout->addWidget(m_shortTypeCombo, 2, 1);
    QPushButton* btnShort = new QPushButton("发送下行短消息");
    btnShort->setStyleSheet("background-color: #f59e0b; color: white;");
    connect(btnShort, &QPushButton::clicked, this, &MainWidget::onSendShortAckClicked);
    shortLayout->addWidget(btnShort, 3, 0, 1, 2);
    sendTabs->addTab(shortTab, "下行短消息");

    // 自定义消息页
    QWidget* customTab = new QWidget;
    QVBoxLayout* customLayout = new QVBoxLayout(customTab);
    customLayout->addWidget(new QLabel("自定义下行寻址消息（支持中文长文本）"));
    QTextEdit* customEdit = new QTextEdit;
    customEdit->setPlaceholderText("输入任意内容，卫星将分片发送...");
    QPushButton* btnCustom = new QPushButton("发送自定义消息");
    btnCustom->setStyleSheet("background-color: #8b5cf6; color: white;");
    connect(btnCustom, &QPushButton::clicked, [this, customEdit]() {
        QString text = customEdit->toPlainText();
        if (text.isEmpty()) text = "Hello VDES from Satellite!";
        m_sim->sendDownlinkToShip(200000002, text.toUtf8());
        appendLog(QString("下行寻址(自定义) 已发送: %1").arg(text), "green");
    });
    customLayout->addWidget(customEdit);
    customLayout->addWidget(btnCustom);
    sendTabs->addTab(customTab, "自定义寻址");

    mainLayout->addWidget(sendTabs, 1);
}

void MainWidget::initSimulator()
{
    m_sim = new Simulator(this);
    m_sim->setSlotReportDestination("127.0.0.1", 9090);

    connect(m_sim, &Simulator::logAppend, this, &MainWidget::appendLog);
    connect(m_sim, &Simulator::slotUpdate, this, &MainWidget::updateSlot);
    connect(m_sim, &Simulator::stateUpdate, this, &MainWidget::updateState);
    connect(m_sim, &Simulator::messageRecorded, this, [this](const QString& type, const QString& summary, const QString& detail) {
        addMessageEntry(m_messageTable, type, summary, detail);
    });
    connect(m_sim, &Simulator::uplinkMessageReceived, this, &MainWidget::onUplinkMessageReceived);

    DownlinkManager* dm = m_sim->getDownlinkManager();
    if (dm) {
        connect(dm, &DownlinkManager::sessionComplete,
                this, [this](int shipId, uint8_t sessionId) {
                    appendLog(QString("下行会话完成: 船舶=%1 会话=%2").arg(shipId).arg(sessionId), "green");
                });
        connect(dm, &DownlinkManager::sessionFailed,
                this, [this](int shipId, uint8_t sessionId, const QString& reason) {
                    appendLog(QString("下行会话失败: 船舶=%1 会话=%2 原因=%3").arg(shipId).arg(sessionId).arg(reason), "red");
                });
    }

    appendLog("=== 卫星模拟器已启动，等待客户端连接 ===", "blue");
}

void MainWidget::appendLog(const QString& text, const QString& color)
{
    if (!m_log) return;
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString colored = QString("<font color='%1'>[%2] %3</font>").arg(color, timestamp, text);
    m_log->append(colored);
    m_log->moveCursor(QTextCursor::End);
}

void MainWidget::addMessageEntry(QTableWidget* table, const QString& type, const QString& summary, const QString& detail)
{
    if (!table) return;
    int row = table->rowCount();
    table->insertRow(row);
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    table->setItem(row, 0, new QTableWidgetItem(time));
    table->setItem(row, 1, new QTableWidgetItem(type));
    table->setItem(row, 2, new QTableWidgetItem(summary));
    table->setItem(row, 3, new QTableWidgetItem(detail));
    table->scrollToBottom();
}

void MainWidget::onUplinkMessageReceived(const QString& type, const QString& summary, const QString& detail)
{
    addMessageEntry(m_uplinkTable, type, summary, detail);
}

void MainWidget::onTableItemDoubleClicked(QTableWidgetItem* item)
{
    if (!item) return;
    int row = item->row();
    QTableWidget* table = item->tableWidget();
    if (!table) return;
    QString detail = table->item(row, 3)->text();
    if (!detail.isEmpty()) {
        QMessageBox::information(this, "消息详情", detail);
    }
}

void MainWidget::updateSlot(int slot, const QString& channel)
{
    m_slotLabel->setText(QString("当前时隙: %1").arg(slot));
    m_channelLabel->setText(QString("信道: %1").arg(channel));
}

void MainWidget::updateState(const QString& state)
{
    m_stateLabel->setText("卫星状态: " + state);
}

void MainWidget::onSendBroadcastClicked()
{
    QString text = m_broadcastEdit->text();
    if (text.isEmpty()) {
        text = "卫星广播消息测试 " + QDateTime::currentDateTime().toString("hh:mm:ss");
        m_broadcastEdit->setText(text);
    }
    m_sim->sendBroadcastMessage(text.toUtf8());
    appendLog(QString("发送广播消息: %1").arg(text), "purple");
}

void MainWidget::onSendAddressedClicked()
{
    bool ok;
    int shipId = m_addressedShipIdEdit->text().toInt(&ok);
    if (!ok) shipId = 200000002;
    QString content = m_addressedContentEdit->toPlainText();
    if (content.isEmpty()) {
        content = "卫星下行寻址测试消息 " + QDateTime::currentDateTime().toString("hh:mm:ss");
        m_addressedContentEdit->setPlainText(content);
    }
    m_sim->sendDownlinkToShip(shipId, content.toUtf8());
    appendLog(QString("发送下行寻址消息到船舶%1: %2").arg(shipId).arg(content), "green");
}

void MainWidget::onSendShortAckClicked()
{
    bool ok;
    int shipId = m_shortDestEdit->text().toInt(&ok);
    if (!ok) shipId = 200000002;
    int data = m_shortDataEdit->text().toInt(&ok);
    if (!ok) data = 119;
    bool withAck = (m_shortTypeCombo->currentIndex() == 0);
    if (withAck) {
        m_sim->sendDownlinkShortAck(shipId, (uint8_t)data);
    } else {
        m_sim->sendDownlinkShortNoAck(shipId, (uint8_t)data);
    }
    QString typeStr = withAck ? "带ACK" : "无ACK";
    appendLog(QString("发送下行短消息(%1) 到船舶%2, 数据=%3").arg(typeStr).arg(shipId).arg(data), "orange");
}