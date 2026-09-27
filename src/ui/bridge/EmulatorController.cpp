#include "ui/bridge/EmulatorController.h"

#include "ui/bridge/CHighlighter.h"

#include "core/Addresses.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QQuickTextDocument>
#include <QTimer>

Q_DECLARE_METATYPE(MachineSnapshot)
Q_DECLARE_METATYPE(QVector<DisasmModel::Row>)

MachineWorker::MachineWorker(QObject *parent)
    : QObject(parent) {}

void MachineWorker::initialize() {
    std::string err;
    if (!machine_.initialize(&err)) {
        emit inited(false, QString::fromStdString(err));
        return;
    }
    disasm_.open(nullptr);
    clock_.start();
    machine_.setUartTx([this](uint8_t b) { uartBuf_.append(static_cast<char>(b)); });
    machine_.setWarningSink([this](const std::string &text) { emit warning(QString::fromStdString(text)); });
    emit inited(true, {});
    emitSnap(true);
    requestDisasm();
    requestMemory(0x20000000);
}

void MachineWorker::emitSnap(bool force) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - lastSnapMs_ < 30)
        return;
    lastSnapMs_ = now;
    QByteArray uart = uartBuf_;
    uartBuf_.clear();
    emit snapshotReady(machine_.snapshot(), uart);
}

void MachineWorker::reset() {
    machine_.reset();
    setRunning(false);
    emitSnap(true);
    requestDisasm();
    emit statusChanged(QString::fromStdString(machine_.status()));
}

void MachineWorker::step() {
    setRunning(false);
    machine_.step();
    emitSnap(true);
    requestDisasm();
    emit statusChanged(QString::fromStdString(machine_.status()));
}

void MachineWorker::startRun() {
    setRunning(true);
    pacer_.start(static_cast<uint64_t>(clock_.nsecsElapsed()));
    tick();
}

void MachineWorker::setSpeed(double speed) {
    pacer_.setSpeed(speed);
    if (running_)
        pacer_.start(static_cast<uint64_t>(clock_.nsecsElapsed())); // re-anchor at the new speed
}

void MachineWorker::scheduleTick(uint64_t waitNs) {
    // Ticks from an earlier run (before a pause/stop) must not start a second chain.
    const quint64 gen = runGen_;
    auto next = [this, gen] {
        if (gen == runGen_)
            tick();
    };
    if (waitNs >= 1000000) // sleep only for >= 1 ms; shorter waits are absorbed by the next chunk
        QTimer::singleShot(static_cast<int>(waitNs / 1000000), Qt::PreciseTimer, this, next);
    else
        QMetaObject::invokeMethod(this, next, Qt::QueuedConnection);
}

void MachineWorker::pause() {
    setRunning(false);
    emitSnap(true);
    requestDisasm();
    emit statusChanged(QStringLiteral("Paused"));
}

void MachineWorker::tick() {
    if (!running_)
        return;
    const uint32_t hclk = machine_.rcc().hclk();
    const uint64_t before = machine_.cpu().instructionCount();
    const auto stop = machine_.runQuantum(pacer_.chunkInstructions(hclk));
    const uint64_t executed = machine_.cpu().instructionCount() - before;
    const uint64_t waitNs = pacer_.advance(executed, hclk, static_cast<uint64_t>(clock_.nsecsElapsed()));
    if (pacer_.measuredSpeed() != lastSpeed_) {
        lastSpeed_ = pacer_.measuredSpeed();
        emit speedMeasured(lastSpeed_);
    }
    emitSnap(stop != CpuEngine::StopReason::Quantum);
    if (stop == CpuEngine::StopReason::Breakpoint || stop == CpuEngine::StopReason::Fault) {
        setRunning(false);
        requestDisasm();
        emit statusChanged(QString::fromStdString(machine_.status()));
        return;
    }
    scheduleTick(waitNs);
}

void MachineWorker::loadPath(const QString &path) {
    setRunning(false);
    auto img = machine_.loadFirmware(path.toStdString());
    emitSnap(true);
    requestDisasm();
    emit statusChanged(QString::fromStdString(img.ok ? machine_.status() : img.error));
}

