// The in-app builder (Build & Load): builds the examples the same way the UI does, with the
// bundled device files, HAL and FreeRTOS, then runs them on the Machine.
#include "TestUtil.h"
#include "core/ClockConfig.h"
#include "toolchain/ArmGccBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QRegularExpression>

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

using Files = QVector<QPair<QString, QString>>;

const QString kSource = QStringLiteral(MK_EMUL_SOURCE_DIR);
const QString kWork = QStringLiteral(MK_EMUL_BUILD_DIR);
const QString kDevice = kSource + "/device/stm32f407";

QString readText(const QString &path) {
    QFile f(path);
    // Text mode: a Windows checkout (core.autocrlf) has CRLF, the tests edit sources by "\n".
    f.open(QIODevice::ReadOnly | QIODevice::Text);
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

// CubeMX projects with FreeRTOS use a basic timer instead of SysTick as the HAL time base
// (stm32f4xx_hal_timebase_tim.c): HAL_Delay must work on TIM6.
TEST_F(BuilderTest, HalTimebaseOnTim6) {
    const QString hal = kSource + "/examples/hal_blinky/";
    QString conf = readText(hal + "stm32f4xx_hal_conf.h");
    conf.replace("/* #define HAL_TIM_MODULE_ENABLED */", "#define HAL_TIM_MODULE_ENABLED");
    ASSERT_TRUE(conf.contains("\n#define HAL_TIM_MODULE_ENABLED"));
    QString main = readText(hal + "main.c");
    main.replace("void SysTick_Handler(void) {\n    HAL_IncTick();\n}", "void SysTick_Handler(void) {\n}");
    ASSERT_FALSE(main.contains("    HAL_IncTick();"));
    const char *timebase = R"(#include "stm32f4xx_hal.h"
TIM_HandleTypeDef htim6;
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority) {
    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    RCC_ClkInitTypeDef clk;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clk, &latency);
    uint32_t tclk = HAL_RCC_GetPCLK1Freq();
    if (clk.APB1CLKDivider != RCC_HCLK_DIV1)
        tclk *= 2;
    htim6.Instance = TIM6;
    htim6.Init.Period = 1000000U / 1000U - 1U;
    htim6.Init.Prescaler = tclk / 1000000U - 1U;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
        return HAL_ERROR;
    return HAL_TIM_Base_Start_IT(&htim6);
}
void TIM6_DAC_IRQHandler(void) { HAL_TIM_IRQHandler(&htim6); }
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM6)
        HAL_IncTick();
}
)";
    Files files{{"stm32f4xx_hal_conf.h", conf}, {"main.c", main}, {"stm32f4xx_hal_timebase_tim.c", timebase}};
    ASSERT_TRUE(build(files, "tim6")) << log.toStdString();
    const auto uart = runElf(elf, 40000000);
    EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=168000000\r\n", 0), 0u) << uart;
    EXPECT_NE(uart.find("tick 2\r\n"), std::string::npos) << uart;
}

quint32 flashBytes(const QString &log) {
    static const QRegularExpression re("Size \\(-O.\\): flash (\\d+) bytes");
    const auto m = re.match(log);
    return m.hasMatch() ? m.captured(1).toUInt() : 0;
}

// Every optimization level of the Build menu: the examples still work, and optimized code is
// smaller than -O0.
TEST_F(BuilderTest, BuildsAtEveryOptimizationLevel) {
    QMap<QString, quint32> halFlash;
    for (const QString &opt : ArmGccBuilder::optimizationLevels()) {
        SCOPED_TRACE(opt.toStdString());
        for (const char *name : {"uart_printf", "systick_exti", "fpu_irq"}) {
            SCOPED_TRACE(name);
            ASSERT_TRUE(builder.compile(exampleFiles(name), kWork + "/opt_" + name, kDevice, &elf, &log, opt))
                << log.toStdString();
            EXPECT_TRUE(log.contains("Optimization: " + opt)) << log.toStdString();
            EXPECT_TRUE(log.contains(" " + opt + " ")) << "flags passed to gcc";
            const auto uart = runElf(elf, 3000000);
            if (QString(name) == "uart_printf")
                EXPECT_NE(uart.find("tick"), std::string::npos) << uart;
        }
        ASSERT_TRUE(builder.compile(exampleFiles("hal_blinky"), kWork + "/opt_hal", kDevice, &elf, &log, opt))
            << log.toStdString();
        const auto uart = runElf(elf, 40000000);
        EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=168000000\r\n", 0), 0u) << uart;
        EXPECT_NE(uart.find("tick 2\r\n"), std::string::npos) << uart;
        halFlash[opt] = flashBytes(log);
        EXPECT_GT(halFlash[opt], 1000u) << log.toStdString();
    }
    for (auto it = halFlash.cbegin(); it != halFlash.cend(); ++it)
        std::cout << "hal_blinky " << it.key().toStdString() << ": flash " << it.value() << " bytes\n";
    EXPECT_LT(halFlash["-Os"], halFlash["-O0"] * 3 / 4) << "-Os must shrink the HAL project noticeably";
    EXPECT_LT(halFlash["-O2"], halFlash["-O0"]);

    // The library cache is kept per level: going back to -O0 compiles only the project.
    ASSERT_TRUE(builder.compile(exampleFiles("hal_blinky"), kWork + "/opt_hal", kDevice, &elf, &log, "-O0"));
    EXPECT_TRUE(log.contains("library objects up to date")) << log.toStdString();
}

