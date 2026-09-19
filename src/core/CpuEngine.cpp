#include "core/CpuEngine.h"

#include "core/MmioBus.h"

#include <unicorn/unicorn.h>
#include <unicorn/arm.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct MmioCookie {
    uint32_t base = 0;
    MmioBus *bus = nullptr;
};

uint64_t mmioRead(uc_engine *, uint64_t offset, unsigned size, void *user) {
    auto *c = static_cast<MmioCookie *>(user);
    return c->bus->read(c->base + static_cast<uint32_t>(offset), size);
}

void mmioWrite(uc_engine *, uint64_t offset, unsigned size, uint64_t value, void *user) {
    auto *c = static_cast<MmioCookie *>(user);
    c->bus->write(c->base + static_cast<uint32_t>(offset), static_cast<uint32_t>(value), size);
}

} // namespace

static std::vector<std::unique_ptr<MmioCookie>> g_cookies;

CpuEngine::CpuEngine() = default;

CpuEngine::~CpuEngine() {
    close();
}

bool CpuEngine::open(std::string *error) {
    close();
    uc_err err = uc_open(UC_ARCH_ARM, static_cast<uc_mode>(UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN), &uc_);
    if (err != UC_ERR_OK) {
        lastError_ = uc_strerror(err);
        if (error)
            *error = lastError_;
        uc_ = nullptr;
        return false;
    }

#ifdef UC_CPU_ARM_CORTEX_M4
    uc_ctl_set_cpu_model(uc_, UC_CPU_ARM_CORTEX_M4);
#endif

    installHooks();
    return true;
}

void CpuEngine::close() {
    if (!uc_)
        return;
    uc_close(uc_);
    uc_ = nullptr;
    bpHooks_.clear();
    codeHook_ = 0;
    intrHook_ = 0;
    memHook_ = 0;
}

void CpuEngine::mapRam(uint32_t addr, uint32_t size) {
    uc_mem_map(uc_, addr, size, UC_PROT_ALL);
    std::vector<uint8_t> zero(size, 0);
    uc_mem_write(uc_, addr, zero.data(), size);
}

void CpuEngine::mapMmio(uint32_t addr, uint32_t size, MmioBus *bus) {
    auto cookie = std::make_unique<MmioCookie>();
    cookie->base = addr;
    cookie->bus = bus;
    uc_err err = uc_mmio_map(uc_, addr, size, mmioRead, cookie.get(), mmioWrite, cookie.get());
    if (err != UC_ERR_OK) {
        lastError_ = uc_strerror(err);
        return;
    }
    g_cookies.push_back(std::move(cookie));
}

void CpuEngine::writeMem(uint32_t addr, const void *data, size_t size) {
    uc_mem_write(uc_, addr, data, size);
}

void CpuEngine::readMem(uint32_t addr, void *data, size_t size) const {
    if (uc_mem_read(uc_, addr, data, size) != UC_ERR_OK)
        std::memset(data, 0, size);
}

uint32_t CpuEngine::read32(uint32_t addr) const {
    uint32_t v = 0;
    readMem(addr, &v, 4);
    return v;
}

void CpuEngine::write32(uint32_t addr, uint32_t value) {
    writeMem(addr, &value, 4);
}

uint32_t CpuEngine::reg(int unicornReg) const {
    uint32_t v = 0;
    uc_reg_read(uc_, unicornReg, &v);
    return v;
}

void CpuEngine::setReg(int unicornReg, uint32_t value) {
    uc_reg_write(uc_, unicornReg, &value);
}

uint32_t CpuEngine::pc() const { return reg(UC_ARM_REG_PC) & ~1u; }
uint32_t CpuEngine::sp() const { return reg(UC_ARM_REG_SP); }
uint32_t CpuEngine::lr() const { return reg(UC_ARM_REG_LR); }
uint32_t CpuEngine::xpsr() const { return reg(UC_ARM_REG_CPSR); }
uint32_t CpuEngine::msp() const { return reg(UC_ARM_REG_MSP); }
uint32_t CpuEngine::psp() const { return reg(UC_ARM_REG_PSP); }
uint32_t CpuEngine::control() const { return reg(UC_ARM_REG_CONTROL); }
uint32_t CpuEngine::ipsr() const { return xpsr() & 0x1FFu; }
uint32_t CpuEngine::primask() const { return reg(UC_ARM_REG_PRIMASK); }

void CpuEngine::setPc(uint32_t value) { setReg(UC_ARM_REG_PC, value); }
void CpuEngine::setSp(uint32_t value) { setReg(UC_ARM_REG_SP, value); }
void CpuEngine::setLr(uint32_t value) { setReg(UC_ARM_REG_LR, value); }
void CpuEngine::setXpsr(uint32_t value) { setReg(UC_ARM_REG_CPSR, value); }
void CpuEngine::setMsp(uint32_t value) { setReg(UC_ARM_REG_MSP, value); }
void CpuEngine::setPsp(uint32_t value) { setReg(UC_ARM_REG_PSP, value); }
void CpuEngine::setControl(uint32_t value) { setReg(UC_ARM_REG_CONTROL, value); }
void CpuEngine::setPrimask(uint32_t value) { setReg(UC_ARM_REG_PRIMASK, value); }

