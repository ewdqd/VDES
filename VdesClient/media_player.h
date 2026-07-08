#ifndef MEDIA_PLAYER_H
#define MEDIA_PLAYER_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>

class MediaPlayer : public QWidget
{
    Q_OBJECT
public:
    explicit MediaPlayer(QWidget *parent = nullptr);
    void playFile(const QString &filePath);
private:
    QMediaPlayer *m_player;
    QVideoWidget *m_videoWidget;
};

#endif