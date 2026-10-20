#pragma once
#include "Project.h"
#include <QMainWindow>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaPlayer>
#include <QAudioInput>
#include <QAudioOutput>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QTabWidget;
class QDoubleSpinBox;
class Waveform;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
private:
    Project project;
    QMediaCaptureSession capture;
    QMediaRecorder *recorder;
    QAudioInput *input;
    QMediaPlayer *player;
    QAudioOutput *output;
    QLabel *title;
    QLabel *progressText;
    QLabel *sampleTitle;
    QLabel *sampleDetails;
    QLabel *recordingStatus;
    QLabel *otoStatus;
    QLabel *exportStatus;
    QProgressBar *progressBar;
    QComboBox *devices;
    QListWidget *recordList;
    QListWidget *otoList;
    QPushButton *recordButton;
    QPushButton *stopButton;
    Waveform *recordWave;
    Waveform *otoWave;
    QDoubleSpinBox *fields[5]{};
    int recordingIndex = -1;
    QString capturePath;
    bool updating = false;
    void buildUi();
    void refresh();
    void refreshSelection();
    void selectIndex(int index);
    void loadSample(int index);
    void updateOtoFields();
    void updateDevices();
    void createProject();
    void openProject();
    void importRecording();
    void startRecording();
    void stopRecording();
    void finishRecording();
    void playSample();
    void analyzeAll();
    void saveOtoFields();
    void exportVoicebank();
    void showError(const QString &error);
};
