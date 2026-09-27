#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_MSC_VER)
#define MK_NOINLINE __declspec(noinline)
#else
#define MK_NOINLINE __attribute__((noinline))
#endif

struct uc_struct;
typedef struct uc_struct uc_engine;

class MmioBus;

class CpuEngine {
public:
    CpuEngine();
    ~CpuEngine();

    CpuEngine(const CpuEngine &) = delete;
    CpuEngine &operator=(const CpuEngine &) = delete;

    bool open(std::string *error);
    void close();
    bool isOpen() const { return uc_ != nullptr; }

    void mapRam(uint32_t addr, uint32_t size);
    void mapMmio(uint32_t addr, uint32_t size, MmioBus *bus);
    void writeMem(uint32_t addr, const void *data, size_t size);
    // Drop translated code; required after replacing code in memory (new firmware).
    void flushCodeCache();
    void readMem(uint32_t addr, void *data, size_t size) const;
    uint32_t read32(uint32_t addr) const;
    void write32(uint32_t addr, uint32_t value);

    uint32_t reg(int unicornReg) const;
    void setReg(int unicornReg, uint32_t value);

    uint32_t pc() const;
    uint32_t sp() const;
    uint32_t lr() const;
    uint32_t xpsr() const;
    uint32_t msp() const;
    uint32_t psp() const;
    uint32_t control() const;
    uint32_t ipsr() const;
    uint32_t primask() const;
    uint32_t basepri() const;
    uint32_t faultmask() const;
    void setPc(uint32_t value);
    void setSp(uint32_t value);
    void setLr(uint32_t value);
    void setXpsr(uint32_t value);
    void setMsp(uint32_t value);
    void setPsp(uint32_t value);
    void setControl(uint32_t value);
    void setPrimask(uint32_t value);
    // Sets/clears CONTROL.nPRIV. Unicorn ignores writes to MSP/PSP/CONTROL/PRIMASK (and reads
    // them as 0) while the CPU runs unprivileged thread code, even from the API; the exception
    // logic clears nPRIV around such accesses and restores it afterwards.
    void setNPriv(bool on);
    // FPU: single-precision registers S0-S31 and FPSCR.
    uint32_t sreg(int n) const;
    void setSreg(int n, uint32_t value);
    uint32_t fpscr() const;
    void setFpscr(uint32_t value);

    void snapshotRegs(uint32_t out[16]) const;
    void readBanked();

    enum class StopReason { None, Quantum, Breakpoint, Fault, InterruptHook, Halted };
    enum class FaultKind { None, ReadUnmapped, WriteUnmapped, FetchUnmapped, InvalidInsn, Unaligned, NoCoprocessor, Other };

    StopReason run(uint64_t maxInsns);
    void stop();
    // During run(): stop once `insns` instructions of this run have executed, if that is sooner.
    void limitRun(uint64_t insns) { runLimit_ = std::min(runLimit_, insns); }

    void addBreakpoint(uint32_t addr);
    void removeBreakpoint(uint32_t addr);
    void clearBreakpoints();
    bool hasBreakpoint(uint32_t addr) const;
    std::vector<uint32_t> breakpoints() const;

    using InterruptHandler = std::function<void(uint32_t intno)>;
    void setInterruptHandler(InterruptHandler handler);

    // Called before each instruction while *pendingFlag is true; returning true stops the run so
    // the caller can take the interrupt right away (instead of at the end of the chunk).
    void setInterruptPoll(const bool *pendingFlag, std::function<bool()> shouldStop);

    using FaultHandler = std::function<void(const std::string &msg, uint32_t addr)>;
    void setFaultHandler(FaultHandler handler);

    uint64_t instructionCount() const { return insnCount_; }
    void resetInstructionCount() { insnCount_ = 0; }
    uint64_t lastRunCount() const { return runExecuted_; }
    FaultKind faultKind() const { return faultKind_; }
    const std::string &lastError() const { return lastError_; }
    StopReason lastStop() const { return lastStop_; }
    uint32_t faultAddress() const { return faultAddress_; }

    uc_engine *raw() { return uc_; }

    // SCB->CPACR: an FP instruction without access stops the run with FaultKind::NoCoprocessor
    // (Unicorn itself never checks CPACR).
    void setCpacr(uint32_t cpacr) { fpAccess_ = (cpacr >> 20) & 3u; }

private:
    void installHooks();
    // Host copy of mapped RAM/flash at addr, or nullptr (MMIO, unmapped, crosses a region end).
    const uint8_t *hostPtr(uint32_t addr, uint32_t len) const;
    // Special register that Unicorn reads as 0 from unprivileged thread code.
    uint32_t privilegedReg(int unicornReg) const;
    bool isFpInstruction(uint32_t addr) const;
    bool fpAccessAllowed() const;
    MK_NOINLINE void reportNoCoprocessor(uint32_t addr);

    struct RamRegion {
        uint32_t base;
        uint32_t size;
        std::unique_ptr<uint8_t[]> data;
    };
    std::vector<RamRegion> ram_;
    uint32_t fpAccess_ = 0; // CPACR.CP10: 0 denied, 1 privileged only, 3 full

    uc_engine *uc_ = nullptr;
    InterruptHandler onInterrupt_;
    const bool *irqPendingFlag_ = nullptr;
    std::function<bool()> irqShouldStop_;
    FaultHandler onFault_;
    std::unordered_set<uint32_t> breakpoints_;
    uint64_t codeHook_ = 0;
    uint64_t intrHook_ = 0;
    uint64_t memHook_ = 0;
    uint64_t insnCount_ = 0;
    uint64_t runLimit_ = 0;
    uint64_t runExecuted_ = 0;
    FaultKind faultKind_ = FaultKind::None;
    uint32_t faultAddress_ = 0;
    StopReason lastStop_ = StopReason::None;
    std::string lastError_;
    bool skipNextBreakpoint_ = false;
};
