#ifndef STM32F407XX_H
#define STM32F407XX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    NonMaskableInt_IRQn = -14,
    HardFault_IRQn = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn = -11,
    UsageFault_IRQn = -10,
    SVCall_IRQn = -5,
    DebugMonitor_IRQn = -4,
    PendSV_IRQn = -2,
    SysTick_IRQn = -1,
    WWDG_IRQn = 0,
    PVD_IRQn = 1,
    TAMP_STAMP_IRQn = 2,
    RTC_WKUP_IRQn = 3,
    FLASH_IRQn = 4,
    RCC_IRQn = 5,
    EXTI0_IRQn = 6,
    EXTI1_IRQn = 7,
    EXTI2_IRQn = 8,
    EXTI3_IRQn = 9,
    EXTI4_IRQn = 10,
    DMA1_Stream0_IRQn = 11,
    ADC_IRQn = 18,
    EXTI9_5_IRQn = 23,
    TIM2_IRQn = 28,
    TIM3_IRQn = 29,
    I2C1_EV_IRQn = 31,
    I2C1_ER_IRQn = 32,
    SPI1_IRQn = 35,
    USART1_IRQn = 37,
    USART2_IRQn = 38,
    USART3_IRQn = 39,
    EXTI15_10_IRQn = 40,
    USART6_IRQn = 71
} IRQn_Type;

#include "core_cm4.h"
#include "system_stm32f4xx.h"

typedef struct {
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CR, PLLCFGR, CFGR, CIR, AHB1RSTR, AHB2RSTR, AHB3RSTR, RESERVED0,
        APB1RSTR, APB2RSTR, RESERVED1[2], AHB1ENR, AHB2ENR, AHB3ENR, RESERVED2,
        APB1ENR, APB2ENR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_TypeDef;

typedef struct {
    volatile uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR;
} EXTI_TypeDef;

typedef struct {
    volatile uint32_t MEMRMP, PMC, EXTICR[4], RESERVED[2], CMPCR;
} SYSCFG_TypeDef;

typedef struct {
    volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RESERVED1, CCR1, CCR2, CCR3, CCR4;
} TIM_TypeDef;

typedef struct {
    volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR1, JOFR2, JOFR3, JOFR4, HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR1, JDR2, JDR3, JDR4, DR;
} ADC_TypeDef;

typedef struct {
    volatile uint32_t CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR;
} SPI_TypeDef;

typedef struct {
    volatile uint32_t CR1, CR2, OAR1, OAR2, DR, SR1, SR2, CCR, TRISE, FLTR;
} I2C_TypeDef;

typedef struct {
    volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, OPTCR;
} FLASH_TypeDef;

typedef struct {
    volatile uint32_t CR, CSR;
} PWR_TypeDef;

#define PERIPH_BASE 0x40000000UL
#define APB1PERIPH_BASE PERIPH_BASE
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)

#define TIM2_BASE (APB1PERIPH_BASE + 0x0000)
#define TIM3_BASE (APB1PERIPH_BASE + 0x0400)
#define USART2_BASE (APB1PERIPH_BASE + 0x4400)
#define USART3_BASE (APB1PERIPH_BASE + 0x4800)
#define I2C1_BASE (APB1PERIPH_BASE + 0x5400)
#define PWR_BASE (APB1PERIPH_BASE + 0x7000)
#define USART1_BASE (APB2PERIPH_BASE + 0x1000)
#define USART6_BASE (APB2PERIPH_BASE + 0x1400)
#define ADC1_BASE (APB2PERIPH_BASE + 0x2000)
#define SPI1_BASE (APB2PERIPH_BASE + 0x3000)
#define SYSCFG_BASE (APB2PERIPH_BASE + 0x3800)
#define EXTI_BASE (APB2PERIPH_BASE + 0x3C00)
#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400)
#define GPIOC_BASE (AHB1PERIPH_BASE + 0x0800)
#define GPIOD_BASE (AHB1PERIPH_BASE + 0x0C00)
#define GPIOE_BASE (AHB1PERIPH_BASE + 0x1000)
#define RCC_BASE (AHB1PERIPH_BASE + 0x3800)
#define FLASH_R_BASE (AHB1PERIPH_BASE + 0x3C00)

#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOE ((GPIO_TypeDef *)GPIOE_BASE)
#define RCC ((RCC_TypeDef *)RCC_BASE)
#define USART1 ((USART_TypeDef *)USART1_BASE)
#define USART2 ((USART_TypeDef *)USART2_BASE)
#define USART3 ((USART_TypeDef *)USART3_BASE)
#define USART6 ((USART_TypeDef *)USART6_BASE)
#define EXTI ((EXTI_TypeDef *)EXTI_BASE)
#define SYSCFG ((SYSCFG_TypeDef *)SYSCFG_BASE)
#define TIM2 ((TIM_TypeDef *)TIM2_BASE)
#define TIM3 ((TIM_TypeDef *)TIM3_BASE)
#define ADC1 ((ADC_TypeDef *)ADC1_BASE)
#define SPI1 ((SPI_TypeDef *)SPI1_BASE)
#define I2C1 ((I2C_TypeDef *)I2C1_BASE)
#define FLASH ((FLASH_TypeDef *)FLASH_R_BASE)
#define PWR ((PWR_TypeDef *)PWR_BASE)

#define RCC_AHB1ENR_GPIOAEN (1U << 0)
#define RCC_AHB1ENR_GPIODEN (1U << 3)
#define RCC_APB1ENR_TIM2EN (1U << 0)
#define RCC_APB1ENR_USART2EN (1U << 17)
#define RCC_APB2ENR_USART1EN (1U << 4)
#define RCC_APB2ENR_ADC1EN (1U << 8)
#define RCC_APB2ENR_SPI1EN (1U << 12)
#define RCC_APB2ENR_SYSCFGEN (1U << 14)

#define USART_CR1_UE (1U << 13)
#define USART_CR1_TE (1U << 3)
#define USART_CR1_RE (1U << 2)
#define USART_SR_TXE (1U << 7)
#define USART_SR_TC (1U << 6)
#define USART_SR_RXNE (1U << 5)

#ifdef __cplusplus
}
#endif
#endif
