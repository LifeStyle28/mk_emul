// The in-app builder (Build & Load): builds the examples the same way the UI does, with the
// bundled device files, HAL and FreeRTOS, then runs them on the Machine.
#include "TestUtil.h"
#include "toolchain/ArmGccBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>

#include <gtest/gtest.h>

#include <string>

namespace {

using Files = QVector<QPair<QString, QString>>;

const QString kSource = QStringLiteral(MK_EMUL_SOURCE_DIR);
const QString kWork = QStringLiteral(MK_EMUL_BUILD_DIR);
const QString kDevice = kSource + "/device/stm32f407";

QString readText(const QString &path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return QString::fromUtf8(f.readAll());
}

// Project files of an example folder, as ProjectModel gives them (relative path, content).
Files exampleFiles(const QString &name) {
    Files out;
    const QDir root(kSource + "/examples/" + name);
    for (QDirIterator it(root.path(), {"*.c", "*.h", "*.s"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString path = it.next();
        out.push_back({root.relativeFilePath(path), readText(path)});
    }
    return out;
}

struct BuilderTest : ::testing::Test {
    ArmGccBuilder builder;
    QString elf;
    QString log;

    void SetUp() override {
        if (builder.findToolchain().isEmpty())
            GTEST_SKIP() << "arm-none-eabi-gcc not found";
    }
    bool build(const Files &files, const QString &work) {
        elf.clear();
        return builder.compile(files, kWork + "/" + work, kDevice, &elf, &log);
    }
};

// Loads the ELF and runs it; returns the UART output. Fails on faults and on accesses to
// peripherals the emulator does not have.
std::string runElf(const QString &elf, uint64_t insns) {
    Machine m;
    std::string err;
    EXPECT_TRUE(m.initialize(&err)) << err;
    std::string uart;
    std::string warnings;
    m.setUartTx([&](uint8_t b) { uart.push_back(static_cast<char>(b)); });
    m.setWarningSink([&](const std::string &w) { warnings += w + "; "; });
    const auto img = m.loadFirmware(elf.toStdString());
    EXPECT_TRUE(img.ok) << img.error;
    EXPECT_NE(runFor(m, insns), CpuEngine::StopReason::Fault) << m.status();
    EXPECT_EQ(warnings, "");
    return uart;
}

TEST(HalSources, PicksEnabledModulesAndTheirExtensions) {
    const QStringList available = QDir(kDevice + "/cube/hal/Src").entryList({"*.c"}, QDir::Files);
    const QString conf = "#define HAL_MODULE_ENABLED\n"
                         "#define HAL_RCC_MODULE_ENABLED\n"
                         "  #define HAL_FLASH_MODULE_ENABLED\n"
                         "/* #define HAL_TIM_MODULE_ENABLED */\n"
                         "// #define HAL_SPI_MODULE_ENABLED\n"
                         "#define HAL_PCD_MODULE_ENABLED\n";
    const QStringList got = ArmGccBuilder::halSourcesFor(conf, available);
    EXPECT_TRUE(got.contains("stm32f4xx_hal.c"));
    EXPECT_TRUE(got.contains("stm32f4xx_hal_rcc.c"));
    EXPECT_TRUE(got.contains("stm32f4xx_hal_rcc_ex.c"));
    EXPECT_TRUE(got.contains("stm32f4xx_hal_flash_ramfunc.c"));
    EXPECT_TRUE(got.contains("stm32f4xx_ll_usb.c")) << "PCD needs the USB low-level driver";
    EXPECT_FALSE(got.contains("stm32f4xx_hal_tim.c")) << "commented out";
    EXPECT_FALSE(got.contains("stm32f4xx_hal_spi.c")) << "commented out";
}

TEST_F(BuilderTest, BuildsAndRunsTheRegisterLevelExamples) {
    for (const char *name : {"blinky", "uart_printf", "systick_exti", "fpu_irq", "tim4_pwm"}) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(build(exampleFiles(name), name)) << log.toStdString();
        runElf(elf, 2000000);
    }
}

TEST_F(BuilderTest, BuildsHalProjectWithBundledHal) {
    ASSERT_TRUE(build(exampleFiles("hal_blinky"), "hal")) << log.toStdString();
    EXPECT_TRUE(log.contains("STM32CubeF4 HAL"));
    const auto uart = runElf(elf, 40000000);
    EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=168000000\r\n", 0), 0u) << uart;
    EXPECT_NE(uart.find("tick 2\r\n"), std::string::npos) << uart;

    // Second build: library objects come from the cache, only main.c is compiled.
    ASSERT_TRUE(build(exampleFiles("hal_blinky"), "hal")) << log.toStdString();
    EXPECT_TRUE(log.contains("library objects up to date")) << log.toStdString();
    EXPECT_FALSE(log.contains("CC cube/")) << log.toStdString();
}

TEST_F(BuilderTest, BuildsFreeRtosProjectWithBundledKernel) {
    ASSERT_TRUE(build(exampleFiles("freertos"), "freertos")) << log.toStdString();
    EXPECT_TRUE(log.contains("FreeRTOS kernel"));
    const auto uart = runElf(elf, 5000000);
    EXPECT_EQ(uart.rfind("FreeRTOS start\r\n", 0), 0u) << uart;
    EXPECT_NE(uart.find("rx 2 "), std::string::npos) << uart;
}

// Layout of an STM32CubeMX / CubeIDE project: Core/Inc, Core/Src, own system file, own
// startup (replaced by the emulator's) and sysmem.c, which needs the CubeMX linker symbols.
TEST_F(BuilderTest, BuildsCubeMxProjectLayout) {
    const QString hal = kSource + "/examples/hal_blinky/";
    QString main = readText(hal + "main.c");
    main.replace("#include \"stm32f4xx_hal.h\"", "#include \"main.h\"\nuint8_t *sysmem_bounds(void);");
    main.replace("    HAL_Init();", "    (void)sysmem_bounds(); // keep sysmem.c in the link (--gc-sections)\n    HAL_Init();");
    ASSERT_TRUE(main.contains("sysmem_bounds();"));
    Files files{
        {"Core/Inc/stm32f4xx_hal_conf.h", readText(hal + "stm32f4xx_hal_conf.h")},
        {"Core/Inc/main.h", "#include \"stm32f4xx_hal.h\"\n"},
        {"Core/Src/main.c", main},
        {"Core/Src/system_stm32f4xx.c", readText(kDevice + "/cube/cmsis/system_stm32f4xx.c")},
        {"Core/Src/sysmem.c",
         "#include <stddef.h>\n#include <stdint.h>\n"
         "extern uint8_t _end, _estack; extern uint32_t _Min_Stack_Size;\n"
         "uint8_t *sysmem_bounds(void) { return &_end + (size_t)&_Min_Stack_Size + (size_t)&_estack; }\n"},
        {"Core/Startup/startup_stm32f407vgtx.s", "this is not assembly: the file must be skipped\n"},
    };
    ASSERT_TRUE(build(files, "cubemx")) << log.toStdString();
    EXPECT_TRUE(log.contains("Skipping Core/Startup/startup_stm32f407vgtx.s")) << log.toStdString();
    const auto uart = runElf(elf, 20000000);
    EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=168000000\r\n", 0), 0u) << uart;
}

TEST_F(BuilderTest, ReportsCompileErrors) {
    Files files{{"main.c", "int main(void) { return undefined_name; }\n"}};
    EXPECT_FALSE(build(files, "error"));
    EXPECT_TRUE(log.contains("undefined_name")) << log.toStdString();
    EXPECT_TRUE(log.contains("gcc exit 1")) << log.toStdString();
}

// A header removed from the project must not stay visible through the build copy.
TEST_F(BuilderTest, RemovedHeaderIsNotFound) {
    Files files{{"main.c", "#include \"cfg.h\"\nint main(void) { for (;;) {} }\n"}, {"cfg.h", "#define X 1\n"}};
    ASSERT_TRUE(build(files, "removed")) << log.toStdString();
    files.removeLast();
    EXPECT_FALSE(build(files, "removed"));
    EXPECT_TRUE(log.contains("cfg.h")) << log.toStdString();
}

// C++-style static initialization: __libc_init_array runs constructors before main.
TEST_F(BuilderTest, RunsStaticConstructorsBeforeMain) {
    Files files{{"main.c",
                 "#include \"stm32f407xx.h\"\n"
                 "static volatile int ready;\n"
                 "__attribute__((constructor)) static void init(void) { ready = 1; }\n"
                 "__attribute__((section(\".ccmram\"))) volatile int in_ccm = 7;\n"
                 "int main(void) {\n"
                 "    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;\n"
                 "    GPIOD->MODER = 0x55u << 24;\n"
                 "    if (ready && in_ccm == 7) GPIOD->BSRR = 1u << 12; else GPIOD->BSRR = 1u << 14;\n"
                 "    for (;;) {}\n"
                 "}\n"}};
    ASSERT_TRUE(build(files, "ctor")) << log.toStdString();
    Machine m;
    std::string err;
    ASSERT_TRUE(m.initialize(&err)) << err;
    ASSERT_TRUE(m.loadFirmware(elf.toStdString()).ok);
    runFor(m, 100000);
    EXPECT_EQ(m.snapshot().leds, 0x1u) << "green: constructor ran and .ccmram was initialized";
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); // ArmGccBuilder looks for the toolchain next to the exe
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
