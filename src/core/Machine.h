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
    uint32_t leds = 0;
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
    MmioBus &bus() { return bus_; }

    using UartTx = std::function<void(uint8_t)>;
    void setUartTx(UartTx cb);

    const std::string &status() const { return status_; }
    bool firmwareLoaded() const { return firmwareLoaded_; }
    uint32_t vectorBase() const { return vectorBase_; }

private:
    void wirePeripherals();
    void onGpioPin(int port, uint16_t odr, uint16_t idr);
    void applyResetCpu();

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
    uint32_t vectorBase_ = mcu::kFlashBase;
    std::string status_ = "Idle";
    UartTx uartTx_;
    uint16_t lastGpioIdr_[9]{};
};
