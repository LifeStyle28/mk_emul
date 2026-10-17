#include "system_stm32f4xx.h"
#include "stm32f407xx.h"

#ifndef HSE_VALUE
#define HSE_VALUE 8000000u /* STM32F4-Discovery crystal (also the emulator's HSE) */
#endif
#ifndef HSI_VALUE
#define HSI_VALUE 16000000u
#endif

uint32_t SystemCoreClock = 16000000;

void SystemInit(void) {
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    SCB->VTOR = 0x08000000;
}

/* SystemCoreClock = HCLK from the RCC registers (as in the CMSIS file of STM32CubeF4).
 * Call it after changing the clock, e.g. at the end of SystemClock_Config(). */
void SystemCoreClockUpdate(void) {
    static const uint8_t ahbShift[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
    uint32_t sysclk;
    switch ((RCC->CFGR >> 2) & 3u) { /* SWS */
    case 1:
        sysclk = HSE_VALUE;
        break;
    case 2: {
        const uint32_t m = RCC->PLLCFGR & 0x3Fu;
        const uint32_t n = (RCC->PLLCFGR >> 6) & 0x1FFu;
        const uint32_t p = (((RCC->PLLCFGR >> 16) & 3u) + 1u) * 2u;
        const uint32_t src = (RCC->PLLCFGR & (1u << 22)) ? HSE_VALUE : HSI_VALUE;
        sysclk = m ? (src / m) * n / p : HSI_VALUE;
        break;
    }
    default:
        sysclk = HSI_VALUE;
        break;
    }
    SystemCoreClock = sysclk >> ahbShift[(RCC->CFGR >> 4) & 0xFu];
}
