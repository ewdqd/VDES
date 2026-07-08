// widget.cpp - Complete rewrite with fixed UI, colors, separated short/addressed send
#include "widget.h"
#include "slotprogresscards.h"
#include "tcp_client.h"
#include "media_player.h"
#include "media_player_widget.h"
#include "vdes_global.h"
#include "file_transfer_client.h"
#include "zk_service_discovery.h"
#include "localdb.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextBrowser>
#include <QTabWidget>
#include <QSplitter>
#include <QTableView>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QUdpSocket>
#include <QTimer>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QApplication>
#include <QFrame>

extern QString g_boardIp,g_localIp; extern quint16 g_boardPort,g_localPort; extern QUdpSocket*g_udpSocket;
#define SHIP_BASE_ID 200000001
#define SAT_ID 99

Widget::Widget(QWidget*p):QWidget(p),m_tcpClient(nullptr),m_mediaPlayer(nullptr),
    m_fileTransfer(nullptr),m_zk(ZkServiceDiscovery::instance()),m_slotCards(nullptr),
    m_logTableView(nullptr),m_logModel(nullptr),m_logProxy(nullptr),
    m_filterShipCombo(nullptr),m_filterMachineCombo(nullptr),m_filterTypeCombo(nullptr),
    m_shipTabs(nullptr),m_currentSlot(0),m_prevSlot(0){
    setWindowTitle("VDES 船舶通信系统");resize(1500,950);m_selectedFilePaths.resize(8);initUI();
    QString ip="127.0.0.1";quint16 port=12345;
    m_tcpClient=new TcpClient(ip,port,this);m_tcpClient->setMyMmsi(SHIP_BASE_ID);
    m_shipConnMgr=new ShipConnectionManager(ip,port,this);
    connect(m_shipConnMgr,&ShipConnectionManager::anySlotUpdate,this,[this](uint32_t,int s){onSlotUpdateFromServer(s);});
    connect(m_shipConnMgr,&ShipConnectionManager::anyFileChunk,this,[this](uint32_t,const QString&f,int i,const QByteArray&d,bool l){onFileChunkFromServer(f,i,d,l);});
    connect(m_shipConnMgr,&ShipConnectionManager::anyLogReceived,this,[this](uint32_t,const QString&t,int sl,const QString&d,const QString&mt,const QString&s,const QString&de){onLogFromServer(t,sl,0,d,mt,s,de);});
    m_shipConnMgr->connectAll();
    connect(m_tcpClient,&TcpClient::slotUpdate,this,&Widget::onSlotUpdateFromServer);
    connect(m_tcpClient,&TcpClient::logReceived,this,&Widget::onLogFromServer);
    connect(m_tcpClient,&TcpClient::broadcastDataReceived,this,&Widget::onBroadcastFromServer);
    connect(m_tcpClient,&TcpClient::fileInfoReceived,this,&Widget::onFileInfoFromServer);
    connect(m_tcpClient,&TcpClient::fileChunkReceived,this,&Widget::onFileChunkFromServer);
    connect(m_tcpClient,&TcpClient::connected,this,[this]{m_connectionStatus->setText("已连接");m_connectionStatus->setStyleSheet("color:#10b981;font-weight:bold;");});
    connect(m_tcpClient,&TcpClient::disconnected,this,[this]{m_connectionStatus->setText("已断开");m_connectionStatus->setStyleSheet("color:#ef4444;font-weight:bold;");});
    connect(m_tcpClient,&TcpClient::errorOccurred,this,&Widget::onTcpError);
    connect(m_tcpClient,&TcpClient::fileTransferStatus,this,&Widget::onFileStatus);
    m_fileTransfer=new FileTransferClient(m_tcpClient,this);
    connect(m_fileTransfer,&FileTransferClient::uploadProgress,this,&Widget::onUploadProgress);
    connect(m_fileTransfer,&FileTransferClient::uploadComplete,this,&Widget::onUploadComplete);
    connect(m_fileTransfer,&FileTransferClient::downloadProgress,this,&Widget::onDownloadProgress);
    connect(m_fileTransfer,&FileTransferClient::downloadComplete,this,&Widget::onDownloadComplete);
    connect(m_shipTabs,&QTabWidget::currentChanged,this,&Widget::onShipTabChanged);
    m_tcpClient->connectToServer();QTimer::singleShot(3000,[this]{checkConnectionStatus();});
}
Widget::~Widget(){}

