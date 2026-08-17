#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "Applying patches..."

for patch in "$SCRIPT_DIR"/*.patch; do
    if [ -f "$patch" ]; then
        filename=$(basename "$patch")
        if [[ "$filename" == *"mininetsockets"* ]]; then
            echo "Applying to mininetsockets: $filename"
            git -C "$ROOT_DIR/external/mininetsockets" am "$patch"
        elif [[ "$filename" == *"miniruntime"* ]]; then
            echo "Applying to miniruntime: $filename"
            git -C "$ROOT_DIR/external/miniruntime" am "$patch"
        fi
    fi
done

echo "Done."