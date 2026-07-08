#include "slotprogresscards.h"
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QHelpEvent>

SlotProgressCards::SlotProgressCards(QWidget *parent)
    : QWidget(parent), m_currentSlot(0)
{
    setMinimumHeight(60);
    setMaximumHeight(80);
    setStyleSheet("background-color: #e2e8f0; border-radius: 6px;");
    setMouseTracking(true);
}

void SlotProgressCards::setCurrentSlot(int slot)
{
    m_currentSlot = slot % 2250;
    update();
}

void SlotProgressCards::addEvent(const MonitorEvent &event)
{
    Card card{event, QRect()};
    m_cards.append(card);
    update();   // 触发重绘，卡片位置会在 paintEvent 中重新计算
}

void SlotProgressCards::clearEvents()
{
    m_cards.clear();
    update();
}

void SlotProgressCards::refresh()
{
    update();
}

void SlotProgressCards::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#e2e8f0"));

    QRect barRect(10, height()/2 - 6, width()-20, 12);
    int currentX = slotToX(m_currentSlot);
    if (currentX > 10) {
        QRect passedRect(10, barRect.y(), currentX - 10, barRect.height());
        painter.fillRect(passedRect, QColor("#3b82f6"));
    }
    if (currentX < width() - 10) {
        QRect futureRect(currentX, barRect.y(), width() - 10 - currentX, barRect.height());
        painter.fillRect(futureRect, QColor("#cbd5e1"));
    }
    painter.fillRect(currentX-2, barRect.y()-3, 4, barRect.height()+6, QColor("#2563eb"));

    drawCards(painter);
}

void SlotProgressCards::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    bool found = false;
    for (const Card &card : m_cards) {
        if (card.rect.contains(pos)) {
            found = true;
            if (m_hoverEvent.stateMachine != card.event.stateMachine ||
                m_hoverEvent.slot != card.event.slot) {
                m_hoverEvent = card.event;
                QToolTip::showText(mapToGlobal(pos),
                                   QString("时隙: %1\n船舶: %2\n状态机: %3\n类型: %4\n详情: %5")
                                       .arg(card.event.slot).arg(card.event.shipId)
                                       .arg(card.event.stateMachine)
                                       .arg(card.event.msgType).arg(card.event.detail.left(100)));
            }
            break;
        }
    }
    if (!found) QToolTip::hideText();
    QWidget::mouseMoveEvent(event);
}

void SlotProgressCards::mousePressEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    for (const Card &card : m_cards) {
        if (card.rect.contains(pos)) {
            emit cardClicked(card.event);
            break;
        }
    }
    QWidget::mousePressEvent(event);
}

bool SlotProgressCards::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *helpEvent = static_cast<QHelpEvent*>(event);
        QPoint pos = helpEvent->pos();
        for (const Card &card : m_cards) {
            if (card.rect.contains(pos)) {
                QToolTip::showText(helpEvent->globalPos(),
                                   QString("时隙: %1\n船舶: %2\n状态机: %3\n类型: %4\n摘要: %5")
                                       .arg(card.event.slot).arg(card.event.shipId)
                                       .arg(card.event.stateMachine)
                                       .arg(card.event.msgType).arg(card.event.summary));
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return false;
    }
    return QWidget::event(event);
}

QString SlotProgressCards::formatCardText(const MonitorEvent &event) const
{
    // 保留备用
    return QString("船舶%1 %2 %3")
        .arg(event.shipId)
        .arg(event.stateMachine)
        .arg(event.msgType);
}

QColor SlotProgressCards::getCardColor(const MonitorEvent &event) const
{
    QString machine = event.stateMachine;
    // 根据状态机名称（中文）决定颜色
    if (machine.contains("上行寻址")) return QColor("#3b82f6");   // 蓝色
    if (machine.contains("下行寻址")) return QColor("#10b981");   // 绿色
    if (machine.contains("广播")) return QColor("#f59e0b");       // 橙色
    if (machine.contains("寻呼")) return QColor("#8b5cf6");       // 紫色
    if (machine.contains("短消息")) return QColor("#ec4898");     // 粉红
    if (machine.contains("文件")) return QColor("#14b8a6");       // 青绿
    return QColor("#6b7280");                                     // 灰色
}

void SlotProgressCards::drawCards(QPainter &painter)
{
    if (width() <= 0) return;
    if (m_cards.isEmpty()) return;

    for (Card &card : m_cards) {
        // 计算卡片中心 X 坐标（对应时隙位置）
        int centerX = slotToX(card.event.slot);
        int cardWidth = estimateCardWidth(card.event);
        // 避免卡片超出左右边界
        int x = centerX - cardWidth / 2;
        x = qBound(10, x, width() - cardWidth - 10);
        // 如果计算出的 x 仍然无效，跳过绘制
        if (x + cardWidth > width() || x < 0) continue;
        QRect rect(x, height()/2 - 30, cardWidth, 60);
        card.rect = rect;   // 保存用于鼠标交互

        painter.setBrush(getCardColor(card.event));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect, 6, 6);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);

        // 第一行：船舶ID + 状态机（中文）
        QString firstLine = QString("船舶%1 %2").arg(card.event.shipId).arg(card.event.stateMachine);
        // 第二行：只显示消息类型（中文），不再显示方向
        QString secondLine = card.event.msgType;
        QString text = firstLine + "\n" + secondLine;

        painter.drawText(rect.adjusted(4, 4, -4, -4),
                         Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                         text);
    }
}

int SlotProgressCards::slotToX(int slot) const
{
    int totalSlots = 2250;
    double ratio = static_cast<double>(slot) / totalSlots;
    int usableWidth = width() - 20;
    return 10 + static_cast<int>(ratio * usableWidth);
}

int SlotProgressCards::estimateCardWidth(const MonitorEvent &event) const
{
    QString firstLine = QString("船舶%1 %2").arg(event.shipId).arg(event.stateMachine);
    QString secondLine = event.msgType;
    int len = qMax(firstLine.length(), secondLine.length());
    // 提高最小宽度，确保卡片可见
    return qBound(100, len * 8, 180);
}