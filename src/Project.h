#pragma once
#include "Oto.h"
#include <QString>
#include <QVector>

struct Sample {
    QString alias;
    QString kana;
    Oto oto;
};

class Project {
public:
    QString name;
    QString root;
    QVector<Sample> samples;
    bool create(const QString &parentDirectory, const QString &projectName, QString *error = nullptr);
    bool open(const QString &projectFile, QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    QString projectFile() const;
    QString audioFile(int index) const;
    bool hasAudio(int index) const;
    bool importAudio(int index, const QString &source, QString *error = nullptr);
    bool exportBank(const QString &destination, QString *error = nullptr) const;
    int recordedCount() const;
    static QVector<Sample> japaneseCv();
};
