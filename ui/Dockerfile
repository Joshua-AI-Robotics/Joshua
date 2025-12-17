# Multi-stage build for React UI
# Build context should be the project root (not just ui/)
# This allows access to proto files in config/, robot/, ai/ directories

# Stage 1: Build
FROM node:20-alpine AS builder

WORKDIR /app/ui

# Copy package files from ui directory
COPY ui/package.json ui/package-lock.json* ./

# Install dependencies
RUN npm ci

# Copy entire project (needed for proto files in config/, robot/, ai/ directories)
COPY . /app/

# Build the application
# The build script automatically runs generate-schema.js to dynamically generate
# proto-schema.json from proto files before building the React app
WORKDIR /app/ui
RUN npm run build

# Stage 2: Production
FROM nginx:alpine

# Copy built files from builder stage
COPY --from=builder /app/ui/dist /usr/share/nginx/html

# Copy nginx configuration template
# nginx:alpine's entrypoint automatically processes .template files in /etc/nginx/templates/
# using envsubst for environment variable substitution
COPY ui/nginx.conf /etc/nginx/templates/default.conf.template

# Expose port
EXPOSE 80

# Use default nginx entrypoint which processes templates automatically
# No need to override CMD - default entrypoint handles template processing

