cmake_minimum_required(VERSION 3.20)
project(mk_emul VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "C:/Qt/6.3.2/msvc2019_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.3.2/msvc2019_64")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(UNICORN_ARCH "arm" CACHE STRING "Unicorn architectures" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)
set(UNICORN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(unicorn
    URL https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(unicorn)

set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(capstone
    URL https://github.com/capstone-engine/capstone/archive/refs/tags/5.0.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(capstone)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

set(MK_EMUL_SOURCES
    src/app/main.cpp
    src/core/CpuEngine.cpp
    src/core/MmioBus.cpp
    src/core/Nvic.cpp
    src/core/FirmwareLoader.cpp
    src/core/Machine.cpp
    src/core/Disassembler.cpp
    src/periph/Rcc.cpp
    src/periph/Gpio.cpp
    src/periph/Usart.cpp
    src/periph/SysTickTimer.cpp
    src/periph/Exti.cpp
    src/periph/SimplePeriphs.cpp
    src/ui/bridge/Models.cpp
    src/ui/bridge/EmulatorController.cpp
    src/ui/bridge/CHighlighter.cpp
    src/toolchain/ArmGccBuilder.cpp
)

qt_add_executable(mk_emul ${MK_EMUL_SOURCES})

set_source_files_properties(
    src/ui/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(
    src/ui/qml/Toolbar.qml PROPERTIES QT_RESOURCE_ALIAS Toolbar.qml)
set_source_files_properties(
    src/ui/qml/BoardPanel.qml PROPERTIES QT_RESOURCE_ALIAS BoardPanel.qml)
set_source_files_properties(
    src/ui/qml/RegisterPanel.qml PROPERTIES QT_RESOURCE_ALIAS RegisterPanel.qml)
set_source_files_properties(
    src/ui/qml/MemoryPanel.qml PROPERTIES QT_RESOURCE_ALIAS MemoryPanel.qml)
set_source_files_properties(
    src/ui/qml/DisasmPanel.qml PROPERTIES QT_RESOURCE_ALIAS DisasmPanel.qml)
set_source_files_properties(
    src/ui/qml/PeriphPanel.qml PROPERTIES QT_RESOURCE_ALIAS PeriphPanel.qml)
set_source_files_properties(
    src/ui/qml/UartPanel.qml PROPERTIES QT_RESOURCE_ALIAS UartPanel.qml)
set_source_files_properties(
    src/ui/qml/BuildPanel.qml PROPERTIES QT_RESOURCE_ALIAS BuildPanel.qml)
set_source_files_properties(
    src/ui/qml/EditorPanel.qml PROPERTIES QT_RESOURCE_ALIAS EditorPanel.qml)
set_source_files_properties(
    src/ui/qml/Theme.qml PROPERTIES
        QT_RESOURCE_ALIAS Theme.qml
        QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(
    src/ui/qml/PanelCard.qml PROPERTIES QT_RESOURCE_ALIAS PanelCard.qml)
set_source_files_properties(
    src/ui/qml/AppButton.qml PROPERTIES QT_RESOURCE_ALIAS AppButton.qml)
set_source_files_properties(
    src/ui/qml/CodeEditor.qml PROPERTIES QT_RESOURCE_ALIAS CodeEditor.qml)

qt_add_qml_module(mk_emul
    URI MkEmul
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/Toolbar.qml
        src/ui/qml/BoardPanel.qml
        src/ui/qml/RegisterPanel.qml
        src/ui/qml/MemoryPanel.qml
        src/ui/qml/DisasmPanel.qml
        src/ui/qml/PeriphPanel.qml
        src/ui/qml/UartPanel.qml
        src/ui/qml/BuildPanel.qml
        src/ui/qml/EditorPanel.qml
        src/ui/qml/Theme.qml
        src/ui/qml/PanelCard.qml
        src/ui/qml/AppButton.qml
        src/ui/qml/CodeEditor.qml
)

target_include_directories(mk_emul PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${unicorn_SOURCE_DIR}/include
    ${capstone_SOURCE_DIR}/include
)

target_link_libraries(mk_emul PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    unicorn
    capstone
)

if(MSVC)
    target_compile_options(mk_emul PRIVATE /W3 /utf-8 /Zc:__cplusplus)
    target_compile_definitions(mk_emul PRIVATE NOMINMAX)
endif()

add_custom_command(TARGET mk_emul POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/device $<TARGET_FILE_DIR:mk_emul>/device
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples $<TARGET_FILE_DIR:mk_emul>/examples
)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/toolchain")
    add_custom_command(TARGET mk_emul POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/third_party/toolchain $<TARGET_FILE_DIR:mk_emul>/toolchain
    )
endif()

if(WIN32)
    add_custom_command(TARGET mk_emul POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:unicorn> $<TARGET_FILE_DIR:mk_emul>
    )
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${CMAKE_PREFIX_PATH}/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET mk_emul POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --qmldir "${CMAKE_SOURCE_DIR}/src/ui/qml" "$<TARGET_FILE:mk_emul>"
        )
    endif()
endif()
