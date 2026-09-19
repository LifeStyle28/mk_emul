#include "toolchain/ArmGccBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

ArmGccBuilder::ArmGccBuilder(QObject *parent)
    : QObject(parent) {
    gccPath_ = findToolchain();
}

static bool isGcc(const QString &path) {
    return QFile::exists(path);
}

QString ArmGccBuilder::findToolchain() const {
    const QString name = QStringLiteral("arm-none-eabi-gcc.exe");
    QStringList candidates;
    const QString app = QCoreApplication::applicationDirPath();
    candidates << app + "/toolchain/bin/" + name;
    candidates << app + "/../third_party/toolchain/bin/" + name;
    candidates << app + "/../toolchain/bin/" + name;
    if (qEnvironmentVariableIsSet("ARM_NONE_EABI_GCC"))
        candidates.prepend(qEnvironmentVariable("ARM_NONE_EABI_GCC"));
    const auto pathEnv = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &p : pathEnv)
        candidates << QDir(p).filePath(name);
    // xpack default
    candidates << QDir::homePath() + "/AppData/Roaming/xPacks/arm-none-eabi-gcc";
    for (const QString &c : candidates) {
        if (isGcc(c))
            return QFileInfo(c).absoluteFilePath();
        if (QDir(c).exists()) {
            const auto found = QDir(c).entryList(QStringList() << "arm-none-eabi-gcc.exe", QDir::Files | QDir::Executable, QDir::Name);
            if (!found.isEmpty())
                return QDir(c).filePath(found.first());
        }
    }
    return {};
}

bool ArmGccBuilder::compile(const QVector<QPair<QString, QString>> &files, const QString &workDir,
                            const QString &deviceDir, QString *elfOut, QString *log) {
    QString gcc = gccPath_.isEmpty() ? findToolchain() : gccPath_;
    gccPath_ = gcc;
    emit toolchainPathChanged();
    if (gcc.isEmpty()) {
        if (log)
            *log = "arm-none-eabi-gcc not found. Run scripts/fetch_toolchain.ps1 or set ARM_NONE_EABI_GCC.\n";
        return false;
    }
    QDir().mkpath(workDir);
    QStringList objs;
    QString accumulated;
    const QString include = QDir(deviceDir + "/include").absolutePath();
    const QString gccBin = QFileInfo(gcc).absoluteFilePath();
    accumulated += "Using " + gccBin + "\n";
    accumulated += "Device dir: " + deviceDir + "\n";

    auto run = [&](const QStringList &args) {
        accumulated += "\n$ arm-none-eabi-gcc " + args.join(' ') + "\n";
        QProcess p;
        p.setWorkingDirectory(workDir);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(gccBin, args);
        if (!p.waitForStarted(10000)) {
            accumulated += "Failed to start gcc: " + p.errorString() + "\n";
            return false;
        }
        if (!p.waitForFinished(120000)) {
            p.kill();
            accumulated += "gcc timed out\n";
            return false;
        }
        accumulated += QString::fromUtf8(p.readAll());
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            accumulated += QString("gcc exit %1 (%2)\n").arg(p.exitCode()).arg(p.errorString());
            return false;
        }
        return true;
    };

    for (const auto &f : files) {
        const QString path = workDir + "/" + QDir::fromNativeSeparators(f.first);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (log)
                *log = "Cannot write " + path;
            return false;
        }
        out.write(f.second.toUtf8());
        out.close();
        if (!f.first.endsWith(".c") && !f.first.endsWith(".s") && !f.first.endsWith(".S"))
            continue;
        const QString obj = workDir + "/" + QFileInfo(f.first).completeBaseName() + ".o";
        QStringList args;
        args << "-c" << path << "-o" << obj << "-mcpu=cortex-m4" << "-mthumb" << "-mfpu=fpv4-sp-d16"
             << "-mfloat-abi=hard" << "-g3" << "-O0" << "-ffunction-sections" << "-fdata-sections"
             << "-I" << include << "-I" << workDir << "-DSTM32F407xx";
        if (!run(args)) {
            if (log)
                *log = accumulated;
            return false;
        }
        objs << obj;
    }

    const QString startup = QDir(deviceDir).filePath("startup_stm32f407xx.s");
    const QString systemc = QDir(deviceDir).filePath("system_stm32f4xx.c");
    const QString ld = QDir(deviceDir).filePath("STM32F407VGTx_FLASH.ld");
    for (const QString &extra : {startup, systemc}) {
        if (!QFile::exists(extra))
            continue;
        const QString obj = workDir + "/" + QFileInfo(extra).completeBaseName() + ".o";
        QStringList args;
        args << "-c" << extra << "-o" << obj << "-mcpu=cortex-m4" << "-mthumb" << "-mfpu=fpv4-sp-d16"
             << "-mfloat-abi=hard" << "-g3" << "-O0" << "-I" << include << "-DSTM32F407xx";
        if (!run(args)) {
            if (log)
                *log = accumulated;
            return false;
        }
        objs << obj;
    }

    const QString elf = workDir + "/firmware.elf";
    QStringList link;
    link << "-mcpu=cortex-m4" << "-mthumb" << "-mfpu=fpv4-sp-d16" << "-mfloat-abi=hard" << "-T" << ld
         << "-nostartfiles" << "-Wl,--gc-sections" << "-Wl,-Map=" + workDir + "/firmware.map" << "-o" << elf << objs
         << "--specs=nano.specs" << "-lc" << "-lm" << "-lnosys";
    if (!run(link)) {
        if (log)
            *log = accumulated;
        return false;
    }
    if (elfOut)
        *elfOut = elf;
    if (log)
        *log = accumulated + "\nLinked " + elf + "\n";
    return true;
}
