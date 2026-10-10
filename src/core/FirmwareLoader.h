#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LoadedImage {
    uint32_t entry = 0;
    uint32_t vectorBase = 0x08000000u;
    bool ok = false;
    std::string error;
};

class CpuEngine;

class FirmwareLoader {
public:
    static LoadedImage load(CpuEngine &cpu, const std::string &path);
    static LoadedImage loadBuffer(CpuEngine &cpu, const uint8_t *data, size_t size,
                                  const std::string &hintName);
    static void writeFlash(CpuEngine &cpu, uint32_t addr, const void *data, size_t size);
    static void installBuiltinBlinky(CpuEngine &cpu);
    // True if data starts with a Cortex-M vector table: initial SP in SRAM/CCM, Thumb reset vector.
    static bool looksLikeVectorTable(const uint8_t *data, size_t size, std::string *why);

private:
    static LoadedImage loadElf(CpuEngine &cpu, const uint8_t *data, size_t size);
    static LoadedImage loadHex(CpuEngine &cpu, const uint8_t *data, size_t size);
    static LoadedImage loadBin(CpuEngine &cpu, const uint8_t *data, size_t size, uint32_t base);
};
