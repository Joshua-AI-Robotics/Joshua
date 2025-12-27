#!/bin/sh
# Custom entrypoint for nginx that extracts host:port from ZENOH_BRIDGE_URL
# This is needed because nginx's proxy_pass with variables expects just host:port,
# not the full URL with protocol (http://host:port)

# Extract host:port from ZENOH_BRIDGE_URL if it contains a protocol
if [ -n "$ZENOH_BRIDGE_URL" ]; then
    # Remove http:// or https:// prefix if present
    ZENOH_BRIDGE_HOST_PORT=$(echo "$ZENOH_BRIDGE_URL" | sed 's|^https\?://||')
    export ZENOH_BRIDGE_HOST_PORT
else
    # Default fallback
    export ZENOH_BRIDGE_HOST_PORT="host.docker.internal:8000"
fi

# Call the default nginx entrypoint
exec /docker-entrypoint.sh "$@"

