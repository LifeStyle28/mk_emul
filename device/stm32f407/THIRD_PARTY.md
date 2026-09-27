# Сторонний код в `device/stm32f407`

Копии без изменений. Используются встроенной сборкой (Build & Load) и тестами.

| Папка | Что | Версия | Лицензия |
|---|---|---|---|
| `cube/hal` | STM32CubeF4 HAL driver (`Inc`, `Src`; шаблоны `*_template.*` не включены) | github.com/STMicroelectronics/stm32f4xx_hal_driver, коммит `1f6451c` (04.07.2025) | BSD-3-Clause, `cube/hal/LICENSE.md` |
| `cube/cmsis/Include`, `cube/cmsis/system_stm32f4xx.c` | CMSIS Device F4: `stm32f4xx.h`, `stm32f407xx.h`, `system_stm32f4xx.h`, шаблон `system_stm32f4xx.c` | github.com/STMicroelectronics/cmsis_device_f4, коммит `a833f4a` | Apache-2.0, `cube/cmsis/LICENSE-device.md` |
| `cube/cmsis/Core` | CMSIS Core для Cortex-M4 (`core_cm4.h`, `cmsis_gcc.h`, …) | CMSIS 6.3.0 (github.com/STMicroelectronics/cmsis_core, `afc5ca6`) | Apache-2.0, `cube/cmsis/LICENSE-core.md` |
| `freertos` | FreeRTOS-Kernel: ядро, `include`, `portable/GCC/ARM_CM4F`, `portable/MemMang/heap_4.c` | V11.1.0 | MIT, `freertos/LICENSE.md` |

Собственные файлы эмулятора: `include/` (упрощённые заголовки для регистровых примеров),
`startup_stm32f407xx.s`, `system_stm32f4xx.c`, `STM32F407VGTx_FLASH.ld`.

## Как сборщик выбирает, что подключить
- В проекте есть `stm32f4xx_hal_conf.h`: компилируются `stm32f4xx_hal.c` и модули, включённые
  в нём (`#define HAL_xxx_MODULE_ENABLED`, вместе с `_ex` и т. п.), `-DUSE_HAL_DRIVER`, заголовки
  из `cube/`, системный файл `cube/cmsis/system_stm32f4xx.c`.
- В проекте есть `FreeRTOSConfig.h`: ядро FreeRTOS, порт ARM_CM4F и `heap_4.c` (если в проекте
  нет своего `heap_N.c`). Обработчики порта подключаются к векторам так, как в
  `examples/freertos/FreeRTOSConfig.h` (`#define xPortSysTickHandler SysTick_Handler` и т. д.).
- Файл библиотеки не добавляется, если в проекте есть файл с тем же именем (например, свой
  `system_stm32f4xx.c` или копия FreeRTOS в `Middlewares/`). `startup_*.s` проекта заменяется
  на startup эмулятора, папка `Drivers/` проекта CubeMX не загружается.
- Объекты библиотек кешируются в `project_build/lib` и пересобираются, только когда меняются
  заголовки проекта или флаги.

Обновить: скопировать новые версии в те же папки и прогнать `scripts\build.bat test`
(тесты `HalFirmwareTest.*`, `FreeRtosFirmwareTest.*`, `BuilderTest.*`).
