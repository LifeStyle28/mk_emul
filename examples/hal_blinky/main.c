// STM32CubeF4 HAL project, the way CubeMX generates it: 168 MHz from HSE through the PLL,
// HAL_Delay on SysTick, USART2 through HAL_UART_Transmit, USER button (PA0) through EXTI.
// Built with the real ST HAL (third_party/stm32cube, see docs/TODO.md), not the emulator's
// own headers. Expected in the emulator:
//   UART: "HAL ok, SYSCLK=168000000" then "tick N" every 100 ms,
//   green LED (PD12) blinks, USER button toggles the red LED (PD14).
// If any HAL call fails (e.g. a ready flag never set), Error_Handler turns on the blue LED
// (PD15) and stops.
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart2;
volatile uint32_t error_line;

void Error_Handler(uint32_t line) {
    error_line = line;
    __disable_irq();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);
    for (;;) {
    }
}

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler(__LINE__);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
        Error_Handler(__LINE__);
}

static void GPIO_Init(void) {
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    g.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &g);

    g.Pin = GPIO_PIN_0;
    g.Mode = GPIO_MODE_IT_RISING;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

static void UART2_Init(void) {
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_USART2_CLK_ENABLE();
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler(__LINE__);
}

static void print(const char *s) {
    if (HAL_UART_Transmit(&huart2, (const uint8_t *)s, (uint16_t)strlen(s), 100) != HAL_OK)
        Error_Handler(__LINE__);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    UART2_Init();

    char buf[48];
    snprintf(buf, sizeof(buf), "HAL ok, SYSCLK=%lu\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    print(buf);

    for (uint32_t n = 1;; ++n) {
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12);
        snprintf(buf, sizeof(buf), "tick %lu\r\n", (unsigned long)n);
        print(buf);
        HAL_Delay(100);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == GPIO_PIN_0)
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
}

// stm32f4xx_it.c in a CubeMX project
void SysTick_Handler(void) {
    HAL_IncTick();
}

void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}
