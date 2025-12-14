#!/bin/bash

# Colors
GREEN='\033[32m'
RED='\033[31m'
YELLOW='\033[33m'
BLUE='\033[34m'
RESET='\033[0m'

echo -e "${BLUE}========================================${RESET}"
echo -e "${BLUE}Webserv Test Suite Runner${RESET}"
echo -e "${BLUE}========================================${RESET}"
echo ""

# Kill any existing webserv processes
echo -e "${YELLOW}Cleaning up any existing webserv processes...${RESET}"
pkill -9 webserv 2>/dev/null
sleep 1

# Build webserv
echo -e "${YELLOW}Building webserv...${RESET}"
make -s > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${RESET}"
    exit 1
fi
echo -e "${GREEN}Build successful${RESET}"
echo ""

# Start webserv in background
echo -e "${YELLOW}Starting webserv on port 1234...${RESET}"
./webserv config/testing/official_tester.conf > webserv_test.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
echo -e "${YELLOW}Waiting for server to initialize...${RESET}"
sleep 3

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start! Check webserv_test.log for details${RESET}"
    cat webserv_test.log
    exit 1
fi
echo -e "${GREEN}Server started (PID: $SERVER_PID)${RESET}"
echo ""

# Run tests
echo -e "${YELLOW}Running test suite...${RESET}"
echo ""
python3 test_suite.py
TEST_EXIT_CODE=$?

# Stop server
echo ""
echo -e "${YELLOW}Stopping server...${RESET}"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
echo -e "${GREEN}Server stopped${RESET}"

# Show server log summary if tests failed
if [ $TEST_EXIT_CODE -ne 0 ]; then
    echo ""
    echo -e "${YELLOW}=== Server log (errors only) ===${RESET}"
    grep -E "ERROR|FAIL|error" webserv_test.log | tail -20
fi

echo ""
echo -e "${BLUE}========================================${RESET}"
echo -e "${BLUE}Test run completed${RESET}"
echo -e "${BLUE}Full server logs available in: webserv_test.log${RESET}"
echo -e "${BLUE}========================================${RESET}"

exit $TEST_EXIT_CODE
