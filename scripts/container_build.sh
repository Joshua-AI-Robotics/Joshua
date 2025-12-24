#!/bin/bash
# This script is used by the build.py script to build the target in the container.

set -e

# Get the target OS, architecture, and label from the command line arguments
TARGET_OS="$1"
TARGET_ARCH="$2"
TARGET_LABEL="$3"
shift 3
BAZEL_FLAGS="$@"

# Increase Bazel's HTTP timeout for slow connections or QEMU emulation
export BAZEL_HTTP_TIMEOUT_SCALING=5.0

echo "🔨 Building $TARGET_LABEL for $TARGET_OS/$TARGET_ARCH..."
echo "   Flags: $BAZEL_FLAGS"

# 1. Build the target
bazel build $BAZEL_FLAGS "$TARGET_LABEL"

# 2. Find output artifacts using cquery
echo "🔍 Locating output artifacts..."
# We use starlark output to get files that are actual build products, not runfiles
OUTPUTS=$(bazel cquery "$TARGET_LABEL" $BAZEL_FLAGS --output=files 2>/dev/null)

if [ -z "$OUTPUTS" ]; then
    echo "❌ Error: No output files found for $TARGET_LABEL"
    exit 1
fi

DEST_DIR="/workspace/dist/$TARGET_OS/$TARGET_ARCH"
mkdir -p "$DEST_DIR"

# 3. Copy artifacts
# Convert newline-separated list to array to handle multiple outputs
mapfile -t FILE_LIST <<< "$OUTPUTS"

FOUND_ANY=false
for FILE_PATH in "${FILE_LIST[@]}"; do
    if [ -f "$FILE_PATH" ]; then
        BASENAME=$(basename "$FILE_PATH")
        
        # Filter out obvious non-final artifacts if needed (optional)
        # For now, we copy everything produced by the target
        
        echo "📦 Copying $BASENAME..."
        cp -f "$FILE_PATH" "$DEST_DIR/$BASENAME"
        
        # Fix permissions (match the mounted workspace owner, usually 1000:1000)
        # We assume the /workspace folder ownership is the target UID/GID
        REF_FILE="/workspace/scripts/build.py"
        if [ -f "$REF_FILE" ]; then
            chown --reference="$REF_FILE" "$DEST_DIR/$BASENAME"
        fi
        
        FOUND_ANY=true
    fi
done

if [ "$FOUND_ANY" = true ]; then
    echo "✅ Artifacts saved to dist/$TARGET_OS/$TARGET_ARCH/"
else
    echo "⚠️  Targets were built, but no file artifacts were found to copy."
fi

