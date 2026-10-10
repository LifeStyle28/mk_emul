#include "core/Machine.h"

#include "core/Addresses.h"

#include <unicorn/arm.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

Machine::Machine()
    : nvic_(&cpu_)
    , systick_(&nvic_, &rcc_)
    , exti_(&nvic_)
    , syscfg_(&exti_)
    , usart1_("USART1", mcu::kUsart1Base, mcu::kIrqUsart1, &rcc_, &nvic_, 1)
    , usart2_("USART2", mcu::kUsart2Base, mcu::kIrqUsart2, &rcc_, &nvic_, 2)
    , usart3_("USART3", mcu::kUsart3Base, mcu::kIrqUsart3, &rcc_, &nvic_, 3)
    , usart6_("USART6", mcu::kUsart6Base, mcu::kIrqUsart6, &rcc_, &nvic_, 6)
    , tim2_("TIM2", mcu::kTim2Base, mcu::kIrqTim2, 2, &rcc_, &nvic_)
    , tim3_("TIM3", mcu::kTim3Base, mcu::kIrqTim3, 3, &rcc_, &nvic_)
    , tim4_("TIM4", mcu::kTim4Base, mcu::kIrqTim4, 4, &rcc_, &nvic_)
    , tim5_("TIM5", mcu::kTim5Base, mcu::kIrqTim5, 5, &rcc_, &nvic_)
    , tim6_("TIM6", mcu::kTim6Base, mcu::kIrqTim6Dac, 6, &rcc_, &nvic_)
    , tim7_("TIM7", mcu::kTim7Base, mcu::kIrqTim7, 7, &rcc_, &nvic_)
    , adc1_(&rcc_, &nvic_)
    , spi1_("SPI1", mcu::kSpi1Base, mcu::kIrqSpi1, &rcc_, &nvic_)
    , i2c1_("I2C1", mcu::kI2c1Base, mcu::kIrqI2c1Ev, mcu::kIrqI2c1Er, &rcc_, &nvic_)
    , flashAcr_("FLASH", mcu::kFlashRegsBase, 0x400)
    , pwr_("PWR", mcu::kPwrBase, 0x400)
    , iwdg_("IWDG", mcu::kIwdgBase, 0x400)
    , wwdg_("WWDG", mcu::kWwdgBase, 0x400)
    , ahb2_("AHB2", mcu::kAhb2Base, mcu::kAhb2Size) {
    const uint32_t gpioBases[9] = {
        mcu::kGpioABase, mcu::kGpioBBase, mcu::kGpioCBase, mcu::kGpioDBase, mcu::kGpioEBase,
        mcu::kGpioFBase, mcu::kGpioGBase, mcu::kGpioHBase, mcu::kGpioIBase,
    };
    for (int i = 0; i < 9; ++i) {
        gpio_[i] = std::make_unique<GpioBank>(i, gpioBases[i], &rcc_);
        gpio_[i]->setExti(&exti_);
        gpio_[i]->setPinChanged([this](int port, uint16_t odr, uint16_t idr) { onGpioPin(port, odr, idr); });
    }
}

Machine::~Machine() = default;

