# Что осталось сделать

Состояние на 27.09.2026 (ветка `my-version`). Сделанное — в [CHANGELOG.md](../CHANGELOG.md).
Уже работает и проверено: ядро Cortex-M4 (прерывания, приоритеты, SVC/PendSV, FPU, CPACR/NOCP,
PSP и непривилегированный режим), таймеры, UART, EXTI, режим реального времени, весь ручной
чек-лист `docs/MANUAL_TESTING.md`, проект на STM32CubeF4 HAL `examples/hal_blinky`, FreeRTOS `examples/freertos` (86 автотестов).

## 1. Проверить на «настоящих» прошивках (главный риск)
- [x] **Проект на STM32CubeF4 HAL** — `examples/hal_blinky`, тесты `HalFirmwareTest.*`
      (понадобился bit-band и символ `end` в линкер-скрипте, см. CHANGELOG).
      Исходники HAL кладутся в `third_party/stm32cube/` (не в git): клоны
      github.com/STMicroelectronics/stm32f4xx_hal_driver, cmsis_device_f4 и cmsis_core.
- [x] **FreeRTOS** — `examples/freertos`, тесты `FreeRtosFirmwareTest.*`; эмулятор менять
      не пришлось. Ядро: `git clone --depth 1 --branch V11.1.0
      https://github.com/FreeRTOS/FreeRTOS-Kernel.git third_party/freertos`.
- [ ] **Build & Load для HAL- и FreeRTOS-проектов в приложении.** `ArmGccBuilder` знает только
      `device/stm32f407/include`, поэтому `examples/hal_blinky` и `examples/freertos`, открытые в
      окне, не соберутся. Вариант: поставлять HAL (BSD-3) и FreeRTOS (MIT) рядом с `device/`
      и добавлять их `-I`/исходники, если в папке проекта есть `stm32f4xx_hal_conf.h` /
      `FreeRTOSConfig.h`.

## 2. Пробелы в эмуляции, заметные студенту
- [ ] **TIM4 и ШИМ** на PD12–PD15 (классическое упражнение на Discovery: яркость светодиодов через
      TIM4 CH1–CH4). Сейчас есть только TIM2/TIM3, запись в TIM4 молча игнорируется.
      Нужно: TIM4 (и лучше TIM1/TIM5), режим PWM (CCMRx/CCER/CCRx), альтернативная функция GPIO
      (MODER=10, AFR), на плате — яркость светодиода по скважности.
- [ ] **Предупреждение об обращении к неэмулируемой периферии** (DMA, TIM1/4–14, CAN, …): сейчас
      чтение даёт 0, запись теряется без следа. Показывать в статусе/логе
      «write to unemulated peripheral @ 0x40000800» (один раз на адрес).
- [ ] Непривилегированный код может обращаться к NVIC/SCB и периферии; на МК это BusFault.
- [ ] FPDSCR хранится, но не применяется к новому FP-контексту (редкий случай).

## 3. Интерфейс
- [ ] Консоль UART не очищается при загрузке другой прошивки (есть кнопка Clear) — **решить**:
      очищать автоматически или нет.
- [ ] Build & Load, нажатый сразу после выбора папки в диалоге, может собрать предыдущий проект
      (видно только при автоматизации, быстрее 1,2 с). Если всплывёт у людей — блокировать кнопку,
      пока открыт диалог.

## 4. Сдача
- [ ] Краткая сводка в начале CHANGELOG: таблица «исправлено / добавлено / чем проверено».
- [ ] Решить форму сдачи: pull request `my-version` → `main` или просто ветка.
- [ ] Перед сдачей: `scripts\test.bat` (все тесты зелёные) и прогон `docs/MANUAL_TESTING.md`.

## Полезное для продолжения
- Сборка `scripts\build.bat`, тесты `scripts\test.bat`, UI-автоматизация `scripts\ui.ps1`.
- Бенчмарк скорости: `build\tests\mk_emul_tests --gtest_also_run_disabled_tests --gtest_filter=*Speed*`.
  Сравнивать версии только попеременными прогонами и медианами (разброс одиночных ±10%).
- Маленькие программы для тестов ядра: `tests/asm/*.s` → `py tests/asm/gen.py`.
