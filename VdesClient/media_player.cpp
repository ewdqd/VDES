#include "media_player.h"
#include <QVBoxLayout>

MediaPlayer::MediaPlayer(QWidget *parent) : QWidget(parent)
{
    m_player = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_videoWidget);
    m_player->setVideoOutput(m_videoWidget);
    setMinimumSize(400, 300);
    hide(); // 默认隐藏
}

void MediaPlayer::playFile(const QString &filePath)
{
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
    show();
}