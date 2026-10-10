#pragma once

#include "periph/Peripheral.h"

#include <cstdint>
#include <functional>
#include <vector>

class MmioBus {
public:
    void add(Peripheral *p);
    uint32_t read(uint32_t addr, unsigned size);
    void write(uint32_t addr, uint32_t value, unsigned size);
    Peripheral *find(uint32_t addr) const;
    const std::vector<Peripheral *> &peripherals() const { return items_; }

    // beforeAccess runs before every read/write (to bring timers up to date), afterWrite after
    // every write (a new timer setting may move the next interrupt closer).
    void setAccessHooks(std::function<void()> beforeAccess, std::function<void()> afterWrite);

    // Bit-band (Cortex-M): every word in an alias region reads/writes one bit of the target
    // region: alias = aliasBase + (addr - targetBase) * 32 + bit * 4. HAL uses it, e.g.
    // __HAL_RCC_PLL_ENABLE() writes 1 to the alias of RCC->CR.PLLON.
    static constexpr uint32_t kPeriphBitbandBase = 0x42000000u; // -> 0x40000000
    static constexpr uint32_t kSramBitbandBase = 0x22000000u;   // -> 0x20000000
    // The bus is mapped over the alias regions too; SRAM bits go through these accessors.
    void setMemoryAccess(std::function<uint32_t(uint32_t)> read32, std::function<void(uint32_t, uint32_t)> write32);

    // Called on an access that no peripheral covers (reads as 0, writes are dropped).
    void setUnmappedHook(std::function<void(uint32_t addr, bool write)> hook) { unmapped_ = std::move(hook); }
    // Asked before every access to the Private Peripheral Bus (0xE0000000+: NVIC, SCB, SysTick,
    // DWT, ...); returning false blocks it (reads 0) -- unprivileged code gets a BusFault.
    static constexpr uint32_t kPpbStart = 0xE0000000u;
    void setPpbAccessCheck(std::function<bool(uint32_t addr, bool write)> check) { ppbCheck_ = std::move(check); }

private:
    uint32_t bitbandRead(uint32_t alias);
    void bitbandWrite(uint32_t alias, uint32_t value);

    std::function<uint32_t(uint32_t)> memRead32_;
    std::function<void(uint32_t, uint32_t)> memWrite32_;
    std::vector<Peripheral *> items_;
    std::function<void()> beforeAccess_;
    std::function<void()> afterWrite_;
    std::function<void(uint32_t, bool)> unmapped_;
    std::function<bool(uint32_t, bool)> ppbCheck_;
};
