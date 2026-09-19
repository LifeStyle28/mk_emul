#ifndef SYSTEM_STM32F4XX_H
#define SYSTEM_STM32F4XX_H
#include <stdint.h>
extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);
#endif
