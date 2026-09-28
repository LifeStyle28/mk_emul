# mk_emul — STM32F407 emulator

> Branch `my-version`, based on the original repository at commit `bf0b419`.
> **What changed and why:** [CHANGELOG.md](CHANGELOG.md) (summary table at the top, then every fix
> with the files it touched). Diff of the code: `git diff bf0b419 my-version`, step by step:
> `git log --reverse bf0b419..my-version`. Remaining work: [docs/TODO.md](docs/TODO.md).

Desktop emulator of **STM32F407VG** (Cortex-M4): Unicorn Engine for the CPU, a custom MMIO model for peripherals, Qt 6 Quick/QML UI.

## Features

- Load **ELF / HEX / BIN** or the built-in LED demo
- In-app C editor + **Build & Load** via `arm-none-eabi-gcc`, with the bundled
  **STM32CubeF4 HAL** (project has `stm32f4xx_hal_conf.h`) and **FreeRTOS** (project has
  `FreeRTOSConfig.h`); CubeMX project folders (`Core/Inc`, `Core/Src`) build as they are.
  The build runs in the background, library objects are cached.
  See [device/stm32f407/THIRD_PARTY.md](device/stm32f407/THIRD_PARTY.md)
- Editor can take the whole window: **Expand** in the Source panel or **F11** (**Restore** / F11 back)
- Run / Pause / Step, breakpoints, registers, memory, disassembly
- **Real-time mode** (default): 1 emulated second = 1 wall-clock second at the current HCLK;
  also ×0.1, ×0.5, ×2, ×10 and max speed; measured speed is shown in the toolbar
- Cortex-M4 core: exact interrupt latency, priorities, BASEPRI/PRIMASK, SVC/PendSV,
  HardFault/BusFault/UsageFault with CFSR/HFSR/BFAR, FPU context on exception entry,
  `WFI` sleeps until the next interrupt (status `Running (WFI sleep)`)
- Virtual Discovery board: PD12–PD15 LEDs, PA0 button, ADC slider
- TIM2–TIM7 (TIM6/TIM7 also as the HAL time base) with PWM: LED brightness follows the TIM4 duty
  cycle when PD12–15 are in AF2
- USART console, RCC/SysTick/NVIC/EXTI/TIM/ADC/SPI/I2C stubs (HAL-friendly ready flags),
  bit-band regions
