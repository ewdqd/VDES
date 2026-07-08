#ifndef MEDIA_PLAYER_WIDGET_H
#define MEDIA_PLAYER_WIDGET_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

class MediaPlayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MediaPlayerWidget(QWidget *parent = nullptr);
    ~MediaPlayerWidget();

    void playFile(const QString &filePath);

private slots:
    void onPlayPause();
    void onStop();
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int value);
    void onError(QMediaPlayer::Error error, const QString &errorString);

private:
    void setupUI();

    QMediaPlayer *m_player;
    QVideoWidget *m_videoWidget;
    QSlider *m_positionSlider;
    QLabel *m_timeLabel;
    QPushButton *m_playPauseBtn;
    QPushButton *m_stopBtn;
    bool m_playing;
};

#endif // MEDIA_PLAYER_WIDGET_H