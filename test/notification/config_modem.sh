#!/bin/bash

# Function to display usage
usage() {
  echo "Usage: $0 <tty_port>"
  echo "  <tty_port> : The name of the TTY port (e.g., ttyUSB0, ttyS0)"
  exit 1
}

# Check if no arguments are provided
if [ -z "$1" ]; then
  echo "Error: No TTY port specified."
  usage
fi

TTY_PORT="$1"
DEVICE_PATH="/dev/$TTY_PORT"

# Verify if the device exists and is a character device (tty)
if [ ! -e "$DEVICE_PATH" ]; then
  echo "Error: Device '$DEVICE_PATH' does not exist."
  echo "Please ensure the TTY port is correct and the device is connected."
  exit 1
elif [ ! -c "$DEVICE_PATH" ]; then
  echo "Error: '$DEVICE_PATH' is not a character device (TTY port)."
  echo "Please provide a valid TTY port name (e.g., ttyUSB0, ttyS0)."
  exit 1
fi

sudo systemctl stop ModemManager.service

# do the configuration
cat telekom.cmd | socat - "$DEVICE_PATH,crnl"
