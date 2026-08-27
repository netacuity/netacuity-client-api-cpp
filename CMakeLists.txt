# Copyright 2026 Digital Envoy, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.14)

# Must be set before project() so the vcpkg toolchain file (invoked by project())
# only installs gtest when the test suite is actually going to be built.
option(BUILD_TESTS "Build the test suite" ON)
if(BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(netacuity-client-api-cpp VERSION 7.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Library ─────────────────────────────────────────────────────────────────

file(GLOB API_SOURCES "src/*.cpp")

add_library(netacuity-client-api ${API_SOURCES})

target_include_directories(netacuity-client-api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# libxml2 is required for XMLResponse
find_package(LibXml2 REQUIRED)
target_link_libraries(netacuity-client-api PUBLIC LibXml2::LibXml2)

if(WIN32)
    target_link_libraries(netacuity-client-api PUBLIC ws2_32)
endif()

# ── Examples ────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    foreach(example testXMLAPI)
        add_executable(${example} examples/${example}.cpp)
        target_link_libraries(${example} PRIVATE netacuity-client-api)
    endforeach()
endif()

# ── Tests ───────────────────────────────────────────────────────────────────

if(BUILD_TESTS)
    enable_testing()

    find_package(GTest REQUIRED)

    file(GLOB TEST_SOURCES "tests/test_*.cpp")

    add_executable(run_tests ${TEST_SOURCES})
    target_include_directories(run_tests PRIVATE tests)
    target_link_libraries(run_tests PRIVATE netacuity-client-api GTest::GTest GTest::Main)

    include(GoogleTest)
    gtest_discover_tests(run_tests)
endif()

# ── Install ──────────────────────────────────────────────────────────────────

install(TARGETS netacuity-client-api
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
