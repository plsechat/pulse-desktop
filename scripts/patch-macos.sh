#!/bin/bash
set -e

# Find the .app bundle
APP_DIR="$(dirname "$0")/../out"
APP_PATH=$(find "$APP_DIR" -maxdepth 2 -name "Pulse.app" -type d | head -1)

if [ -z "$APP_PATH" ]; then
  echo "ERROR: Pulse.app not found in out/"
  exit 1
fi

echo "Patching: $APP_PATH"

ENTRIES=(
  "NSMicrophoneUsageDescription:Pulse needs microphone access for voice chat."
  "NSCameraUsageDescription:Pulse needs camera access for video calls."
  "NSScreenCaptureUsageDescription:Pulse needs screen capture access for screen sharing."
)

# Patch ALL Info.plist files in the app bundle
find "$APP_PATH" -name "Info.plist" | while read -r PLIST; do
  echo "  Patching: $PLIST"
  for entry in "${ENTRIES[@]}"; do
    KEY="${entry%%:*}"
    VALUE="${entry#*:}"
    /usr/libexec/PlistBuddy -c "Add :$KEY string '$VALUE'" "$PLIST" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Set :$KEY '$VALUE'" "$PLIST"
  done
done

# Re-sign the entire app bundle
echo "Re-signing app..."
codesign --force --deep --sign - "$APP_PATH"
echo "Done. Verifying..."
codesign -dv "$APP_PATH" 2>&1 | grep -E "Identifier|Signature"

# Verify plist entries
echo ""
echo "Main app plist mic entry:"
/usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$APP_PATH/Contents/Info.plist"

echo ""
echo "Renderer helper plist mic entry:"
RENDERER="$APP_PATH/Contents/Frameworks/Pulse Helper (Renderer).app/Contents/Info.plist"
if [ -f "$RENDERER" ]; then
  /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$RENDERER"
else
  echo "WARNING: Renderer helper not found"
fi
