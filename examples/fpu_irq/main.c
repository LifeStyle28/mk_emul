#include "stm32f407xx.h"

// Float math both in main() and in a frequent interrupt. The compiler relies on the hardware
// to save S0-S15/FPSCR on interrupt entry (extended stack frame), so it keeps intermediate
// values in FP registers without saving them.
// Expected: the green LED (PD12) blinks, the red LED (PD14) never turns on.
// Without FPU context saving the SysTick handler overwrites main()'s registers and the sum
// comes out wrong: red LED on, main() stops.

volatile float isr_acc;
volatile uint32_t isr_count;

void SysTick_Handler(void) {
    isr_acc = isr_acc * 0.5f + 3.0f; // uses S-registers, just like main() does
    isr_count++;
}

static float sum_quarters(int n) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i)
        s = s + 0.25f;
    return s;
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);
    SysTick_Config(157); // interrupt every 157 cycles: lands inside the loop above

    for (;;) {
        if (sum_quarters(4000) != 1000.0f) {
            GPIOD->BSRR = 1u << 14; // red: FP registers were corrupted
            for (;;) {
            }
        }
        GPIOD->ODR ^= 1u << 12; // green: another correct round
    }
}
