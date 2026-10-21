#pragma once
#include "Oto.h"
#include <QByteArray>
#include <QString>
#include <QVector>

struct AiInput {
    QString alias;
    QString kana;
    QString audioPath;
};

struct AiResult {
    QString alias;
    Oto oto;
    QString error;
};

struct AiBatch {
    QVector<AiResult> results;
    QString error;
};

class AiOto {
public:
    static Oto fromTextGrid(const QByteArray &text, const Wav &audio, const QString &alias);
    static AiBatch generate(const QVector<AiInput> &samples);
};
