#!/bin/bash

# Base URL and auth
BASE_URL="http://192.168.1.89"  # Change this to your ESP32's IP
AUTH="admin:esp32"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "Testing Door Strike API Endpoints"
echo "================================="

# Function to print response with a header
print_response() {
    local header=$1
    local response=$2
    echo -e "\n${BLUE}$header${NC}"
    echo "----------------------------------------"
    echo "$response"
    echo "----------------------------------------"
}

# Test GET /diagnostics/strike/0
echo -e "\n${GREEN}Testing GET /diagnostics/strike/0${NC}"
response=$(curl -s -u $AUTH "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

# Test GET /diagnostics/strike/1
echo -e "\n${GREEN}Testing GET /diagnostics/strike/1${NC}"
response=$(curl -s -u $AUTH "$BASE_URL/diagnostics/strike/1")
print_response "Response:" "$response"

# Test GET /diagnostics/strikes (list all strikes)
echo -e "\n${GREEN}Testing GET /diagnostics/strikes${NC}"
response=$(curl -s -u $AUTH "$BASE_URL/diagnostics/strikes")
print_response "Response:" "$response"

# Test PUT /diagnostics/strike/0 (engage strike)
echo -e "\n${GREEN}Testing PUT /diagnostics/strike/0 (engage)${NC}"
response=$(curl -s -X PUT -u $AUTH \
    -H "Content-Type: application/json" \
    -d '{"isEngaged": true}' \
    "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

# Wait 2 seconds
sleep 2

# Test PUT /diagnostics/strike/0 (disengage strike)
echo -e "\n${GREEN}Testing PUT /diagnostics/strike/0 (disengage)${NC}"
response=$(curl -s -X PUT -u $AUTH \
    -H "Content-Type: application/json" \
    -d '{"isEngaged": false}' \
    "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

# Test error cases
echo -e "\n${GREEN}Testing error cases:${NC}"

# Test invalid strike number
echo -e "\n${RED}Testing invalid strike number${NC}"
response=$(curl -s -u $AUTH "$BASE_URL/diagnostics/strike/99")
print_response "Response:" "$response"

# Test invalid JSON
echo -e "\n${RED}Testing invalid JSON${NC}"
response=$(curl -s -X PUT -u $AUTH \
    -H "Content-Type: application/json" \
    -d '{invalid json}' \
    "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

# Test missing authentication
echo -e "\n${RED}Testing missing authentication${NC}"
response=$(curl -s "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

# Test invalid authentication
echo -e "\n${RED}Testing invalid authentication${NC}"
response=$(curl -s -u "wrong:wrong" "$BASE_URL/diagnostics/strike/0")
print_response "Response:" "$response"

echo -e "\nTests completed!" 