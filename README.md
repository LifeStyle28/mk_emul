# mk_emul — STM32F407 emulator

Desktop emulator of **STM32F407VG** (Cortex-M4): Unicorn Engine for the CPU, a custom MMIO model for peripherals, Qt 6 Quick/QML UI.

## Features

- Load **ELF / HEX / BIN** or the built-in LED demo
- In-app C editor + **Build & Load** via `arm-none-eabi-gcc`
- Run / Pause / Step, breakpoints, registers, memory, disassembly
- Virtual Discovery board: PD12–PD15 LEDs, PA0 button, ADC slider
- USART console, RCC/SysTick/NVIC/EXTI/TIM/ADC/SPI/I2C stubs (HAL-friendly ready flags)

## Build (Windows)

Qt 6.3+ (msvc2019_64) and Visual Studio 2022 Build Tools with C++.

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.3.2/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

First configure downloads Unicorn and Capstone (needs network).

Run `build\mk_emul.exe`. Optional: `mk_emul.exe --headless` for a short smoke run.

## Toolchain for in-app compile

```powershell
powershell -ExecutionPolicy Bypass -File scripts/fetch_toolchain.ps1
```

Copy `third_party/toolchain` next to the exe as `toolchain\`, or set `ARM_NONE_EABI_GCC` to `arm-none-eabi-gcc.exe`.

## Examples

- `examples/blinky` — GPIOD 12–15 toggle
- `examples/uart_printf` — USART2 text
- `examples/systick_exti` — SysTick delay + EXTI0 (USER button)
