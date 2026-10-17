// The UI debugger end to end: a project built like Build & Load, the Machine behind the GDB
// server, and DebugSession driving the real arm-none-eabi-gdb over MI. Skipped without the
// toolchain.
#include "TestUtil.h"

#include "debug/DebugSession.h"
#include "debug/GdbMi.h"
#include "debug/GdbServer.h"
#include "toolchain/ArmGccBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>

namespace {

const QString kSource = QStringLiteral(MK_EMUL_SOURCE_DIR);
const QString kWork = QStringLiteral(MK_EMUL_BUILD_DIR);

const char *kProgram = R"(#include "stm32f407xx.h"
struct point { int x; int y; };
volatile uint32_t counter;
static struct point origin = {3, 4};
int values[4] = {10, 20, 30, 40};

static int add(int a, int b) {
    int sum = a + b; // @add
    return sum;
}

int main(void) {
    int total = 0;
    struct point p = {1, 2};
    for (int i = 0; i < 4; ++i) {
        total = add(total, values[i]); // @call
        counter++;                     // @after
    }
    p.x = total + origin.x;            // @end
    for (;;) {
        counter++;                     // @loop
    }
}
)";

int lineOf(const char *marker) {
    const QStringList lines = QString(kProgram).split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].contains(marker))
            return i + 1;
    }
    return -1;
}

bool waitFor(const std::function<bool()> &done, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// What MachineWorker does in the app: runs the machine while gdb has it running.
struct Runner {
    Machine machine;
    bool running = false;
    GdbServer *server = nullptr;
    QTimer timer;

    Runner() {
        timer.setInterval(0);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            if (!running)
                return;
            const auto stop = machine.runQuantum(20000);
            if (stop == CpuEngine::StopReason::Breakpoint || stop == CpuEngine::StopReason::StepDone ||
                stop == CpuEngine::StopReason::Fault)
                halt(GdbStub::kSigTrap);
        });
        timer.start();
    }
    void halt(int sig) {
        running = false;
        machine.cpu().clearStepRange();
        server->stub().stopped(sig);
    }
    GdbStub::Target target() {
        return {[this](uint32_t start, uint32_t end) {
                    machine.cpu().setStepRange(start, end);
                    machine.cpu().resumeHere();
                    running = true;
                },
                [this] { halt(GdbStub::kSigInt); }, nullptr, nullptr};
    }
};

QString rowValue(QObject *model, const QString &name) {
    auto *m = static_cast<RowsModel *>(model);
    for (const QVariantMap &r : m->rows()) {
        if (r["name"].toString() == name && !r["header"].toBool())
            return r["value"].toString();
    }
    return QStringLiteral("<missing>");
}

int rowIndex(QObject *model, const QString &name) {
    auto *m = static_cast<RowsModel *>(model);
    for (int i = 0; i < m->rows().size(); ++i) {
        if (m->rows()[i]["name"].toString() == name)
            return i;
    }
    return -1;
}

struct DebuggerTest : ::testing::Test {
    ArmGccBuilder builder;
    Runner runner;
    std::unique_ptr<GdbServer> server;
    DebugSession session;
    QString srcRoot;
    int refreshes = 0;

    virtual QString optimization() const { return QStringLiteral("-O0"); }

    void SetUp() override {
        const QString gcc = builder.findToolchain();
        if (gcc.isEmpty())
            GTEST_SKIP() << "arm-none-eabi-gcc not found";
        QString elf, log;
        const QString work = kWork + "/dbg";
        ASSERT_TRUE(builder.compile({{"main.c", QString(kProgram)}}, work, kSource + "/device/stm32f407", &elf, &log, optimization()))
            << log.toStdString();
        srcRoot = QDir(work + "/src").absolutePath();
        std::string err;
        ASSERT_TRUE(runner.machine.initialize(&err)) << err;
        ASSERT_TRUE(runner.machine.loadFirmware(elf.toStdString()).ok);
        server = std::make_unique<GdbServer>(runner.machine, runner.target());
        runner.server = server.get();
        ASSERT_TRUE(server->listen());
        QObject::connect(&session, &DebugSession::refreshed, [this] { ++refreshes; });
        const QString gdb = QFileInfo(gcc).absolutePath() + "/arm-none-eabi-gdb.exe";
        session.start(gdb, elf, server->port(), srcRoot);
        ASSERT_TRUE(waitFor([&] { return session.state() == "stopped" && refreshes > 0; }))
            << session.state().toStdString() << " " << session.message().toStdString();
        settle();
    }
    // Waits until no view update has arrived for a while (the connect may refresh twice).
    void settle() {
        int last = -1;
        while (last != refreshes) {
            last = refreshes;
            QElapsedTimer t;
            t.start();
            waitFor([&] { return t.elapsed() > 300; });
        }
    }
    void TearDown() override { session.stop(); }

