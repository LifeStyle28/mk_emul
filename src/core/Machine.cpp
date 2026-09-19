#include "core/Machine.h"

#include "core/Addresses.h"

#include <unicorn/arm.h>

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
        if (intno == 8) {
            uint32_t exc = cpu_.pc();
            if ((exc & 0xFFFFFF00u) != 0xFFFFFF00u)
                exc = cpu_.lr();
            nvic_.exitException(cpu_, exc);
        }
    });
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
    applyResetCpu();
    status_ = "Reset";
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
    vectorBase_ = mcu::kFlashBase;
    firmwareLoaded_ = true;
    applyResetCpu();
    status_ = "Builtin blinky loaded";
}

LoadedImage Machine::loadFirmware(const std::string &path) {
    auto img = FirmwareLoader::load(cpu_, path);
    if (!img.ok) {
        status_ = img.error;
        return img;
    }
    vectorBase_ = img.vectorBase ? img.vectorBase : mcu::kFlashBase;
    firmwareLoaded_ = true;
    nvic_.write32(0xE000ED08u, vectorBase_); // VTOR
    applyResetCpu();
    status_ = "Loaded " + path;
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

CpuEngine::StopReason Machine::step() {
    auto r = cpu_.run(1);
    systick_.tick(1, rcc_.sysclk());
    tim2_.tick(1, rcc_.sysclk());
    tim3_.tick(1, rcc_.sysclk());
    nvic_.service(cpu_);
    if (r == CpuEngine::StopReason::Fault)
        status_ = cpu_.lastError();
    else if (r == CpuEngine::StopReason::Breakpoint)
        status_ = "Breakpoint";
    else
        status_ = "Paused";
    return r;
}

CpuEngine::StopReason Machine::runQuantum(uint32_t insns) {
    constexpr uint32_t kChunk = 256;
    uint32_t left = insns;
    CpuEngine::StopReason last = CpuEngine::StopReason::Quantum;
    while (left > 0) {
        const uint32_t n = left > kChunk ? kChunk : left;
        last = cpu_.run(n);
        systick_.tick(n, rcc_.sysclk());
        tim2_.tick(n, rcc_.sysclk());
        tim3_.tick(n, rcc_.sysclk());
        nvic_.service(cpu_);
        if (last == CpuEngine::StopReason::Breakpoint || last == CpuEngine::StopReason::Fault)
            break;
        left -= n;
    }
    if (last == CpuEngine::StopReason::Fault)
        status_ = cpu_.lastError();
    else if (last == CpuEngine::StopReason::Breakpoint)
        status_ = "Breakpoint";
    else
        status_ = "Running";
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
    s.instructions = cpu_.instructionCount();
    s.haltReason = status_;
    s.stop = cpu_.lastStop();
    return s;
}

void Machine::readMemory(uint32_t addr, uint8_t *out, size_t len) const {
    cpu_.readMem(addr, out, len);
}
