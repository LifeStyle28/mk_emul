#include "stm32f407xx.h"

// LED brightness through PWM: on STM32F4-Discovery the four LEDs PD12-PD15 are TIM4 CH1-CH4
// (alternate function AF2). TIM4 runs at 1 MHz (16 MHz / 16), period 1000 -> 1 kHz PWM.
// Expected: orange 25%, red 50%, blue 100% brightness; green "breathes" 0 -> 100% -> 0
// in about 2 s (SysTick changes CCR1 every 1 ms).

#define PWM_PERIOD 1000u

void SysTick_Handler(void) {
    static int duty, step = 1;
    duty += step;
    if (duty >= (int)PWM_PERIOD || duty <= 0)
        step = -step;
    TIM4->CCR1 = (uint32_t)duty;
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    // PD12-PD15: alternate function mode (MODER = 10), AF2 = TIM4.
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0xAAu << 24);
    GPIOD->AFR[1] &= ~0xFFFF0000u;
    GPIOD->AFR[1] |= 0x22220000u;

    TIM4->PSC = 16 - 1;
    TIM4->ARR = PWM_PERIOD - 1;
    // PWM mode 1 with preload on all four channels.
    TIM4->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE
                | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE;
    TIM4->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE
                | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
    TIM4->CCR1 = 0;
    TIM4->CCR2 = PWM_PERIOD / 4;
    TIM4->CCR3 = PWM_PERIOD / 2;
    TIM4->CCR4 = PWM_PERIOD;
    TIM4->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM4->EGR = TIM_EGR_UG; // load PSC/ARR/CCR preload registers
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    SysTick_Config(16000); // 1 ms

    for (;;) {
    }
}
