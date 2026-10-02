# Unit tests for the firmware, built and run on the host rather than the
# ESP32-C3.  The ESP-IDF APIs the firmware calls are replaced by the fakes in
# fakes/.  From the firmware directory:
#
#   cmake -S test -B test/build
#   cmake --build test/build
#   ctest --test-dir test/build --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(rainbow_music_pegasus_tests C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)  # gnu17, as ESP-IDF uses
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(unity
    URL      https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz
    URL_HASH SHA256=e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c
)
FetchContent_MakeAvailable(unity)

# The same warnings as ESP-IDF.
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)

# Make out-of-bounds accesses and undefined behaviour fail the tests.  On the
# ESP32-C3 they would go unnoticed.
option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
if(SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main)

add_library(fake_idf STATIC fakes/fake_idf.c)
target_include_directories(fake_idf PUBLIC fakes ${FIRMWARE_DIR})
target_link_libraries(fake_idf PUBLIC unity m)

enable_testing()

# add_firmware_test(<name> <firmware source>...) builds <name>.c with the
# given sources from main/ and the fakes, and adds it to CTest.
function(add_firmware_test name)
    list(TRANSFORM ARGN PREPEND ${FIRMWARE_DIR}/ OUTPUT_VARIABLE sources)
    add_executable(${name} ${name}.c ${sources})
    target_link_libraries(${name} PRIVATE fake_idf)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_firmware_test(test_config  config.c)
add_firmware_test(test_digipot digipot.c)
add_firmware_test(test_fft     fft.c)

# These tests #include the .c file they test, to reach its static state, so
# it isn't listed here.
add_firmware_test(test_battery)
add_firmware_test(test_leds    config.c)
add_firmware_test(test_mic     config.c digipot.c)
add_firmware_test(test_main    config.c digipot.c fft.c leds.c)
