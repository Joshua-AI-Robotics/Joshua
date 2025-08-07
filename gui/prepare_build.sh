#!/bin/bash

# Generate MOC files for Qt6 classes
echo "Generating MOC files for Qt6..."

# Check if moc is available
if ! command -v moc &> /dev/null; then
    echo "Error: moc not found. Please install Qt6 development tools:"
    echo "sudo apt install qt6-base-dev qt6-base-dev-tools"
    exit 1
fi

# Generate MOC files
moc main_window.h -o main_window.moc
moc control_panel.h -o control_panel.moc
moc status_panel.h -o status_panel.moc

echo "MOC files generated successfully!" 