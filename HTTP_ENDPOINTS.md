# HTTP Endpoints Documentation

This document details all HTTP endpoints available in the Card Reader Web Server.

## Authentication

All endpoints require HTTP Basic Authentication using the credentials defined in `secret.h`:
- Username: `www_username`
- Password: `www_password`
- Realm: "MyApp"

**Note**: Replace `username:password` in the CURL examples below with your actual credentials.


## Card Management

### Add Card
- **PUT** `/card`
  - **Description**: Add a new card to the database
  - **Parameters**:
    - `number` (required): Card number (integer)
  - **Response**:
    - `200`: Card number on success
    - `400`: "Missing card number parameter" or "Invalid card number"
    - `500`: "Failed to add card"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -X PUT -u username:password "http://device-ip/card?number=12345"
    ```

### Remove Card
- **DELETE** `/card`
  - **Description**: Remove a card from the database
  - **Parameters**:
    - `number` (required): Card number (integer)
  - **Response**:
    - `200`: Card number on success
    - `400`: "Missing card number parameter" or "Invalid card number"
    - `500`: "Failed to remove card"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -X DELETE -u username:password "http://device-ip/card?number=12345"
    ```

### List Cards
- **GET** `/cards`
  - **Description**: Get all cards in the database
  - **Response**: `200` - Plain text list of card numbers (one per line)
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password http://device-ip/cards
    ```

### Card Options
- **OPTIONS** `/card`
  - **Description**: CORS preflight request
  - **Response**: `200` - Empty response
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -X OPTIONS -u username:password http://device-ip/card
    ```

## Door Strike Diagnostics

### Get Strike Status
- **GET** `/diagnostics/strike/status`
  - **Description**: Get electrical status of a door strike
  - **Parameters**:
    - `number` (required): Strike number (integer)
  - **Response**:
    - `200`: "Good Electrical Connection" or "Bad connection or burnt out"
    - `400`: "Missing strike number parameter" or "Invalid strike number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/status?number=0"
    ```

### Get Strike Current
- **GET** `/diagnostics/strike/current`
  - **Description**: Get current draw of a door strike
  - **Parameters**:
    - `number` (required): Strike number (integer)
  - **Response**:
    - `200`: Current value as string
    - `400`: "Missing strike number parameter" or "Invalid strike number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/current?number=0"
    ```

### Check Strike Connection
- **GET** `/diagnostics/strike/connected`
  - **Description**: Check if a door strike is properly connected
  - **Parameters**:
    - `number` (required): Strike number (integer)
  - **Response**:
    - `200`: "true" or "false"
    - `400`: "Missing strike number parameter" or "Invalid strike number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/connected?number=0"
    ```

### Actuate Strike
- **PUT** `/diagnostics/strike/actuate`
  - **Description**: Manually actuate a door strike for 5 seconds
  - **Parameters**:
    - `number` (required): Strike number (integer)
  - **Response**:
    - `200`: "OK"
    - `400`: "Missing strike number parameter" or "Invalid strike number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -X PUT -u username:password "http://device-ip/diagnostics/strike/actuate?number=0"
    ```

### List Strikes
- **GET** `/diagnostics/strikes`
  - **Description**: Get list of available strikes
  - **Response**: `200` - Plain text list of strike numbers (one per line)
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password http://device-ip/diagnostics/strikes
    ```

## Card Reader Diagnostics

### Get Reader Current
- **GET** `/diagnostics/cardreader/current`
  - **Description**: Get current draw of a card reader
  - **Parameters**:
    - `number` (required): Reader number (integer)
  - **Response**:
    - `200`: Current value as string
    - `400`: "Missing reader number parameter" or "Invalid reader number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/current?number=0"
    ```