void MachineWorker::loadBuiltin() {
    setRunning(false);
    machine_.loadBuiltinBlinky();
    emitSnap(true);
    requestDisasm();
    emit statusChanged(QString::fromStdString(machine_.status()));
}

void MachineWorker::setButton(bool pressed) { machine_.setButtonPressed(pressed); }
void MachineWorker::uartIn(const QString &text) { machine_.uartPush(text.toLatin1().toStdString()); }
void MachineWorker::addBreakpoint(quint32 addr) { machine_.addBreakpoint(addr); requestDisasm(); }
void MachineWorker::removeBreakpoint(quint32 addr) { machine_.removeBreakpoint(addr); requestDisasm(); }
void MachineWorker::setAnalog(int value) { machine_.setAnalog(static_cast<uint32_t>(value)); }

void MachineWorker::requestMemory(quint32 addr) {
    QByteArray data(256, 0);
    machine_.readMemory(addr, reinterpret_cast<uint8_t *>(data.data()), data.size());
    emit memoryReady(addr, data);
}

void MachineWorker::requestDisasm() {
    const uint32_t pc = machine_.cpu().pc();
    uint32_t start = pc > 32 ? (pc - 32) & ~1u : pc & ~1u;
    if (pc >= mcu::kFlashBase && start < mcu::kFlashBase)
        start = mcu::kFlashBase; // nothing is mapped just below flash
    uint8_t buf[128];
    machine_.readMemory(start, buf, sizeof(buf));
    auto lines = disasm_.disassemble(buf, sizeof(buf), start, pc, machine_.breakpoints());
    QVector<DisasmModel::Row> rows;
    for (const auto &l : lines) {
        DisasmModel::Row r;
        r.addr = l.address;
        r.bytes = QString::fromStdString(l.bytes);
        r.text = QString::fromStdString(l.mnemonic + " " + l.op);
        r.current = l.current;
        r.breakpoint = l.breakpoint;
        rows.push_back(r);
    }
    emit disasmReady(rows);
}

EmulatorController::EmulatorController(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<MachineSnapshot>("MachineSnapshot");
    qRegisterMetaType<QVector<DisasmModel::Row>>("QVector<DisasmModel::Row>");

    dataRoot_ = QCoreApplication::applicationDirPath();
    project_.loadDefaultProject(dataRoot_ + "/examples");

    worker_ = new MachineWorker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &EmulatorController::destroyed, &workerThread_, &QThread::quit);

    connect(worker_, &MachineWorker::inited, this, [this](bool ok, const QString &err) {
        status_ = ok ? QStringLiteral("Ready") : err;
        emit statusChanged();
    });
    connect(worker_, &MachineWorker::snapshotReady, this, &EmulatorController::applySnapshot);
    // Emulator warnings (e.g. access to an unemulated peripheral) go to the build log panel.
    connect(worker_, &MachineWorker::warning, this, [this](const QString &text) {
        if (!buildLog_.isEmpty() && !buildLog_.endsWith('\n'))
            buildLog_ += '\n';
        buildLog_ += QStringLiteral("emulator warning: ") + text + '\n';
        emit buildLogChanged();
    });
    connect(worker_, &MachineWorker::statusChanged, this, [this](const QString &s) {
        status_ = s;
        emit statusChanged();
    });
    // The worker reports run/stop explicitly; guessing from status text broke when fault
    // messages changed ("HardFault: ..." left the UI showing RUNNING).
    connect(worker_, &MachineWorker::speedMeasured, this, [this](double speed) {
        measuredSpeed_ = speed;
        emit speedChanged();
    });
    connect(worker_, &MachineWorker::runStateChanged, this, [this](bool running) {
        if (running_ != running) {
            running_ = running;
            emit runningChanged();
        }
        if (!running)
            measuredSpeed_ = 0.0; // show "—" until the next run is measured
        emit speedChanged();
    });
    connect(worker_, &MachineWorker::memoryReady, this, [this](quint32 addr, const QByteArray &data) {
        memory_.setBytes(addr, data);
    });
    connect(worker_, &MachineWorker::disasmReady, this, [this](const QVector<DisasmModel::Row> &rows) {
        disasm_.setRows(rows);
    });
    connect(&board_, &BoardModel::buttonPressed, this, [this](bool p) {
        QMetaObject::invokeMethod(worker_, "setButton", Qt::QueuedConnection, Q_ARG(bool, p));
    });
    connect(&board_, &BoardModel::analogMoved, this, [this](int v) {
        QMetaObject::invokeMethod(worker_, "setAnalog", Qt::QueuedConnection, Q_ARG(int, v));
    });
    connect(&uart_, &UartModel::sendRequested, this, [this](const QString &t) {
        QMetaObject::invokeMethod(worker_, "uartIn", Qt::QueuedConnection, Q_ARG(QString, t));
    });

    workerThread_.start();
    QMetaObject::invokeMethod(worker_, "initialize", Qt::QueuedConnection);
}