// Clock Configuration dialog: the generated SystemClock_Config() runs on the emulator and sets
// exactly the frequencies the dialog computed.
TEST_F(BuilderTest, GeneratedClockConfigRunsOnTheEmulator) {
    std::vector<ClockConfig> configs(4);
    configs[1].pllSource = ClockConfig::Hsi; // 84 MHz from HSI: /8 x168 /4
    configs[1].pllM = 8;
    configs[1].pllN = 168;
    configs[1].pllP = 4;
    configs[1].apb1Div = 2;
    configs[1].apb2Div = 1;
    configs[2].sysclkSource = ClockConfig::Hse; // 8 MHz HSE, AHB /2
    configs[2].ahbDiv = 2;
    configs[2].apb1Div = 1;
    configs[2].apb2Div = 1;
    configs[3].sysclkSource = ClockConfig::Hsi; // back to the reset clock, APB2 /4
    configs[3].apb1Div = 1;
    configs[3].apb2Div = 4;
    for (const ClockConfig &c : configs) {
        const auto r = c.compute();
        SCOPED_TRACE(c.summary());
        ASSERT_TRUE(r.ok()) << r.errors[0];
        const QString main = "#include \"stm32f407xx.h\"\n" + QString::fromStdString(c.registerCode()) +
                             QString("int main(void) {\n"
                                     "    SystemClock_Config();\n"
                                     "    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;\n"
                                     "    GPIOD->MODER = 0x55u << 24;\n"
                                     "    GPIOD->BSRR = SystemCoreClock == %1u ? 1u << 12 : 1u << 14;\n"
                                     "    for (;;) {\n    }\n}\n")
                                 .arg(r.hclk);
        ASSERT_TRUE(build({{"main.c", main}}, "clock")) << log.toStdString();
        Machine m;
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
        ASSERT_TRUE(m.loadFirmware(elf.toStdString()).ok);
        EXPECT_NE(runFor(m, 300000), CpuEngine::StopReason::Fault) << m.status();
        EXPECT_EQ(m.rcc().sysclk(), r.sysclk);
        EXPECT_EQ(m.rcc().hclk(), r.hclk);
        EXPECT_EQ(m.rcc().pclk1(), r.pclk1);
        EXPECT_EQ(m.rcc().pclk2(), r.pclk2);
        EXPECT_EQ(m.rcc().timclk1(), r.tim1);
        EXPECT_EQ(m.rcc().timclk2(), r.tim2);
        EXPECT_EQ(m.snapshot().leds, 0x1u) << "green: SystemCoreClockUpdate() agrees";
        const ClockConfig back = ClockConfig::fromRegisters(m.rcc().pllcfgr(), m.rcc().cfgr());
        EXPECT_EQ(back.compute().hclk, r.hclk) << "From Emulator reads the same tree back";
    }

    // HAL project (hal_blinky): its SystemClock_Config() replaced by the generated one.
    std::string main;
    ASSERT_TRUE(ClockConfig::replaceFunction(readText(kSource + "/examples/hal_blinky/main.c").toStdString(),
                                             configs[1].halCode(), &main));
    const Files files{{"main.c", QString::fromStdString(main)},
                      {"stm32f4xx_hal_conf.h", readText(kSource + "/examples/hal_blinky/stm32f4xx_hal_conf.h")}};
    ASSERT_TRUE(build(files, "clock_hal")) << log.toStdString();
    const auto uart = runElf(elf, 20000000);
    EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=84000000\r\n", 0), 0u) << uart;
    EXPECT_NE(uart.find("tick 2\r\n"), std::string::npos) << "HAL_Delay follows the new HCLK: " << uart;
}

TEST_F(BuilderTest, RejectsUnknownOptimization) {
    EXPECT_FALSE(builder.compile(exampleFiles("blinky"), kWork + "/opt_bad", kDevice, &elf, &log, "-O9"));
    EXPECT_TRUE(log.contains("Unknown optimization")) << log.toStdString();
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