void Machine::wirePeripherals() {
    bus_.add(&systick_);
    bus_.add(&nvic_);
    bus_.add(&rcc_);
    bus_.add(&syscfg_);
    bus_.add(&exti_);
    for (auto &g : gpio_)
        bus_.add(g.get());
    bus_.add(&usart1_);
    bus_.add(&usart2_);
    bus_.add(&usart3_);
    bus_.add(&usart6_);
    bus_.add(&tim2_);
    bus_.add(&tim3_);
    bus_.add(&tim4_);
    bus_.add(&tim5_);
    bus_.add(&tim6_);
    bus_.add(&tim7_);
    bus_.add(&adc1_);
    bus_.add(&spi1_);
    bus_.add(&i2c1_);
    bus_.add(&flashAcr_);
    bus_.add(&pwr_);
    bus_.add(&iwdg_);
    bus_.add(&wwdg_);
    bus_.add(&ahb2_);

    cpu_.mapRam(mcu::kFlashAlias, mcu::kFlashSize);
    cpu_.mapRam(mcu::kFlashBase, mcu::kFlashSize);
    cpu_.mapRam(mcu::kCcmBase, mcu::kCcmSize);
    cpu_.mapRam(mcu::kSramBase, mcu::kSramSize);
    cpu_.mapMmio(mcu::kApbAhbBase, mcu::kApbAhbSize, &bus_);
    cpu_.mapMmio(mcu::kAhb2Base, mcu::kAhb2Size, &bus_);
    cpu_.mapMmio(mcu::kPpbBase, mcu::kPpbSize, &bus_);
    cpu_.mapMmio(mcu::kSramBitbandBase, mcu::kSramBitbandSize, &bus_);
    cpu_.mapMmio(mcu::kPeriphBitbandBase, mcu::kPeriphBitbandSize, &bus_);
    bus_.setUnmappedHook([this](uint32_t addr, bool write) { onUnmappedAccess(addr, write); });
    bus_.setPpbAccessCheck([this](uint32_t addr, bool write) {
        // The PPB (NVIC, SCB, SysTick, DWT, ...) is privileged-only: unprivileged thread code
        // gets a precise BusFault (PRECISERR, BFAR = addr). Peripherals at 0x40000000 stay
        // accessible, as on the MCU without an MPU. UI reads outside a chunk are not checked.
        if (!inChunk_ || cpu_.privileged())
            return true;
        char msg[80];
        std::snprintf(msg, sizeof(msg), "unprivileged %s system register @ 0x%08X", write ? "write to" : "read from",
                      static_cast<unsigned>(addr));
        cpu_.raiseBusFault(addr, write, msg);
        return false;
    });
    bus_.setMemoryAccess([this](uint32_t addr) { return cpu_.read32(addr); },
                         [this](uint32_t addr, uint32_t value) { cpu_.write32(addr, value); });

    cpu_.setInterruptHandler([this](uint32_t intno) {
        if (intno == 8) { // EXCP_EXCEPTION_EXIT
            // PC holds EXC_RETURN with bit 0 cleared (0xFFFFFFF8 for 0xFFFFFFF9); restore it,
            // otherwise the "return to handler" / "return to PSP" modes are misdecoded.
            uint32_t exc = cpu_.pc() | 1u;
            if ((exc & 0xFFFFFF00u) != 0xFFFFFF00u)
                exc = cpu_.lr();
            nvic_.exitException(cpu_, exc);
        } else if (intno == 2) { // EXCP_SWI: SVC instruction, PC already points past it
            if (!nvic_.raiseSvc(cpu_)) {
                svcLockup_ = true;
                cpu_.stop();
            }
        }
    });
    bus_.setAccessHooks([this] { syncTimers(); },
                        [this] {
                            // A timer write may bring the next interrupt closer: end the chunk there.
                            if (inChunk_)
                                cpu_.limitRun(cpu_.lastRunCount() + cyclesToNextEvent());
                        });
    cpu_.setInterruptPoll(nvic_.pendingFlag(), [this] { return nvic_.wouldPreempt(cpu_); });
}

bool Machine::initialize(std::string *error) {
    if (!cpu_.open(error))
        return false;
    wirePeripherals();
    reset();
    loadBuiltinBlinky();
    return true;
}

void Machine::reset() {
    resetMcu();
    cpu_.resetInstructionCount();
    status_ = "Reset";
}

void Machine::resetMcu() {
    reportedUnmapped_.clear();
    sleeping_ = false;
    rcc_.reset();
    nvic_.reset();
    systick_.reset();
    exti_.reset();
    syscfg_.reset();
    for (auto &g : gpio_)
        g->reset();
    usart1_.reset();
    usart2_.reset();
    usart3_.reset();
    usart6_.reset();
    tim2_.reset();
    tim3_.reset();
    tim4_.reset();
    tim5_.reset();
    tim6_.reset();
    tim7_.reset();
    adc1_.reset();
    spi1_.reset();
    i2c1_.reset();
    flashAcr_.reset();
    pwr_.reset();
    iwdg_.reset();
    wwdg_.reset();
    buttonPressed_ = false;
    std::memset(lastGpioIdr_, 0, sizeof(lastGpioIdr_));
    nvic_.write32(0xE000ED08u, vectorBase_); // VTOR -> loaded image
    svcLockup_ = false;
    applyResetCpu();
}

