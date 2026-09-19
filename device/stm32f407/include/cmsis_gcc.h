#ifndef CMSIS_GCC_H
#define CMSIS_GCC_H
#include <stdint.h>
#define __ASM __asm
#define __INLINE inline
#define __STATIC_INLINE static inline
#define __STATIC_FORCEINLINE static inline
#define __NO_RETURN __attribute__((noreturn))
#define __USED __attribute__((used))
#define __WEAK __attribute__((weak))
#define __PACKED __attribute__((packed))
#define __ALIGNED(x) __attribute__((aligned(x)))
#define __COMPILER_BARRIER() __ASM volatile("" ::: "memory")
__STATIC_INLINE void __enable_irq(void) { __ASM volatile("cpsie i" : : : "memory"); }
__STATIC_INLINE void __disable_irq(void) { __ASM volatile("cpsid i" : : : "memory"); }
__STATIC_INLINE uint32_t __get_PRIMASK(void) {
    uint32_t r;
    __ASM volatile("mrs %0, primask" : "=r"(r));
    return r;
}
__STATIC_INLINE void __set_PRIMASK(uint32_t priMask) {
    __ASM volatile("msr primask, %0" : : "r"(priMask) : "memory");
}
__STATIC_INLINE void __DSB(void) { __ASM volatile("dsb 0xF" ::: "memory"); }
__STATIC_INLINE void __ISB(void) { __ASM volatile("isb 0xF" ::: "memory"); }
__STATIC_INLINE void __NOP(void) { __ASM volatile("nop"); }
#endif
