#include <napi.h>
#include "driver-detect.h"
#include "aggregate-device.h"
#include "default-device.h"

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("isDriverInstalled",
        Napi::Function::New(env, IsDriverInstalled));
    exports.Set("getPulseDeviceId",
        Napi::Function::New(env, GetPulseDeviceId));
    exports.Set("getDefaultOutputDevice",
        Napi::Function::New(env, GetDefaultOutputDevice));
    exports.Set("setDefaultOutputDevice",
        Napi::Function::New(env, SetDefaultOutputDevice));
    exports.Set("createAggregateDevice",
        Napi::Function::New(env, CreateAggregateDevice));
    exports.Set("destroyAggregateDevice",
        Napi::Function::New(env, DestroyAggregateDevice));

    return exports;
}

NODE_API_MODULE(pulse_coreaudio, Init)
