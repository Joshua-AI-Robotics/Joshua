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

# Process nginx template with envsubst BEFORE calling nginx entrypoint
# This ensures ZENOH_BRIDGE_HOST_PORT is available when the template is processed
if [ -f /etc/nginx/templates/default.conf.template ]; then
    # Substitute only the variable we need (ZENOH_BRIDGE_HOST_PORT)
    # envsubst will replace ${ZENOH_BRIDGE_HOST_PORT} in the template
    envsubst '${ZENOH_BRIDGE_HOST_PORT}' < /etc/nginx/templates/default.conf.template > /etc/nginx/conf.d/default.conf
    # Remove the template so nginx entrypoint doesn't try to process it again
    rm -f /etc/nginx/templates/default.conf.template
fi

# Call the default nginx entrypoint
# Since we've already processed the template and removed it, nginx entrypoint won't process it again
exec /docker-entrypoint.sh "$@"

