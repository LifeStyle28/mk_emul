# Что осталось сделать

Состояние на 28.09.2026 (ветка `my-version`). Сделанное — в [CHANGELOG.md](../CHANGELOG.md).
Всё из прошлого списка сделано: HAL- и FreeRTOS-проекты собираются кнопкой Build & Load (HAL,
CMSIS и FreeRTOS лежат в `device/stm32f407`), TIM4 с ШИМ, TIM5–TIM7, предупреждения о
неэмулируемой периферии, BusFault при непривилегированном доступе к NVIC/SCB, FPDSCR, WFI со
сном до прерывания, автоочистка консоли, блокировка Build & Load. 103 автотеста, ручной
чек-лист обновлён.

## Решения, принятые по ходу
- **Консоль UART** очищается при загрузке новой прошивки (Build & Load, Open FW…, Builtin LED),
  но не при Reset: после Reset удобно сравнить вывод со старым.
- **HAL и FreeRTOS в репозитории** (`device/stm32f407/cube`, `device/stm32f407/freertos`,
  около 13 МБ, в git ~2 МБ): иначе Build & Load не работает «из коробки». Версии и лицензии —
  `device/stm32f407/THIRD_PARTY.md`.
- **Startup проекта CubeMX заменяется** на startup эмулятора (он такой же для F407 и точно
  совпадает с линкер-скриптом эмулятора), линкер-скрипт тоже всегда свой.
- **Сдача**: ветка `my-version` целиком (история коммитов по шагам + CHANGELOG). Pull request
  в `main` своего форка по желанию: `gh pr create --base main --head my-version`.

## Можно сделать дальше (не обязательно)
- TIM1/TIM8 (продвинутые таймеры: BDTR.MOE, APB2), TIM9–TIM14; флаги CCxIF и прерывания по
  сравнению; режимы toggle/match; preload CCR/ARR (сейчас запись действует сразу).
- DMA (HAL_UART_Transmit_DMA, ADC по DMA) — самый частый «следующий шаг» в учебных проектах.
- WFE/SEV: `WFE` пока выполняется как NOP (без флага события); `WFI` уже спит до прерывания.
- Нереализованный регистр внутри эмулируемого блока (например, `TIMx->BDTR`) не даёт
  предупреждения — только целиком отсутствующие блоки.
- FPDSCR: у задачи/потока без FP-контекста после возврата из обработчика FPSCR остаётся
  значением обработчика (на МК следующая FP-инструкция взяла бы FPDSCR).
- `NVIC->STIR` из непривилегированного кода при `CCR.USERSETMPEND = 1` тоже даёт BusFault
  (на МК разрешено).
- Обёртка CMSIS-RTOS2 (`cmsis_os2.c` из CubeMX) не проверялась.

## Полезное для продолжения
- Сборка `scripts\build.bat`, тесты `scripts\test.bat`, UI-автоматизация `scripts\ui.ps1`.
- Бенчмарк скорости: `build\tests\mk_emul_tests --gtest_also_run_disabled_tests --gtest_filter=*Speed*`.
  Сравнивать версии только попеременными прогонами и медианами (разброс одиночных ±10%).
  Последний замер: blinky 137, uart_printf 146, systick_exti 109, fpu_irq 36 MIPS.
- Маленькие программы для тестов ядра: `tests/asm/*.s` → `py tests/asm/gen.py`.
- Тесты сборщика (как Build & Load): `build\tests\mk_emul_builder_tests.exe`.
