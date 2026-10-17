#include "toolchain/ArmGccBuilder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <memory>

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
    candidates << app + "/../../third_party/toolchain/bin/" + name; // build/tests/*.exe
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

QStringList ArmGccBuilder::halSourcesFor(const QString &halConf, const QStringList &available) {
    static const QRegularExpression re(QStringLiteral(R"(^[ \t]*#[ \t]*define[ \t]+HAL_(\w+)_MODULE_ENABLED\b)"),
                                       QRegularExpression::MultilineOption);
    QStringList out;
    auto add = [&](const QString &name) {
        if (available.contains(name) && !out.contains(name))
            out << name;
    };
    add(QStringLiteral("stm32f4xx_hal.c"));
    for (auto it = re.globalMatch(halConf); it.hasNext();) {
        const QString m = it.next().captured(1).toLower();
        // stm32f4xx_hal_<m>.c and its extensions: _ex.c, flash_ramfunc.c, ...
        const QString base = QStringLiteral("stm32f4xx_hal_") + m;
        add(base + ".c");
        for (const QString &f : available) {
            if (f.startsWith(base + "_") && f.endsWith(".c"))
                add(f);
        }
        // HAL modules built on low-level drivers
        if (m == "pcd" || m == "hcd")
            add(QStringLiteral("stm32f4xx_ll_usb.c"));
        if (m == "sd" || m == "mmc")
            add(QStringLiteral("stm32f4xx_ll_sdmmc.c"));
        if (m == "sram" || m == "nor" || m == "nand" || m == "sdram" || m == "pccard") {
            add(QStringLiteral("stm32f4xx_ll_fmc.c"));
            add(QStringLiteral("stm32f4xx_ll_fsmc.c"));
        }
    }
    return out;
}

namespace {

struct Job {
    QString source;
    QString object;
    QStringList args;
    bool verbose = true; // log the full command line (project files) or just the file (library)
};

// Writes content unless the file already has it, so unchanged files keep their mtime.
bool writeIfChanged(const QString &path, const QByteArray &content) {
    QFile in(path);
    if (in.open(QIODevice::ReadOnly) && in.readAll() == content)
        return true;
    in.close();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return out.write(content) == content.size();
}

bool hasSuffix(const QString &name, std::initializer_list<const char *> suffixes) {
    for (const char *s : suffixes) {
        if (name.endsWith(QLatin1String(s)))
            return true;
    }
    return false;
}

} // namespace

QStringList ArmGccBuilder::optimizationLevels() {
    return {"-O0", "-Og", "-O1", "-O2", "-O3", "-Os"};
}

bool ArmGccBuilder::elfSize(const QString &elf, quint32 *flash, quint32 *ram) const {
    QString gcc = gccPath_.isEmpty() ? findToolchain() : gccPath_;
    if (gcc.isEmpty())
        return false;
    QString size = gcc;
    size.replace(QRegularExpression("gcc(\\.exe)?$"), "size\\1");
    QProcess p;
    p.start(size, {elf});
    if (!p.waitForFinished(30000) || p.exitCode() != 0)
        return false;
    // "   text    data     bss     dec     hex filename" / "   1234     8   1576  ..."
    const QStringList lines = QString::fromUtf8(p.readAll()).split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 2)
        return false;
    const QStringList v = lines[1].simplified().split(' ');
    if (v.size() < 3)
        return false;
    const quint32 text = v[0].toUInt();
    const quint32 data = v[1].toUInt();
    const quint32 bss = v[2].toUInt();
    if (flash)
        *flash = text + data;
    if (ram)
        *ram = data + bss;
    return true;
}

