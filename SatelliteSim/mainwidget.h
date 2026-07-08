#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <QWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QTableWidget>
#include <QSplitter>
#include "Simulator.h"

class MainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MainWidget(QWidget *parent = nullptr);

private slots:
    void appendLog(const QString& text, const QString& color = "black");
    void updateSlot(int slot, const QString& channel);
    void updateState(const QString& state);
    void onSendBroadcastClicked();
    void onSendAddressedClicked();
    void onSendShortAckClicked();
    void onTableItemDoubleClicked(QTableWidgetItem* item);
    void onUplinkMessageReceived(const QString& type, const QString& summary, const QString& detail);

private:
    void setupUI();
    void addMessageEntry(QTableWidget* table, const QString& type, const QString& summary, const QString& detail);
    void initSimulator();

    QTextBrowser* m_log;
    QLabel* m_slotLabel;
    QLabel* m_channelLabel;
    QLabel* m_stateLabel;
    QTableWidget* m_messageTable;      // 下行发送/混合消息表格
    QTableWidget* m_uplinkTable;       // 上行接收消息专用表格

    // 发送控件
    QLineEdit* m_broadcastEdit;
    QLineEdit* m_addressedShipIdEdit;
    QTextEdit* m_addressedContentEdit;
    QLineEdit* m_shortDestEdit;
    QLineEdit* m_shortDataEdit;
    QComboBox* m_shortTypeCombo;

    Simulator* m_sim;
};

#endif // MAINWIDGET_H