EmulatorController::~EmulatorController() {
    QMetaObject::invokeMethod(worker_, "pause", Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();
}

void EmulatorController::applySnapshot(const MachineSnapshot &snap, const QByteArray &uart) {
            auto hex = [](uint32_t v) {
                return QStringLiteral("0x") + QString("%1").arg(v, 8, 16, QChar('0')).toUpper();
            };
    QVector<QPair<QString, QString>> regs;
    const char *names[16] = {"R0", "R1", "R2",  "R3",  "R4", "R5", "R6", "R7",
                             "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC"};
    for (int i = 0; i < 16; ++i)
        regs.push_back({names[i], hex(snap.r[i])});
    regs.push_back({"xPSR", hex(snap.xpsr)});
    regs.push_back({"MSP", hex(snap.msp)});
    regs.push_back({"PSP", hex(snap.psp)});
    regs.push_back({"CONTROL", hex(snap.control)});
    regs.push_back({"PRIMASK", hex(snap.primask)});
    registers_.update(regs);

    QVector<QPair<QString, QString>> per;
    per.push_back({"SYSCLK", QString::number(snap.sysclk)});
    per.push_back({"RCC_CR", hex(snap.rccCr)});
    per.push_back({"RCC_CFGR", hex(snap.rccCfgr)});
    per.push_back({"RCC_AHB1ENR", hex(snap.rccAhb1enr)});
    per.push_back({"GPIOD_ODR", hex(snap.gpioD)});
    per.push_back({"SysTick_CTRL", hex(snap.systickCtrl)});
    per.push_back({"SysTick_LOAD", hex(snap.systickLoad)});
    per.push_back({"SysTick_VAL", hex(snap.systickVal)});
    per.push_back({"USART2_SR", hex(snap.usart2Sr)});
    per.push_back({"TIM2_CNT", hex(snap.tim2Cnt)});
    per.push_back({"ADC1_DR", hex(snap.adcDr)});
    per.push_back({"VTOR", hex(snap.vtor)});
    per.push_back({"SCB_CFSR", hex(snap.cfsr)});
    per.push_back({"SCB_HFSR", hex(snap.hfsr)});
    per.push_back({"SCB_BFAR", hex(snap.bfar)});
    per.push_back({"SCB_CPACR", hex(snap.cpacr)});
    per.push_back({"INSNS", QString::number(snap.instructions)});
    periph_.update(per);

    board_.setLeds(snap.leds, snap.ledLevel);
    uart_.appendBytes(uart);
    pcText_ = hex(snap.pc);
    status_ = QString::fromStdString(snap.haltReason);
    emit statusChanged();
}

void EmulatorController::reset() {
    running_ = false;
    emit runningChanged();
    QMetaObject::invokeMethod(worker_, "reset", Qt::QueuedConnection);
}

void EmulatorController::setSpeed(double speed) {
    if (speed < 0.0)
        speed = 0.0;
    if (speed == speed_)
        return;
    speed_ = speed;
    emit speedChanged();
    QMetaObject::invokeMethod(worker_, "setSpeed", Qt::QueuedConnection, Q_ARG(double, speed));
}

QString EmulatorController::measuredSpeedText() const {
    if (!running_ || measuredSpeed_ <= 0.0)
        return QStringLiteral("—");
    return QStringLiteral("×%1").arg(measuredSpeed_, 0, 'f', measuredSpeed_ < 10.0 ? 2 : 0);
}

void EmulatorController::run() {
    running_ = true;
    emit runningChanged();
    QMetaObject::invokeMethod(worker_, "startRun", Qt::QueuedConnection);
}

void EmulatorController::pause() {
    running_ = false;
    emit runningChanged();
    QMetaObject::invokeMethod(worker_, "pause", Qt::QueuedConnection);
}

void EmulatorController::step() {
    running_ = false;
    emit runningChanged();
    QMetaObject::invokeMethod(worker_, "step", Qt::QueuedConnection);
}

void EmulatorController::loadFirmware(const QUrl &url) {
    const QString path = url.toLocalFile();
    firmwarePath_ = path;
    emit firmwarePathChanged();
    QMetaObject::invokeMethod(worker_, "loadPath", Qt::QueuedConnection, Q_ARG(QString, path));
}

void EmulatorController::loadBuiltin() {
    firmwarePath_ = QStringLiteral("(builtin blinky)");
    emit firmwarePathChanged();
    QMetaObject::invokeMethod(worker_, "loadBuiltin", Qt::QueuedConnection);
}

static bool parseAddr(const QString &t, quint32 *out) {
    bool ok = false;
    *out = t.trimmed().toUInt(&ok, 0);
    return ok;
}

bool EmulatorController::addressFromText(const QString &text, quint32 *out) {
    if (parseAddr(text, out))
        return true;
    status_ = QStringLiteral("Invalid address: \"%1\" (use hex like 0x08000000)").arg(text.trimmed());
    emit statusChanged();
    return false;
}

void EmulatorController::setBreakpointText(const QString &t) {
    if (breakpointText_ == t)
        return;
    breakpointText_ = t;
    emit breakpointTextChanged();
}

void EmulatorController::setMemoryAddressText(const QString &t) {
    if (memoryAddressText_ == t)
        return;
    memoryAddressText_ = t;
    emit memoryAddressTextChanged();
}

void EmulatorController::addBreakpoint() {
    quint32 addr = 0;
    if (!addressFromText(breakpointText_, &addr))
        return;
    QMetaObject::invokeMethod(worker_, "addBreakpoint", Qt::QueuedConnection, Q_ARG(quint32, addr));
}

void EmulatorController::removeBreakpoint() {
    quint32 addr = 0;
    if (!addressFromText(breakpointText_, &addr))
        return;
    QMetaObject::invokeMethod(worker_, "removeBreakpoint", Qt::QueuedConnection, Q_ARG(quint32, addr));
}

void EmulatorController::goMemory() {
    quint32 addr = 0;
    if (!addressFromText(memoryAddressText_, &addr))
        return;
    QMetaObject::invokeMethod(worker_, "requestMemory", Qt::QueuedConnection, Q_ARG(quint32, addr));
}

void EmulatorController::attachHighlighter(QQuickTextDocument *doc) {
    if (!doc)
        return;
    new CHighlighter(doc->textDocument());
}

void EmulatorController::compileAndLoad() {
    project_.saveAll();
    const QString work = dataRoot_ + "/project_build";
    const QString device = dataRoot_ + "/device/stm32f407";
    QString elf;
    QString log;
    const bool ok = builder_.compile(project_.files(), work, device, &elf, &log);
    buildLog_ = log;
    emit buildLogChanged();
    if (ok) {
        firmwarePath_ = elf;
        emit firmwarePathChanged();
        QMetaObject::invokeMethod(worker_, "loadPath", Qt::QueuedConnection, Q_ARG(QString, elf));
    }
}
