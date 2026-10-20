#include "Project.h"
#include "Wav.h"
#include "Oto.h"
#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstdio>

namespace {
void write16(QByteArray &data, quint16 value) {
    char bytes[2];
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar *>(bytes));
    data.append(bytes, 2);
}

void write32(QByteArray &data, quint32 value) {
    char bytes[4];
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar *>(bytes));
    data.append(bytes, 4);
}

bool synthesize(const QString &path) {
    const int rate = 24000;
    const int frames = rate;
    QByteArray bytes;
    bytes.append("RIFF", 4);
    write32(bytes, 36 + frames * 2);
    bytes.append("WAVEfmt ", 8);
    write32(bytes, 16);
    write16(bytes, 1);
    write16(bytes, 1);
    write32(bytes, rate);
    write32(bytes, rate * 2);
    write16(bytes, 2);
    write16(bytes, 16);
    bytes.append("data", 4);
    write32(bytes, frames * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / rate;
        const double envelope = t < 0.12 || t > 0.82 ? 0 : std::min(1.0, (t - 0.12) * 18);
        write16(bytes, quint16(qint16(std::sin(t * 2 * 3.141592653589793 * 220) * 18000 * envelope)));
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const QString source = temp.filePath("input.wav");
    if (!synthesize(source)) return 2;
    const Wav wav = Wav::load(source);
    if (!wav.valid() || wav.sampleRate != 24000 || std::abs(wav.durationMs() - 1000) > 1) return 3;
    const Oto result = OtoAnalyzer::analyze(wav, "a");
    if (!result.analyzed || !OtoAnalyzer::valid(result, wav.durationMs())) return 4;
    Project project;
    QString error;
    if (!project.create(temp.path(), "Singer", &error) || project.samples.size() < 90) return 5;
    if (!project.importAudio(0, source, &error) || project.recordedCount() != 1) return 6;
    Project restored;
    if (!restored.open(project.projectFile(), &error) || restored.recordedCount() != 1) return 7;
    const QString exportPath = temp.filePath("output");
    if (!restored.exportBank(exportPath, &error)) return 8;
    if (!QFile::exists(exportPath + "/oto.ini") || !QFile::exists(exportPath + "/a.wav") ||
        !QFile::exists(exportPath + "/character.txt") || !QFile::exists(exportPath + "/icon.bmp")) return 9;
    QFile otoFile(exportPath + "/oto.ini");
    if (!otoFile.open(QIODevice::ReadOnly) || !otoFile.readAll().startsWith("a.wav=a,")) return 10;
    return 0;
}