    // Runs a debugger action and waits until the views are refreshed after it (for run and
    // step commands: after the target ran and stopped again).
    bool act(const std::function<void()> &fn, bool runs = true, int ms = 10000) {
        bool ran = false;
        int after = -1;
        auto c1 = QObject::connect(&session, &DebugSession::stateChanged, [&] {
            if (session.state() == "running")
                ran = true;
        });
        auto c2 = QObject::connect(&session, &DebugSession::refreshed, [&] {
            if (ran || !runs)
                after = refreshes;
        });
        fn();
        const bool ok = waitFor([&] { return after >= 0 && session.state() == "stopped"; }, ms);
        QObject::disconnect(c1);
        QObject::disconnect(c2);
        return ok;
    }
    bool breakpointOk(int line) {
        return waitFor([&] {
            for (const QVariant &v : session.breakpointLines("main.c")) {
                if (v.toMap()["line"].toInt() == line && v.toMap()["state"] == "ok")
                    return true;
            }
            return false;
        });
    }
};

TEST(GdbMi, ParsesRecords) {
    const MiRecord r = parseMiRecord(
        R"(12^done,bkpt={number="1",addr="0x08000234",line="15",locations=[{number="1.1"}]},x=["a","b\"c"])");
    EXPECT_EQ(r.type, MiRecord::Result);
    EXPECT_EQ(r.token, 12);
    EXPECT_EQ(r.cls, "done");
    EXPECT_EQ(r.value["bkpt"].text("addr"), "0x08000234");
    EXPECT_EQ(r.value["bkpt"]["locations"].items.size(), 1);
    EXPECT_EQ(r.value["x"].items[1].second.str, "b\"c");
    const MiRecord s = parseMiRecord(R"(*stopped,reason="end-stepping-range",frame={func="main",line="7"})");
    EXPECT_EQ(s.type, MiRecord::Exec);
    EXPECT_EQ(s.cls, "stopped");
    EXPECT_EQ(s.value["frame"].text("func"), "main");
    EXPECT_EQ(parseMiRecord(R"(~"\320\237\321\200\n")").value.str, QString::fromUtf8("\xD0\x9F\xD1\x80\n"));
    EXPECT_EQ(miQuote(R"(C:/a b/"x".c)"), R"("C:/a b/\"x\".c")");
}

