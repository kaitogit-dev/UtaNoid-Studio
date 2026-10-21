#include "MainWindow.h"
#include "AiOto.h"
#include "Waveform.h"
#include <QAudioDevice>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel *heading(const QString &text) {
    auto *label = new QLabel(text);
    label->setProperty("heading", true);
    return label;
}

QPushButton *action(const QString &text, bool primary = false) {
    auto *button = new QPushButton(text);
    button->setMinimumHeight(36);
    if (primary) button->setProperty("primary", true);
    return button;
}

QFrame *divider() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("divider");
    return line;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    recorder(new QMediaRecorder(this)), input(new QAudioInput(this)),
    player(new QMediaPlayer(this)), output(new QAudioOutput(this)) {
    setWindowTitle("UtaNoid Studio");
    resize(1120, 730);
    setMinimumSize(820, 570);
    capture.setAudioInput(input);
    capture.setRecorder(recorder);
    QMediaFormat format;
    format.setFileFormat(QMediaFormat::FileFormat::Wave);
    format.setAudioCodec(QMediaFormat::AudioCodec::Wave);
    recorder->setMediaFormat(format);
    recorder->setAudioSampleRate(44100);
    recorder->setAudioChannelCount(1);
    recorder->setQuality(QMediaRecorder::VeryHighQuality);
    player->setAudioOutput(output);
    output->setVolume(0.8);
    buildUi();
    updateDevices();
    auto *deviceMonitor = new QMediaDevices(this);
    connect(deviceMonitor, &QMediaDevices::audioInputsChanged, this, &MainWindow::updateDevices);
    connect(recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && recordingIndex >= 0) finishRecording();
    });
    connect(recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error error, const QString &message) {
        if (error == QMediaRecorder::NoError) return;
        recordingIndex = -1;
        stopButton->setEnabled(false);
        recordButton->setEnabled(!project.root.isEmpty());
        recordingStatus->setText("Recording failed");
        showError(message);
    });
    refresh();
}

