#include "stm32f407xx.h"

static void delay(volatile uint32_t n) {
    while (n--)
        __NOP();
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);

    for (;;) {
        GPIOD->BSRR = (0xF << 12);
        delay(200000);
        GPIOD->BSRR = (0xF << (12 + 16));
        delay(200000);
    }
}