void Machine::applyResetCpu() {
    const uint32_t table = vectorBase_;
    const uint32_t sp = cpu_.read32(table);
    const uint32_t pc = cpu_.read32(table + 4);
    // Order matters (see Nvic::enterException): privileged first, then thread mode with
    // CONTROL = 0 (MSP active), then the stack pointers. PC before xPSR: Unicorn takes the Thumb
    // bit from bit 0 of a PC write, so writing an even PC afterwards would clear EPSR.T.
    cpu_.setNPriv(false);
    cpu_.setPc(pc & ~1u);
    cpu_.setXpsr(0x01000000u); // Thumb, thread mode
    cpu_.setControl(0);
    cpu_.setMsp(sp);
    cpu_.setPsp(sp);
    cpu_.setLr(0xFFFFFFFFu);
    cpu_.setPrimask(0);
    for (int i = 0; i < 13; ++i)
        cpu_.setReg(UC_ARM_REG_R0 + i, 0);
}

void Machine::loadBuiltinBlinky() {
    FirmwareLoader::installBuiltinBlinky(cpu_);
    cpu_.flushCodeCache();
    vectorBase_ = mcu::kFlashBase;
    firmwareLoaded_ = true;
    reset(); // like flashing a real board: the whole MCU restarts
    status_ = "Builtin blinky loaded";
}

LoadedImage Machine::loadFirmware(const std::string &path) {
    return applyImage(FirmwareLoader::load(cpu_, path), path);
}

LoadedImage Machine::loadFirmwareBuffer(const uint8_t *data, size_t size, const std::string &name) {
    return applyImage(FirmwareLoader::loadBuffer(cpu_, data, size, name), name);
}

LoadedImage Machine::applyImage(LoadedImage img, const std::string &name) {
    if (!img.ok) {
        status_ = img.error;
        return img;
    }
    cpu_.flushCodeCache(); // the old firmware's translated code must not run any more
    vectorBase_ = img.vectorBase ? img.vectorBase : mcu::kFlashBase;
    firmwareLoaded_ = true;
    reset(); // like flashing a real board: peripherals, NVIC and CPU restart

    status_ = "Loaded " + name;
    return img;
}

void Machine::onGpioPin(int port, uint16_t, uint16_t) {
    lastGpioIdr_[port] = gpio_[port]->idr();
}

void Machine::setButtonPressed(bool pressed) {
    buttonPressed_ = pressed;
    gpio_[mcu::kDiscoveryButtonPort]->setExternalInput(mcu::kDiscoveryButtonPin, pressed);
}

void Machine::uartPush(const std::string &text) {
    for (unsigned char c : text)
        usart2_.pushRx(c);
}

void Machine::setAnalog(uint32_t value12) {
    adc1_.setAnalogValue(value12);
}

