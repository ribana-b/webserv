# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/01/27 00:00:00 by mancorte          #+#    #+#              #
#    Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com       #
#                                                                              #
# **************************************************************************** #

# Test executable names
TEST_REQUEST := test_httprequest
TEST_RESPONSE := test_httpresponse
TEST_SERVER := test_httpserver
TEST_STATIC := test_static_files
DEMO := demo_http

# Paths
PARENT_DIR := ..
SRC_DIR := $(PARENT_DIR)/src
INCLUDE_DIR := $(PARENT_DIR)/include

# Source files needed for testing
REQUEST_SOURCES := test_httprequest.cpp \
				   $(SRC_DIR)/HttpRequest.cpp \
				   $(SRC_DIR)/Logger.cpp \
				   $(SRC_DIR)/colour.cpp

RESPONSE_SOURCES := test_httpresponse.cpp \
					$(SRC_DIR)/HttpResponse.cpp \
					$(SRC_DIR)/Logger.cpp \
					$(SRC_DIR)/colour.cpp

SERVER_SOURCES := test_httpserver.cpp \
				  $(SRC_DIR)/HttpServer.cpp \
				  $(SRC_DIR)/HttpRequest.cpp \
				  $(SRC_DIR)/HttpResponse.cpp \
				  $(SRC_DIR)/Config.cpp \
				  $(SRC_DIR)/Logger.cpp \
				  $(SRC_DIR)/colour.cpp

DEMO_SOURCES := demo_http.cpp \
				$(SRC_DIR)/HttpServer.cpp \
				$(SRC_DIR)/HttpRequest.cpp \
				$(SRC_DIR)/HttpResponse.cpp \
				$(SRC_DIR)/Config.cpp \
				$(SRC_DIR)/Logger.cpp \
				$(SRC_DIR)/colour.cpp

STATIC_SOURCES := test_static_files.cpp \
				  $(SRC_DIR)/HttpServer.cpp \
				  $(SRC_DIR)/HttpRequest.cpp \
				  $(SRC_DIR)/HttpResponse.cpp \
				  $(SRC_DIR)/Config.cpp \
				  $(SRC_DIR)/Logger.cpp \
				  $(SRC_DIR)/colour.cpp

# Compilation flags
CXX := clang++
CXXFLAGS := -Wall -Wextra -Werror -std=c++98 -pedantic
CPPFLAGS := -I$(INCLUDE_DIR)

# Colors for output
T_GREEN := \033[32m
T_RED := \033[31m
T_BLUE := \033[34m
RESET := \033[0m

.PHONY: all test test-request test-response test-server clean help

all: test

$(TEST_REQUEST): $(REQUEST_SOURCES)
	@echo "$(T_BLUE)🔨 Compiling HttpRequest test suite...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(TEST_REQUEST) $(REQUEST_SOURCES)
	@echo "$(T_GREEN)✅ HttpRequest test suite compiled!$(RESET)"

$(TEST_RESPONSE): $(RESPONSE_SOURCES)
	@echo "$(T_BLUE)🔨 Compiling HttpResponse test suite...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(TEST_RESPONSE) $(RESPONSE_SOURCES)
	@echo "$(T_GREEN)✅ HttpResponse test suite compiled!$(RESET)"

$(TEST_SERVER): $(SERVER_SOURCES)
	@echo "$(T_BLUE)🔨 Compiling HttpServer test suite...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(TEST_SERVER) $(SERVER_SOURCES)
	@echo "$(T_GREEN)✅ HttpServer test suite compiled!$(RESET)"

$(DEMO): $(DEMO_SOURCES)
	@echo "$(T_BLUE)🔨 Compiling HTTP demo...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(DEMO) $(DEMO_SOURCES)
	@echo "$(T_GREEN)✅ HTTP demo compiled!$(RESET)"

$(TEST_STATIC): $(STATIC_SOURCES)
	@echo "$(T_BLUE)🔨 Compiling static files test suite...$(RESET)"
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(TEST_STATIC) $(STATIC_SOURCES)
	@echo "$(T_GREEN)✅ Static files test suite compiled!$(RESET)"

test-request: $(TEST_REQUEST)
	@echo "$(T_BLUE)🧪 Running HttpRequest tests...$(RESET)"
	@./$(TEST_REQUEST)
	@echo ""

test-response: $(TEST_RESPONSE)
	@echo "$(T_BLUE)🧪 Running HttpResponse tests...$(RESET)"
	@./$(TEST_RESPONSE)
	@echo ""

test-server: $(TEST_SERVER)
	@echo "$(T_BLUE)🧪 Running HttpServer tests...$(RESET)"
	@./$(TEST_SERVER)
	@echo ""

test-static: $(TEST_STATIC)
	@echo "$(T_BLUE)🧪 Running static file tests...$(RESET)"
	@./$(TEST_STATIC)
	@echo ""

test: $(TEST_REQUEST) $(TEST_RESPONSE) $(TEST_SERVER)
	@echo "$(T_BLUE)🧪 Running all HTTP tests...$(RESET)"
	@echo "$(T_BLUE)--- HttpRequest Tests ---$(RESET)"
	@./$(TEST_REQUEST)
	@echo ""
	@echo "$(T_BLUE)--- HttpResponse Tests ---$(RESET)"
	@./$(TEST_RESPONSE)
	@echo ""
	@echo "$(T_BLUE)--- HttpServer Tests ---$(RESET)"
	@./$(TEST_SERVER)
	@echo ""

demo: $(DEMO)
	@echo "$(T_BLUE)🚀 Running HTTP demo...$(RESET)"
	@./$(DEMO)
	@echo ""

clean:
	@rm -f $(TEST_REQUEST) $(TEST_RESPONSE) $(TEST_SERVER) $(TEST_STATIC) $(DEMO)
	@echo "$(T_GREEN)🗑️  Test files cleaned$(RESET)"

help:
	@echo "HTTP Testing Suite"
	@echo "=================="
	@echo "Available targets:"
	@echo "  all            - Compile all test suites (default)"
	@echo "  test           - Compile and run all tests"
	@echo "  test-request   - Run only HttpRequest tests"
	@echo "  test-response  - Run only HttpResponse tests"
	@echo "  test-server    - Run only HttpServer tests"
	@echo "  clean          - Remove test executables"
	@echo "  help           - Show this help"
	@echo ""
	@echo "Usage examples:"
	@echo "  make test           # Run all tests"
	@echo "  make test-request   # Test only HttpRequest"
	@echo "  make test-response  # Test only HttpResponse"
	@echo "  make test-server    # Test only HttpServer"
	@echo "  make clean          # Clean up"