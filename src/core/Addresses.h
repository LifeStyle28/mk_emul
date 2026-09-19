#pragma once

#include <cstdint>

namespace mcu {

constexpr uint32_t kFlashBase = 0x08000000u;
constexpr uint32_t kFlashSize = 1024u * 1024u;
constexpr uint32_t kFlashAlias = 0x00000000u;
constexpr uint32_t kCcmBase = 0x10000000u;
constexpr uint32_t kCcmSize = 64u * 1024u;
constexpr uint32_t kSramBase = 0x20000000u;
constexpr uint32_t kSramSize = 128u * 1024u;

constexpr uint32_t kApbAhbBase = 0x40000000u;
constexpr uint32_t kApbAhbSize = 0x00040000u;
constexpr uint32_t kAhb2Base = 0x50000000u;
constexpr uint32_t kAhb2Size = 0x00010000u;
constexpr uint32_t kPpbBase = 0xE0000000u;
constexpr uint32_t kPpbSize = 0x00010000u;

constexpr uint32_t kTim2Base = 0x40000000u;
constexpr uint32_t kTim3Base = 0x40000400u;
constexpr uint32_t kWwdgBase = 0x40002C00u;
constexpr uint32_t kIwdgBase = 0x40003000u;
constexpr uint32_t kUsart2Base = 0x40004400u;
constexpr uint32_t kUsart3Base = 0x40004800u;
constexpr uint32_t kI2c1Base = 0x40005400u;
constexpr uint32_t kPwrBase = 0x40007000u;

constexpr uint32_t kUsart1Base = 0x40011000u;
constexpr uint32_t kUsart6Base = 0x40011400u;
constexpr uint32_t kAdc1Base = 0x40012000u;
constexpr uint32_t kSpi1Base = 0x40013000u;
constexpr uint32_t kSyscfgBase = 0x40013800u;
constexpr uint32_t kExtiBase = 0x40013C00u;

constexpr uint32_t kGpioABase = 0x40020000u;
constexpr uint32_t kGpioBBase = 0x40020400u;
constexpr uint32_t kGpioCBase = 0x40020800u;
constexpr uint32_t kGpioDBase = 0x40020C00u;
constexpr uint32_t kGpioEBase = 0x40021000u;
constexpr uint32_t kGpioFBase = 0x40021400u;
constexpr uint32_t kGpioGBase = 0x40021800u;
constexpr uint32_t kGpioHBase = 0x40021C00u;
constexpr uint32_t kGpioIBase = 0x40022000u;
constexpr uint32_t kRccBase = 0x40023800u;
constexpr uint32_t kFlashRegsBase = 0x40023C00u;

constexpr uint32_t kScsBase = 0xE000E000u;
constexpr uint32_t kSysTickBase = 0xE000E010u;
constexpr uint32_t kNvicBase = 0xE000E100u;
constexpr uint32_t kScbBase = 0xE000ED00u;

constexpr uint32_t kHsiHz = 16000000u;
constexpr uint32_t kHseHz = 8000000u;

constexpr int kSysTickException = 15;
constexpr int kIrqExti0 = 6;
constexpr int kIrqExti1 = 7;
constexpr int kIrqExti2 = 8;
constexpr int kIrqExti3 = 9;
constexpr int kIrqExti4 = 10;
constexpr int kIrqAdc = 18;
constexpr int kIrqExti9_5 = 23;
constexpr int kIrqTim2 = 28;
constexpr int kIrqTim3 = 29;
constexpr int kIrqI2c1Ev = 31;
constexpr int kIrqI2c1Er = 32;
constexpr int kIrqSpi1 = 35;
constexpr int kIrqUsart1 = 37;
constexpr int kIrqUsart2 = 38;
constexpr int kIrqUsart3 = 39;
constexpr int kIrqExti15_10 = 40;
constexpr int kIrqUsart6 = 71;

constexpr int kDiscoveryLedPort = 3; // GPIOD
constexpr uint32_t kDiscoveryLedMask = 0xF000u; // PD12-15
constexpr int kDiscoveryButtonPort = 0; // GPIOA
constexpr int kDiscoveryButtonPin = 0;

} // namespace mcu
