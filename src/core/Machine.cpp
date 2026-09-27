#include "core/Machine.h"

#include "core/Addresses.h"

#include <unicorn/arm.h>

#include <algorithm>
#include <cstdint>
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
    cpu_.setMsp(sp);
    cpu_.setPsp(sp);
    cpu_.setSp(sp);
    cpu_.setPc(pc & ~1u);
    cpu_.setLr(0xFFFFFFFFu);
    cpu_.setXpsr(0x01000000u); // Thumb
    cpu_.setControl(0);
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
                                 tim3_.cyclesUntilInterrupt(sys)});
    return c > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(c);
}

CpuEngine::StopReason Machine::runChunk(uint32_t n) {
    const auto r = cpu_.run(n);
    tickPeripherals(static_cast<uint32_t>(cpu_.lastRunCount()));
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
    const auto r = runChunk(1);
    if (r == CpuEngine::StopReason::Fault)
        return r;
    status_ = r == CpuEngine::StopReason::Breakpoint ? "Breakpoint" : "Paused";
    return r;
}

CpuEngine::StopReason Machine::runQuantum(uint32_t insns) {
    // Timers/CNT are updated between chunks, so keep chunks short; they also end exactly when
    // SysTick/TIM pend an interrupt, and the CPU stops early on its own when one becomes
    // pending from code (see CpuEngine::setInterruptPoll), so interrupt latency is exact.
    constexpr uint32_t kChunk = 256;
    uint32_t left = insns;
    int idle = 0;
    CpuEngine::StopReason last = CpuEngine::StopReason::Quantum;
    while (left > 0) {
        const uint32_t n = std::max(1u, std::min({left, kChunk, cyclesToNextEvent()}));
        last = runChunk(n);
        if (last == CpuEngine::StopReason::Fault)
            return last;
        if (last == CpuEngine::StopReason::Breakpoint)
            break;
        const uint32_t done = static_cast<uint32_t>(cpu_.lastRunCount());
        if (done == 0) {
            // Stopped before the first instruction to take an interrupt (runChunk entered it).
            if (++idle > 4)
                break; // safety net: never spin without progress
            continue;
        }
        idle = 0;
        left -= std::min(left, done);
    }
    status_ = last == CpuEngine::StopReason::Breakpoint ? "Breakpoint" : "Running";
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
    s.leds = (s.gpioD >> 12) & 0xFu;
    s.button = buttonPressed_;
    s.rccCr = rcc_.cr();
    s.rccCfgr = rcc_.cfgr();
    s.rccAhb1enr = rcc_.ahb1enr();
    s.sysclk = rcc_.sysclk();
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
    s.instructions = cpu_.instructionCount();
    s.haltReason = status_;
    s.stop = cpu_.lastStop();
    return s;
}

void Machine::readMemory(uint32_t addr, uint8_t *out, size_t len) const {
    cpu_.readMem(addr, out, len);
}