- Accesses to peripherals the emulator does not have are reported in the Build log panel

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
   scripts\build.bat
   ```
   First configure downloads Unicorn and Capstone (needs network).
   Close a running `mk_emul.exe` before rebuilding, otherwise linking fails (LNK1104).

Run `build\mk_emul.exe` (desktop UI).

Console smoke run without UI: `mk_emul.exe --headless [steps] [firmware.elf|.hex|.bin]` prints
`status=... leds=0x? pc=0x...`. Without a firmware path it runs the built-in blinky.

## Quick start

1. **Open project…** → `examples\blinky` (the code appears in the Source panel).
2. **Build & Load** → the Build log ends with `Linked ...firmware.elf`.
3. **Run** → the four LEDs on the board (top left) blink.
4. Change the code, **Build & Load** again, **Run**. A compile error is shown in the Build log with
   the line number; the previous firmware keeps running.

## Tests

```bat
scripts\test.bat
```
Builds everything and runs CTest: 103 tests, ~12 s, expected `100% tests passed`
(`FirmwareTest.Speed` is a disabled benchmark).

| What | Run | Checks |
|---|---|---|
| All tests | `scripts\test.bat` | everything below |
| Core unit + firmware tests | `build\tests\mk_emul_tests.exe` | CPU, NVIC, peripherals, examples on the emulator |
| One group | `build\tests\mk_emul_tests.exe --gtest_filter=NvicTest.*` | |
| Build & Load code | `build\tests\mk_emul_builder_tests.exe` | examples, CubeMX layout, HAL time base on TIM6, compile errors |
| End-to-end | `build\mk_emul.exe --headless 20000 build\tests\firmware\systick_exti.elf` | prints `status=... leds=0x? pc=0x...` |

Test groups (`--gtest_filter`):

| Group | File | Covers |
|---|---|---|
| `MachineTest.*` | `tests/test_cpu.cpp` | instruction count, breakpoints, faults, lockup, WFI |
| `NvicTest.*` | `tests/test_nvic.cpp` | interrupt latency, priorities, SVC/PendSV, FPU context, CPACR/NOCP, FPDSCR |
| `SysTickFixture.*`, `TimerFixture.*`, `UsartFixture.*`, `Timer.*`, `Adc.*`, `Bitband.*`, `Exti.*` | `tests/test_periph_regs.cpp` | register semantics (partial writes, rc_w0/rc_w1), counters |
| `ExtiFixture.*`, `Gpio.*` | `tests/test_gpio_exti.cpp` | GPIO, button, EXTI |
| `Rcc.*` | `tests/test_rcc.cpp` | clock tree, HSE + PLL |
| `RealTimePacer.*` | `tests/test_pacer.cpp` | real-time mode |
| `Disassembler.*` | `tests/test_disasm.cpp` | disassembly panel data |
| `FirmwareTest.*`, `HalFirmwareTest.*`, `FreeRtosFirmwareTest.*` | `tests/test_firmware.cpp` | `examples/*` built with gcc and run (skipped without the toolchain) |
| `BuilderTest.*`, `HalSources.*` | `tests/test_builder.cpp` | same code as **Build & Load** |

The UI itself is checked by hand: [docs/MANUAL_TESTING.md](docs/MANUAL_TESTING.md) (~15 min).
`scripts\ui.ps1` takes window screenshots and presses buttons to go through it automatically.

## Examples

Open with **Open project…** → **Build & Load** → **Run**. Prebuilt ELF files (after `test.bat`)
are in `build\tests\firmware\` for **Open FW…**.

| Folder | What it shows | Expected |
|---|---|---|
| `examples/blinky` | GPIOD 12–15 toggle | all four LEDs blink |
| `examples/uart_printf` | USART2 text | `STM32F407 emulator UART2`, then `tick` in the USART console |
| `examples/systick_exti` | SysTick delay + EXTI0 | green blinks; **USER PA0** toggles red |
| `examples/hardfault` | unmapped read → `HardFault_Handler` | stops with `HardFault: read unmapped @ 0x60000000`, `SCB_CFSR = 0x00008200` |
| `examples/fpu_irq` | float math in `main()` and in a frequent SysTick interrupt | green blinks; red would mean corrupted FP registers |
| `examples/tim4_pwm` | TIM4 PWM, main loop sleeps in `WFI` | 25/50/100% brightness, green "breathes", status `Running (WFI sleep)` |
| `examples/hal_blinky` | STM32CubeF4 HAL as CubeMX generates it: 168 MHz from HSE + PLL | `HAL ok, SYSCLK=168000000`, `tick 1, 2…`; PA0 toggles red |
| `examples/freertos` | six tasks, a queue, a semaphore from an interrupt, per-task FPU context | `rx 1 a=.. b=..` every 100 ms; PA0 toggles orange |

## Repository layout

| Path | Contents |
|---|---|
| `src/core`, `src/periph` | Qt-free emulator core (`mk_emul_core`): CPU, NVIC, peripherals |
| `src/app` | `main.cpp`: UI start and `--headless` mode |
| `src/toolchain` | project builder behind **Build & Load** |
| `src/ui` | Qt Quick UI (QML panels and the C++ bridge) |
| `device/stm32f407` | startup, linker script, CMSIS, STM32CubeF4 HAL, FreeRTOS |
| `examples/` | firmware examples |
| `tests/` | GoogleTest suites, `tests/asm` small Cortex-M programs |
| `scripts/` | build, tests, toolchain download, UI automation |
| `docs/` | manual checklist, remaining work |