bool ArmGccBuilder::compile(const QVector<QPair<QString, QString>> &files, const QString &workDir,
                            const QString &deviceDir, QString *elfOut, QString *log, const QString &optimization) {
    QElapsedTimer clock;
    clock.start();
    if (!optimizationLevels().contains(optimization)) {
        if (log)
            *log = "Unknown optimization level " + optimization + "\n";
        return false;
    }
    const QString gcc = gccPath_.isEmpty() ? findToolchain() : gccPath_;
    if (gcc != gccPath_) {
        gccPath_ = gcc;
        emit toolchainPathChanged();
    }
    if (gcc.isEmpty()) {
        if (log)
            *log = "arm-none-eabi-gcc not found. Run scripts/fetch_toolchain.ps1 or set ARM_NONE_EABI_GCC.\n";
        return false;
    }
    QString accumulated;
    auto fail = [&](const QString &why) {
        if (!why.isEmpty())
            accumulated += why + "\n";
        if (log)
            *log = accumulated;
        return false;
    };
    const QDir dev(deviceDir);
    const QString gccBin = QFileInfo(gcc).absoluteFilePath();
    accumulated += "Using " + gccBin + "\n";
    accumulated += "Device dir: " + deviceDir + "\n";

    // 1. Project files into workDir/src (same relative layout as the project folder).
    const QString srcDir = workDir + "/src";
    bool hal = false;
    bool rtos = false;
    QString halConf;
    QByteArray headerHash; // project headers: library objects depend on them (configs, main.h)
    QStringList sources;
    QStringList includeDirs{QDir(srcDir).absolutePath()};
    QSet<QString> projectNames; // lower-case file names
    QSet<QString> written;
    for (const auto &f : files) {
        const QString rel = QDir::fromNativeSeparators(f.first);
        const QString path = QDir(srcDir).absoluteFilePath(rel);
        written.insert(QDir::cleanPath(path));
        const QByteArray content = f.second.toUtf8();
        if (!writeIfChanged(path, content))
            return fail("Cannot write " + path);
        const QString name = QFileInfo(rel).fileName().toLower();
        projectNames.insert(name);
        if (hasSuffix(name, {".h", ".hpp", ".inc"})) {
            const QString dir = QFileInfo(path).absolutePath();
            if (!includeDirs.contains(dir))
                includeDirs << dir;
            headerHash += rel.toUtf8() + '\0' + content + '\0';
        }
        if (name == "stm32f4xx_hal_conf.h") {
            hal = true;
            halConf = f.second;
        }
        if (name == "freertosconfig.h")
            rtos = true;
        if (hasSuffix(name, {".c", ".s"}) || rel.endsWith(".S")) {
            // CubeMX projects bring their own startup; the emulator's one is equivalent and
            // matches its linker script, so it is always used instead.
            if (name.startsWith("startup_") && hasSuffix(name, {".s"})) {
                accumulated += "Skipping " + rel + ": the emulator's startup file is used\n";
                continue;
            }
            sources << rel;
        }
    }

    // Files removed from the project (or left by another project) must not be found by #include.
    for (QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString path = QDir::cleanPath(it.next());
        if (!written.contains(path))
            QFile::remove(path);
    }

    // 2. Flags.
    const QStringList cpu{"-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"};
    QStringList cflags = cpu;
    // Debug info at every level: the debugger needs it, and it does not change the code.
    cflags << "-g3" << optimization << "-ffunction-sections" << "-fdata-sections" << "-DSTM32F407xx";
    accumulated += "Optimization: " + optimization + "\n";
    for (const QString &d : includeDirs)
        cflags << "-I" << d;
    if (hal) {
        cflags << "-DUSE_HAL_DRIVER";
        cflags << "-I" << dev.absoluteFilePath("cube/hal/Inc") << "-I" << dev.absoluteFilePath("cube/cmsis/Include")
               << "-I" << dev.absoluteFilePath("cube/cmsis/Core");
        accumulated += "stm32f4xx_hal_conf.h found: building with STM32CubeF4 HAL (" + dev.absoluteFilePath("cube") + ")\n";
    } else {
        cflags << "-I" << dev.absoluteFilePath("include");
    }
    if (rtos) {
        cflags << "-I" << dev.absoluteFilePath("freertos/include")
               << "-I" << dev.absoluteFilePath("freertos/portable/GCC/ARM_CM4F");
        accumulated += "FreeRTOSConfig.h found: building with FreeRTOS kernel (" + dev.absoluteFilePath("freertos") + ")\n";
    }

    // 3. Library sources, unless the project has a file with the same name (e.g. its own
    // system_stm32f4xx.c or a FreeRTOS copy in Middlewares/).
    QStringList lib{dev.absoluteFilePath("startup_stm32f407xx.s")};
    auto addLib = [&](const QString &path) {
        if (!projectNames.contains(QFileInfo(path).fileName().toLower()))
            lib << path;
    };
    addLib(hal ? dev.absoluteFilePath("cube/cmsis/system_stm32f4xx.c") : dev.absoluteFilePath("system_stm32f4xx.c"));
    if (hal) {
        const QDir halSrc(dev.absoluteFilePath("cube/hal/Src"));
        for (const QString &f : halSourcesFor(halConf, halSrc.entryList({"*.c"}, QDir::Files)))
            addLib(halSrc.absoluteFilePath(f));
    }
    if (rtos) {
        for (const char *f : {"tasks.c", "queue.c", "list.c", "timers.c", "event_groups.c", "stream_buffer.c"})
            addLib(dev.absoluteFilePath(QString("freertos/") + f));
        addLib(dev.absoluteFilePath("freertos/portable/GCC/ARM_CM4F/port.c"));
        bool ownHeap = false;
        for (const QString &n : projectNames)
            ownHeap = ownHeap || QRegularExpression("^heap_[1-5]\\.c$").match(n).hasMatch();
        if (!ownHeap)
            lib << dev.absoluteFilePath("freertos/portable/MemMang/heap_4.c");
    }

    // 4. Compile jobs: project files always, library files only when their cached object is
    // missing or was built with other flags / project headers.
    const QString objDir = workDir + "/obj";
    // One library cache per level: switching -O0 <-> -O2 back and forth does not rebuild HAL.
    const QString libDir = workDir + "/lib" + QString(optimization).remove('-');
    QDir().mkpath(objDir);
    QDir().mkpath(libDir);
    QCryptographicHash stampHash(QCryptographicHash::Sha1);
    stampHash.addData((gccBin + '\n' + cflags.join(' ') + '\n' + lib.join('\n')).toUtf8());
    stampHash.addData(headerHash);
    const QByteArray stamp = stampHash.result().toHex();
    QFile stampFile(libDir + "/stamp");
    const bool libValid = stampFile.open(QIODevice::ReadOnly) && stampFile.readAll() == stamp;
    stampFile.close();

    QVector<Job> jobs;
    QStringList objs;
    for (const QString &rel : sources) {
        QString objName = rel;
        objName.replace('/', '_').replace(':', '_');
        Job j;
        j.source = QDir(srcDir).absoluteFilePath(rel);
        j.object = objDir + "/" + objName + ".o";
        j.args << "-c" << j.source << "-o" << j.object << cflags;
        jobs << j;
        objs << j.object;
    }
    int cached = 0;
    for (const QString &src : lib) {
        Job j;
        j.source = src;
        j.object = libDir + "/" + QFileInfo(src).fileName() + ".o";
        j.args << "-c" << src << "-o" << j.object << cflags;
        j.verbose = false;
        objs << j.object;
        if (libValid && QFileInfo::exists(j.object)) {
            ++cached;
            continue;
        }
        jobs << j;
    }
    if (!libValid)
        QFile::remove(libDir + "/stamp");
    if (cached)
        accumulated += QString("%1 library objects up to date\n").arg(cached);

    // 5. Run the jobs in parallel; output is logged in job order.
    const int parallel = std::max(1, QThread::idealThreadCount());
    bool ok = true;
    QVector<std::shared_ptr<QProcess>> procs(jobs.size());
    int next = 0;
    int logged = 0;
    auto finishLog = [&](int i) {
        const Job &j = jobs[i];
        QProcess &p = *procs[i];
        if (j.verbose)
            accumulated += "\n$ arm-none-eabi-gcc " + j.args.join(' ') + "\n";
        else
            accumulated += "CC " + QDir(deviceDir).relativeFilePath(j.source) + "\n";
        accumulated += QString::fromUtf8(p.readAll());
        if (p.error() == QProcess::FailedToStart) {
            accumulated += "Failed to start gcc: " + p.errorString() + "\n";
            ok = false;
        } else if (p.exitStatus() != QProcess::NormalExit) {
            accumulated += "gcc crashed or timed out: " + p.errorString() + "\n";
            ok = false;
        } else if (p.exitCode() != 0) {
            // A normal non-zero exit (compile errors); errorString() would say "Unknown error".
            accumulated += QString("gcc exit %1\n").arg(p.exitCode());
            ok = false;
        }
    };
    while (logged < jobs.size()) {
        while (ok && next < jobs.size() && next - logged < parallel) {
            auto p = std::make_shared<QProcess>();
            p->setWorkingDirectory(workDir);
            p->setProcessChannelMode(QProcess::MergedChannels);
            p->start(gccBin, jobs[next].args);
            procs[next++] = p;
        }
        if (logged == next)
            break; // stopped starting new jobs after an error
        QProcess &p = *procs[logged];
        if (!p.waitForFinished(120000))
            p.kill();
        p.waitForFinished(1000);
        finishLog(logged++);
    }
    if (!ok)
        return fail({});
    // Only a complete, successful set of library objects makes the cache valid.
    if (!libValid) {
        QFile out(libDir + "/stamp");
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(stamp);
    }

    // 6. Link.
    const QString ld = dev.absoluteFilePath("STM32F407VGTx_FLASH.ld");
    const QString elf = workDir + "/firmware.elf";
    QStringList link = cpu;
    link << "-T" << ld << "-nostartfiles" << "-Wl,--gc-sections" << "-Wl,-Map=" + workDir + "/firmware.map" << "-o" << elf
         << objs << "--specs=nano.specs" << "-lc" << "-lm" << "-lnosys";
    accumulated += "\n$ arm-none-eabi-gcc " + link.join(' ') + "\n";
    QProcess p;
    p.setWorkingDirectory(workDir);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(gccBin, link);
    if (!p.waitForFinished(120000)) {
        p.kill();
        return fail("link timed out or failed to start: " + p.errorString());
    }
    accumulated += QString::fromUtf8(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return fail(QString("link failed (exit %1)").arg(p.exitCode()));
    if (elfOut)
        *elfOut = elf;
    quint32 flash = 0;
    quint32 ram = 0;
    if (elfSize(elf, &flash, &ram))
        accumulated += QString("\nSize (%1): flash %2 bytes, RAM %3 bytes (static)\n").arg(optimization).arg(flash).arg(ram);
    if (log)
        *log = accumulated + QString("\nLinked %1 in %2 s\n").arg(elf).arg(clock.elapsed() / 1000.0, 0, 'f', 1);
    return true;
}
