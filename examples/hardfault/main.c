#include "stm32f407xx.h"

// Blinks the green LED, then reads from an unmapped address.
// Expected: emulator stops in HardFault_Handler, status "HardFault: read unmapped @ 0x60000000",
// SCB_CFSR = 0x00008200 (PRECISERR | BFARVALID), SCB_HFSR = 0x40000000 (FORCED),
// SCB_BFAR = 0x60000000. Press Run: the red LED (PD14) turns on.

static void delay(volatile uint32_t n) {
    while (n--) {
    }
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);

    for (int i = 0; i < 6; ++i) {
        GPIOD->ODR ^= (1u << 12);
        delay(20000);
    }

    volatile uint32_t *bad = (volatile uint32_t *)0x60000000u; // FSMC, not emulated
    (void)*bad;

    for (;;) {
    }
}

void HardFault_Handler(void) {
    GPIOD->BSRR = (1u << 14);
    for (;;) {
    }
}
