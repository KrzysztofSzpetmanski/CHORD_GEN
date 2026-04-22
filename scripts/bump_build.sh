#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_FILE="$ROOT_DIR/BUILD_NUMBER.txt"
HEADER_FILE="$ROOT_DIR/src/BuildNumber.hpp"
PLUGIN_JSON="$ROOT_DIR/plugin.json"

if [[ ! -f "$BUILD_FILE" ]]; then
	printf '100\n' > "$BUILD_FILE"
fi

current="$(tr -cd '0-9' < "$BUILD_FILE")"
if [[ -z "$current" ]]; then
	current=100
fi

next=$((current + 1))
printf '%s\n' "$next" > "$BUILD_FILE"
next_padded="$(printf '%03d' "$next")"
next_version="2.0.${next_padded}"

if [[ -f "$PLUGIN_JSON" ]]; then
	perl -0777 -i -pe "s/\"version\"\\s*:\\s*\"[^\"]+\"/\"version\": \"${next_version}\"/" "$PLUGIN_JSON"
fi

cat > "$HEADER_FILE" <<EOF2
#pragma once

constexpr int kChordGenBuildNumber = $next;
EOF2

printf 'CHORD_GEN build -> %s (%s)\n' "$next" "$next_version"
