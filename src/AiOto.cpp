#include "AiOto.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
struct Phone {
    double start = 0;
    double end = 0;
    QString text;
};

QVector<Phone> parsePhones(const QByteArray &grid) {
    QVector<Phone> phones;
    const QStringList lines = QString::fromUtf8(grid).split('\n');
    bool inPhones = false;
    bool interval = false;
    Phone current;
    auto flush = [&] {
        if (interval && current.end > current.start && std::isfinite(current.end) &&
            std::isfinite(current.start)) phones.append(current);
        interval = false;
        current = {};
    };
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith("item [")) {
            if (inPhones) { flush(); break; }
            continue;
        }
        if (line.startsWith("name = ")) {
            inPhones = line.mid(7).trimmed() == "\"phones\"";
            continue;
        }
        if (!inPhones) continue;
        if (line.startsWith("intervals [")) {
            flush();
            interval = true;
            continue;
        }
        if (!interval) continue;
        if (line.startsWith("xmin = ")) current.start = line.mid(7).toDouble();
        else if (line.startsWith("xmax = ")) current.end = line.mid(7).toDouble();
        else if (line.startsWith("text = ")) {
            current.text = line.mid(7).trimmed();
            if (current.text.startsWith('"') && current.text.endsWith('"'))
                current.text = current.text.mid(1, current.text.size() - 2);
        }
    }
    if (inPhones) flush();
    return phones;
}

bool silence(const QString &phone) {
    return phone.isEmpty() || phone == "sil" || phone == "sp" || phone == "spn" ||
           phone == "eps" || phone == "<eps>" || phone == "SIL" || phone == "SILENCE";
}

bool vowel(const QString &phone) {
    const QString normalized = phone.normalized(QString::NormalizationForm_D).remove(QChar(0x0325));
    return normalized == "a" || normalized == "i" || normalized == "u" ||
           normalized == "e" || normalized == "o" || normalized == "ɯ" ||
           normalized == "ɨ" || normalized == "aː" || normalized == "iː" ||
           normalized == "eː" || normalized == "oː" || normalized == "ɯː" ||
           normalized == "ɨː";
}

bool vowelAlias(const QString &alias) {
    return alias == "a" || alias == "i" || alias == "u" || alias == "e" || alias == "o";
}

bool fricative(const QString &alias) {
    return alias.startsWith("s") || alias.startsWith("sh") || alias.startsWith("z") ||
           alias.startsWith("j") || alias.startsWith("f") || alias.startsWith("h");
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

bool run(const QString &program, const QStringList &args, QString *error, int timeout = 1800000) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    if (!process.waitForStarted(120000)) {
        if (error) *error = "AI engine could not start: " + process.errorString();
        return false;
    }
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(3000);
        if (error) *error = "AI engine did not finish";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QString::fromUtf8(process.readAll()).right(1600).trimmed();
        if (error && error->isEmpty()) *error = "AI engine failed";
        return false;
    }
    return true;
}

#ifdef Q_OS_WIN
bool download(const QUrl &url, const QString &path, QString *error) {
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network.get(request);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        reply->deleteLater();
        if (error) *error = "Unable to store AI installer";
        return false;
    }
    QObject::connect(reply, &QNetworkReply::readyRead, &network, [&] {
        file.write(reply->readAll());
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    file.write(reply->readAll());
    file.close();
    const bool ok = reply->error() == QNetworkReply::NoError &&
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    if (!ok && error) *error = "AI download failed: " + reply->errorString();
    reply->deleteLater();
    if (!ok) QFile::remove(path);
    return ok;
}

QString aiExecutable(QString *error) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai";
    const QString miniforge = base + "/miniforge";
    const QString env = base + "/runtime";
    const QString conda = miniforge + "/Scripts/conda.exe";
    const QString mfa = env + "/Scripts/mfa.exe";
    if (!QDir().mkpath(base)) {
        if (error) *error = "Unable to create AI directory";
        return {};
    }
    if (!QFileInfo::exists(conda)) {
        const QString installer = base + "/Miniforge3-Windows-x86_64.exe";
        const QString checksum = base + "/installer.sha256";
        const QUrl baseUrl("https://github.com/conda-forge/miniforge/releases/latest/download/");
        if (!download(baseUrl.resolved(QUrl("Miniforge3-Windows-x86_64.exe.sha256")), checksum, error)) return {};
        QFile sumFile(checksum);
        if (!sumFile.open(QIODevice::ReadOnly)) {
            if (error) *error = "Unable to verify AI installer";
            return {};
        }
        const QByteArray expected = sumFile.readAll().left(64).toLower();
        if (!QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(expected)).hasMatch()) {
            if (error) *error = "Invalid AI installer checksum";
            return {};
        }
        if (!download(baseUrl.resolved(QUrl("Miniforge3-Windows-x86_64.exe")), installer, error)) return {};
        QFile installedFile(installer);
        if (!installedFile.open(QIODevice::ReadOnly)) {
            if (error) *error = "Unable to verify AI installer";
            return {};
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&installedFile) || hash.result().toHex() != expected) {
            QFile::remove(installer);
            if (error) *error = "AI installer verification failed";
            return {};
        }
        installedFile.close();
        if (!run(installer, {"/InstallationType=JustMe", "/RegisterPython=0", "/AddToPath=0",
                              "/S", "/D=" + QDir::toNativeSeparators(miniforge)}, error, 900000)) return {};
        QFile::remove(installer);
    }
    if (!QFileInfo::exists(mfa)) {
        if (!run(conda, {"create", "--prefix", env, "--channel", "conda-forge", "--yes",
                         "montreal-forced-aligner"}, error)) return {};
    }
    for (const QString &type : {"acoustic", "dictionary", "g2p"}) {
        const QString marker = base + "/ready-" + type;
        if (QFileInfo::exists(marker)) continue;
        if (!run(conda, {"run", "--prefix", env, "mfa", "model", "download", type, "japanese_mfa"}, error)) return {};
        if (!writeFile(marker, "ready")) {
            if (error) *error = "Unable to save AI setup";
            return {};
        }
    }
    return conda;
}
#else
QString aiExecutable(QString *error) {
    const QString mfa = QStandardPaths::findExecutable("mfa");
    if (mfa.isEmpty() && error) *error = "MFA is not installed";
    return mfa;
}
#endif
}

