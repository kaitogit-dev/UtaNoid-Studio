#include "Wav.h"
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
quint16 u16(const char *p) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p)); }
quint32 u32(const char *p) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p)); }
}

Wav Wav::load(const QString &path) {
    Wav wav;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { wav.error = "Unable to open WAV"; return wav; }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || bytes.left(4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
        wav.error = "Not a RIFF WAV file";
        return wav;
    }
    quint16 format = 0, bits = 0, blockAlign = 0;
    quint32 rate = 0;
    int dataStart = -1, dataSize = 0;
    int p = 12;
    while (p + 8 <= bytes.size()) {
        const quint32 chunkSize = u32(bytes.constData() + p + 4);
        const qint64 end = qint64(p) + 8 + chunkSize;
        if (end > bytes.size()) break;
        const QByteArray tag = bytes.mid(p, 4);
        if (tag == "fmt " && chunkSize >= 16) {
            const char *f = bytes.constData() + p + 8;
            format = u16(f);
            wav.channels = u16(f + 2);
            rate = u32(f + 4);
            blockAlign = u16(f + 12);
            bits = u16(f + 14);
            if (format == 0xfffe && chunkSize >= 40) format = u16(f + 24);
        }
        if (tag == "data" && dataStart < 0) {
            dataStart = p + 8;
            dataSize = int(chunkSize);
        }
        p = int(end + (chunkSize & 1));
    }
    const int sampleBytes = bits / 8;
    if (dataStart < 0 || (format != 1 && format != 3) || (bits != 16 && bits != 24 && bits != 32) ||
        (format == 3 && bits != 32) || wav.channels < 1 || wav.channels > 8 ||
        rate < 8000 || rate > 192000 || blockAlign != sampleBytes * wav.channels) {
        wav.error = "Unsupported WAV encoding";
        return wav;
    }
    const int frames = std::min(dataSize, bytes.size() - dataStart) / blockAlign;
    if (frames == 0) { wav.error = "WAV contains no samples"; return wav; }
    wav.sampleRate = int(rate);
    wav.samples.resize(frames);
    const char *raw = bytes.constData() + dataStart;
    for (int i = 0; i < frames; ++i) {
        double total = 0;
        for (int c = 0; c < wav.channels; ++c) {
            const char *s = raw + i * blockAlign + c * sampleBytes;
            float value = 0;
            if (format == 3) {
                const quint32 bitPattern = u32(s);
                std::memcpy(&value, &bitPattern, sizeof(value));
            } else if (bits == 16) {
                value = float(qint16(u16(s))) / 32768.0f;
            } else if (bits == 24) {
                qint32 v = quint8(s[0]) | (quint8(s[1]) << 8) | (quint8(s[2]) << 16);
                if (v & 0x800000) v |= ~0xffffff;
                value = float(v) / 8388608.0f;
            } else {
                value = float(qint32(u32(s))) / 2147483648.0f;
            }
            if (!std::isfinite(value)) value = 0;
            total += value;
        }
        wav.samples[i] = float(total / wav.channels);
    }
    return wav;
}
