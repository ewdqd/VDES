#ifndef SLOTPROGRESSCARDS_H
#define SLOTPROGRESSCARDS_H

#include <QWidget>
#include <QList>
#include <QRect>
#include <QDateTime>

struct MonitorEvent {
    int slot;
    uint32_t shipId;
    QString stateMachine;   // 中文状态机名称（完整）
    QString direction;      // 方向（用于卡片颜色及悬浮提示）
    QString msgType;        // 中文消息类型（用于卡片）
    QString rawMsgType;     // 原始英文类型，用于转换为数字
    QString summary;
    QString detail;
    QDateTime timestamp;
};

class SlotProgressCards : public QWidget
{
    Q_OBJECT
public:
    explicit SlotProgressCards(QWidget *parent = nullptr);
    void setCurrentSlot(int slot);
    void addEvent(const MonitorEvent &event);
    void clearEvents();
    void refresh();

signals:
    void cardClicked(const MonitorEvent &event);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Card {
        MonitorEvent event;
        QRect rect;
    };
    QList<Card> m_cards;
    int m_currentSlot;
    MonitorEvent m_hoverEvent;

    void drawCards(QPainter &painter);
    int slotToX(int slot) const;
    int estimateCardWidth(const MonitorEvent &event) const;
    QString formatCardText(const MonitorEvent &event) const;
    QColor getCardColor(const MonitorEvent &event) const;
};

#endif // SLOTPROGRESSCARDS_H