TEST_F(DebuggerTest, LineBreakpointVariablesAndStepping) {
    const int callLine = lineOf("@call");
    session.toggleBreakpoint("main.c", callLine);
    ASSERT_TRUE(breakpointOk(callLine)) << session.message().toStdString();

    // Continue -> stops on the source line, in main(), with locals and globals.
    ASSERT_TRUE(act([&] { session.continueRun(); })) << session.message().toStdString();
    EXPECT_EQ(session.currentFile(), "main.c");
    EXPECT_EQ(session.currentLine(), callLine);
    EXPECT_EQ(rowValue(session.variables(), "total"), "0");
    EXPECT_EQ(rowValue(session.variables(), "i"), "0");
    EXPECT_EQ(rowValue(session.variables(), "counter"), "0");
    EXPECT_EQ(rowValue(session.variables(), "values"), "[4]") << "arrays and structs expand";
    EXPECT_EQ(rowValue(session.variables(), "origin"), "{...}");
    session.toggleExpand(false, rowIndex(session.variables(), "values"));
    ASSERT_TRUE(waitFor([&] { return rowValue(session.variables(), "[3]") == "40"; }));

    // Step Over: the call runs as one line; total changed (marked for the UI).
    ASSERT_TRUE(act([&] { session.stepOver(); }));
    EXPECT_EQ(session.currentLine(), lineOf("@after"));
    EXPECT_EQ(rowValue(session.variables(), "total"), "10");
    const auto &rows = static_cast<RowsModel *>(session.variables())->rows();
    EXPECT_TRUE(rows[rowIndex(session.variables(), "total")]["changed"].toBool());

    // Expand a struct.
    const int p = rowIndex(session.variables(), "p");
    ASSERT_GE(p, 0);
    EXPECT_TRUE(rows[p]["expandable"].toBool());
    session.toggleExpand(false, p);
    ASSERT_TRUE(waitFor([&] { return rowValue(session.variables(), "y") == "2"; }));
    EXPECT_EQ(rowValue(session.variables(), "x"), "1");

    // Next iteration, Step Into add(): call stack main <- add.
    ASSERT_TRUE(act([&] { session.continueRun(); }));
    EXPECT_EQ(session.currentLine(), callLine);
    EXPECT_EQ(rowValue(session.variables(), "i"), "1");
    ASSERT_TRUE(act([&] { session.stepInto(); }));
    EXPECT_EQ(session.currentLine(), lineOf("@add"));
    const auto &frames = static_cast<RowsModel *>(session.frames())->rows();
    ASSERT_GE(frames.size(), 2);
    EXPECT_EQ(frames[0]["func"].toString(), "add");
    EXPECT_EQ(frames[1]["func"].toString(), "main");
    EXPECT_EQ(rowValue(session.variables(), "a"), "10");
    EXPECT_EQ(rowValue(session.variables(), "b"), "20");

    // The caller's frame shows main's locals and line.
    ASSERT_TRUE(act([&] { session.selectFrame(1); }, false));
    EXPECT_EQ(session.currentLine(), callLine);
    EXPECT_EQ(rowValue(session.variables(), "total"), "10");

    // Step Out returns from the selected frame: back to add(), then out of it to main.
    ASSERT_TRUE(act([&] { session.selectFrame(0); }, false));
    EXPECT_EQ(session.currentLine(), lineOf("@add"));
    ASSERT_TRUE(act([&] { session.stepOut(); })) << session.message().toStdString();
    EXPECT_EQ(static_cast<RowsModel *>(session.frames())->rows()[0]["func"].toString(), "main");
    EXPECT_TRUE(session.message().contains("returned 30")) << session.message().toStdString();
}

TEST_F(DebuggerTest, WatchHexAndAssign) {
    session.toggleBreakpoint("main.c", lineOf("@end"));
    ASSERT_TRUE(breakpointOk(lineOf("@end")));
    ASSERT_TRUE(act([&] { session.continueRun(); }));
    EXPECT_EQ(rowValue(session.variables(), "total"), "100");

    ASSERT_TRUE(act([&] { session.addWatch("values[1] * 2"); }, false));
    EXPECT_EQ(rowValue(session.watches(), "values[1] * 2"), "40");
    ASSERT_TRUE(act([&] { session.addWatch("no_such_name"); }, false));
    const auto &w = static_cast<RowsModel *>(session.watches())->rows();
    EXPECT_TRUE(w[rowIndex(session.watches(), "no_such_name")]["error"].toBool());

    ASSERT_TRUE(act([&] { session.setHex(true); }, false));
    EXPECT_EQ(rowValue(session.variables(), "total"), "0x64");
    ASSERT_TRUE(act([&] { session.setHex(false); }, false));

    // Edit a value: the firmware sees it.
    ASSERT_TRUE(act([&] { session.setValue(false, rowIndex(session.variables(), "total"), "7"); }, false));
    EXPECT_EQ(rowValue(session.variables(), "total"), "7");
    ASSERT_TRUE(act([&] { session.stepOver(); }));
    const int p = rowIndex(session.variables(), "p");
    session.toggleExpand(false, p);
    ASSERT_TRUE(waitFor([&] { return rowValue(session.variables(), "x") == "10"; })) << "p.x = 7 + origin.x";
}