void MainWindow::buildUi() {
    const QString style = QStringLiteral(R"(
        QMainWindow, QWidget { background: #121720; color: #e6eaf2; font-family: 'Segoe UI'; font-size: 13px; }
        QLabel[heading="true"] { font-size: 22px; font-weight: 650; }
        QLabel[secondary="true"] { color: #8995a8; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab { background: transparent; color: #8995a8; padding: 13px 22px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #8ae0cb; border-bottom: 2px solid #8ae0cb; }
        QTabBar::tab:hover { color: #e6eaf2; }
        QPushButton { background: #222c3a; border: 1px solid #364252; border-radius: 8px; padding: 7px 15px; }
        QPushButton:hover { background: #2a3747; }
        QPushButton:disabled { color: #697688; background: #171e28; border-color: #253141; }
        QPushButton[primary="true"] { background: #72cfb7; color: #10241f; border: 1px solid #72cfb7; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #95e4cd; }
        QListWidget { background: #181f2b; border: 1px solid #303b4c; border-radius: 9px; padding: 5px; outline: none; }
        QListWidget::item { padding: 10px 12px; border-radius: 5px; }
        QListWidget::item:selected { background: #30483f; color: #d8fff2; }
        QListWidget::item:hover:!selected { background: #263242; }
        QComboBox, QDoubleSpinBox { border: 1px solid #364252; background: #202a38; border-radius: 7px; padding: 7px; min-height: 22px; }
        QComboBox QAbstractItemView { background: #202a38; color: #e6eaf2; selection-background-color: #30483f; }
        QProgressBar { background: #252f3d; border: 0; border-radius: 4px; height: 7px; text-align: center; }
        QProgressBar::chunk { background: #72cfb7; border-radius: 4px; }
        QFrame#divider { color: #313d4d; background: #313d4d; max-height: 1px; border: none; }
    )");
    setStyleSheet(style);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(26, 19, 26, 22);
    root->setSpacing(16);

    auto *header = new QHBoxLayout;
    auto *branding = new QVBoxLayout;
    branding->setSpacing(2);
    title = heading("UtaNoid Studio");
    branding->addWidget(title);
    progressText = new QLabel;
    progressText->setProperty("secondary", true);
    branding->addWidget(progressText);
    header->addLayout(branding, 1);
    auto *newButton = action("New");
    auto *openButton = action("Open");
    auto *saveButton = action("Save");
    header->addWidget(newButton);
    header->addWidget(openButton);
    header->addWidget(saveButton);
    root->addLayout(header);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::createProject);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openProject);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!project.save(&error)) showError(error);
    });

    auto *tabs = new QTabWidget;
    root->addWidget(tabs, 1);

    auto *recordPage = new QWidget;
    auto *recordLayout = new QHBoxLayout(recordPage);
    recordLayout->setContentsMargins(0, 14, 0, 0);
    recordLayout->setSpacing(22);
    auto *recordSide = new QVBoxLayout;
    recordSide->addWidget(new QLabel("SAMPLES"));
    recordList = new QListWidget;
    recordList->setMinimumWidth(210);
    recordList->setMaximumWidth(290);
    recordSide->addWidget(recordList, 1);
    recordLayout->addLayout(recordSide, 1);
    auto *recordMain = new QVBoxLayout;
    recordMain->setSpacing(16);
    sampleTitle = heading("No project");
    recordMain->addWidget(sampleTitle);
    sampleDetails = new QLabel;
    sampleDetails->setProperty("secondary", true);
    recordMain->addWidget(sampleDetails);
    recordWave = new Waveform;
    recordMain->addWidget(recordWave, 1);
    auto *recordControls = new QHBoxLayout;
    recordButton = action("Record", true);
    stopButton = action("Stop");
    auto *playButton = action("Play");
    auto *importButton = action("Import WAV");
    recordControls->addWidget(recordButton);
    recordControls->addWidget(stopButton);
    recordControls->addWidget(playButton);
    recordControls->addWidget(importButton);
    recordControls->addStretch();
    recordMain->addLayout(recordControls);
    recordMain->addWidget(divider());
    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(new QLabel("Input"));
    devices = new QComboBox;
    devices->setMinimumWidth(230);
    deviceRow->addWidget(devices, 1);
    recordingStatus = new QLabel;
    recordingStatus->setProperty("secondary", true);
    deviceRow->addWidget(recordingStatus);
    recordMain->addLayout(deviceRow);
    recordLayout->addLayout(recordMain, 3);
    tabs->addTab(recordPage, "Record");
    connect(recordList, &QListWidget::currentRowChanged, this, &MainWindow::selectIndex);
    connect(recordButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::stopRecording);
    connect(playButton, &QPushButton::clicked, this, &MainWindow::playSample);
    connect(importButton, &QPushButton::clicked, this, &MainWindow::importRecording);
    connect(devices, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int row) {
        const QByteArray id = devices->itemData(row).toByteArray();
        for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
            if (device.id() == id) { input->setDevice(device); break; }
        }
    });

    auto *otoPage = new QWidget;
    auto *otoLayout = new QHBoxLayout(otoPage);
    otoLayout->setContentsMargins(0, 14, 0, 0);
    otoLayout->setSpacing(22);
    otoList = new QListWidget;
    otoList->setMinimumWidth(210);
    otoList->setMaximumWidth(290);
    otoLayout->addWidget(otoList, 1);
    auto *otoMain = new QVBoxLayout;
    otoMain->setSpacing(14);
    auto *otoHeader = new QHBoxLayout;
    otoHeader->addWidget(heading("OTO"));
    otoHeader->addStretch();
    auto *generate = action("Generate AI OTO", true);
    otoHeader->addWidget(generate);
    otoMain->addLayout(otoHeader);
    otoWave = new Waveform;
    otoMain->addWidget(otoWave, 1);
    auto *editorFields = new QHBoxLayout;
    const QString fieldLabels[] = {"Offset", "Overlap", "Preutterance", "Consonant", "Cutoff"};
    for (int i = 0; i < 5; ++i) {
        auto *cell = new QVBoxLayout;
        cell->addWidget(new QLabel(fieldLabels[i]));
        fields[i] = new QDoubleSpinBox;
        fields[i]->setDecimals(2);
        fields[i]->setSingleStep(5.0);
        fields[i]->setRange(i == 4 ? -20000.0 : 0.0, i == 4 ? -0.01 : 20000.0);
        fields[i]->setSuffix(" ms");
        cell->addWidget(fields[i]);
        editorFields->addLayout(cell);
        connect(fields[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::saveOtoFields);
    }
    otoMain->addLayout(editorFields);
    otoStatus = new QLabel;
    otoStatus->setProperty("secondary", true);
    otoMain->addWidget(otoStatus);
    otoLayout->addLayout(otoMain, 3);
    tabs->addTab(otoPage, "OTO");
    connect(otoList, &QListWidget::currentRowChanged, this, &MainWindow::selectIndex);
    connect(generate, &QPushButton::clicked, this, &MainWindow::analyzeAll);
    connect(otoWave, &Waveform::otoChanged, this, [this](const Oto &oto) {
        const int index = otoList->currentRow();
        if (index < 0 || index >= project.samples.size()) return;
        project.samples[index].oto = oto;
        project.samples[index].oto.engine = "manual";
        updateOtoFields();
        project.save();
    });

    auto *exportPage = new QWidget;
    auto *exportLayout = new QVBoxLayout(exportPage);
    exportLayout->setContentsMargins(8, 26, 8, 0);
    exportLayout->setSpacing(18);
    exportLayout->addWidget(heading("Export voicebank"));
    exportStatus = new QLabel;
    exportStatus->setProperty("secondary", true);
    exportLayout->addWidget(exportStatus);
    progressBar = new QProgressBar;
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(8);
    exportLayout->addWidget(progressBar);
    auto *exportButton = action("Export folder", true);
    exportButton->setMaximumWidth(190);
    exportLayout->addWidget(exportButton);
    exportLayout->addStretch();
    tabs->addTab(exportPage, "Export");
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportVoicebank);
}

void MainWindow::updateDevices() {
    devices->clear();
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        devices->addItem(device.description(), device.id());
    }
    if (devices->count() == 0) recordingStatus->setText("No microphone");
}

void MainWindow::refresh() {
    title->setText(project.root.isEmpty() ? "UtaNoid Studio" : project.name);
    progressText->setText(project.root.isEmpty() ? "New or open a project" :
        QString("%1 / %2 recorded").arg(project.recordedCount()).arg(project.samples.size()));
    const int selected = std::max(0, recordList->currentRow());
    recordList->blockSignals(true);
    otoList->blockSignals(true);
    recordList->clear();
    otoList->clear();
    for (int i = 0; i < project.samples.size(); ++i) {
        const Sample &sample = project.samples[i];
        const QString display = QString("%1    %2%3").arg(sample.kana, sample.alias, project.hasAudio(i) ? "  ·" : "");
        recordList->addItem(display);
        otoList->addItem(display);
        const QColor muted = project.hasAudio(i) ? QColor("#dce5ed") : QColor("#8995a8");
        recordList->item(i)->setForeground(muted);
        otoList->item(i)->setForeground(muted);
    }
    recordList->blockSignals(false);
    otoList->blockSignals(false);
    if (!project.samples.isEmpty()) selectIndex(std::min(selected, int(project.samples.size()) - 1));
    else {
        sampleTitle->setText("No project");
        sampleDetails->clear();
        recordWave->setAudio({});
        otoWave->setAudio({});
    }
    progressBar->setRange(0, std::max(1, int(project.samples.size())));
    progressBar->setValue(project.recordedCount());
    exportStatus->setText(project.root.isEmpty() ? "No project" :
        QString("%1 recordings ready · %2 remaining").arg(project.recordedCount())
            .arg(project.samples.size() - project.recordedCount()));
    recordButton->setEnabled(!project.root.isEmpty() && devices->count() > 0 && recordingIndex < 0);
    stopButton->setEnabled(recordingIndex >= 0);
}

void MainWindow::selectIndex(int index) {
    if (index < 0 || index >= project.samples.size()) return;
    if (recordList->currentRow() != index) {
        recordList->blockSignals(true);
        recordList->setCurrentRow(index);
        recordList->blockSignals(false);
    }
    if (otoList->currentRow() != index) {
        otoList->blockSignals(true);
        otoList->setCurrentRow(index);
        otoList->blockSignals(false);
    }
    loadSample(index);
}

void MainWindow::loadSample(int index) {
    const Sample &sample = project.samples[index];
    sampleTitle->setText(sample.kana + "  " + sample.alias);
    sampleDetails->setText(project.hasAudio(index) ? "Recorded" : "Not recorded");
    const Wav wav = project.hasAudio(index) ? Wav::load(project.audioFile(index)) : Wav{};
    recordWave->setAudio(wav);
    recordWave->setOto(sample.oto);
    otoWave->setAudio(wav);
    otoWave->setOto(sample.oto);
    updateOtoFields();
}

void MainWindow::updateOtoFields() {
    updating = true;
    const int index = otoList->currentRow();
    const bool available = index >= 0 && project.hasAudio(index);
    const Oto o = available ? project.samples[index].oto : Oto{};
    const double values[] = {o.offset, o.overlap, o.preutterance, o.consonant, o.cutoff};
    for (int i = 0; i < 5; ++i) {
        fields[i]->setEnabled(available && o.analyzed);
        fields[i]->setValue(values[i]);
    }
    updating = false;
    if (!available) otoStatus->setText("No recording");
    else if (!o.analyzed) otoStatus->setText("Not aligned");
    else if (o.engine == "mfa-japanese") otoStatus->setText("AI aligned");
    else if (o.engine == "manual") otoStatus->setText("Manually edited");
    else otoStatus->setText("Legacy timing");
    otoWave->setOto(o);
}

void MainWindow::saveOtoFields() {
    if (updating) return;
    const int index = otoList->currentRow();
    if (index < 0 || !project.hasAudio(index)) return;
    Oto &o = project.samples[index].oto;
    o.offset = fields[0]->value();
    o.overlap = fields[1]->value();
    o.preutterance = fields[2]->value();
    o.consonant = fields[3]->value();
    o.cutoff = fields[4]->value();
    o.engine = "manual";
    otoWave->setOto(o);
    project.save();
}

void MainWindow::createProject() {
    if (recordingIndex >= 0) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "New project", "Name", QLineEdit::Normal, {}, &accepted);
    if (!accepted || name.trimmed().isEmpty()) return;
    const QString initial = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString folder = QFileDialog::getExistingDirectory(this, "Project location", initial);
    if (folder.isEmpty()) return;
    Project next;
    QString error;
    if (!next.create(folder, name, &error)) { showError(error); return; }
    project = next;
    refresh();
}

void MainWindow::openProject() {
    if (recordingIndex >= 0) return;
    const QString file = QFileDialog::getOpenFileName(this, "Open project", {}, "UtaNoid projects (project.utanoid)");
    if (file.isEmpty()) return;
    Project next;
    QString error;
    if (!next.open(file, &error)) { showError(error); return; }
    project = next;
    refresh();
}

void MainWindow::importRecording() {
    const int index = recordList->currentRow();
    if (index < 0 || recordingIndex >= 0) return;
    const QString path = QFileDialog::getOpenFileName(this, "Import WAV", {}, "WAV audio (*.wav)");
    if (path.isEmpty()) return;
    QString error;
    if (!project.importAudio(index, path, &error)) { showError(error); return; }
    refresh();
}

void MainWindow::startRecording() {
    const int index = recordList->currentRow();
    if (index < 0 || devices->count() == 0 || recordingIndex >= 0) return;
    player->stop();
    const QString tempFolder = QDir(project.root).filePath("recordings");
    if (!QDir().mkpath(tempFolder)) { showError("Unable to create recording folder"); return; }
    capturePath = QDir(tempFolder).filePath("capture-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmsszzz") + ".wav");
    recorder->setOutputLocation(QUrl::fromLocalFile(capturePath));
    recordingIndex = index;
    recordButton->setEnabled(false);
    stopButton->setEnabled(true);
    recordingStatus->setText("Recording");
    recorder->record();
}

void MainWindow::stopRecording() {
    if (recordingIndex >= 0) recorder->stop();
}

void MainWindow::finishRecording() {
    const int index = recordingIndex;
    recordingIndex = -1;
    stopButton->setEnabled(false);
    recordButton->setEnabled(true);
    QString path = recorder->actualLocation().toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) path = capturePath;
    QString error;
    if (!project.importAudio(index, path, &error)) {
        recordingStatus->setText("Recording rejected");
        showError(error);
        return;
    }
    QFile::remove(path);
    recordingStatus->setText("Saved");
    refresh();
}

void MainWindow::playSample() {
    const int index = recordList->currentRow();
    if (index < 0 || !project.hasAudio(index)) return;
    player->stop();
    player->setSource(QUrl::fromLocalFile(project.audioFile(index)));
    player->play();
}

void MainWindow::analyzeAll() {
    if (project.root.isEmpty()) return;
    QVector<AiInput> inputs;
    for (int i = 0; i < project.samples.size(); ++i) {
        if (!project.hasAudio(i)) continue;
        const Sample &sample = project.samples[i];
        inputs.append({sample.alias, sample.kana, project.audioFile(i)});
    }
    if (inputs.isEmpty()) return;
    auto *progress = new QProgressDialog("Aligning voicebank", QString(), 0, 0, this);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    progress->show();
    const QString root = project.root;
    auto *watcher = new QFutureWatcher<AiBatch>(this);
    connect(watcher, &QFutureWatcher<AiBatch>::finished, this, [this, watcher, progress, root] {
        progress->close();
        progress->deleteLater();
        const AiBatch batch = watcher->result();
        watcher->deleteLater();
        if (project.root != root) return;
        if (!batch.error.isEmpty()) {
            showError(batch.error);
            return;
        }
        int aligned = 0;
        int rejected = 0;
        for (const AiResult &item : batch.results) {
            for (Sample &sample : project.samples) {
                if (sample.alias != item.alias) continue;
                sample.oto = item.oto;
                if (item.oto.analyzed) ++aligned;
                else ++rejected;
                break;
            }
        }
        QString error;
        if (!project.save(&error)) showError(error);
        refresh();
        otoStatus->setText(QString("%1 aligned · %2 rejected").arg(aligned).arg(rejected));
    });
    watcher->setFuture(QtConcurrent::run([inputs] { return AiOto::generate(inputs); }));
}

void MainWindow::exportVoicebank() {
    if (project.root.isEmpty()) return;
    if (project.recordedCount() == 0) { showError("Record at least one sample"); return; }
    const int remaining = project.samples.size() - project.recordedCount();
    if (remaining > 0 && QMessageBox::question(this, "Incomplete reclist",
        QString("%1 samples are missing. Export recorded samples?").arg(remaining)) != QMessageBox::Yes) return;
    const QString root = QFileDialog::getExistingDirectory(this, "Export location", project.root);
    if (root.isEmpty()) return;
    const QString target = QDir(root).filePath(QFileInfo(project.root).fileName());
    QString error;
    if (!project.exportBank(target, &error)) { showError(error); return; }
    QMessageBox::information(this, "Export complete", target);
}

void MainWindow::showError(const QString &error) { QMessageBox::warning(this, "UtaNoid Studio", error); }
