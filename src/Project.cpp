#include "Project.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QByteArray legacyText(const QString &value) {
#ifdef Q_OS_WIN
    const int count = WideCharToMultiByte(932, 0, reinterpret_cast<LPCWCH>(value.utf16()), value.size(), nullptr, 0, nullptr, nullptr);
    if (count > 0) {
        QByteArray encoded(count, '\0');
        WideCharToMultiByte(932, 0, reinterpret_cast<LPCWCH>(value.utf16()), value.size(), encoded.data(), count, nullptr, nullptr);
        return encoded;
    }
#endif
    return value.toUtf8();
}

bool writeBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(bytes) == bytes.size() && file.commit();
}

QString cleanName(QString name) {
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), "_");
    while (name.endsWith('.')) name.chop(1);
    return name;
}
}

QVector<Sample> Project::japaneseCv() {
    const QString content = QString::fromUtf8(
        "a:あ i:い u:う e:え o:お "
        "ka:か ki:き ku:く ke:け ko:こ "
        "sa:さ shi:し su:す se:せ so:そ "
        "ta:た chi:ち tsu:つ te:て to:と "
        "na:な ni:に nu:ぬ ne:ね no:の "
        "ha:は hi:ひ fu:ふ he:へ ho:ほ "
        "ma:ま mi:み mu:む me:め mo:も "
        "ya:や yu:ゆ yo:よ "
        "ra:ら ri:り ru:る re:れ ro:ろ "
        "wa:わ wo:を n:ん "
        "ga:が gi:ぎ gu:ぐ ge:げ go:ご "
        "za:ざ ji:じ zu:ず ze:ぜ zo:ぞ "
        "da:だ di:ぢ du:づ de:で do:ど "
        "ba:ば bi:び bu:ぶ be:べ bo:ぼ "
        "pa:ぱ pi:ぴ pu:ぷ pe:ぺ po:ぽ "
        "kya:きゃ kyu:きゅ kyo:きょ "
        "sha:しゃ shu:しゅ sho:しょ "
        "cha:ちゃ chu:ちゅ cho:ちょ "
        "nya:にゃ nyu:にゅ nyo:にょ "
        "hya:ひゃ hyu:ひゅ hyo:ひょ "
        "mya:みゃ myu:みゅ myo:みょ "
        "rya:りゃ ryu:りゅ ryo:りょ "
        "gya:ぎゃ gyu:ぎゅ gyo:ぎょ "
        "ja:じゃ ju:じゅ jo:じょ "
        "bya:びゃ byu:びゅ byo:びょ "
        "pya:ぴゃ pyu:ぴゅ pyo:ぴょ");
    QVector<Sample> result;
    for (const QString &entry : content.split(' ', Qt::SkipEmptyParts)) {
        const QStringList pair = entry.split(':');
        if (pair.size() == 2) result.append(Sample{pair[0], pair[1], {}});
    }
    return result;
}

bool Project::create(const QString &parentDirectory, const QString &projectName, QString *error) {
    const QString safeName = cleanName(projectName);
    if (safeName.isEmpty()) { if (error) *error = "Enter a project name"; return false; }
    QDir parent(parentDirectory);
    root = parent.filePath(safeName);
    if (QFileInfo::exists(QDir(root).filePath("project.utanoid"))) {
        if (error) *error = "Project already exists";
        return false;
    }
    if (!QDir().mkpath(QDir(root).filePath("samples"))) {
        if (error) *error = "Unable to create project folder";
        return false;
    }
    name = projectName.trimmed();
    samples = japaneseCv();
    return save(error);
}

QString Project::projectFile() const { return QDir(root).filePath("project.utanoid"); }
QString Project::audioFile(int index) const {
    if (index < 0 || index >= samples.size()) return {};
    return QDir(root).filePath("samples/" + samples[index].alias + ".wav");
}
bool Project::hasAudio(int index) const { return QFileInfo::exists(audioFile(index)); }

bool Project::save(QString *error) const {
    if (root.isEmpty()) { if (error) *error = "No project open"; return false; }
    QJsonObject object;
    object["format"] = 1;
    object["name"] = name;
    QJsonArray items;
    for (const Sample &s : samples) {
        QJsonObject entry;
        entry["alias"] = s.alias;
        entry["kana"] = s.kana;
        entry["offset"] = s.oto.offset;
        entry["consonant"] = s.oto.consonant;
        entry["cutoff"] = s.oto.cutoff;
        entry["preutterance"] = s.oto.preutterance;
        entry["overlap"] = s.oto.overlap;
        entry["confidence"] = s.oto.confidence;
        entry["analyzed"] = s.oto.analyzed;
        entry["engine"] = s.oto.engine;
        items.append(entry);
    }
    object["samples"] = items;
    if (!writeBytes(projectFile(), QJsonDocument(object).toJson(QJsonDocument::Indented))) {
        if (error) *error = "Unable to save project";
        return false;
    }
    return true;
}

