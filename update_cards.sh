#!/bin/bash

# Card Database Update Script
# This script reads card numbers from a text file and adds them to the card database
# via HTTP API calls using CURL

# Configuration - all values must be provided via command line arguments
DEVICE_IP=""
USERNAME=""
PASSWORD=""
CARD_FILE=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to add a single card
add_card() {
    local card_number=$1
    local response=$(curl -s -w "%{http_code}" -X PUT \
        -u "${USERNAME}:${PASSWORD}" \
        "http://${DEVICE_IP}/card?number=${card_number}")
    
    local http_code="${response: -3}"
    local response_body="${response%???}"
    
    if [ "$http_code" -eq 200 ]; then
        print_status $GREEN "✓ Added card: ${card_number}"
        return 0
    else
        print_status $RED "✗ Failed to add card: ${card_number} (HTTP ${http_code})"
        print_status $YELLOW "Response: ${response_body}"
        return 1
    fi
}

# Function to remove a single card
remove_card() {
    local card_number=$1
    local response=$(curl -s -w "%{http_code}" -X DELETE \
        -u "${USERNAME}:${PASSWORD}" \
        "http://${DEVICE_IP}/card?number=${card_number}")
    
    local http_code="${response: -3}"
    local response_body="${response%???}"
    
    if [ "$http_code" -eq 200 ]; then
        print_status $GREEN "✓ Removed card: ${card_number}"
        return 0
    else
        print_status $RED "✗ Failed to remove card: ${card_number} (HTTP ${http_code})"
        print_status $YELLOW "Response: ${response_body}"
        return 1
    fi
}

# Function to list all cards
list_cards() {
    print_status $YELLOW "Fetching current card list..."
    local response=$(curl -s -w "%{http_code}" \
        -u "${USERNAME}:${PASSWORD}" \
        "http://${DEVICE_IP}/cards")
    
    local http_code="${response: -3}"
    local response_body="${response%???}"
    
    if [ "$http_code" -eq 200 ]; then
        print_status $GREEN "Current cards in database:"
        echo "$response_body"
    else
        print_status $RED "Failed to fetch card list (HTTP ${http_code})"
        print_status $YELLOW "Response: ${response_body}"
        return 1
    fi
}

# Function to clear all cards (remove each one)
clear_all_cards() {
    print_status $YELLOW "Clearing all cards from database..."
    
    # Get current card list
    local response=$(curl -s -w "%{http_code}" \
        -u "${USERNAME}:${PASSWORD}" \
        "http://${DEVICE_IP}/cards")
    
    local http_code="${response: -3}"
    local response_body="${response%???}"
    
    if [ "$http_code" -ne 200 ]; then
        print_status $RED "Failed to fetch current card list (HTTP ${http_code})"
        return 1
    fi
    
    local removed_count=0
    local failed_count=0
    
    # Remove each card
    while IFS= read -r card_number; do
        if [ -n "$card_number" ]; then
            if remove_card "$card_number"; then
                ((removed_count++))
            else
                ((failed_count++))
            fi
        fi
    done <<< "$response_body"
    
    print_status $GREEN "Cleared ${removed_count} cards"
    if [ $failed_count -gt 0 ]; then
        print_status $RED "Failed to remove ${failed_count} cards"
    fi
}

# Function to update cards from file
update_cards_from_file() {
    local file_path=$1
    local action=$2  # "add" or "replace"
    
    if [ ! -f "$file_path" ]; then
        print_status $RED "Error: Card file '${file_path}' not found!"
        exit 1
    fi
    
    print_status $YELLOW "Reading cards from: ${file_path}"
    
    # Count lines in file
    local total_cards=$(wc -l < "$file_path")
    print_status $YELLOW "Found ${total_cards} cards in file"
    
    # If replacing, clear all existing cards first
    if [ "$action" = "replace" ]; then
        clear_all_cards
    fi
    
    local added_count=0
    local failed_count=0
    local line_number=0
    
    # Process each line in the file
    while IFS= read -r card_number; do
        ((line_number++))
        
        # Skip empty lines
        if [ -z "$card_number" ]; then
            continue
        fi
        
        # Remove any whitespace
        card_number=$(echo "$card_number" | tr -d '[:space:]')
        
        # Validate card number (should be numeric)
        if ! [[ "$card_number" =~ ^[0-9]+$ ]]; then
            print_status $RED "✗ Invalid card number on line ${line_number}: '${card_number}'"
            ((failed_count++))
            continue
        fi
        
        print_status $YELLOW "Processing card ${line_number}/${total_cards}: ${card_number}"
        
        if add_card "$card_number"; then
            ((added_count++))
        else
            ((failed_count++))
        fi
        
        # Small delay to avoid overwhelming the server
        sleep 0.1
        
    done < "$file_path"
    
    print_status $GREEN "Update complete!"
    print_status $GREEN "Successfully added: ${added_count} cards"
    if [ $failed_count -gt 0 ]; then
        print_status $RED "Failed to add: ${failed_count} cards"
    fi
}

