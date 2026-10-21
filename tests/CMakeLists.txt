# SPDX-FileCopyrightText: 2026 hyprtacle contributors
#
# SPDX-License-Identifier: BSD-3-Clause

add_executable(tst_capture tst_capture.cpp ../src/HyprlandInfo.cpp ../src/Logging.cpp)

target_include_directories(tst_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(tst_capture PRIVATE Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME capture_test COMMAND tst_capture)
