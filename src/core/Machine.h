#pragma once

#include "core/CpuEngine.h"
#include "core/FirmwareLoader.h"
#include "core/MmioBus.h"
#include "core/Nvic.h"
#include "periph/Exti.h"
#include "periph/Gpio.h"
#include "periph/Rcc.h"
#include "periph/SimplePeriphs.h"
#include "periph/SysTickTimer.h"
#include "periph/Usart.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct MachineSnapshot {
    uint32_t r[16]{};
    uint32_t xpsr = 0;
    uint32_t msp = 0;
    uint32_t psp = 0;
    uint32_t control = 0;
    uint32_t primask = 0;
    uint32_t pc = 0;
    uint32_t sp = 0;
    uint32_t lr = 0;
    uint32_t leds = 0;     // bit i: LED i (PD12+i) lit at all
    float ledLevel[4]{};   // brightness 0..1: ODR bit, or PWM duty when the pin is TIM4 CHx (AF2)
    bool button = false;
    uint32_t gpioD = 0;
    uint32_t rccCr = 0;
    uint32_t rccCfgr = 0;
    uint32_t rccAhb1enr = 0;
    uint32_t sysclk = 16000000;
    uint32_t systickCtrl = 0;
    uint32_t systickVal = 0;
    uint32_t systickLoad = 0;
    uint32_t usart2Sr = 0;
    uint32_t tim2Cnt = 0;
    uint32_t adcDr = 0;
    uint32_t vtor = 0;
    uint32_t cfsr = 0;
    uint32_t hfsr = 0;
    uint32_t bfar = 0;
    uint32_t cpacr = 0;
    uint64_t instructions = 0;
    std::string haltReason;
    CpuEngine::StopReason stop = CpuEngine::StopReason::None;
};

class Machine {
public:
    Machine();
    ~Machine();

    bool initialize(std::string *error);
    void reset();
    LoadedImage loadFirmware(const std::string &path);
    // ELF/HEX/BIN image already in memory (BIN is placed at the flash base); used by tests.
    LoadedImage loadFirmwareBuffer(const uint8_t *data, size_t size, const std::string &name);
    void loadBuiltinBlinky();

    CpuEngine::StopReason step();
    CpuEngine::StopReason runQuantum(uint32_t insns);

    void setButtonPressed(bool pressed);
    bool buttonPressed() const { return buttonPressed_; }
    void uartPush(const std::string &text);
    void setAnalog(uint32_t value12);

    void addBreakpoint(uint32_t addr) { cpu_.addBreakpoint(addr); }
    void removeBreakpoint(uint32_t addr) { cpu_.removeBreakpoint(addr); }
    void clearBreakpoints() { cpu_.clearBreakpoints(); }
    std::vector<uint32_t> breakpoints() const { return cpu_.breakpoints(); }

    MachineSnapshot snapshot() const;
    void readMemory(uint32_t addr, uint8_t *out, size_t len) const;
    CpuEngine &cpu() { return cpu_; }
    const CpuEngine &cpu() const { return cpu_; }
    Rcc &rcc() { return rcc_; }
    Nvic &nvic() { return nvic_; }
    MmioBus &bus() { return bus_; }

    using UartTx = std::function<void(uint8_t)>;
    void setUartTx(UartTx cb);
    // Firmware accessed a peripheral that is not emulated (reads 0, writes are dropped):
    // "write to unemulated peripheral DMA1 @ 0x40026010", once per register until reset.
    using WarningSink = std::function<void(const std::string &)>;
    void setWarningSink(WarningSink cb) { warn_ = std::move(cb); }

    const std::string &status() const { return status_; }
    bool firmwareLoaded() const { return firmwareLoaded_; }
    uint32_t vectorBase() const { return vectorBase_; }

private:
    void wirePeripherals();
    void onGpioPin(int port, uint16_t odr, uint16_t idr);
    void onUnmappedAccess(uint32_t addr, bool write);
    void applyResetCpu();
    LoadedImage applyImage(LoadedImage img, const std::string &name);
    void tickPeripherals(uint32_t insns);
    // Called on MMIO access during a chunk: advance timers to the current instruction, so
    // SysTick->VAL / TIMx->CNT read exactly, and cycles before a clock change use the old clock.
    void syncTimers();
    void deliverFault();
    void resetMcu(); // peripherals, NVIC, CPU; keeps RAM and the instruction counter
    // Runs up to n instructions, then advances timers and takes faults, SVC lockup,
    // NVIC_SystemReset and pending interrupts.
    CpuEngine::StopReason runChunk(uint32_t n);
    // Instructions until SysTick/TIM would pend an interrupt, so chunks end exactly there.
    uint32_t cyclesToNextEvent() const;

    CpuEngine cpu_;
    MmioBus bus_;
    Nvic nvic_;
    Rcc rcc_;
    SysTickTimer systick_;
    Exti exti_;
    Syscfg syscfg_;
    std::array<std::unique_ptr<GpioBank>, 9> gpio_{};
    Usart usart1_;
    Usart usart2_;
    Usart usart3_;
    Usart usart6_;
    Timer tim2_;
    Timer tim3_;
    Timer tim4_;
    Timer tim5_;
    Adc adc1_;
    Spi spi1_;
    I2c i2c1_;
    StubRegs flashAcr_;
    StubRegs pwr_;
    StubRegs iwdg_;
    StubRegs wwdg_;
    StubRegs ahb2_;

    bool buttonPressed_ = false;
    bool firmwareLoaded_ = false;
    bool svcLockup_ = false;
    bool inChunk_ = false;
    uint64_t chunkTicked_ = 0; // instructions of the current chunk already given to the timers
    uint32_t vectorBase_ = mcu::kFlashBase;
    std::string status_ = "Idle";
    UartTx uartTx_;
    WarningSink warn_;
    std::set<uint32_t> reportedUnmapped_; // register address | 1 for writes, already warned about
    uint16_t lastGpioIdr_[9]{};
};