bool Project::open(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "Unable to open project"; return false; }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject object = doc.object();
    if (parseError.error != QJsonParseError::NoError || object.value("format").toInt() != 1 ||
        !object.value("samples").isArray()) {
        if (error) *error = "Invalid project file";
        return false;
    }
    QVector<Sample> parsed;
    for (const QJsonValue &value : object.value("samples").toArray()) {
        const QJsonObject entry = value.toObject();
        Sample sample;
        sample.alias = entry.value("alias").toString();
        sample.kana = entry.value("kana").toString();
        if (sample.alias.isEmpty() || sample.alias.contains('/') || sample.alias.contains('\\') ||
            sample.alias.contains("..")) continue;
        sample.oto.offset = entry.value("offset").toDouble();
        sample.oto.consonant = entry.value("consonant").toDouble();
        sample.oto.cutoff = entry.value("cutoff").toDouble();
        sample.oto.preutterance = entry.value("preutterance").toDouble();
        sample.oto.overlap = entry.value("overlap").toDouble();
        sample.oto.confidence = entry.value("confidence").toDouble();
        sample.oto.analyzed = entry.value("analyzed").toBool();
        sample.oto.engine = entry.value("engine").toString();
        parsed.append(sample);
    }
    if (parsed.isEmpty()) { if (error) *error = "No samples in project"; return false; }
    root = QFileInfo(path).absolutePath();
    name = object.value("name").toString(QFileInfo(root).fileName());
    samples = parsed;
    return true;
}

bool Project::importAudio(int index, const QString &source, QString *error) {
    if (index < 0 || index >= samples.size()) return false;
    const Wav wav = Wav::load(source);
    if (!wav.valid()) { if (error) *error = wav.error; return false; }
    const QString target = audioFile(index);
    if (QFileInfo(source).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
        const QString takes = QDir(root).filePath("takes");
        if (QFileInfo::exists(target)) {
            if (!QDir().mkpath(takes)) { if (error) *error = "Unable to save previous take"; return false; }
            const QString backup = QDir(takes).filePath(samples[index].alias + "-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmsszzz") + ".wav");
            if (!QFile::copy(target, backup)) { if (error) *error = "Unable to back up previous take"; return false; }
        }
        QSaveFile dest(target);
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly) || !dest.open(QIODevice::WriteOnly) ||
            dest.write(input.readAll()) != input.size() || !dest.commit()) {
            if (error) *error = "Unable to import recording";
            return false;
        }
    }
    samples[index].oto = {};
    return save(error);
}

int Project::recordedCount() const {
    int n = 0;
    for (int i = 0; i < samples.size(); ++i) if (hasAudio(i)) ++n;
    return n;
}

bool Project::exportBank(const QString &destination, QString *error) const {
    if (root.isEmpty()) { if (error) *error = "No project open"; return false; }
    if (!QDir().mkpath(destination)) { if (error) *error = "Unable to create export folder"; return false; }
    QByteArray otoText;
    int exported = 0;
    for (int i = 0; i < samples.size(); ++i) {
        if (!hasAudio(i)) continue;
        const Sample &sample = samples[i];
        const Wav wav = Wav::load(audioFile(i));
        if (!wav.valid() || !sample.oto.analyzed || !OtoAnalyzer::valid(sample.oto, wav.durationMs())) {
            if (error) *error = "Invalid OTO for " + sample.alias;
            return false;
        }
        const QString filename = sample.alias + ".wav";
        const QString target = QDir(destination).filePath(filename);
        if (QFileInfo::exists(target) && !QFile::remove(target)) {
            if (error) *error = "Unable to replace " + filename;
            return false;
        }
        if (!QFile::copy(audioFile(i), target)) {
            if (error) *error = "Unable to export " + filename;
            return false;
        }
        const Oto &o = sample.oto;
        const QString row = QString("%1=%2,%3,%4,%5,%6,%7\r\n")
            .arg(filename, sample.alias)
            .arg(o.offset, 0, 'f', 2).arg(o.consonant, 0, 'f', 2)
            .arg(o.cutoff, 0, 'f', 2).arg(o.preutterance, 0, 'f', 2)
            .arg(o.overlap, 0, 'f', 2);
        otoText += row.toUtf8();
        ++exported;
    }
    if (!exported) { if (error) *error = "Record at least one sample"; return false; }
    if (!writeBytes(QDir(destination).filePath("oto.ini"), otoText) ||
        !writeBytes(QDir(destination).filePath("character.txt"), legacyText("name=" + name + "\r\nimage=icon.bmp\r\n"))) {
        if (error) *error = "Unable to write voicebank files";
        return false;
    }
    QImage icon(100, 100, QImage::Format_RGB32);
    icon.fill(QColor("#181d28"));
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor("#91e5d0"));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(58);
    painter.setFont(font);
    painter.drawText(icon.rect(), Qt::AlignCenter, "U");
    painter.end();
    if (!icon.save(QDir(destination).filePath("icon.bmp"), "BMP")) {
        if (error) *error = "Unable to write icon";
        return false;
    }
    return true;
}
