#!/bin/bash

# Dockerfile validation script
set -e

echo "Testing Dockerfile structure..."

# Check if required files exist
echo "Checking required files..."
files=("package.json" "nginx.conf" "vite.config.ts" "index.html")
for file in "${files[@]}"; do
    if [ -f "$file" ]; then
        echo "✓ $file exists"
    else
        echo "✗ $file missing"
        exit 1
    fi
done

# Check Dockerfile syntax
echo ""
echo "Checking Dockerfile syntax..."

# Check for common issues in production Dockerfile
if grep -q "FROM node:20-alpine AS builder" Dockerfile; then
    echo "✓ Multi-stage build detected"
else
    echo "✗ Multi-stage build not found"
    exit 1
fi

if grep -q "COPY --from=builder" Dockerfile; then
    echo "✓ Builder stage copy found"
else
    echo "✗ Builder stage copy missing"
    exit 1
fi

if grep -q "nginx:alpine" Dockerfile; then
    echo "✓ Nginx base image found"
else
    echo "✗ Nginx base image missing"
    exit 1
fi

# Check dev Dockerfile
echo ""
echo "Checking Dockerfile.dev syntax..."

if grep -q "FROM node:20-alpine" Dockerfile.dev; then
    echo "✓ Node base image found"
else
    echo "✗ Node base image missing"
    exit 1
fi

if grep -q "EXPOSE 3000" Dockerfile.dev; then
    echo "✓ Port 3000 exposed"
else
    echo "✗ Port 3000 not exposed"
    exit 1
fi

# Check if vite config has host configured (better than Dockerfile flag)
if grep -q "host.*0.0.0.0" vite.config.ts; then
    echo "✓ Host configured in vite.config.ts for Docker"
else
    echo "⚠ Host might not be configured for Docker (check vite.config.ts)"
fi

echo ""
echo "Dockerfile structure validation complete!"
echo ""
echo "Note: To actually build and test, you need Docker installed:"
echo "  sudo apt install docker.io"
echo "  OR"
echo "  sudo snap install docker"
echo ""
echo "Then run:"
echo "  docker build -t joshua-ui -f Dockerfile ."
echo "  docker build -t joshua-ui-dev -f Dockerfile.dev ."

