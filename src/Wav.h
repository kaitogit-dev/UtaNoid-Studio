#pragma once
#include <QString>
#include <QVector>

struct Wav {
    QVector<float> samples;
    int sampleRate = 0;
    int channels = 0;
    QString error;
    bool valid() const { return sampleRate > 0 && !samples.isEmpty(); }
    double durationMs() const { return valid() ? samples.size() * 1000.0 / sampleRate : 0.0; }
    static Wav load(const QString &path);
};
