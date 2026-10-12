#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Renders docs/assets/social-preview.svg to the 1280x640 PNG used as the GitHub social preview.
# Needs rsvg-convert (librsvg). The text uses Arial or Helvetica when installed and falls back to
# DejaVu Sans, so the PNG can differ slightly between machines; commit the PNG you checked.
# Upload by hand: repository Settings > General > Social preview > Edit > Upload an image.
set -eu
cd "$(dirname "$0")"
command -v rsvg-convert >/dev/null 2>&1 || { echo "rsvg-convert (librsvg) is required" >&2; exit 1; }
rsvg-convert --width 1280 --height 640 --background-color white social-preview.svg -o social-preview.png
echo "wrote docs/assets/social-preview.png ($(wc -c < social-preview.png | tr -d ' ') bytes)"
