#include "system_stm32f4xx.h"
#include "stm32f407xx.h"

uint32_t SystemCoreClock = 16000000;

void SystemInit(void) {
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    SCB->VTOR = 0x08000000;
}

void SystemCoreClockUpdate(void) {
    SystemCoreClock = 16000000;
}
