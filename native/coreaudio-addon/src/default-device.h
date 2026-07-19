#pragma once

#include <napi.h>

// Get the current default output audio device.
// Returns: { id: number, uid: string, name: string }
Napi::Value GetDefaultOutputDevice(const Napi::CallbackInfo& info);

// Set the default output audio device by AudioObjectID.
// Args: deviceId (number)
Napi::Value SetDefaultOutputDevice(const Napi::CallbackInfo& info);
