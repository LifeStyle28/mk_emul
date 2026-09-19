#include "stm32f407xx.h"

volatile uint32_t ticks;

void SysTick_Handler(void) {
    ticks++;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = ticks;
    while ((ticks - start) < ms) {
    }
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);
    GPIOA->MODER &= ~3u;
    SYSCFG->EXTICR[0] &= ~0xF;
    EXTI->IMR |= 1;
    EXTI->RTSR |= 1;
    NVIC_EnableIRQ(EXTI0_IRQn);
    SysTick_Config(16000);

    for (;;) {
        GPIOD->ODR ^= (1u << 12);
        delay_ms(200);
    }
}

void EXTI0_IRQHandler(void) {
    EXTI->PR = 1;
    GPIOD->ODR ^= (1u << 14);
}
