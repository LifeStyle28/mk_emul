// FreeRTOS (port GCC/ARM_CM4F) on the emulator: tasks on PSP, SVC starts the first task,
// PendSV switches context, SysTick drives the tick, the FPU context of each task is saved
// (lazy stacking) and an interrupt gives a semaphore through the ...FromISR API.
// Kernel: device/stm32f407/freertos (Build & Load adds it because of FreeRTOSConfig.h).
// Expected in the emulator:
//   UART: "FreeRTOS start" then "rx N a=.. b=.." every 100 ms (N = 1, 2, 3, ...),
//   green LED (PD12) blinks every 50 ms, USER button toggles the orange LED (PD13).
// On an error (assert, FPU registers corrupted by a context switch, lost queue item, stack
// overflow) the red LED (PD14) turns on and the reason goes to the UART.
#include "stm32f407xx.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>

static QueueHandle_t queue;
static SemaphoreHandle_t button_sem;
static volatile uint32_t rounds_a, rounds_b;
volatile const char *error_reason;

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

static void fail(const char *reason) {
    __disable_irq();
    error_reason = reason;
    GPIOD->BSRR = 1u << 14;
    uart2_write("ERROR: ");
    uart2_write(reason);
    uart2_write("\r\n");
    for (;;) {
    }
}

void freertos_assert_failed(const char *file, int line) {
    static char buf[96];
    snprintf(buf, sizeof(buf), "assert %s:%d", file, line);
    fail(buf);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    (void)name;
    fail("stack overflow");
}

void vApplicationMallocFailedHook(void) {
    fail("malloc failed");
}

// Two same-priority tasks share the CPU by time slicing: every tick may switch between them in
// the middle of the loop, while the partial sum lives in an S-register.
static float sum(float step, int n) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i)
        s = s + step;
    return s;
}

static void cruncher_a(void *arg) {
    (void)arg;
    for (;;) {
        if (sum(0.25f, 4000) != 1000.0f)
            fail("FPU registers corrupted (task a)");
        rounds_a++;
    }
}

static void cruncher_b(void *arg) {
    (void)arg;
    for (;;) {
        if (sum(0.75f, 4000) != 3000.0f)
            fail("FPU registers corrupted (task b)");
        rounds_b++;
    }
}

static void blink_task(void *arg) {
    (void)arg;
    for (;;) {
        GPIOD->ODR ^= 1u << 12;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void producer_task(void *arg) {
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t n = 1;; ++n) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
        if (xQueueSend(queue, &n, 0) != pdPASS)
            fail("queue full");
    }
}

static void printer_task(void *arg) {
    (void)arg;
    char buf[64];
    for (uint32_t expected = 1;; ++expected) {
        uint32_t n;
        xQueueReceive(queue, &n, portMAX_DELAY);
        if (n != expected)
            fail("queue item lost");
        snprintf(buf, sizeof(buf), "rx %lu a=%lu b=%lu\r\n", (unsigned long)n, (unsigned long)rounds_a,
                 (unsigned long)rounds_b);
        uart2_write(buf);
    }
}

static void button_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(button_sem, portMAX_DELAY);
        GPIOD->ODR ^= 1u << 13;
    }
}

// USER button (PA0) rising edge. Priority 6 is below configMAX_SYSCALL_INTERRUPT_PRIORITY (5),
// so the ISR may use the FromISR API.
void EXTI0_IRQHandler(void) {
    EXTI->PR = 1;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(button_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

int main(void) {
    uart2_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    GPIOD->MODER &= ~(0xFFu << 24);
    GPIOD->MODER |= (0x55u << 24);
    GPIOA->MODER &= ~3u;
    SYSCFG->EXTICR[0] &= ~0xFu;
    EXTI->IMR |= 1;
    EXTI->RTSR |= 1;
    NVIC_SetPriority(EXTI0_IRQn, 6);
    NVIC_EnableIRQ(EXTI0_IRQn);

    uart2_write("FreeRTOS start\r\n");
    queue = xQueueCreate(4, sizeof(uint32_t));
    button_sem = xSemaphoreCreateBinary();
    if (!queue || !button_sem)
        fail("create queue");

    xTaskCreate(cruncher_a, "crunch_a", 128, NULL, 1, NULL);
    xTaskCreate(cruncher_b, "crunch_b", 128, NULL, 1, NULL);
    xTaskCreate(blink_task, "blink", 128, NULL, 2, NULL);
    xTaskCreate(producer_task, "producer", 128, NULL, 2, NULL);
    xTaskCreate(printer_task, "printer", 512, NULL, 3, NULL);
    xTaskCreate(button_task, "button", 128, NULL, 3, NULL);
    vTaskStartScheduler();
    fail("scheduler returned");
}