Oto AiOto::fromTextGrid(const QByteArray &text, const Wav &audio, const QString &alias) {
    Oto o;
    if (!audio.valid() || audio.durationMs() < 160) return o;
    const QVector<Phone> parsed = parsePhones(text);
    QVector<Phone> phones;
    for (const Phone &p : parsed) if (!silence(p.text)) phones.append(p);
    if (phones.isEmpty() || phones.size() > 5) return o;
    if (!vowelAlias(alias) && alias != "n" && phones.size() < 2) return o;
    int nucleus = -1;
    if (alias == "n") nucleus = 0;
    else for (int i = 0; i < phones.size(); ++i)
        if (vowel(phones[i].text)) { nucleus = i; break; }
    if (nucleus < 0 || (nucleus == 0 && !vowelAlias(alias) && alias != "n")) return o;
    const double start = phones.first().start * 1000.0;
    const double vocal = phones[nucleus].start * 1000.0;
    const double vocalEnd = phones[nucleus].end * 1000.0;
    const double end = phones.last().end * 1000.0;
    if (start < 0 || end > audio.durationMs() + 12 || vocalEnd - vocal < 65 ||
        end - start < 120 || vocal - start > 550) return {};
    o.offset = std::max(0.0, start - 8.0);
    o.preutterance = std::max(8.0, vocal - o.offset);
    o.overlap = std::min(o.preutterance, vowelAlias(alias) || alias == "n" ? 12.0 :
                         (fricative(alias) ? o.preutterance * 0.32 : o.preutterance * 0.20));
    const double steady = std::clamp((vocalEnd - vocal) * 0.20, 35.0, 90.0);
    o.consonant = o.preutterance + steady;
    const double stop = std::min(audio.durationMs() - 3.0, end - 8.0);
    o.cutoff = std::min(-1.0, stop - audio.durationMs());
    o.confidence = 0;
    o.analyzed = OtoAnalyzer::valid(o, audio.durationMs());
    if (o.analyzed) o.engine = "mfa-japanese";
    return o;
}

AiBatch AiOto::generate(const QVector<AiInput> &samples) {
    AiBatch result;
    if (samples.isEmpty()) return result;
    QString error;
    const QString tool = aiExecutable(&error);
    if (tool.isEmpty()) { result.error = error; return result; }
    QTemporaryDir temp;
    if (!temp.isValid() || !QDir().mkpath(temp.filePath("corpus")) ||
        !QDir().mkpath(temp.filePath("aligned"))) {
        result.error = "Unable to create AI workspace";
        return result;
    }
    for (int i = 0; i < samples.size(); ++i) {
        const QString stem = QString("sample_%1").arg(i, 4, 10, QChar('0'));
        if (!QFile::copy(samples[i].audioPath, temp.filePath("corpus/" + stem + ".wav")) ||
            !writeFile(temp.filePath("corpus/" + stem + ".lab"), samples[i].kana.toUtf8())) {
            result.error = "Unable to prepare audio for AI";
            return result;
        }
    }
#ifdef Q_OS_WIN
    const QString env = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai/runtime";
    const QStringList command = {"run", "--prefix", env, "mfa", "align",
                                 temp.filePath("corpus"), "japanese_mfa", "japanese_mfa",
                                 temp.filePath("aligned"), "--g2p_model_path", "japanese_mfa",
                                 "--single_speaker", "--clean", "-j", "2"};
#else
    const QStringList command = {"align", temp.filePath("corpus"), "japanese_mfa",
                                 "japanese_mfa", temp.filePath("aligned"), "--g2p_model_path",
                                 "japanese_mfa", "--single_speaker", "--clean", "-j", "2"};
#endif
    if (!run(tool, command, &result.error)) return result;
    for (int i = 0; i < samples.size(); ++i) {
        AiResult item;
        item.alias = samples[i].alias;
        const QString stem = QString("sample_%1").arg(i, 4, 10, QChar('0'));
        QFile grid(temp.filePath("aligned/" + stem + ".TextGrid"));
        if (!grid.open(QIODevice::ReadOnly)) {
            item.error = "Alignment unavailable";
        } else {
            const Wav wav = Wav::load(samples[i].audioPath);
            item.oto = fromTextGrid(grid.readAll(), wav, item.alias);
            if (!item.oto.analyzed) item.error = "Unreliable phoneme boundaries";
        }
        result.results.append(item);
    }
    return result;
}