# Function to show usage
show_usage() {
    echo "Card Database Update Script"
    echo ""
    echo "Usage: $0 [REQUIRED_OPTIONS] [ACTION]"
    echo ""
    echo "Required Options:"
    echo "  -i, --ip IP         Device IP address"
    echo "  -u, --user USER     Username"
    echo "  -p, --pass PASS     Password"
    echo ""
    echo "Actions (choose one):"
    echo "  -a, --add           Add cards to existing database"
    echo "  -r, --replace       Replace all cards with file contents"
    echo "  -l, --list          List all current cards"
    echo "  -c, --clear         Clear all cards from database"
    echo ""
    echo "Additional Options:"
    echo "  -f, --file FILE     Card list file (required for add/replace actions)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 -i 192.168.1.22 -u admin -p password -f mycards.txt -a"
    echo "  $0 -i 192.168.1.22 -u admin -p password -f mycards.txt -r"
    echo "  $0 -i 192.168.1.22 -u admin -p password -l"
    echo "  $0 -i 192.168.1.22 -u admin -p password -c"
    echo ""
    echo "Card file format:"
    echo "  One card number per line, e.g.:"
    echo "  12345"
    echo "  67890"
    echo "  11111"
}

# Parse command line arguments
ACTION=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--file)
            CARD_FILE="$2"
            shift 2
            ;;
        -i|--ip)
            DEVICE_IP="$2"
            shift 2
            ;;
        -u|--user)
            USERNAME="$2"
            shift 2
            ;;
        -p|--pass)
            PASSWORD="$2"
            shift 2
            ;;
        -a|--add)
            ACTION="add"
            shift
            ;;
        -r|--replace)
            ACTION="replace"
            shift
            ;;
        -l|--list)
            ACTION="list"
            shift
            ;;
        -c|--clear)
            ACTION="clear"
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            print_status $RED "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Validate required arguments
if [ -z "$DEVICE_IP" ]; then
    print_status $RED "Error: Device IP address is required (-i or --ip)"
    show_usage
    exit 1
fi

if [ -z "$USERNAME" ]; then
    print_status $RED "Error: Username is required (-u or --user)"
    show_usage
    exit 1
fi

if [ -z "$PASSWORD" ]; then
    print_status $RED "Error: Password is required (-p or --pass)"
    show_usage
    exit 1
fi

# Validate action-specific requirements
if [ "$ACTION" = "add" ] || [ "$ACTION" = "replace" ]; then
    if [ -z "$CARD_FILE" ]; then
        print_status $RED "Error: Card file is required for add/replace actions (-f or --file)"
        show_usage
        exit 1
    fi
fi

if [ -z "$ACTION" ]; then
    print_status $RED "Error: Action is required (-a, -r, -l, or -c)"
    show_usage
    exit 1
fi

# Main execution
print_status $YELLOW "Card Database Update Script"
print_status $YELLOW "Device: ${DEVICE_IP}"
print_status $YELLOW "User: ${USERNAME}"

case $ACTION in
    "add")
        update_cards_from_file "$CARD_FILE" "add"
        ;;
    "replace")
        update_cards_from_file "$CARD_FILE" "replace"
        ;;
    "list")
        list_cards
        ;;
    "clear")
        clear_all_cards
        ;;
    *)
        print_status $RED "Unknown action: $ACTION"
        exit 1
        ;;
esac

print_status $GREEN "Script completed!" 