### Check Reader Fuse
- **GET** `/diagnostics/cardreader/fuse`
  - **Description**: Check if a card reader's fuse is good
  - **Parameters**:
    - `number` (required): Reader number (integer)
  - **Response**:
    - `200`: "true" or "false"
    - `400`: "Missing reader number parameter" or "Invalid reader number"
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/fuse?number=0"
    ```

### List Card Readers
- **GET** `/diagnostics/cardreader/list`
  - **Description**: Get list of available card readers
  - **Response**: `200` - Plain text list of reader numbers (one per line)
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password http://device-ip/diagnostics/cardreader/list
    ```

### Get Wiegand Burst Data
- **GET** `/diagnostics/cardreader/wiegand/burst`
  - **Description**: Get the last Wiegand burst data from a card reader
  - **Parameters**:
    - `number` (required): Reader number (integer)
  - **Response**:
    - `200`: JSON object containing burst data
    - `400`: "Missing reader number parameter" or "Invalid reader number"
  - **Headers**: `Access-Control-Allow-Origin: *`
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/wiegand/burst?number=0"
    ```

## Access Log

### Get Access Log
- **GET** `/access`
  - **Description**: Get the contents of the access log
  - **Response**: `200` - Plain text log contents
  - **Authentication**: Required
  - **CURL Example**:
    ```bash
    curl -u username:password http://device-ip/access
    ```

## URL Rewrites

The server includes several URL rewrites for RESTful-style endpoints:

### Card Management
- `/card/{number}` → `/card?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/card/12345"
    ```

### Strike Diagnostics
- `/diagnostics/strike/{number}/status` → `/diagnostics/strike/status?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/0/status"
    ```
- `/diagnostics/strike/{number}/current` → `/diagnostics/strike/current?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/0/current"
    ```
- `/diagnostics/strike/{number}/connected` → `/diagnostics/strike/connected?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/strike/0/connected"
    ```
- `/diagnostics/strike/{number}/actuate` → `/diagnostics/strike/actuate?number={number}`
  - **CURL Example**:
    ```bash
    curl -X PUT -u username:password "http://device-ip/diagnostics/strike/0/actuate"
    ```

### Card Reader Diagnostics
- `/diagnostics/cardreader/{number}/current` → `/diagnostics/cardreader/current?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/0/current"
    ```
- `/diagnostics/cardreader/{number}/fuse` → `/diagnostics/cardreader/fuse?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/0/fuse"
    ```
- `/diagnostics/cardreader/{number}/wiegand/burst` → `/diagnostics/cardreader/wiegand/burst?number={number}`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/0/wiegand/burst"
    ```

### Backward Compatibility
- `/diagnostics/cardreader/current` → `/diagnostics/cardreader/current?number=0`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/current"
    ```
- `/diagnostics/cardreader/fuse` → `/diagnostics/cardreader/fuse?number=0`
  - **CURL Example**:
    ```bash
    curl -u username:password "http://device-ip/diagnostics/cardreader/fuse"
    ```

## Error Responses

All endpoints return appropriate HTTP status codes:
- `200`: Success
- `400`: Bad Request (missing or invalid parameters)
- `500`: Internal Server Error (database or system failures)

## Notes

- All endpoints require HTTP Basic Authentication
- Numeric parameters are validated to ensure they're within valid ranges
- The server runs on port 80
- CORS headers are added for the Wiegand burst endpoint
- Static files are served from LittleFS filesystem

## Quick Reference Examples

### Basic Authentication
```bash
# Replace with your actual credentials
curl -u username:password http://device-ip/
```

### Card Management
```bash
# Add card
curl -X PUT -u username:password "http://device-ip/card?number=12345"

# Remove card
curl -X DELETE -u username:password "http://device-ip/card?number=12345"

# List all cards
curl -u username:password http://device-ip/cards
```

### Diagnostics
```bash
# Check strike 0 status
curl -u username:password "http://device-ip/diagnostics/strike/0/status"

# Actuate strike 0
curl -X PUT -u username:password "http://device-ip/diagnostics/strike/0/actuate"

# Check reader 0 fuse
curl -u username:password "http://device-ip/diagnostics/cardreader/0/fuse"

# Get access log
curl -u username:password http://device-ip/access
``` 