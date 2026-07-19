#pragma once

#include <napi.h>

// Check if the Pulse Audio virtual device is active in CoreAudio
Napi::Value IsDriverInstalled(const Napi::CallbackInfo& info);

// Get the AudioObjectID of the Pulse Audio device, or null if not found
Napi::Value GetPulseDeviceId(const Napi::CallbackInfo& info);
