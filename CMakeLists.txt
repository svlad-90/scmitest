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
