#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="${1:?missing source dir}"
PRIMARY_MOUNT="${2:?missing primary mount}"
FALLBACK_MOUNT="${3:?missing fallback mount}"
RACK_SUBDIR="${4:?missing Rack subdir}"
PLUGIN_SLUG="${5:?missing plugin slug}"

is_mount_active() {
	local mount_dir="$1"
	mount | grep -F " on ${mount_dir} (" >/dev/null 2>&1
}

is_mount_usable() {
	local mount_dir="$1"
	[[ -d "$mount_dir" ]] || return 1
	[[ -x "$mount_dir" ]] || return 1
	[[ -w "$mount_dir" ]] || return 1
	ls -la "$mount_dir" >/dev/null 2>&1 || return 1
	is_mount_active "$mount_dir" || return 1
	return 0
}

target_mount="$PRIMARY_MOUNT"
if ! is_mount_usable "$target_mount"; then
	if is_mount_usable "$FALLBACK_MOUNT"; then
		echo "[deploy-big] using fallback mount: $FALLBACK_MOUNT"
		target_mount="$FALLBACK_MOUNT"
	else
		echo "[deploy-big] no usable SMB mount for big computer"
		echo "[deploy-big] tried: $PRIMARY_MOUNT and $FALLBACK_MOUNT"
		echo "[deploy-big] hint: mkdir -p \"$FALLBACK_MOUNT\" && mount_smbfs \"//<user>@<host>/music\" \"$FALLBACK_MOUNT\""
		exit 1
	fi
fi

deploy_dir="${target_mount}/${RACK_SUBDIR}/${PLUGIN_SLUG}"
echo "[deploy-big] rsync -> ${deploy_dir}"
mkdir -p "$deploy_dir"
rsync -av --delete "${SRC_DIR}/" "${deploy_dir}/"
