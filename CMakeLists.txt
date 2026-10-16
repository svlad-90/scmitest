# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(scmitest LANGUAGES CXX)

include(GNUInstallDirs)
add_executable(scmitest src/scmitest.cpp)
set_target_properties(scmitest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_options(scmitest PRIVATE -Wall -Wextra -Werror)
install(TARGETS scmitest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

option(BUILD_TESTING "Enable tests (requires Python 3.10 or newer)" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME scmitest-cli
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scmitest.py"
                "$<TARGET_FILE:scmitest>"
    )
    set_tests_properties(scmitest-cli PROPERTIES TIMEOUT 60)
endif()
