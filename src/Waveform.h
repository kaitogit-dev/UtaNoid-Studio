#pragma once
#include "Oto.h"
#include <QWidget>

class Waveform : public QWidget {
    Q_OBJECT
public:
    explicit Waveform(QWidget *parent = nullptr);
    void setAudio(const Wav &audio);
    void setOto(const Oto &configuration);
    QSize minimumSizeHint() const override;
signals:
    void otoChanged(const Oto &configuration);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    Wav audio;
    Oto oto;
    int activeMarker = -1;
    double markerMs(int index) const;
    int timeToX(double ms) const;
    double xToTime(int x) const;
};
