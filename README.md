# mk_emul — STM32F407 emulator

> Branch `my-version`: list of changes and fixes relative to the original repository — see [CHANGELOG.md](CHANGELOG.md).

Desktop emulator of **STM32F407VG** (Cortex-M4): Unicorn Engine for the CPU, a custom MMIO model for peripherals, Qt 6 Quick/QML UI.

## Features

- Load **ELF / HEX / BIN** or the built-in LED demo
- In-app C editor + **Build & Load** via `arm-none-eabi-gcc`
- Run / Pause / Step, breakpoints, registers, memory, disassembly
- **Real-time mode** (default): 1 emulated second = 1 wall-clock second at the current HCLK;
  also ×0.1, ×0.5, ×2, ×10 and max speed; measured speed is shown in the toolbar
- Virtual Discovery board: PD12–PD15 LEDs, PA0 button, ADC slider
- USART console, RCC/SysTick/NVIC/EXTI/TIM/ADC/SPI/I2C stubs (HAL-friendly ready flags)

## Build (Windows)

Tested with Visual Studio 2026 Community (workload "Desktop development with C++", includes CMake and Ninja) and Qt 6.8.3 `msvc2022_64`. VS 2022 Build Tools also work.

1. Qt without an account, via [aqtinstall](https://github.com/miurahr/aqtinstall):
   ```powershell
   py -m pip install --user aqtinstall
   py -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
   ```
2. ARM toolchain for the in-app **Build & Load** (do this before the first build: CMake copies
   `third_party/toolchain` next to the exe as `toolchain\`):
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/fetch_toolchain.ps1
   ```
3. Build (finds `vcvars64.bat` itself; override with `QT_DIR` / `VCVARS` env vars):
   ```bat
   scriptsuild.bat
   ```
   First configure downloads Unicorn and Capstone (needs network).

Run `build\mk_emul.exe` (desktop UI).

Console smoke run without UI: `mk_emul.exe --headless [steps] [firmware.elf|.hex|.bin]` prints
`status=... leds=0x? pc=0x...`. Without a firmware path it runs the built-in blinky.

## Tests

```bat
scripts\test.bat
```
Builds everything and runs CTest (77 tests, ~8 s):
- **unit tests** (GoogleTest, `tests/`) for the Qt-free core library `mk_emul_core`: RCC clock tree,
  GPIO/EXTI, register semantics (partial writes, rc_w0/rc_w1), SysTick/TIM/ADC/USART, CPU
  (instruction count, breakpoints, SysTick exception entry/return, HardFault/BusFault/lockup),
  NVIC (interrupt latency, priorities, SVC/PendSV, FPU context on exception entry);
- **firmware tests**: `examples/*` compiled with `arm-none-eabi-gcc` and run on the emulator
  (skipped if the toolchain is missing);
- **end-to-end**: `mk_emul.exe --headless` with real firmware.

Single test: `build\tests\mk_emul_tests.exe --gtest_filter=NvicTest.*`.
Manual UI checklist: [docs/MANUAL_TESTING.md](docs/MANUAL_TESTING.md).

## Examples

- `examples/blinky` — GPIOD 12–15 toggle
- `examples/uart_printf` — USART2 text
- `examples/systick_exti` — SysTick delay + EXTI0 (USER button)
- `examples/hardfault` — unmapped read → `HardFault_Handler`, shows CFSR/HFSR/BFAR
- `examples/fpu_irq` — float math in `main()` and in a frequent SysTick interrupt (FPU context
  saving): green LED blinks, red means FP registers were corrupted
