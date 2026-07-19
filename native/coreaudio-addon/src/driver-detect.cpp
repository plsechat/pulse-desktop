#include "driver-detect.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <vector>

static const CFStringRef kPulseDeviceUID = CFSTR("com.pulse.audio.device");

// Find the Pulse Audio device by iterating all audio devices and matching UID
static AudioObjectID FindPulseDevice()
{
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize);
    if (status != noErr || dataSize == 0) return kAudioObjectUnknown;

    UInt32 deviceCount = dataSize / sizeof(AudioObjectID);
    std::vector<AudioObjectID> devices(deviceCount);

    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, devices.data());
    if (status != noErr) return kAudioObjectUnknown;

    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    for (AudioObjectID deviceId : devices) {
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(CFStringRef);

        status = AudioObjectGetPropertyData(deviceId, &uidProp, 0, nullptr, &uidSize, &uid);
        if (status != noErr || !uid) continue;

        bool match = CFStringCompare(uid, kPulseDeviceUID, 0) == kCFCompareEqualTo;
        CFRelease(uid);

        if (match) return deviceId;
    }

    return kAudioObjectUnknown;
}

Napi::Value IsDriverInstalled(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();
    AudioObjectID deviceId = FindPulseDevice();
    return Napi::Boolean::New(env, deviceId != kAudioObjectUnknown);
}

Napi::Value GetPulseDeviceId(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();
    AudioObjectID deviceId = FindPulseDevice();

    if (deviceId == kAudioObjectUnknown) {
        return env.Null();
    }

    return Napi::Number::New(env, static_cast<double>(deviceId));
}
