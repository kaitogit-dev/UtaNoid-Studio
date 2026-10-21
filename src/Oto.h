#pragma once
#include "Wav.h"
#include <QString>

struct Oto {
    double offset = 0;
    double consonant = 0;
    double cutoff = 0;
    double preutterance = 0;
    double overlap = 0;
    double confidence = 0;
    bool analyzed = false;
    QString engine;
};

class OtoAnalyzer {
public:
    static Oto analyze(const Wav &wav, const QString &alias);
    static bool valid(const Oto &oto, double durationMs);
};
