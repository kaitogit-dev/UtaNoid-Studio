#include "Oto.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
struct Frame {
    double rms = 0;
    double zcr = 0;
    double voicing = 0;
    double flux = 0;
};

double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty()) return 0;
    const int index = std::clamp(int(fraction * (values.size() - 1)), 0, int(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

bool isFricative(const QString &alias) {
    return alias.startsWith("s") || alias.startsWith("sh") || alias.startsWith("z") ||
           alias.startsWith("j") || alias.startsWith("f") || alias.startsWith("h");
}

bool isVowel(const QString &alias) {
    return alias == "a" || alias == "i" || alias == "u" || alias == "e" || alias == "o" || alias == "n";
}
}

Oto OtoAnalyzer::analyze(const Wav &wav, const QString &alias) {
    Oto oto;
    if (!wav.valid() || wav.durationMs() < 100) return oto;
    const int rate = wav.sampleRate;
    const int hop = std::max(1, int(rate * 0.005));
    const int width = std::max(1, int(rate * 0.025));
    QVector<Frame> frames;
    frames.reserve(wav.samples.size() / hop);
    const int n = wav.samples.size();
    for (int start = 0; start + width <= n; start += hop) {
        double energy = 0, changes = 0;
        for (int i = start; i < start + width; ++i) {
            const double x = wav.samples[i];
            energy += x * x;
            if (i > start && ((x >= 0) != (wav.samples[i - 1] >= 0))) ++changes;
        }
        Frame frame;
        frame.rms = std::sqrt(energy / width);
        frame.zcr = changes / width;
        const int decimation = std::max(1, rate / 12000);
        const int count = width / decimation;
        const int minLag = std::max(1, int(rate / 500.0 / decimation));
        const int maxLag = std::min(count / 2, int(rate / 75.0 / decimation));
        double best = 0;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double dot = 0, a2 = 0, b2 = 0;
            for (int k = 0; k + lag < count; ++k) {
                const double a = wav.samples[start + k * decimation];
                const double b = wav.samples[start + (k + lag) * decimation];
                dot += a * b;
                a2 += a * a;
                b2 += b * b;
            }
            if (a2 > 1e-9 && b2 > 1e-9) best = std::max(best, dot / std::sqrt(a2 * b2));
        }
        frame.voicing = best;
        if (!frames.isEmpty()) frame.flux = std::abs(frame.rms - frames.last().rms);
        frames.append(frame);
    }
    if (frames.size() < 12) return oto;
    QVector<double> levels;
    for (const auto &f : frames) levels.append(f.rms);
    const double noise = percentile(levels, 0.10);
    const double peak = percentile(levels, 0.97);
    if (peak < 0.003 || peak < noise * 2.2) return oto;
    const double threshold = std::max({0.0025, noise * 2.8, peak * 0.055});
    int first = -1, last = -1;
    for (int i = 0; i + 2 < frames.size(); ++i) {
        if (frames[i].rms > threshold && frames[i + 1].rms > threshold && frames[i + 2].rms > threshold) {
            if (first < 0) first = i;
            last = i + 2;
        }
    }
    if (first < 0 || last - first < 8) return oto;
    const double hopMs = hop * 1000.0 / rate;
    const double soundStart = first * hopMs;
    const double soundEnd = std::min(wav.durationMs(), (last + 1) * hopMs + 15);
    const bool vowel = isVowel(alias);
    int vowelStart = first;
    if (!vowel) {
        const int scanEnd = std::min(last, first + int(300 / hopMs));
        double bestScore = -1;
        for (int i = first + 2; i + 4 < scanEnd; ++i) {
            double periodicity = 0, energy = 0, zcr = 0;
            for (int j = i; j < i + 4; ++j) {
                periodicity += frames[j].voicing;
                energy += frames[j].rms;
                zcr += frames[j].zcr;
            }
            periodicity /= 4;
            energy /= 4;
            zcr /= 4;
            const double score = periodicity * 0.72 + std::min(1.0, energy / (peak + 1e-8)) * 0.28 - zcr * 0.25;
            if (periodicity > 0.62 && energy > threshold * 1.5 && score > bestScore) {
                bestScore = score;
                vowelStart = i;
                break;
            }
        }
        if (vowelStart == first) {
            int strongest = first;
            double score = -1;
            for (int i = first; i < scanEnd; ++i) {
                const double s = frames[i].voicing * frames[i].rms / (peak + 1e-8);
                if (s > score) { score = s; strongest = i; }
            }
            vowelStart = std::max(first, strongest - int(25 / hopMs));
        }
    }
    const double vowelTime = std::min(soundEnd - 35, vowelStart * hopMs);
    oto.offset = std::max(0.0, soundStart - (vowel ? 8.0 : 12.0));
    oto.preutterance = std::max(5.0, vowelTime - oto.offset);
    if (!vowel) oto.preutterance = std::max(18.0, oto.preutterance);
    oto.preutterance = std::min(oto.preutterance, std::max(8.0, soundEnd - oto.offset - 45));
    oto.overlap = vowel ? std::min(oto.preutterance, 12.0) : (isFricative(alias) ? oto.preutterance * 0.35 : oto.preutterance * 0.18);
    oto.consonant = std::min(soundEnd - oto.offset - 20, oto.preutterance + (vowel ? 95.0 : 45.0));
    oto.consonant = std::max(oto.preutterance, oto.consonant);
    const double end = std::max(oto.offset + oto.consonant + 10, soundEnd - 12);
    oto.cutoff = std::min(-1.0, end - wav.durationMs());
    const double dynamic = std::clamp((peak - noise) / (peak + 1e-9), 0.0, 1.0);
    const double usable = std::clamp((soundEnd - soundStart) / 400.0, 0.0, 1.0);
    const double evidence = frames[std::clamp(vowelStart, 0, int(frames.size()) - 1)].voicing;
    oto.confidence = std::clamp(0.25 * dynamic + 0.3 * usable + 0.45 * evidence, 0.0, 1.0);
    oto.analyzed = valid(oto, wav.durationMs());
    return oto;
}

bool OtoAnalyzer::valid(const Oto &o, double durationMs) {
    return durationMs >= 100 && o.offset >= 0 && o.consonant >= o.preutterance &&
           o.preutterance >= o.overlap && o.overlap >= 0 && o.cutoff < 0 &&
           o.offset + o.consonant < durationMs + o.cutoff && durationMs + o.cutoff > 0;
}
