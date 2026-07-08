#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextBrowser>
#include <QComboBox>
#include <QTabWidget>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QTableView>
#include "tcp_client.h"
#include "media_player.h"
#include "slotprogresscards.h"
#include "ship_connection.h"

class FileTransferClient;
class MediaPlayerWidget;
class ZkServiceDiscovery;

struct ShipPanel {
    QWidget *page = nullptr;

    // 通信参数（可动态修改）
    QLineEdit *satIdEdit = nullptr;
    QLineEdit *myMmsiEdit = nullptr;
    QLineEdit *destMmsiEdit = nullptr;
    QLineEdit *localIpEdit = nullptr;
    QLineEdit *localPortEdit = nullptr;
    QLineEdit *boardIpEdit = nullptr;
    QLineEdit *boardPortEdit = nullptr;
    QLineEdit *phyChannelEdit = nullptr;
    QLineEdit *freqEdit = nullptr;
    QLineEdit *linkIdEdit = nullptr;
    QLineEdit *reserve1Edit = nullptr;
    QLineEdit *reserve2Edit = nullptr;
    QPushButton *btnApplyLocal = nullptr;

    // 配置面板
    QLineEdit *configFreqEdit = nullptr;
    QLineEdit *configDopplerEdit = nullptr;
    QLineEdit *configPowerEdit = nullptr;
    QLineEdit *configModulationEdit = nullptr;
    QLineEdit *configCodingRateEdit = nullptr;
    QPushButton *btnSendConfig = nullptr;

    // 发送控制
    QLineEdit *slotInput = nullptr;
    QComboBox *msgTypeCombo = nullptr;
    QLineEdit *msgContentInput = nullptr;
    QPushButton *btnConfirmSlot = nullptr;
    QPushButton *btnSend = nullptr;

    // 上行寻址附件类型
    QComboBox *addrContentTypeCombo = nullptr;
    QPushButton *btnSelectFile = nullptr;
    QLabel *selectedFilePathLabel = nullptr;

    // 帧显示
    QTextBrowser *frameDisplay = nullptr;

    // 媒体播放器（逐船）
    MediaPlayerWidget *videoPlayer = nullptr;
};

class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

public slots:
    void onSlotAdvanced(int slot);
    void onBroadcastMessageReceived(const QByteArray &message, uint32_t sourceId);

private slots:
    void onSendClicked();
    void onShortSendClicked(int shipIndex);
    void onAddrContentTypeChanged(int index, int shipIndex);
    void onSelectFileClicked(int shipIndex);
    void onSendConfigClicked(int shipIndex);
    void onApplyLocalParams(int shipIndex);
    void onConfirmSlotClicked(int shipIndex);
    void onFilterChanged();
    void onCardClicked(const MonitorEvent &event);
    void onTableClicked(const QModelIndex &index);
    void onSlotUpdateFromServer(int slot);
    void onLogFromServer(const QString &time, int slot, uint32_t shipId,
                         const QString &direction, const QString &msgType,
                         const QString &summary, const QString &detail);
    void onBroadcastFromServer(const QByteArray &data, uint32_t sourceId);
    void onFileInfoFromServer(const QString &fileId, const QString &fileName,
                              qint64 totalSize, const QString &md5);
    void onFileChunkFromServer(const QString &fileId, int chunkIndex,
                               const QByteArray &data, bool isLast);
    void checkConnectionStatus();
    void onTcpError(const QString &errorString);

    void onShipTabChanged(int index);
    void onUploadProgress(const QString &fileName, int percent);
    void onUploadComplete(const QString &fileName, bool success, const QString &error);
    void onDownloadProgress(const QString &fileName, int percent);
    void onDownloadComplete(const QString &filePath, const QString &md5, const QString &sha256, bool success);
    void onFileStatus(const QString &transferId, quint32 completed, quint32 total,
                      const QString &status, const QString &errorMsg);
    void onExportLogs();

private:
    void initUI();
    ShipPanel* createShipPanel(int index);
    void addMonitorEvent(const MonitorEvent &event);
    void highlightLogRow(int row);
    void appendGlobalLog(const QString &text, const QString &color = "black");
    QString getChineseStateMachineName(const QString &enName) const;
    QString getChineseDirection(const QString &enDir) const;
    QString getChineseMsgType(const QString &enType) const;
    QString getMsgTypeNumber(const QString &rawMsgType) const;
    void appendToFrameDisplay(int shipIndex, const QString &text, bool isError = false);
    void appendToFrameDisplayAll(const QString &text, bool isError = false);

    // 向服务端提交修改后的面板参数
    void submitPanelParamsToServer(int shipIndex);

    TcpClient *m_tcpClient;
    ShipConnectionManager *m_shipConnMgr;
    MediaPlayer *m_mediaPlayer;
    FileTransferClient *m_fileTransfer;
    ZkServiceDiscovery &m_zk;

    QLabel *m_timeLabel;
    QLabel *m_channelLabel;
    QLabel *m_minuteSlotLabel;
    QLabel *m_connectionStatus;
    SlotProgressCards *m_slotCards;
    QTableView *m_logTableView;
    QStandardItemModel *m_logModel;
    QSortFilterProxyModel *m_logProxy;
    QComboBox *m_filterShipCombo;
    QComboBox *m_filterMachineCombo;
    QComboBox *m_filterTypeCombo;
    QTabWidget *m_shipTabs;
    QList<ShipPanel*> m_shipPanels;
    QStringList m_selectedFilePaths;

    int m_currentSlot;
    int m_prevSlot;
    QList<MonitorEvent> m_allEvents;
};

#endif // WIDGET_H
