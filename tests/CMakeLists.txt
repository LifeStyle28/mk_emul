add_executable(mk_emul_tests
    TestUtil.h
    test_rcc.cpp
    test_gpio_exti.cpp
    test_periph_regs.cpp
    test_cpu.cpp
    test_firmware.cpp
    test_disasm.cpp
    test_pacer.cpp
    test_nvic.cpp
    cortex_programs.inc
)
set_target_properties(mk_emul_tests PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_link_libraries(mk_emul_tests PRIVATE mk_emul_core GTest::gtest_main)
if(MSVC)
    target_compile_options(mk_emul_tests PRIVATE /W3 /utf-8)
endif()

if(WIN32)
    add_custom_command(TARGET mk_emul_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:unicorn> $<TARGET_FILE_DIR:mk_emul_tests>
    )
endif()

# Firmware tests: build examples/* with arm-none-eabi-gcc (same flags as in-app Build & Load).
# Without the toolchain these tests are skipped, the rest still run.
find_program(MK_EMUL_ARM_GCC arm-none-eabi-gcc
    HINTS ${CMAKE_SOURCE_DIR}/third_party/toolchain/bin
    ENV ARM_NONE_EABI_GCC)
set(FW_DIR ${CMAKE_CURRENT_BINARY_DIR}/firmware)
file(GLOB DEVICE_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/device/stm32f407/include/*.h)
set(DEVICE_DIR ${CMAKE_SOURCE_DIR}/device/stm32f407)
set(FW_EXAMPLES blinky uart_printf systick_exti hardfault fpu_irq tim4_pwm)

if(MK_EMUL_ARM_GCC)
    message(STATUS "mk_emul tests: firmware built with ${MK_EMUL_ARM_GCC}")
    set(FW_ELFS)
    foreach(ex IN LISTS FW_EXAMPLES)
        set(elf ${FW_DIR}/${ex}.elf)
        add_custom_command(OUTPUT ${elf}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FW_DIR}
            COMMAND ${MK_EMUL_ARM_GCC}
                -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -g3 -O0
                -I${DEVICE_DIR}/include -DSTM32F407xx
                -nostartfiles -T ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld -Wl,--gc-sections --specs=nano.specs
                ${CMAKE_SOURCE_DIR}/examples/${ex}/main.c
                ${DEVICE_DIR}/startup_stm32f407xx.s
                ${DEVICE_DIR}/system_stm32f4xx.c
                -o ${elf} -lc -lm -lnosys
            DEPENDS ${CMAKE_SOURCE_DIR}/examples/${ex}/main.c
                    ${DEVICE_DIR}/startup_stm32f407xx.s
                    ${DEVICE_DIR}/system_stm32f4xx.c
                    ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld
                    ${DEVICE_HEADERS}
            COMMENT "Building test firmware ${ex}.elf"
            VERBATIM)
        list(APPEND FW_ELFS ${elf})
    endforeach()

    # examples/hal_blinky: the STM32CubeF4 HAL bundled in device/stm32f407/cube (the modules
    # enabled in its stm32f4xx_hal_conf.h, as the in-app builder picks them).
    set(CUBE_DIR ${DEVICE_DIR}/cube)
    set(HAL_SOURCES)
    foreach(m hal hal_rcc hal_rcc_ex hal_gpio hal_cortex hal_uart hal_pwr hal_pwr_ex
              hal_flash hal_flash_ex hal_flash_ramfunc hal_dma hal_dma_ex hal_exti)
        list(APPEND HAL_SOURCES ${CUBE_DIR}/hal/Src/stm32f4xx_${m}.c)
    endforeach()
    set(elf ${FW_DIR}/hal_blinky.elf)
    add_custom_command(OUTPUT ${elf}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FW_DIR}
        COMMAND ${MK_EMUL_ARM_GCC}
            -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -g3 -O0
            -ffunction-sections -fdata-sections -DSTM32F407xx -DUSE_HAL_DRIVER
            -I${CMAKE_SOURCE_DIR}/examples/hal_blinky
            -I${CUBE_DIR}/hal/Inc -I${CUBE_DIR}/cmsis/Include -I${CUBE_DIR}/cmsis/Core
            -nostartfiles -T ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld -Wl,--gc-sections --specs=nano.specs
            ${CMAKE_SOURCE_DIR}/examples/hal_blinky/main.c
            ${DEVICE_DIR}/startup_stm32f407xx.s
            ${CUBE_DIR}/cmsis/system_stm32f4xx.c ${HAL_SOURCES}
            -o ${elf} -lc -lm -lnosys
        DEPENDS ${CMAKE_SOURCE_DIR}/examples/hal_blinky/main.c
                ${CMAKE_SOURCE_DIR}/examples/hal_blinky/stm32f4xx_hal_conf.h
                ${DEVICE_DIR}/startup_stm32f407xx.s
                ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld
                ${CUBE_DIR}/cmsis/system_stm32f4xx.c ${HAL_SOURCES}
        COMMENT "Building test firmware hal_blinky.elf (STM32CubeF4 HAL)"
        VERBATIM)
    list(APPEND FW_ELFS ${elf})

    # examples/freertos: the FreeRTOS kernel bundled in device/stm32f407/freertos.
    set(RTOS_DIR ${DEVICE_DIR}/freertos)
    set(RTOS_PORT ${RTOS_DIR}/portable/GCC/ARM_CM4F)
    set(RTOS_SOURCES ${RTOS_DIR}/tasks.c ${RTOS_DIR}/queue.c ${RTOS_DIR}/list.c ${RTOS_DIR}/timers.c
                     ${RTOS_DIR}/event_groups.c ${RTOS_DIR}/stream_buffer.c
                     ${RTOS_PORT}/port.c ${RTOS_DIR}/portable/MemMang/heap_4.c)
    set(elf ${FW_DIR}/freertos.elf)
    add_custom_command(OUTPUT ${elf}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FW_DIR}
        COMMAND ${MK_EMUL_ARM_GCC}
            -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -g3 -O0
            -ffunction-sections -fdata-sections -I${DEVICE_DIR}/include -DSTM32F407xx
            -I${CMAKE_SOURCE_DIR}/examples/freertos -I${RTOS_DIR}/include -I${RTOS_PORT}
            -nostartfiles -T ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld -Wl,--gc-sections --specs=nano.specs
            ${CMAKE_SOURCE_DIR}/examples/freertos/main.c
            ${DEVICE_DIR}/startup_stm32f407xx.s
            ${DEVICE_DIR}/system_stm32f4xx.c
            ${RTOS_SOURCES}
            -o ${elf} -lc -lm -lnosys
        DEPENDS ${CMAKE_SOURCE_DIR}/examples/freertos/main.c
                ${CMAKE_SOURCE_DIR}/examples/freertos/FreeRTOSConfig.h
                ${DEVICE_DIR}/startup_stm32f407xx.s
                ${DEVICE_DIR}/system_stm32f4xx.c
                ${DEVICE_DIR}/STM32F407VGTx_FLASH.ld
                ${DEVICE_HEADERS} ${RTOS_SOURCES}
        COMMENT "Building test firmware freertos.elf (FreeRTOS kernel)"
        VERBATIM)
    list(APPEND FW_ELFS ${elf})

    add_custom_target(mk_emul_test_firmware ALL DEPENDS ${FW_ELFS})
    add_dependencies(mk_emul_tests mk_emul_test_firmware)
    target_compile_definitions(mk_emul_tests PRIVATE MK_EMUL_FW_DIR="${FW_DIR}")
else()
    message(STATUS "mk_emul tests: arm-none-eabi-gcc not found, firmware tests will be skipped")
endif()

add_executable(mk_emul_builder_tests test_builder.cpp ${CMAKE_SOURCE_DIR}/src/toolchain/ArmGccBuilder.cpp)
set_target_properties(mk_emul_builder_tests PROPERTIES AUTOMOC ON)
target_include_directories(mk_emul_builder_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mk_emul_builder_tests PRIVATE mk_emul_core Qt6::Core GTest::gtest)
target_compile_definitions(mk_emul_builder_tests PRIVATE
    MK_EMUL_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    MK_EMUL_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}/builder_work")
if(MSVC)
    target_compile_options(mk_emul_builder_tests PRIVATE /W3 /utf-8 /Zc:__cplusplus)
endif()
if(WIN32)
    add_custom_command(TARGET mk_emul_builder_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:unicorn> $<TARGET_FILE_DIR:mk_emul_builder_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:mk_emul_builder_tests>
    )
endif()

include(GoogleTest)
gtest_discover_tests(mk_emul_tests DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(mk_emul_builder_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 300)

# End-to-end: the real mk_emul.exe in --headless mode.
add_test(NAME headless.builtin_blinky COMMAND mk_emul --headless 20000)
set_tests_properties(headless.builtin_blinky PROPERTIES
    PASS_REGULAR_EXPRESSION "status=Running"
    FAIL_REGULAR_EXPRESSION "Fault|Lockup"
    TIMEOUT 60)
if(MK_EMUL_ARM_GCC)
    add_test(NAME headless.systick_exti COMMAND mk_emul --headless 20000 ${FW_DIR}/systick_exti.elf)
    set_tests_properties(headless.systick_exti PROPERTIES
        PASS_REGULAR_EXPRESSION "status=Running"
        FAIL_REGULAR_EXPRESSION "Fault|Lockup"
        TIMEOUT 60)
    add_test(NAME headless.hardfault COMMAND mk_emul --headless 20000 ${FW_DIR}/hardfault.elf)
    set_tests_properties(headless.hardfault PROPERTIES
        PASS_REGULAR_EXPRESSION "status=HardFault: read unmapped @ 0x60000000"
        TIMEOUT 60)
endif()
