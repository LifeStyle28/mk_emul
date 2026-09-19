#include "core/Disassembler.h"

#include <capstone/capstone.h>

#include <cstddef>
#include <cstdio>
#include <sstream>

Disassembler::Disassembler() = default;

Disassembler::~Disassembler() {
    if (handle_) {
        csh h = static_cast<csh>(handle_);
        cs_close(&h);
        handle_ = 0;
    }
}

bool Disassembler::open(std::string *error) {
    csh h = 0;
    if (cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &h) != CS_ERR_OK) {
        if (error)
            *error = "Capstone cs_open failed";
        return false;
    }
    cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
    handle_ = static_cast<std::size_t>(h);
    return true;
}

std::vector<DisasmLine> Disassembler::disassemble(const uint8_t *code, size_t len, uint32_t address,
                                                  uint32_t pc, const std::vector<uint32_t> &bps) const {
    std::vector<DisasmLine> out;
    if (!handle_ || !code || len == 0)
        return out;
    cs_insn *insn = nullptr;
    const size_t n = cs_disasm(static_cast<csh>(handle_), code, len, address, 0, &insn);
    auto isBp = [&](uint32_t a) {
        for (uint32_t b : bps) {
            if ((b & ~1u) == (a & ~1u))
                return true;
        }
        return false;
    };
    for (size_t i = 0; i < n; ++i) {
        DisasmLine line;
        line.address = static_cast<uint32_t>(insn[i].address);
        line.size = insn[i].size;
        line.mnemonic = insn[i].mnemonic;
        line.op = insn[i].op_str;
        std::ostringstream bytes;
        for (uint16_t b = 0; b < insn[i].size; ++b) {
            char tmp[8];
            std::snprintf(tmp, sizeof(tmp), "%02X", insn[i].bytes[b]);
            if (b)
                bytes << ' ';
            bytes << tmp;
        }
        line.bytes = bytes.str();
        line.current = (line.address & ~1u) == (pc & ~1u);
        line.breakpoint = isBp(line.address);
        out.push_back(std::move(line));
    }
    if (insn)
        cs_free(insn, n);
    return out;
}
