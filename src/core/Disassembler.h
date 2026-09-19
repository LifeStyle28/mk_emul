#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DisasmLine {
    uint32_t address = 0;
    uint32_t size = 2;
    std::string bytes;
    std::string mnemonic;
    std::string op;
    bool current = false;
    bool breakpoint = false;
};

class Disassembler {
public:
    Disassembler();
    ~Disassembler();

    bool open(std::string *error);
    std::vector<DisasmLine> disassemble(const uint8_t *code, size_t len, uint32_t address,
                                        uint32_t pc, const std::vector<uint32_t> &bps) const;

private:
    std::size_t handle_ = 0;
};
