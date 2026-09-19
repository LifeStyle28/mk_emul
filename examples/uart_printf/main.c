#include "stm32f407xx.h"

static void uart2_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    GPIOA->MODER &= ~(3u << 4);
    GPIOA->MODER |= (2u << 4); /* PA2 AF */
    GPIOA->AFR[0] &= ~(0xFu << 8);
    GPIOA->AFR[0] |= (7u << 8);
    USART2->BRR = 0x008B; /* ~115200 @ 16 MHz */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart2_write(const char *s) {
    while (*s) {
        while ((USART2->SR & USART_SR_TXE) == 0) {
        }
        USART2->DR = (uint8_t)*s++;
    }
}

int main(void) {
    uart2_init();
    uart2_write("STM32F407 emulator UART2\r\n");

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);

    for (;;) {
        uart2_write("tick\r\n");
        GPIOD->ODR ^= (0xF << 12);
        for (volatile int i = 0; i < 300000; ++i) {
        }
    }
}
