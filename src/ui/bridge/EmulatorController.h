#pragma once

#include "core/Disassembler.h"
#include "core/Machine.h"
#include "core/RealTimePacer.h"
#include "toolchain/ArmGccBuilder.h"
#include "ui/bridge/Models.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QUrl>

class QQuickTextDocument;

class MachineWorker : public QObject {
    Q_OBJECT
public:
    explicit MachineWorker(QObject *parent = nullptr);

public slots:
    void initialize();
    void reset();
    void step();
    void startRun();
    void pause();
    void loadPath(const QString &path);
    void loadBuiltin();
    void setButton(bool pressed);
    void uartIn(const QString &text);
    void addBreakpoint(quint32 addr);
    void removeBreakpoint(quint32 addr);
    void setAnalog(int value);
    void requestMemory(quint32 addr);
    void requestDisasm();
    void tick();
    void setSpeed(double speed); // 1 = real time, 0 = unlimited

signals:
    void inited(bool ok, const QString &error);
    void snapshotReady(const MachineSnapshot &snap, const QByteArray &uart);
    void memoryReady(quint32 addr, const QByteArray &data);
    void disasmReady(const QVector<DisasmModel::Row> &rows);
    void statusChanged(const QString &text);
    void runStateChanged(bool running);
    void speedMeasured(double speed); // emulated/real time ratio
    void warning(const QString &text);

private:
    void emitSnap(bool force);
    void setRunning(bool running) {
        running_ = running;
        ++runGen_;
        emit runStateChanged(running);
    }
    void scheduleTick(uint64_t waitNs);

    Machine machine_;
    Disassembler disasm_;
    bool running_ = false;
    quint64 runGen_ = 0;
    RealTimePacer pacer_;
    QElapsedTimer clock_;
    double lastSpeed_ = -1.0;
    qint64 lastSnapMs_ = 0;
    QByteArray uartBuf_;
};

class EmulatorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString haltReason READ haltReason NOTIFY statusChanged)
    Q_PROPERTY(QString buildLog READ buildLog NOTIFY buildLogChanged)
    Q_PROPERTY(QString pcText READ pcText NOTIFY statusChanged)
    Q_PROPERTY(QString breakpointText READ breakpointText WRITE setBreakpointText NOTIFY breakpointTextChanged)
    Q_PROPERTY(QString memoryAddressText READ memoryAddressText WRITE setMemoryAddressText NOTIFY memoryAddressTextChanged)
    Q_PROPERTY(QString firmwarePath READ firmwarePath NOTIFY firmwarePathChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QString measuredSpeedText READ measuredSpeedText NOTIFY speedChanged)
public:
    explicit EmulatorController(QObject *parent = nullptr);
    ~EmulatorController() override;

    bool running() const { return running_; }
    QString status() const { return status_; }
    QString haltReason() const { return status_; }
    QString buildLog() const { return buildLog_; }
    QString pcText() const { return pcText_; }
    QString breakpointText() const { return breakpointText_; }
    void setBreakpointText(const QString &t);
    QString memoryAddressText() const { return memoryAddressText_; }
    void setMemoryAddressText(const QString &t);
    QString firmwarePath() const { return firmwarePath_; }
    // Emulation speed: 1 = real time (default), 0.1 = slower, 10 = faster, 0 = unlimited.
    double speed() const { return speed_; }
    void setSpeed(double speed);
    QString measuredSpeedText() const;

    RegisterModel *registerModel() { return &registers_; }
    MemoryModel *memoryModel() { return &memory_; }
    DisasmModel *disasmModel() { return &disasm_; }
    PeriphModel *periphModel() { return &periph_; }
    BoardModel *boardModel() { return &board_; }
    UartModel *uartModel() { return &uart_; }
    ProjectModel *projectModel() { return &project_; }

    Q_INVOKABLE void reset();
    Q_INVOKABLE void run();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void step();
    Q_INVOKABLE void loadFirmware(const QUrl &url);
    Q_INVOKABLE void loadBuiltin();
    Q_INVOKABLE void addBreakpoint();
    Q_INVOKABLE void removeBreakpoint();
    Q_INVOKABLE void compileAndLoad();
    Q_INVOKABLE void attachHighlighter(QQuickTextDocument *doc);
    Q_INVOKABLE void goMemory();

signals:
    void runningChanged();
    void statusChanged();
    void buildLogChanged();
    void breakpointTextChanged();
    void memoryAddressTextChanged();
    void firmwarePathChanged();
    void speedChanged();

private:
    void applySnapshot(const MachineSnapshot &snap, const QByteArray &uart);
    // Parses "0x..." / decimal; on error shows a status message and returns false.
    bool addressFromText(const QString &text, quint32 *out);

    QThread workerThread_;
    MachineWorker *worker_ = nullptr;
    RegisterModel registers_;
    MemoryModel memory_;
    DisasmModel disasm_;
    PeriphModel periph_;
    BoardModel board_;
    UartModel uart_;
    ProjectModel project_;
    ArmGccBuilder builder_;
    bool running_ = false;
    double speed_ = 1.0;
    double measuredSpeed_ = 0.0;
    QString status_ = QStringLiteral("Starting…");
    QString buildLog_;
    QString pcText_ = QStringLiteral("0x00000000");
    QString breakpointText_ = QStringLiteral("0x08000008");
    QString memoryAddressText_ = QStringLiteral("0x20000000");
    QString firmwarePath_ = QStringLiteral("(builtin blinky)"); // Machine::initialize loads it
    QString dataRoot_;
};