void CpuEngine::snapshotRegs(uint32_t out[16]) const {
    const int ids[16] = {
        UC_ARM_REG_R0,  UC_ARM_REG_R1,  UC_ARM_REG_R2,  UC_ARM_REG_R3,
        UC_ARM_REG_R4,  UC_ARM_REG_R5,  UC_ARM_REG_R6,  UC_ARM_REG_R7,
        UC_ARM_REG_R8,  UC_ARM_REG_R9,  UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP,  UC_ARM_REG_LR,  UC_ARM_REG_PC,
    };
    for (int i = 0; i < 16; ++i)
        out[i] = reg(ids[i]);
    out[15] &= ~1u;
}

void CpuEngine::installHooks() {
    auto *self = this;

    uc_hook_add(uc_, &intrHook_, UC_HOOK_INTR,
                (void *)+[](uc_engine *, uint32_t intno, void *user) {
                    auto *cpu = static_cast<CpuEngine *>(user);
                    if (cpu->onInterrupt_)
                        cpu->onInterrupt_(intno);
                    if (intno == 8)
                        cpu->lastStop_ = StopReason::InterruptHook;
                },
                self, 1, 0);

    uc_hook_add(uc_, &memHook_, UC_HOOK_MEM_UNMAPPED,
                (void *)+[](uc_engine *uc, uc_mem_type type, uint64_t addr, int, int64_t, void *user) -> bool {
                    auto *cpu = static_cast<CpuEngine *>(user);
                    cpu->faultAddress_ = static_cast<uint32_t>(addr);
                    cpu->lastStop_ = StopReason::Fault;
                    const char *kind = "memory";
                    if (type == UC_MEM_READ_UNMAPPED)
                        kind = "read unmapped";
                    else if (type == UC_MEM_WRITE_UNMAPPED)
                        kind = "write unmapped";
                    else if (type == UC_MEM_FETCH_UNMAPPED)
                        kind = "fetch unmapped";
                    cpu->lastError_ = std::string(kind) + " @ " + std::to_string(addr);
                    if (cpu->onFault_)
                        cpu->onFault_(cpu->lastError_, cpu->faultAddress_);
                    uc_emu_stop(uc);
                    return false;
                },
                self, 1, 0);

    rebuildBreakpointHooks();
}

void CpuEngine::rebuildBreakpointHooks() {
    for (auto h : bpHooks_)
        uc_hook_del(uc_, h);
    bpHooks_.clear();

    for (uint32_t addr : breakpoints_) {
        uc_hook h = 0;
        uint64_t a = addr & ~1u;
        uc_hook_add(uc_, &h, UC_HOOK_CODE,
                    (void *)+[](uc_engine *uc, uint64_t address, uint32_t, void *user) {
                        auto *cpu = static_cast<CpuEngine *>(user);
                        if (cpu->skipNextBreakpoint_) {
                            cpu->skipNextBreakpoint_ = false;
                            return;
                        }
                        cpu->lastStop_ = StopReason::Breakpoint;
                        uc_emu_stop(uc);
                        (void)address;
                    },
                    this, a, a);
        bpHooks_.push_back(h);
    }
}

void CpuEngine::addBreakpoint(uint32_t addr) {
    breakpoints_.insert(addr & ~1u);
    if (uc_)
        rebuildBreakpointHooks();
}

void CpuEngine::removeBreakpoint(uint32_t addr) {
    breakpoints_.erase(addr & ~1u);
    if (uc_)
        rebuildBreakpointHooks();
}

void CpuEngine::clearBreakpoints() {
    breakpoints_.clear();
    if (uc_)
        rebuildBreakpointHooks();
}

bool CpuEngine::hasBreakpoint(uint32_t addr) const {
    return breakpoints_.count(addr & ~1u) != 0;
}

std::vector<uint32_t> CpuEngine::breakpoints() const {
    std::vector<uint32_t> v(breakpoints_.begin(), breakpoints_.end());
    std::sort(v.begin(), v.end());
    return v;
}

void CpuEngine::setInterruptHandler(InterruptHandler handler) { onInterrupt_ = std::move(handler); }
void CpuEngine::setFaultHandler(FaultHandler handler) { onFault_ = std::move(handler); }

CpuEngine::StopReason CpuEngine::run(uint64_t maxInsns) {
    if (!uc_)
        return StopReason::Fault;
    lastStop_ = StopReason::Quantum;
    lastError_.clear();
    const uint32_t pcNow = pc();
    if (hasBreakpoint(pcNow))
        skipNextBreakpoint_ = true;
    const uint64_t start = static_cast<uint64_t>(pcNow) | 1ull;
    const uc_err err = uc_emu_start(uc_, start, 0, 0, maxInsns);
    insnCount_ += maxInsns;
    if (err != UC_ERR_OK && lastStop_ != StopReason::Breakpoint && lastStop_ != StopReason::InterruptHook) {
        lastStop_ = StopReason::Fault;
        lastError_ = uc_strerror(err);
        if (onFault_)
            onFault_(lastError_, pc());
    }
    return lastStop_;
}

void CpuEngine::stop() {
    if (uc_)
        uc_emu_stop(uc_);
}
