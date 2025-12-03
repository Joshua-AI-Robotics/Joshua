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

# Copy entire project (needed for proto files)
COPY . /app/

# Build the application (this will run generate-schema which needs proto files)
WORKDIR /app/ui
RUN npm run build

# Stage 2: Production
FROM nginx:alpine

# Copy built files from builder stage
COPY --from=builder /app/ui/dist /usr/share/nginx/html

# Copy nginx configuration
COPY ui/nginx.conf /etc/nginx/conf.d/default.conf

# Expose port
EXPOSE 80

# Start nginx
CMD ["nginx", "-g", "daemon off;"]