namespace {

// STM32F407 memory map (RM0090, table 1) for naming unemulated peripherals.
struct PeriphRange {
    uint32_t base;
    uint32_t size;
    const char *name;
};

constexpr PeriphRange kPeriphMap[] = {
    {0x40000000u, 0x400u, "TIM2"}, {0x40000400u, 0x400u, "TIM3"}, {0x40000800u, 0x400u, "TIM4"},
    {0x40000C00u, 0x400u, "TIM5"}, {0x40001000u, 0x400u, "TIM6"}, {0x40001400u, 0x400u, "TIM7"},
    {0x40001800u, 0x400u, "TIM12"}, {0x40001C00u, 0x400u, "TIM13"}, {0x40002000u, 0x400u, "TIM14"},
    {0x40002800u, 0x400u, "RTC/BKP"}, {0x40002C00u, 0x400u, "WWDG"}, {0x40003000u, 0x400u, "IWDG"},
    {0x40003400u, 0x400u, "I2S2ext"}, {0x40003800u, 0x400u, "SPI2/I2S2"}, {0x40003C00u, 0x400u, "SPI3/I2S3"},
    {0x40004000u, 0x400u, "I2S3ext"}, {0x40004400u, 0x400u, "USART2"}, {0x40004800u, 0x400u, "USART3"},
    {0x40004C00u, 0x400u, "UART4"}, {0x40005000u, 0x400u, "UART5"}, {0x40005400u, 0x400u, "I2C1"},
    {0x40005800u, 0x400u, "I2C2"}, {0x40005C00u, 0x400u, "I2C3"}, {0x40006400u, 0x400u, "CAN1"},
    {0x40006800u, 0x400u, "CAN2"}, {0x40007000u, 0x400u, "PWR"}, {0x40007400u, 0x400u, "DAC"},
    {0x40010000u, 0x400u, "TIM1"}, {0x40010400u, 0x400u, "TIM8"}, {0x40011000u, 0x400u, "USART1"},
    {0x40011400u, 0x400u, "USART6"}, {0x40012000u, 0x400u, "ADC"}, {0x40012C00u, 0x400u, "SDIO"},
    {0x40013000u, 0x400u, "SPI1"}, {0x40013800u, 0x400u, "SYSCFG"}, {0x40013C00u, 0x400u, "EXTI"},
    {0x40014000u, 0x400u, "TIM9"}, {0x40014400u, 0x400u, "TIM10"}, {0x40014800u, 0x400u, "TIM11"},
    {0x40020000u, 0x2400u, "GPIO"}, {0x40023000u, 0x400u, "CRC"}, {0x40023800u, 0x400u, "RCC"},
    {0x40023C00u, 0x400u, "FLASH"}, {0x40024000u, 0x1000u, "BKPSRAM"}, {0x40026000u, 0x400u, "DMA1"},
    {0x40026400u, 0x400u, "DMA2"}, {0x40028000u, 0x1400u, "ETH"},
    {0xE0000000u, 0x1000u, "ITM"}, {0xE0001000u, 0x1000u, "DWT"}, {0xE0002000u, 0x1000u, "FPB"},
    {0xE000E000u, 0x1000u, "SCS (NVIC/SCB)"},
};

const char *periphName(uint32_t addr) {
    for (const auto &r : kPeriphMap) {
        if (addr >= r.base && addr - r.base < r.size)
            return r.name;
    }
    return nullptr;
}

} // namespace

void Machine::onUnmappedAccess(uint32_t addr, bool write) {
    // Only firmware accesses count: the UI memory view also reads MMIO outside a chunk.
    constexpr size_t kMaxReports = 32;
    if (!inChunk_ || !warn_ || reportedUnmapped_.size() > kMaxReports)
        return;
    if (!reportedUnmapped_.insert((addr & ~3u) | (write ? 1u : 0u)).second)
        return;
    char msg[128];
    if (reportedUnmapped_.size() > kMaxReports) {
        std::snprintf(msg, sizeof(msg), "more accesses to unemulated peripherals are not reported");
    } else {
        const char *name = periphName(addr);
        std::snprintf(msg, sizeof(msg), "%s unemulated peripheral %s%s@ 0x%08X (%s)",
                      write ? "write to" : "read from", name ? name : "", name ? " " : "", addr,
                      write ? "value dropped" : "reads as 0");
    }
    warn_(msg);
}

void Machine::setUartTx(UartTx cb) {
    uartTx_ = std::move(cb);
    usart2_.setTxCallback(uartTx_);
    usart1_.setTxCallback(uartTx_);
    usart3_.setTxCallback(uartTx_);
    usart6_.setTxCallback(uartTx_);
}

void Machine::tickPeripherals(uint32_t insns) {
    systick_.tick(insns, rcc_.sysclk());
    tim2_.tick(insns, rcc_.sysclk());
    tim3_.tick(insns, rcc_.sysclk());
    tim4_.tick(insns, rcc_.sysclk());
    tim5_.tick(insns, rcc_.sysclk());
    tim6_.tick(insns, rcc_.sysclk());
    tim7_.tick(insns, rcc_.sysclk());
}

void Machine::syncTimers() {
    if (!inChunk_)
        return;
    const uint64_t done = cpu_.lastRunCount();
    if (done > chunkTicked_) {
        tickPeripherals(static_cast<uint32_t>(done - chunkTicked_));
        chunkTicked_ = done;
    }
}

