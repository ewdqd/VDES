#include "media_player_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStyle>
#include <QDebug>

MediaPlayerWidget::MediaPlayerWidget(QWidget *parent)
    : QWidget(parent), m_playing(false)
{
    m_player = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    setupUI();

    // Qt6 信号
    connect(m_player, &QMediaPlayer::positionChanged, this, &MediaPlayerWidget::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MediaPlayerWidget::onDurationChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MediaPlayerWidget::onError);
}

MediaPlayerWidget::~MediaPlayerWidget()
{
    m_player->stop();
}

void MediaPlayerWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->addWidget(m_videoWidget, 1);

    QHBoxLayout *ctrlLayout = new QHBoxLayout;
    ctrlLayout->setSpacing(4);
    m_playPauseBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "");
    m_stopBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "");
    m_positionSlider = new QSlider(Qt::Horizontal);
    m_positionSlider->setRange(0, 100);
    m_timeLabel = new QLabel("00:00/00:00");

    ctrlLayout->addWidget(m_playPauseBtn);
    ctrlLayout->addWidget(m_stopBtn);
    ctrlLayout->addWidget(m_positionSlider);
    ctrlLayout->addWidget(m_timeLabel);
    mainLayout->addLayout(ctrlLayout);

    // 打开文件按钮
    QPushButton *openBtn = new QPushButton("打开文件");
    openBtn->setStyleSheet("QPushButton { background-color: #3b82f6; color: white; "
                           "border: none; border-radius: 4px; padding: 4px 8px; "
                           "font-size: 11px; max-height: 24px; }");
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(this, "选择媒体文件", "",
            "视频文件 (*.mp4 *.avi *.mkv *.flv);;所有文件 (*.*)");
        if (!filePath.isEmpty()) playFile(filePath);
    });
    ctrlLayout->addWidget(openBtn);

    connect(m_playPauseBtn, &QPushButton::clicked, this, &MediaPlayerWidget::onPlayPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MediaPlayerWidget::onStop);
    connect(m_positionSlider, &QSlider::sliderMoved, this, &MediaPlayerWidget::onSliderMoved);
}

void MediaPlayerWidget::playFile(const QString &filePath)
{
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
    m_playing = true;
    m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
}

void MediaPlayerWidget::onPlayPause()
{
    if (m_playing) {
        m_player->pause();
        m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    } else {
        m_player->play();
        m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    }
    m_playing = !m_playing;
}

void MediaPlayerWidget::onStop()
{
    m_player->stop();
    m_playing = false;
    m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_positionSlider->setValue(0);
}

void MediaPlayerWidget::onPositionChanged(qint64 pos)
{
    if (!m_positionSlider->isSliderDown()) {
        qint64 duration = m_player->duration();
        if (duration > 0)
            m_positionSlider->setValue(int(pos * 100 / duration));
    }
    qint64 total = m_player->duration();
    QString timeStr = QString("%1/%2")
                          .arg(QTime(0,0).addMSecs(pos).toString("mm:ss"))
                          .arg(QTime(0,0).addMSecs(total).toString("mm:ss"));
    m_timeLabel->setText(timeStr);
}

void MediaPlayerWidget::onDurationChanged(qint64 duration) { Q_UNUSED(duration); }

void MediaPlayerWidget::onSliderMoved(int value)
{
    qint64 pos = value * m_player->duration() / 100;
    m_player->setPosition(pos);
}

void MediaPlayerWidget::onError(QMediaPlayer::Error error, const QString &errorString)
{
    qWarning() << "MediaPlayer error:" << error << errorString;
}
