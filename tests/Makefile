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
###############################################################################
# Makefile — NetAcuity C++ API Test Suite
#
# Requirements (Debian/Ubuntu):
#   sudo apt-get install libgtest-dev libxml2-dev
#
# Requirements (RHEL/CentOS/Fedora):
#   sudo yum install gtest-devel libxml2-devel
#
# Build and run all tests:
#   make
#
# Run a specific test group:
#   ./run_tests --gtest_filter=XMLResponse*
#
# Clean:
#   make clean
#
# Notes:
#   • Integration tests that use MockNaServer bind to an OS-assigned ephemeral
#     port, so they do not require port 5400 to be free.
###############################################################################

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -I../include
LDFLAGS  := -lgtest -lgtest_main -lpthread $(shell xml2-config --libs)
XML_CFLAGS := $(shell xml2-config --cflags)

# Source files under the parent src/ directory (the API library)
API_SRCS := \
    ../src/XMLResponse.cpp

# Test source files
TEST_SRCS := \
    test_xml_response.cpp

API_OBJS  := $(API_SRCS:.cpp=.o)
TEST_OBJS := $(TEST_SRCS:.cpp=.o)

TARGET := run_tests

.PHONY: all clean run

all: $(TARGET)
	@echo ""
	@echo "===== Running tests ====="
	./$(TARGET) --gtest_color=yes

run: $(TARGET)
	./$(TARGET) --gtest_color=yes

$(TARGET): $(API_OBJS) $(TEST_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Compile API source files (../src/ directory)
../src/%.o: ../src/%.cpp
	$(CXX) $(CXXFLAGS) $(XML_CFLAGS) -c $< -o $@

# Compile test source files (this directory)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(XML_CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(TEST_OBJS) $(API_OBJS)
