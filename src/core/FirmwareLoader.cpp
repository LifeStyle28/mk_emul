#include "core/FirmwareLoader.h"

#include "core/Addresses.h"
#include "core/CpuEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#pragma pack(push, 1)
struct Elf32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};
#pragma pack(pop)

void FirmwareLoader::writeFlash(CpuEngine &cpu, uint32_t addr, const void *data, size_t size) {
    cpu.writeMem(addr, data, size);
    if (addr >= mcu::kFlashBase && addr < mcu::kFlashBase + mcu::kFlashSize) {
        const uint32_t alias = mcu::kFlashAlias + (addr - mcu::kFlashBase);
        cpu.writeMem(alias, data, size);
    } else if (addr < mcu::kFlashSize) {
        cpu.writeMem(mcu::kFlashBase + addr, data, size);
    }
}

LoadedImage FirmwareLoader::load(CpuEngine &cpu, const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LoadedImage r;
        r.error = "Cannot open " + path;
        return r;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return loadBuffer(cpu, data.data(), data.size(), path);
}

LoadedImage FirmwareLoader::loadBuffer(CpuEngine &cpu, const uint8_t *data, size_t size,
                                       const std::string &hintName) {
    if (size >= 4 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
        return loadElf(cpu, data, size);
    bool looksHex = false;
    if (size > 0 && data[0] == ':')
        looksHex = true;
    else {
        auto extPos = hintName.find_last_of('.');
        if (extPos != std::string::npos) {
            std::string ext = hintName.substr(extPos);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".hex" || ext == ".ihex")
                looksHex = true;
        }
    }
    if (looksHex)
        return loadHex(cpu, data, size);
    return loadBin(cpu, data, size, mcu::kFlashBase);
}

LoadedImage FirmwareLoader::loadElf(CpuEngine &cpu, const uint8_t *data, size_t size) {
    LoadedImage out;
    if (size < sizeof(Elf32_Ehdr)) {
        out.error = "ELF too small";
        return out;
    }
    Elf32_Ehdr eh;
    std::memcpy(&eh, data, sizeof(eh));
    if (eh.e_ident[4] != 1) {
        out.error = "Only ELF32 is supported";
        return out;
    }
    if (eh.e_machine != 40) {
        out.error = "ELF is not ARM";
        return out;
    }
    out.entry = eh.e_entry;
    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        const size_t off = static_cast<size_t>(eh.e_phoff) + static_cast<size_t>(i) * eh.e_phentsize;
        if (off + sizeof(Elf32_Phdr) > size) {
            out.error = "ELF phdr truncated";
            return out;
        }
        Elf32_Phdr ph;
        std::memcpy(&ph, data + off, sizeof(ph));
        if (ph.p_type != 1) // PT_LOAD
            continue;
        if (ph.p_filesz && ph.p_offset + ph.p_filesz > size) {
            out.error = "ELF segment truncated";
            return out;
        }
        uint32_t dest = ph.p_paddr ? ph.p_paddr : ph.p_vaddr;
        if (ph.p_filesz)
            writeFlash(cpu, dest, data + ph.p_offset, ph.p_filesz);
        if (ph.p_memsz > ph.p_filesz) {
            std::vector<uint8_t> z(ph.p_memsz - ph.p_filesz, 0);
            writeFlash(cpu, dest + ph.p_filesz, z.data(), z.size());
        }
        if (dest >= mcu::kFlashBase && dest < mcu::kFlashBase + mcu::kFlashSize)
            out.vectorBase = mcu::kFlashBase;
    }
    out.ok = true;
    return out;
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

LoadedImage FirmwareLoader::loadHex(CpuEngine &cpu, const uint8_t *data, size_t size) {
    LoadedImage out;
    std::string text(reinterpret_cast<const char *>(data), size);
    std::istringstream ss(text);
    std::string line;
    uint32_t high = 0;
    uint32_t minAddr = 0xFFFFFFFFu;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] != ':')
            continue;
        auto byteAt = [&](int idx) {
            return (hexVal(line[1 + idx * 2]) << 4) | hexVal(line[2 + idx * 2]);
        };
        const int count = byteAt(0);
        const int addr = (byteAt(1) << 8) | byteAt(2);
        const int type = byteAt(3);
        if (type == 0) {
            std::vector<uint8_t> buf(count);
            for (int i = 0; i < count; ++i)
                buf[i] = static_cast<uint8_t>(byteAt(4 + i));
            const uint32_t dest = high + static_cast<uint32_t>(addr);
            writeFlash(cpu, dest, buf.data(), buf.size());
            minAddr = std::min(minAddr, dest);
        } else if (type == 1) {
            break;
        } else if (type == 4) {
            high = (static_cast<uint32_t>(byteAt(4)) << 24) | (static_cast<uint32_t>(byteAt(5)) << 16);
        } else if (type == 5) {
            out.entry = (static_cast<uint32_t>(byteAt(4)) << 24) | (static_cast<uint32_t>(byteAt(5)) << 16)
                        | (static_cast<uint32_t>(byteAt(6)) << 8) | static_cast<uint32_t>(byteAt(7));
        }
    }
    out.vectorBase = (minAddr >= mcu::kFlashBase) ? mcu::kFlashBase : minAddr;
    out.ok = true;
    return out;
}

LoadedImage FirmwareLoader::loadBin(CpuEngine &cpu, const uint8_t *data, size_t size, uint32_t base) {
    LoadedImage out;
    writeFlash(cpu, base, data, size);
    out.vectorBase = base;
    out.ok = true;
    return out;
}

void FirmwareLoader::installBuiltinBlinky(CpuEngine &cpu) {
    static const uint8_t kFw[] = {
#include "core/BuiltinFirmware.inc"
    };
    loadBin(cpu, kFw, sizeof(kFw), mcu::kFlashBase);
}