void Machine::deliverFault() {
    Nvic::FaultType type = Nvic::FaultType::Other;
    switch (cpu_.faultKind()) {
    case CpuEngine::FaultKind::ReadUnmapped:
    case CpuEngine::FaultKind::WriteUnmapped:
        type = Nvic::FaultType::BusData;
        break;
    case CpuEngine::FaultKind::FetchUnmapped:
        type = Nvic::FaultType::BusFetch;
        break;
    case CpuEngine::FaultKind::InvalidInsn:
        type = Nvic::FaultType::UndefInstr;
        break;
    case CpuEngine::FaultKind::Unaligned:
        type = Nvic::FaultType::Unaligned;
        break;
    case CpuEngine::FaultKind::NoCoprocessor:
        type = Nvic::FaultType::NoCoprocessor;
        break;
    default:
        break;
    }
    const std::string reason = cpu_.lastError();
    const int exc = nvic_.raiseFault(cpu_, type, cpu_.faultAddress());
    // Like a debugger's vector catch: stop at the first handler instruction, Run continues.
    switch (exc) {
    case 3:
        status_ = "HardFault: " + reason;
        break;
    case 5:
        status_ = "BusFault: " + reason;
        break;
    case 6:
        status_ = "UsageFault: " + reason;
        break;
    default:
        status_ = "Lockup: " + reason;
        break;
    }
}

uint32_t Machine::cyclesToNextEvent() const {
    const uint32_t sys = rcc_.sysclk();
    const uint64_t c = std::min({systick_.cyclesUntilInterrupt(sys), tim2_.cyclesUntilInterrupt(sys),
                                 tim3_.cyclesUntilInterrupt(sys), tim4_.cyclesUntilInterrupt(sys),
                                 tim5_.cyclesUntilInterrupt(sys), tim6_.cyclesUntilInterrupt(sys),
                                 tim7_.cyclesUntilInterrupt(sys)});
    return c > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(c);
}

CpuEngine::StopReason Machine::runChunk(uint32_t n) {
    if (sleeping_) {
        // WFI: the core waits for an interrupt; time runs on to the next timer event (or n
        // cycles, e.g. while waiting for the USER button). Pending interrupts wake it.
        if (!*nvic_.pendingFlag()) {
            const uint32_t idle = std::max(1u, std::min(n, cyclesToNextEvent()));
            tickPeripherals(idle);
            cpu_.addIdleCycles(idle);
            lastChunkCycles_ = idle;
        } else {
            lastChunkCycles_ = 0;
        }
        if (*nvic_.pendingFlag())
            sleeping_ = false;
        nvic_.service(cpu_);
        return CpuEngine::StopReason::Quantum;
    }
    inChunk_ = true;
    chunkTicked_ = 0;
    auto r = cpu_.run(n);
    lastChunkCycles_ = cpu_.lastRunCount();
    if (r == CpuEngine::StopReason::Sleep) {
        sleeping_ = true;
        r = CpuEngine::StopReason::Quantum;
    }
    syncTimers(); // the rest of the chunk
    inChunk_ = false;
    if (svcLockup_) {
        svcLockup_ = false;
        status_ = "Lockup: SVC inside HardFault/NMI";
        return CpuEngine::StopReason::Fault;
    }
    if (r == CpuEngine::StopReason::Fault) {
        deliverFault();
        return r;
    }
    if (nvic_.takeResetRequest()) {
        resetMcu(); // like the real MCU: restart from the reset vector, SRAM keeps its contents
        return CpuEngine::StopReason::Quantum;
    }
    nvic_.service(cpu_);
    return r;
}

CpuEngine::StopReason Machine::step() {
    cpu_.resumeHere(); // a step always executes the instruction, breakpoint or not
    const auto r = runChunk(1);
    if (r == CpuEngine::StopReason::Fault)
        return r;
    status_ = "Paused";
    return r;
}

