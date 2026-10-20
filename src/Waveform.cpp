#include "Waveform.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

Waveform::Waveform(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(180);
}

void Waveform::setAudio(const Wav &value) { audio = value; update(); }
void Waveform::setOto(const Oto &value) { oto = value; update(); }
QSize Waveform::minimumSizeHint() const { return QSize(380, 180); }

int Waveform::timeToX(double ms) const {
    return 22 + int((width() - 44) * ms / std::max(1.0, audio.durationMs()));
}

double Waveform::xToTime(int x) const {
    return std::clamp(double(x - 22) / std::max(1, width() - 44) * audio.durationMs(), 0.0, audio.durationMs());
}

double Waveform::markerMs(int index) const {
    if (index == 0) return oto.offset;
    if (index == 1) return oto.offset + oto.overlap;
    if (index == 2) return oto.offset + oto.preutterance;
    if (index == 3) return oto.offset + oto.consonant;
    return audio.durationMs() + oto.cutoff;
}

void Waveform::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#171d27"));
    painter.setPen(QColor("#344051"));
    painter.drawLine(22, height() / 2, width() - 22, height() / 2);
    if (!audio.valid()) {
        painter.setPen(QColor("#7a8899"));
        painter.drawText(rect(), Qt::AlignCenter, "No recording");
        return;
    }
    const int available = std::max(1, width() - 44);
    const int center = height() / 2;
    const int amplitude = (height() - 66) / 2;
    painter.setPen(QPen(QColor("#71cdbb"), 1));
    for (int x = 0; x < available; ++x) {
        const int first = int(qint64(x) * audio.samples.size() / available);
        const int last = std::min(int(audio.samples.size()), int(qint64(x + 1) * audio.samples.size() / available) + 1);
        float lower = 0, upper = 0;
        for (int i = first; i < last; ++i) {
            lower = std::min(lower, audio.samples[i]);
            upper = std::max(upper, audio.samples[i]);
        }
        painter.drawLine(x + 22, center - int(upper * amplitude), x + 22, center - int(lower * amplitude));
    }
    if (!oto.analyzed) return;
    const QColor colors[] = {QColor("#e6bb68"), QColor("#86ce92"), QColor("#eb827e"), QColor("#80a7ed"), QColor("#bc9adf")};
    const QString labels[] = {"Offset", "Overlap", "Pre", "Consonant", "End"};
    for (int i = 0; i < 5; ++i) {
        const int x = timeToX(markerMs(i));
        painter.setPen(QPen(colors[i], i == activeMarker ? 3 : 2));
        painter.drawLine(x, 25, x, height() - 21);
        const int labelX = std::clamp(x - 24, 3, std::max(3, width() - 71));
        painter.fillRect(QRect(labelX, i % 2 ? height() - 20 : 4, 68, 17), QColor("#273243"));
        painter.setPen(colors[i]);
        painter.drawText(QRect(labelX, i % 2 ? height() - 20 : 4, 68, 17), Qt::AlignCenter, labels[i]);
    }
}

void Waveform::mousePressEvent(QMouseEvent *event) {
    if (!audio.valid() || !oto.analyzed || event->button() != Qt::LeftButton) return;
    int closest = 15;
    activeMarker = -1;
    for (int i = 0; i < 5; ++i) {
        const int distance = std::abs(event->position().x() - timeToX(markerMs(i)));
        if (distance < closest) { closest = distance; activeMarker = i; }
    }
    if (activeMarker >= 0) setCursor(Qt::SizeHorCursor);
    update();
}

void Waveform::mouseMoveEvent(QMouseEvent *event) {
    if (activeMarker < 0) return;
    const double time = xToTime(int(event->position().x()));
    const double end = audio.durationMs() + oto.cutoff;
    if (activeMarker == 0) {
        const double newOffset = std::clamp(time, 0.0, end - oto.consonant - 1);
        oto.offset = newOffset;
    } else if (activeMarker == 1) {
        oto.overlap = std::clamp(time - oto.offset, 0.0, oto.preutterance);
    } else if (activeMarker == 2) {
        oto.preutterance = std::clamp(time - oto.offset, oto.overlap, oto.consonant);
    } else if (activeMarker == 3) {
        oto.consonant = std::clamp(time - oto.offset, oto.preutterance, end - oto.offset - 1);
    } else {
        const double newEnd = std::clamp(time, oto.offset + oto.consonant + 1, audio.durationMs() - 1);
        oto.cutoff = newEnd - audio.durationMs();
    }
    emit otoChanged(oto);
    update();
}

void Waveform::mouseReleaseEvent(QMouseEvent *) {
    activeMarker = -1;
    unsetCursor();
    update();
}
