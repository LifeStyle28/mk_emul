#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class Machine;

// GDB Remote Serial Protocol server side for the emulated Cortex-M4: what OpenOCD or the
// ST-LINK gdbserver are for a real board. arm-none-eabi-gdb connects to it and reads registers
// and memory, sets breakpoints and runs/steps the machine; the UI debugger drives gdb (MI).
//
// Transport-independent: bytes from gdb go to feed(), replies leave through `send`.
// Register, memory and breakpoint packets are answered at once. Continue and range step are
// asynchronous: Target::resume starts the machine and the owner calls stopped() when it stops.
class GdbStub {
public:
    struct Target {
        // Run until a breakpoint, a fault or interrupt(); with end > start also until PC leaves
        // [start, end) (vCont;r, how gdb steps over a source line).
        std::function<void(uint32_t start, uint32_t end)> resume;
        std::function<void()> interrupt;    // Ctrl-C from gdb while running
        std::function<void()> stateChanged; // stopped target changed: step, register/memory write
        std::function<void()> reset;        // "monitor reset"
    };

    static constexpr int kSigInt = 2;
    static constexpr int kSigTrap = 5;

    GdbStub(Machine &machine, std::function<void(const std::string &)> send, Target target);

    void connected(); // a new gdb connection: protocol state back to the start
    void feed(const char *data, size_t len);
    // The machine stopped after resume(): sends the stop reply gdb is waiting for.
    void stopped(int signal);
    bool running() const { return running_; }

    // Register layout reported to gdb (org.gnu.gdb.arm.m-profile + vfp + m-system).
    static std::string targetXml();
    static constexpr int kRegCount = 40; // r0-r15, xpsr, d0-d15, fpscr, msp, psp, primask, basepri, faultmask, control

private:
    void handlePacket(const std::string &p);
    void sendPacket(const std::string &payload);
    void stopReply(int signal);
    std::string readRegister(int n) const; // hex, target byte order
    void writeRegister(int n, const std::string &hex);
    std::string readMemory(uint32_t addr, uint32_t len) const;
    bool writeMemory(uint32_t addr, uint32_t len, const std::string &hex);
    std::string monitor(const std::string &cmd);
    void startResume(uint32_t start, uint32_t end);
    void stepOnce();

    Machine &m_;
    std::function<void(const std::string &)> send_;
    Target target_;
    std::string in_;
    bool noAck_ = false;
    bool running_ = false;
};