TEST_F(DebuggerTest, BreakpointWhileRunningAndPause) {
    // Running in the endless loop: Pause stops it, with the location in main.c.
    ASSERT_TRUE(waitFor([&] { return session.state() == "stopped"; }));
    session.continueRun();
    ASSERT_TRUE(waitFor([&] { return session.state() == "running"; }));
    ASSERT_TRUE(waitFor([&] { return runner.machine.cpu().instructionCount() > 200000; }));
    ASSERT_TRUE(act([&] { session.interrupt(); }, false));
    EXPECT_EQ(session.currentFile(), "main.c");
    EXPECT_TRUE(session.message().startsWith("Paused")) << session.message().toStdString();
    const uint64_t counter = rowValue(session.variables(), "counter").toULongLong();
    EXPECT_GT(counter, 4u);

    // A breakpoint set while running: gdb is interrupted, inserts it and continues.
    session.continueRun();
    ASSERT_TRUE(waitFor([&] { return session.state() == "running"; }));
    ASSERT_TRUE(act([&] { session.toggleBreakpoint("main.c", lineOf("@loop")); }));
    EXPECT_EQ(session.currentLine(), lineOf("@loop"));
    EXPECT_TRUE(session.message().startsWith("Breakpoint")) << session.message().toStdString();

    // Removed: Continue runs on.
    session.toggleBreakpoint("main.c", lineOf("@loop"));
    EXPECT_TRUE(session.breakpointLines("main.c").isEmpty());
    session.continueRun();
    ASSERT_TRUE(waitFor([&] { return session.state() == "running"; }));
    QElapsedTimer t;
    t.start();
    waitFor([&] { return t.elapsed() > 300; });
    EXPECT_EQ(session.state(), "running");
    ASSERT_TRUE(act([&] { session.interrupt(); }, false));
}

TEST_F(DebuggerTest, LineWithoutCodeMovesToTheNextOne) {
    // An empty line inside main(): gdb puts the breakpoint on the next line with code.
    const int blank = lineOf("int main(void)") - 1;
    session.toggleBreakpoint("main.c", blank + 2); // "int total = 0;" line
    ASSERT_TRUE(breakpointOk(blank + 2));
    session.toggleBreakpoint("main.c", 999);
    ASSERT_TRUE(waitFor([&] {
        for (const QVariant &v : session.breakpointLines("main.c")) {
            if (v.toMap()["line"].toInt() == 999)
                return v.toMap()["state"] == "error";
        }
        return false;
    }));
}

// Build menu -> Optimization -O2: gdb still stops on source lines and shows variables
// (some as <optimized out>, as on a real board).
struct DebuggerOptimizedTest : DebuggerTest {
    QString optimization() const override { return QStringLiteral("-O2"); }
};

TEST_F(DebuggerOptimizedTest, BreakpointsAndSteppingWorkWithO2) {
    const int loop = lineOf("@loop");
    session.toggleBreakpoint("main.c", loop);
    ASSERT_TRUE(breakpointOk(loop)) << session.message().toStdString();
    ASSERT_TRUE(act([&] { session.continueRun(); })) << session.message().toStdString();
    EXPECT_EQ(session.currentFile(), "main.c");
    EXPECT_EQ(session.currentLine(), loop);
    const QString counter = rowValue(session.variables(), "counter");
    EXPECT_GE(counter.toInt(), 4) << counter.toStdString();
    // The optimizer removed the loop over values[] (its result is unused), so is the array.
    EXPECT_EQ(rowValue(session.variables(), "values"), "<missing>");
    ASSERT_TRUE(act([&] { session.continueRun(); }));
    EXPECT_GT(rowValue(session.variables(), "counter").toInt(), counter.toInt()) << "the loop ran on";
    EXPECT_EQ(session.currentLine(), loop);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
