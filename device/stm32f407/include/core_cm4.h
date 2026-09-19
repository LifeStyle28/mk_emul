#ifndef CORE_CM4_H
#define CORE_CM4_H
#include <stdint.h>
#include "cmsis_gcc.h"

#ifndef __I
#define __I volatile const
#endif
#ifndef __O
#define __O volatile
#endif
#ifndef __IO
#define __IO volatile
#endif

typedef struct {
    __IO uint32_t ISER[8];
    uint32_t RESERVED0[24];
    __IO uint32_t ICER[8];
    uint32_t RESERVED1[24];
    __IO uint32_t ISPR[8];
    uint32_t RESERVED2[24];
    __IO uint32_t ICPR[8];
    uint32_t RESERVED3[24];
    __IO uint32_t IABR[8];
    uint32_t RESERVED4[56];
    __IO uint8_t IP[240];
} NVIC_Type;

typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __I uint32_t CALIB;
} SysTick_Type;

typedef struct {
    __I uint32_t CPUID;
    __IO uint32_t ICSR;
    __IO uint32_t VTOR;
    __IO uint32_t AIRCR;
    __IO uint32_t SCR;
    __IO uint32_t CCR;
    __IO uint8_t SHP[12];
    __IO uint32_t SHCSR;
    uint32_t RESERVED1[32];
    __IO uint32_t CPACR;
} SCB_Type;

#define SCS_BASE (0xE000E000UL)
#define SysTick_BASE (SCS_BASE + 0x0010UL)
#define NVIC_BASE (SCS_BASE + 0x0100UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SysTick ((SysTick_Type *)SysTick_BASE)
#define NVIC ((NVIC_Type *)NVIC_BASE)
#define SCB ((SCB_Type *)SCB_BASE)

#define SysTick_CTRL_ENABLE_Msk (1UL)
#define SysTick_CTRL_TICKINT_Msk (1UL << 1)
#define SysTick_CTRL_CLKSOURCE_Msk (1UL << 2)

__STATIC_INLINE void NVIC_EnableIRQ(IRQn_Type IRQn) {
    if ((int32_t)IRQn >= 0)
        NVIC->ISER[(uint32_t)IRQn >> 5] = (1UL << ((uint32_t)IRQn & 0x1F));
}
__STATIC_INLINE void NVIC_DisableIRQ(IRQn_Type IRQn) {
    if ((int32_t)IRQn >= 0)
        NVIC->ICER[(uint32_t)IRQn >> 5] = (1UL << ((uint32_t)IRQn & 0x1F));
}
__STATIC_INLINE void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority) {
    if ((int32_t)IRQn >= 0)
        NVIC->IP[(uint32_t)IRQn] = (uint8_t)((priority << 4) & 0xFF);
    else
        SCB->SHP[(((uint32_t)IRQn) & 0xF) - 4] = (uint8_t)((priority << 4) & 0xFF);
}
__STATIC_INLINE uint32_t SysTick_Config(uint32_t ticks) {
    if ((ticks - 1UL) > 0xFFFFFFUL)
        return 1;
    SysTick->LOAD = ticks - 1;
    NVIC_SetPriority(SysTick_IRQn, 15);
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    return 0;
}
#endif