CpuEngine::StopReason Machine::runQuantum(uint32_t insns) {
    // Chunks end exactly when SysTick/TIM pend an interrupt (also after a timer write moves it
    // closer), and the CPU stops early on its own when one becomes pending from code (see
    // CpuEngine::setInterruptPoll), so interrupt latency is exact. Timers are synced on every
    // MMIO access, so the chunk size only bounds how long the UI waits for a pause.
    constexpr uint32_t kChunk = 256;
    uint32_t left = insns;
    int idle = 0;
    CpuEngine::StopReason last = CpuEngine::StopReason::Quantum;
    while (left > 0) {
        const uint32_t n = std::max(1u, std::min({left, kChunk, cyclesToNextEvent()}));
        last = runChunk(n);
        if (last == CpuEngine::StopReason::Fault)
            return last;
        if (last == CpuEngine::StopReason::Breakpoint || last == CpuEngine::StopReason::StepDone)
            break;
        const uint32_t done = static_cast<uint32_t>(lastChunkCycles_); // executed + slept
        if (done == 0) {
            // Stopped before the first instruction to take an interrupt (runChunk entered it).
            if (++idle > 4)
                break; // safety net: never spin without progress
            continue;
        }
        idle = 0;
        left -= std::min(left, done);
    }
    status_ = last == CpuEngine::StopReason::Breakpoint ? "Breakpoint"
              : last == CpuEngine::StopReason::StepDone ? "Paused"
              : sleeping_                               ? "Running (WFI sleep)"
                                                        : "Running";
    return last;
}

MachineSnapshot Machine::snapshot() const {
    MachineSnapshot s;
    cpu_.snapshotRegs(s.r);
    s.xpsr = cpu_.xpsr();
    s.msp = cpu_.msp();
    s.psp = cpu_.psp();
    s.control = cpu_.control();
    s.primask = cpu_.primask();
    s.pc = cpu_.pc();
    s.sp = cpu_.sp();
    s.lr = cpu_.lr();
    s.gpioD = gpio_[3]->odr();
    const GpioBank &ledPort = *gpio_[mcu::kDiscoveryLedPort];
    for (int i = 0; i < 4; ++i) {
        const int pin = 12 + i;
        float level = (s.gpioD >> pin) & 1u ? 1.0f : 0.0f;
        if (ledPort.pinMode(pin) == 2) {
            // Alternate function: driven by the peripheral, not by ODR. AF2 on PD12-15 is
            // TIM4 CH1-CH4 (the Discovery PWM exercise); other AFs are not modelled.
            const double pwm = ledPort.pinAf(pin) == 2 ? tim4_.outputLevel(i + 1) : -1.0;
            level = pwm > 0 ? static_cast<float>(pwm) : 0.0f;
        }
        s.ledLevel[i] = level;
        if (level > 0)
            s.leds |= 1u << i;
    }
    s.button = buttonPressed_;
    s.rccCr = rcc_.cr();
    s.rccCfgr = rcc_.cfgr();
    s.rccAhb1enr = rcc_.ahb1enr();
    s.sysclk = rcc_.sysclk();
    s.hclk = rcc_.hclk();
    s.pclk1 = rcc_.pclk1();
    s.pclk2 = rcc_.pclk2();
    s.timclk1 = rcc_.timclk1();
    s.timclk2 = rcc_.timclk2();
    s.rccPllcfgr = rcc_.pllcfgr();
    s.flashAcr = flashAcr_.value(0);
    s.systickCtrl = systick_.ctrl();
    s.systickVal = systick_.val();
    s.systickLoad = systick_.load();
    s.usart2Sr = usart2_.sr();
    s.tim2Cnt = tim2_.cnt();
    s.adcDr = adc1_.dr();
    s.vtor = nvic_.vtor();
    s.cfsr = nvic_.cfsr();
    s.hfsr = nvic_.hfsr();
    s.bfar = nvic_.bfar();
    s.cpacr = nvic_.debugCpacr();
    s.instructions = cpu_.instructionCount();
    s.haltReason = status_;
    s.sleeping = sleeping_;
    s.stop = cpu_.lastStop();
    return s;
}

void Machine::readMemory(uint32_t addr, uint8_t *out, size_t len) const {
    cpu_.readMem(addr, out, len);
}

void Machine::writeMemory(uint32_t addr, const uint8_t *data, size_t len) {
    cpu_.writeMem(addr, data, len);
    const auto inFlash = [&](uint32_t base) { return addr < base + mcu::kFlashSize && addr + len > base; };
    if (inFlash(mcu::kFlashBase) || inFlash(mcu::kFlashAlias))
        cpu_.flushCodeCache();
}
