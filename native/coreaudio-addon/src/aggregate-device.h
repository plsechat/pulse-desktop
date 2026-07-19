#pragma once

#include <napi.h>

// Create a private aggregate device combining the real output and Pulse Audio devices.
// Args: realOutputUID (string), pulseAudioUID (string)
// Returns: { id: number, uid: string }
Napi::Value CreateAggregateDevice(const Napi::CallbackInfo& info);

// Destroy a previously created aggregate device.
// Args: aggregateUID (string)
Napi::Value DestroyAggregateDevice(const Napi::CallbackInfo& info);