void Widget::initUI(){
    setObjectName("MainWidget");
    setStyleSheet(R"(
        #MainWidget{background:#f0f4f8;font-family:"Microsoft YaHei";font-size:12px;}
        QGroupBox{background:#fff;border:1px solid #e2e8f0;border-radius:8px;margin-top:10px;}
        QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 5px;color:#1e293b;font-weight:bold;}
        QLineEdit,QComboBox,QTextEdit{border:1px solid #cbd5e1;border-radius:4px;padding:4px;background:#fff;color:#0f172a;}
        QComboBox::drop-down{border:0;width:20px;}
        QComboBox QAbstractItemView{background:#fff;color:#0f172a;selection-background-color:#3b82f6;selection-color:#fff;}
        QPushButton{background:#3b82f6;color:#fff;border:none;border-radius:6px;padding:6px 12px;font-weight:bold;}
        QPushButton:hover{background:#2563eb;}
        QTabWidget::pane{border:1px solid #cbd5e1;background:#fff;border-radius:8px;}
        QTabBar::tab{background:#f1f5f9;padding:6px 16px;margin-right:2px;border-top-left-radius:6px;border-top-right-radius:6px;color:#334155;}
        QTabBar::tab:selected{background:#3b82f6;color:#fff;}
        QTableView{alternate-background-color:#f8fafc;gridline-color:#e2e8f0;background:#fff;color:#0f172a;}
        QHeaderView::section{background:#f1f5f9;padding:4px;border:1px solid #e2e8f0;color:#0f172a;}
        QTextBrowser{background:#fff;border:1px solid #cbd5e1;border-radius:4px;color:#0f172a;}
        QLabel{color:#0f172a;}
    )");
    QVBoxLayout*mL=new QVBoxLayout(this);mL->setContentsMargins(10,10,10,10);mL->setSpacing(8);
    QWidget*ic=new QWidget;ic->setStyleSheet("background:#fff;border-radius:10px;padding:8px;");
    QHBoxLayout*il=new QHBoxLayout(ic);
    m_timeLabel=new QLabel("UTC:--");m_channelLabel=new QLabel("信道:--");m_minuteSlotLabel=new QLabel("时隙:--");
    m_connectionStatus=new QLabel("未连接");m_connectionStatus->setStyleSheet("color:#ef4444;font-weight:bold;");
    for(auto*l:{m_timeLabel,m_channelLabel,m_minuteSlotLabel})l->setStyleSheet("font-weight:bold;color:#0f172a;");
    il->addWidget(m_timeLabel);il->addWidget(m_channelLabel);il->addWidget(m_minuteSlotLabel);il->addStretch();il->addWidget(m_connectionStatus);
    QPushButton*exp=new QPushButton("导出日志");exp->setStyleSheet("background:#f59e0b;");connect(exp,&QPushButton::clicked,this,&Widget::onExportLogs);il->addWidget(exp);
    mL->addWidget(ic);
    m_slotCards=new SlotProgressCards;connect(m_slotCards,&SlotProgressCards::cardClicked,this,&Widget::onCardClicked);
    mL->addWidget(m_slotCards,1);
    m_logModel=new QStandardItemModel(this);m_logModel->setHorizontalHeaderLabels({"时间","时隙","船舶ID","状态机","类型","详情"});
    m_logProxy=new QSortFilterProxyModel(this);m_logProxy->setSourceModel(m_logModel);
    m_logTableView=new QTableView;m_logTableView->setModel(m_logProxy);m_logTableView->setAlternatingRowColors(true);
    m_logTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);m_logTableView->horizontalHeader()->setStretchLastSection(true);
    connect(m_logTableView,&QTableView::clicked,this,&Widget::onTableClicked);
    QHBoxLayout*fl=new QHBoxLayout;
    fl->addWidget(new QLabel("船舶:"));m_filterShipCombo=new QComboBox;m_filterShipCombo->addItem("全部");fl->addWidget(m_filterShipCombo);
    fl->addWidget(new QLabel("状态机:"));m_filterMachineCombo=new QComboBox;m_filterMachineCombo->addItem("全部");fl->addWidget(m_filterMachineCombo);
    fl->addWidget(new QLabel("类型:"));m_filterTypeCombo=new QComboBox;m_filterTypeCombo->addItem("全部");fl->addWidget(m_filterTypeCombo);fl->addStretch();
    connect(m_filterShipCombo,&QComboBox::currentTextChanged,this,&Widget::onFilterChanged);
    connect(m_filterMachineCombo,&QComboBox::currentTextChanged,this,&Widget::onFilterChanged);
    connect(m_filterTypeCombo,&QComboBox::currentTextChanged,this,&Widget::onFilterChanged);
    QVBoxLayout*ll=new QVBoxLayout;ll->setContentsMargins(0,0,0,0);ll->addWidget(m_logTableView);
    QWidget*lw=new QWidget;lw->setLayout(ll);mL->addWidget(lw,10);
    m_shipTabs=new QTabWidget;
    for(int i=0;i<8;i++){auto*p=createShipPanel(i);m_shipPanels.append(p);m_shipTabs->addTab(p->page,QString("船舶%1").arg(i+1));}
    mL->addWidget(m_shipTabs,4);
}
ShipPanel*Widget::createShipPanel(int index){
    auto*p=new ShipPanel;p->page=new QWidget;
    QVBoxLayout*pl=new QVBoxLayout(p->page);pl->setContentsMargins(8,8,8,8);pl->setSpacing(6);
    quint32 base=SHIP_BASE_ID+index;
    QHBoxLayout*tl=new QHBoxLayout;
    auto*pg=new QGroupBox("面板参数(修改后确认发送卫星)");QGridLayout*gl=new QGridLayout(pg);
    p->satIdEdit=new QLineEdit(QString::number(SAT_ID));p->myMmsiEdit=new QLineEdit(QString::number(base));p->destMmsiEdit=new QLineEdit("200000003");
    p->localIpEdit=new QLineEdit(g_localIp);p->localPortEdit=new QLineEdit(QString::number(g_localPort));
    p->boardIpEdit=new QLineEdit(g_boardIp);p->boardPortEdit=new QLineEdit(QString::number(g_boardPort));
    p->phyChannelEdit=new QLineEdit("0x0A");p->freqEdit=new QLineEdit("0");p->linkIdEdit=new QLineEdit("20");p->reserve1Edit=new QLineEdit;p->reserve2Edit=new QLineEdit;
    gl->addWidget(new QLabel("卫星ID:"),0,0);gl->addWidget(p->satIdEdit,0,1);gl->addWidget(new QLabel("本船ID:"),1,0);gl->addWidget(p->myMmsiEdit,1,1);
    gl->addWidget(new QLabel("目的:"),2,0);gl->addWidget(p->destMmsiEdit,2,1);gl->addWidget(new QLabel("本地IP:"),3,0);gl->addWidget(p->localIpEdit,3,1);
    gl->addWidget(new QLabel("端口:"),4,0);gl->addWidget(p->localPortEdit,4,1);gl->addWidget(new QLabel("板卡IP:"),0,2);gl->addWidget(p->boardIpEdit,0,3);
    gl->addWidget(new QLabel("端口:"),1,2);gl->addWidget(p->boardPortEdit,1,3);gl->addWidget(new QLabel("信道:"),2,2);gl->addWidget(p->phyChannelEdit,2,3);
    gl->addWidget(new QLabel("频点:"),3,2);gl->addWidget(p->freqEdit,3,3);gl->addWidget(new QLabel("LinkID:"),4,2);gl->addWidget(p->linkIdEdit,4,3);
    p->btnApplyLocal=new QPushButton("确认并发送至卫星");p->btnApplyLocal->setStyleSheet("background:#10b981;max-height:26px;");
    gl->addWidget(p->btnApplyLocal,5,0,1,4);
    auto*cg=new QGroupBox("射频配置");QGridLayout*cgl=new QGridLayout(cg);
    p->configFreqEdit=new QLineEdit("161000");p->configDopplerEdit=new QLineEdit("0");p->configPowerEdit=new QLineEdit("33");
    p->configModulationEdit=new QLineEdit("1");p->configCodingRateEdit=new QLineEdit("1");p->btnSendConfig=new QPushButton("下发配置");p->btnSendConfig->setStyleSheet("background:#3b82f6;max-height:26px;");
    cgl->addWidget(new QLabel("频率(kHz):"),0,0);cgl->addWidget(p->configFreqEdit,0,1);cgl->addWidget(new QLabel("多普勒(Hz):"),0,2);cgl->addWidget(p->configDopplerEdit,0,3);
    cgl->addWidget(new QLabel("功率(dBm):"),1,0);cgl->addWidget(p->configPowerEdit,1,1);cgl->addWidget(new QLabel("调制:"),1,2);cgl->addWidget(p->configModulationEdit,1,3);
    cgl->addWidget(new QLabel("编码率:"),2,0);cgl->addWidget(p->configCodingRateEdit,2,1);cgl->addWidget(p->btnSendConfig,3,0,1,4);
    tl->addWidget(pg);tl->addWidget(cg);pl->addLayout(tl);
    // middle
    QSplitter*ms=new QSplitter(Qt::Horizontal);
    QWidget*lft=new QWidget;QVBoxLayout*ll2=new QVBoxLayout(lft);ll2->setContentsMargins(2,2,2,2);p->frameDisplay=new QTextBrowser;
    ll2->addWidget(new QLabel("发送帧打印"));ll2->addWidget(p->frameDisplay,1);ms->addWidget(lft);
    // right panel
    QWidget*rt=new QWidget;QVBoxLayout*rl=new QVBoxLayout(rt);rl->setContentsMargins(5,5,5,5);rl->setSpacing(4);
    auto*sb=new QGroupBox("发送控制");QVBoxLayout*sbl=new QVBoxLayout(sb);
    QHBoxLayout*sr=new QHBoxLayout;sr->addWidget(new QLabel("时隙:"));p->slotInput=new QLineEdit;p->slotInput->setPlaceholderText("自动");p->slotInput->setMaximumWidth(80);
    p->btnConfirmSlot=new QPushButton("确认");p->btnConfirmSlot->setStyleSheet("background:#909399;max-height:24px;");sr->addWidget(p->slotInput);sr->addWidget(p->btnConfirmSlot);sr->addStretch();sbl->addLayout(sr);
    // SHORT MESSAGE SECTION
    auto*slbl=new QLabel("=== 短消息发送 ===");slbl->setStyleSheet("font-weight:bold;color:#8b5cf6;font-size:11px;");sbl->addWidget(slbl);
    QHBoxLayout*shr=new QHBoxLayout;p->msgTypeCombo=new QComboBox;p->msgTypeCombo->addItems({"带确认(type33)","无确认-有目的(type23)","无确认-无目的(type24-28)"});
    shr->addWidget(p->msgTypeCombo);QLineEdit*shortInp=new QLineEdit;shortInp->setPlaceholderText("短消息内容(首字节)...");
    shr->addWidget(shortInp,1);QPushButton*shortBtn=new QPushButton("发短消息");shortBtn->setStyleSheet("background:#8b5cf6;min-width:70px;max-height:26px;");
    shr->addWidget(shortBtn);sbl->addLayout(shr);
    connect(shortBtn,&QPushButton::clicked,this,[this,index,shortInp](){
        if(index<0||index>=m_shipPanels.size())return;auto*pp=m_shipPanels[index];
        if(!pp||!m_tcpClient->isConnected())return;bool ok;uint32_t dest=pp->destMmsiEdit->text().toULong(&ok);if(!ok)dest=0;
        uint32_t my=pp->myMmsiEdit->text().toULong();m_tcpClient->setMyMmsi(my);
        int slot=pp->slotInput->text().toInt(&ok)?pp->slotInput->text().toInt():-1;int st=pp->msgTypeCombo->currentIndex();
        QString txt=shortInp->text().trimmed();
        if(st==0){uint8_t d=txt.isEmpty()?119:(uint8_t)txt.at(0).toLatin1();m_tcpClient->sendShortAck(slot,dest,my,d);appendToFrameDisplay(index,QString("短消息ACK(type33)|目的:%1 数据:0x%2").arg(dest).arg(d,2,16,QChar('0')));}
        else if(st==1){uint8_t d=txt.isEmpty()?120:(uint8_t)txt.at(0).toLatin1();m_tcpClient->sendShortNoAck(slot,dest,my,d,true,{});appendToFrameDisplay(index,QString("短消息NoACK(type23有目的)|目的:%1").arg(dest));}
        else{QByteArray pl=txt.toUtf8();if(pl.size()>5)pl.truncate(5);while(pl.size()<5)pl.append(char(0));m_tcpClient->sendShortNoAck(slot,dest,my,0,false,pl);appendToFrameDisplay(index,QString("短消息NoACK(type24无目的)|载荷:%1").arg(QString(pl.toHex(' '))));}
        shortInp->clear();});
    QFrame*sep=new QFrame;sep->setFrameShape(QFrame::HLine);sep->setStyleSheet("color:#e2e8f0;");sbl->addWidget(sep);
    // ADDRESSED SECTION
    auto*albl=new QLabel("=== 上行寻址发送(文字/文件/图片三选一) ===");albl->setStyleSheet("font-weight:bold;color:#2563eb;font-size:11px;");sbl->addWidget(albl);
    QHBoxLayout*atr=new QHBoxLayout;atr->addWidget(new QLabel("类型:"));p->addrContentTypeCombo=new QComboBox;p->addrContentTypeCombo->addItems({"文字消息","发送文件","发送图片"});
    atr->addWidget(p->addrContentTypeCombo);p->btnSelectFile=new QPushButton("选择...");p->btnSelectFile->setStyleSheet("background:#64748b;max-height:24px;");p->btnSelectFile->setVisible(false);
    p->selectedFilePathLabel=new QLabel;p->selectedFilePathLabel->setStyleSheet("color:#334155;font-size:11px;");p->selectedFilePathLabel->setVisible(false);
    atr->addWidget(p->btnSelectFile);atr->addWidget(p->selectedFilePathLabel,1);sbl->addLayout(atr);
    QHBoxLayout*acr=new QHBoxLayout;p->msgContentInput=new QLineEdit;p->msgContentInput->setPlaceholderText("输入寻址文字消息...");
    p->btnSend=new QPushButton("发送上行寻址");p->btnSend->setObjectName("btnSend");p->btnSend->setStyleSheet("QPushButton#btnSend{background:#2563eb;min-width:100px;}");
    acr->addWidget(p->msgContentInput,1);acr->addWidget(p->btnSend);sbl->addLayout(acr);
    rl->addWidget(sb);
    auto*vb=new QGroupBox("媒体播放器");QVBoxLayout*vbl=new QVBoxLayout(vb);vbl->setContentsMargins(2,2,2,2);p->videoPlayer=new MediaPlayerWidget(nullptr);p->videoPlayer->setMinimumHeight(140);vbl->addWidget(p->videoPlayer);
    rl->addWidget(vb,1);
    ms->addWidget(rt);ms->setStretchFactor(0,1);ms->setStretchFactor(1,1);pl->addWidget(ms,1);
    connect(p->btnSendConfig,&QPushButton::clicked,this,[this,index](){onSendConfigClicked(index);});
    connect(p->btnApplyLocal,&QPushButton::clicked,this,[this,index](){onApplyLocalParams(index);submitPanelParamsToServer(index);});
    connect(p->btnConfirmSlot,&QPushButton::clicked,this,[this,index](){onConfirmSlotClicked(index);});
    connect(p->btnSend,&QPushButton::clicked,this,&Widget::onSendClicked);
    connect(p->addrContentTypeCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,index](int i){onAddrContentTypeChanged(i,index);});
    connect(p->btnSelectFile,&QPushButton::clicked,this,[this,index](){onSelectFileClicked(index);});
    return p;
}
void Widget::appendToFrameDisplay(int si,const QString&t,bool err){if(si>=0&&si<m_shipPanels.size()&&m_shipPanels[si])m_shipPanels[si]->frameDisplay->append((err?"<font color=\"#ef4444\">[错误]</font> ":"[信息] ")+t);}
void Widget::onSendClicked(){
    int si=m_shipTabs->currentIndex();if(si<0||si>=m_shipPanels.size())return;auto*p=m_shipPanels[si];
    if(!p||!m_tcpClient->isConnected()){appendToFrameDisplay(si,"发送失败:未连接",true);return;}
    bool ok;uint32_t dest=p->destMmsiEdit->text().toULong(&ok);if(!ok)dest=0;uint32_t my=p->myMmsiEdit->text().toULong();
    m_tcpClient->setMyMmsi(my);int slot=p->slotInput->text().toInt(&ok)?p->slotInput->text().toInt():-1;
    int at=p->addrContentTypeCombo->currentIndex();
    if(at==0){QString t=p->msgContentInput->text();QByteArray d=t.toUtf8();if(t.isEmpty()){t="VDES寻址消息";d=t.toUtf8();p->msgContentInput->setText(t);}
        m_tcpClient->sendAddressed(slot,dest,my,d);appendToFrameDisplay(si,QString("上行寻址(文字)|目的:%1 长度:%2").arg(dest).arg(d.size()));}
    else{QString fp=m_selectedFilePaths[si];if(fp.isEmpty()){QMessageBox::warning(this,"错误","请先选择文件");return;}
        QFileInfo fi(fp);if(!fi.exists()){QMessageBox::warning(this,"错误","文件不存在");return;}m_fileTransfer->uploadFile(dest,fp);
        appendToFrameDisplay(si,QString("上行寻址(%1):%2->船舶%3").arg(at==2?"图片":"文件").arg(fi.fileName()).arg(dest));m_selectedFilePaths[si].clear();p->selectedFilePathLabel->clear();}
}
void Widget::onAddrContentTypeChanged(int idx,int si){
    if(si<0||si>=m_shipPanels.size())return;auto*p=m_shipPanels[si];if(!p)return;
    bool f=(idx==1||idx==2);p->btnSelectFile->setVisible(f);p->selectedFilePathLabel->setVisible(f);
    if(p->msgContentInput){p->msgContentInput->setVisible(!f);p->msgContentInput->setPlaceholderText(f?"选择文件后点击发送":"输入文字消息...");}
}
void Widget::onSelectFileClicked(int si){
    if(si<0||si>=m_shipPanels.size())return;auto*p=m_shipPanels[si];if(!p)return;
    bool img=(p->addrContentTypeCombo->currentIndex()==2);QString fp=QFileDialog::getOpenFileName(this,"选择文件","",img?"图片(*.png *.jpg *.jpeg *.gif *.bmp);;所有文件(*.*)":"所有文件(*.*)");
    if(fp.isEmpty())return;m_selectedFilePaths[si]=fp;QFileInfo fi(fp);p->selectedFilePathLabel->setText(fi.fileName()+QString("(%1KB)").arg(fi.size()/1024));
}
void Widget::onSendConfigClicked(int si){
    auto*p=m_shipPanels[si];if(!p||!m_tcpClient->isConnected())return;bool ok;
    uint16_t f=p->configFreqEdit->text().toUInt(&ok);if(!ok)f=0;int16_t d=p->configDopplerEdit->text().toInt(&ok);if(!ok)d=0;
    uint8_t pw=p->configPowerEdit->text().toUInt(&ok);if(!ok)pw=33;uint8_t m=p->configModulationEdit->text().toUInt(&ok);if(!ok)m=1;uint8_t cr=p->configCodingRateEdit->text().toUInt(&ok);if(!ok)cr=1;
    QByteArray fr;fr.append(0x50);fr.append((f>>8)&0xFF);fr.append(f&0xFF);fr.append((d>>8)&0xFF);fr.append(d&0xFF);fr.append(pw);fr.append(m);fr.append(cr);
    m_tcpClient->sendConfig(si,fr);appendToFrameDisplay(si,QString("配置下发|频率:%1kHz 功率:%2dBm").arg(f).arg(pw));
}
void Widget::onApplyLocalParams(int si){
    if(si<0||si>=m_shipPanels.size())return;auto*p=m_shipPanels[si];if(!p)return;
    g_localIp=p->localIpEdit->text();bool ok;quint16 pt=p->localPortEdit->text().toUShort(&ok);if(ok)g_localPort=pt;
    g_boardIp=p->boardIpEdit->text();quint16 bp=p->boardPortEdit->text().toUShort(&ok);if(ok)g_boardPort=bp;
    MonitorEvent ev;ev.slot=m_currentSlot;ev.shipId=p->myMmsiEdit->text().toULong();
    ev.stateMachine="面板参数";ev.direction="发送";ev.msgType="参数确认";ev.rawMsgType="PanelConfig";
    ev.summary=QString("IP:%1 板卡:%2:%3 船:%4").arg(g_localIp).arg(g_boardIp).arg(g_boardPort).arg(ev.shipId);
    ev.detail=ev.summary;ev.timestamp=QDateTime::currentDateTime();addMonitorEvent(ev);
    appendToFrameDisplay(si,"参数已发送至卫星");submitPanelParamsToServer(si);
}
void Widget::submitPanelParamsToServer(int si){
    auto*p=m_shipPanels[si];if(!p||!m_tcpClient->isConnected())return;bool ok;
    uint32_t my=p->myMmsiEdit->text().toULong(&ok);if(!ok)my=SHIP_BASE_ID+si;
    QByteArray fr;fr.append(0x50);fr.append((my>>24)&0xFF);fr.append((my>>16)&0xFF);fr.append((my>>8)&0xFF);fr.append(my&0xFF);
    uint32_t dest=p->destMmsiEdit->text().toULong(&ok);if(!ok)dest=200000003;
    fr.append((dest>>24)&0xFF);fr.append((dest>>16)&0xFF);fr.append((dest>>8)&0xFF);fr.append(dest&0xFF);
    fr.append((uint8_t)p->phyChannelEdit->text().toUInt(&ok,0));fr.append((uint8_t)p->linkIdEdit->text().toUInt(&ok));
    m_tcpClient->sendConfig(si,fr);m_tcpClient->setMyMmsi(my);
    appendToFrameDisplay(si,QString("面板参数->卫星|本船:%1 目的:%2").arg(my).arg(dest));
}
void Widget::onConfirmSlotClicked(int si){
    auto*p=m_shipPanels[si];if(!p)return;int s=p->slotInput->text().toInt();
    bool v=(s>=630&&s<=808)||(s>=1350&&s<=1528)||(s>=2070&&s<=2248);
    MonitorEvent ev;ev.slot=m_currentSlot;ev.shipId=p->myMmsiEdit->text().toULong();ev.stateMachine="UI";ev.direction="验证";ev.msgType="时隙确认";ev.rawMsgType="SlotConfirm";
    ev.summary=v?QString("时隙%1有效").arg(s):QString("时隙%1无效").arg(s);ev.detail=ev.summary;ev.timestamp=QDateTime::currentDateTime();addMonitorEvent(ev);appendToFrameDisplay(si,ev.summary);
}
void Widget::onShipTabChanged(int idx){if(idx>=0&&idx<m_shipPanels.size()&&m_shipPanels[idx])m_tcpClient->setMyMmsi(m_shipPanels[idx]->myMmsiEdit->text().toULong());}
QString Widget::getChineseStateMachineName(const QString&e)const{
    if(e=="UplinkAddressed")return"上行寻址";if(e=="DownlinkAddressed")return"下行寻址";if(e=="Paging")return"寻呼";
    if(e=="DownlinkShortNoAck")return"下行短消息(NACK)";if(e=="DownlinkShortAck")return"下行短消息(ACK)";
    if(e=="UplinkShortAck")return"上行短消息(ACK)";if(e=="UplinkShortNoAck")return"上行短消息(NACK)";if(e=="Broadcast")return"广播";return e;
}
QString Widget::getChineseDirection(const QString&e)const{if(e=="Tx")return"发送";if(e=="Rx")return"接收";return e;}
QString Widget::getChineseMsgType(const QString&e)const{
    if(e=="UplinkAddressed")return"上行寻址";if(e=="DownlinkAddressed")return"下行寻址";if(e=="Broadcast")return"广播";if(e=="FileInfo")return"文件信息";if(e=="FileChunk")return"文件块";return e;
}
QString Widget::getMsgTypeNumber(const QString&e)const{
    if(e=="UplinkShortAck")return"33";if(e=="UplinkShortNoAck")return"23";if(e=="UplinkAddressed")return"30/31/32";
    if(e=="DownlinkShortAck")return"14";if(e=="DownlinkShortNoAck")return"16";if(e=="DownlinkAddressed")return"30/31/32";
    if(e=="Broadcast")return"广播";if(e=="Paging")return"11";if(e=="FileInfo")return"文件信息";if(e=="FileChunk")return"文件块";return e;
}
void Widget::addMonitorEvent(const MonitorEvent&ev){
    m_allEvents.append(ev);QList<QStandardItem*>r;
    r<<new QStandardItem(ev.timestamp.toString("hh:mm:ss.zzz"))<<new QStandardItem(QString::number(ev.slot))<<new QStandardItem(QString::number(ev.shipId))<<new QStandardItem(ev.stateMachine)<<new QStandardItem(getMsgTypeNumber(ev.rawMsgType))<<new QStandardItem(ev.detail.isEmpty()?ev.summary:ev.detail);
    for(auto*i:r)i->setEditable(false);m_logModel->appendRow(r);m_logTableView->scrollToBottom();
    if(ev.shipId&&m_filterShipCombo->findText(QString::number(ev.shipId))==-1)m_filterShipCombo->addItem(QString::number(ev.shipId));
    if(m_filterMachineCombo->findText(ev.stateMachine)==-1)m_filterMachineCombo->addItem(ev.stateMachine);
    if(m_filterTypeCombo->findText(ev.msgType)==-1)m_filterTypeCombo->addItem(ev.msgType);m_slotCards->addEvent(ev);
}
void Widget::onFilterChanged(){m_slotCards->refresh();QString sf=m_filterShipCombo->currentText(),mf=m_filterMachineCombo->currentText(),tf=m_filterTypeCombo->currentText();QStringList p;if(sf!="全部")p<<sf;if(mf!="全部")p<<mf;if(tf!="全部")p<<tf;m_logProxy->setFilterRegularExpression(p.isEmpty()?QString():p.join("|"));}
void Widget::onCardClicked(const MonitorEvent&ev){for(int i=0;i<m_logModel->rowCount();++i)if(m_logModel->item(i,0)->text()==ev.timestamp.toString("hh:mm:ss.zzz")&&m_logModel->item(i,1)->text().toInt()==ev.slot){QModelIndex si=m_logModel->index(i,0);QModelIndex pi=m_logProxy->mapFromSource(si);if(pi.isValid()){m_logTableView->selectRow(pi.row());m_logTableView->scrollTo(pi);highlightLogRow(pi.row());}break;}}
void Widget::onTableClicked(const QModelIndex&){}void Widget::highlightLogRow(int r){m_logTableView->selectRow(r);}
void Widget::onSlotUpdateFromServer(int slot){m_currentSlot=slot;if(slot<m_prevSlot)m_slotCards->clearEvents();m_prevSlot=slot;m_timeLabel->setText(QString("UTC:%1").arg(QDateTime::currentDateTimeUtc().toString("HH:mm:ss")));m_minuteSlotLabel->setText(QString("时隙:%1/2250").arg(slot));m_slotCards->setCurrentSlot(slot);}
void Widget::onLogFromServer(const QString&t,int sl,uint32_t sid,const QString&dir,const QString&mt,const QString&sum,const QString&det){MonitorEvent ev;ev.timestamp=QDateTime::fromMSecsSinceEpoch(t.toLongLong());ev.slot=sl;ev.shipId=sid;ev.stateMachine=getChineseStateMachineName(mt);ev.direction=getChineseDirection(dir);ev.msgType=getChineseMsgType(mt);ev.rawMsgType=mt;ev.summary=sum;ev.detail=det;addMonitorEvent(ev);}
void Widget::onBroadcastFromServer(const QByteArray&d,uint32_t src){MonitorEvent ev;ev.slot=m_currentSlot;ev.shipId=src;ev.stateMachine="广播";ev.direction="接收";ev.msgType="广播消息";ev.rawMsgType="Broadcast";ev.summary=QString::fromUtf8(d).left(30);ev.detail=QString::fromUtf8(d);ev.timestamp=QDateTime::currentDateTime();addMonitorEvent(ev);}
void Widget::onFileInfoFromServer(const QString&,const QString&fn,qint64 sz,const QString&md5){MonitorEvent ev;ev.slot=m_currentSlot;ev.shipId=0;ev.stateMachine="文件传输";ev.direction="接收";ev.msgType="文件信息";ev.rawMsgType="FileInfo";ev.summary=QString("%1(%2B)").arg(fn).arg(sz);ev.detail=QString("MD5:%1").arg(md5);ev.timestamp=QDateTime::currentDateTime();addMonitorEvent(ev);}
void Widget::onFileChunkFromServer(const QString&fid,int idx,const QByteArray&d,bool last){if(m_fileTransfer)m_fileTransfer->handleFileChunk(fid,idx,d,last);}
void Widget::checkConnectionStatus(){}void Widget::onTcpError(const QString&e){QMessageBox::warning(this,"网络错误",e);m_connectionStatus->setText("错误");m_connectionStatus->setStyleSheet("color:#ef4444;font-weight:bold;");}
void Widget::appendGlobalLog(const QString&t,const QString&){qDebug()<<t;}
void Widget::onSlotAdvanced(int){}void Widget::onBroadcastMessageReceived(const QByteArray&d,uint32_t s){onBroadcastFromServer(d,s);}
void Widget::onUploadProgress(const QString&fn,int p){appendGlobalLog(QString("上传%1:%2%").arg(fn).arg(p));}
void Widget::onUploadComplete(const QString&fn,bool ok,const QString&e){appendGlobalLog(ok?QString("上传成功%1").arg(fn):QString("上传失败%1:%2").arg(fn).arg(e));}
void Widget::onDownloadProgress(const QString&fn,int p){appendGlobalLog(QString("下载%1:%2%").arg(fn).arg(p));}
void Widget::onDownloadComplete(const QString&fp,const QString&md5,const QString&,bool ok){if(ok){appendGlobalLog(QString("下载完成%1 MD5:%2").arg(fp).arg(md5));QMessageBox::information(this,"完成",QString("文件:%1").arg(fp));}else appendGlobalLog("下载失败","red");}
void Widget::onFileStatus(const QString&tid,quint32 c,quint32 t,const QString&s,const QString&e){appendGlobalLog(QString("传输[%1]:%2(%3/%4)%5").arg(tid).arg(s).arg(c).arg(t).arg(e));}
void Widget::onExportLogs(){
    QStringList fs;fs<<"CSV(*.csv)"<<"JSON(*.json)";QString sf;QString fp=QFileDialog::getSaveFileName(this,"导出日志",QString("vdes_logs_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),fs.join(";;"),&sf);
    if(fp.isEmpty())return;QList<MonitorEvent>ex;QString sf2=m_filterShipCombo->currentText(),mf=m_filterMachineCombo->currentText(),tf=m_filterTypeCombo->currentText();
    for(auto&e:m_allEvents){bool m=true;if(sf2!="全部"&&QString::number(e.shipId)!=sf2)m=false;if(mf!="全部"&&e.stateMachine!=mf)m=false;if(tf!="全部"&&e.msgType!=tf)m=false;if(m)ex.append(e);}
    bool json=sf.contains("JSON")||fp.endsWith(".json");
    if(json){QJsonArray a;for(auto&e:ex){QJsonObject o;o["timestamp"]=e.timestamp.toString(Qt::ISODateWithMs);o["slot"]=e.slot;o["ship_id"]=(qint64)e.shipId;o["state_machine"]=e.stateMachine;o["direction"]=e.direction;o["msg_type"]=e.msgType;o["raw_msg_type"]=e.rawMsgType;o["summary"]=e.summary;o["detail"]=e.detail;a.append(o);}
        QJsonObject r;r["export_time"]=QDateTime::currentDateTime().toString(Qt::ISODate);r["total_events"]=ex.size();r["events"]=a;QFile f(fp);if(f.open(QIODevice::WriteOnly|QIODevice::Text)){f.write(QJsonDocument(r).toJson(QJsonDocument::Indented));f.close();appendGlobalLog(QString("JSON导出:%1(%2条)").arg(fp).arg(ex.size()));}}
    else{QFile f(fp);if(f.open(QIODevice::WriteOnly|QIODevice::Text)){QTextStream s(&f);s<<"\xEF\xBB\xBF";s<<"时间,时隙,船舶ID,状态机,方向,消息类型,原始类型,摘要,详情\n";
        for(auto&e:ex){auto esc=[](const QString&s){if(s.contains(',')||s.contains('"')||s.contains('\n'))return "\""+QString(s).replace("\"","\"\"")+"\"";return s;};
        s<<esc(e.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz"))<<","<<e.slot<<","<<e.shipId<<","<<esc(e.stateMachine)<<","<<esc(e.direction)<<","<<esc(e.msgType)<<","<<esc(e.rawMsgType)<<","<<esc(e.summary)<<","<<esc(e.detail)<<"\n";}
        f.close();appendGlobalLog(QString("CSV导出:%1(%2条)").arg(fp).arg(ex.size()));}}
}
void Widget::onShortSendClicked(int){}
