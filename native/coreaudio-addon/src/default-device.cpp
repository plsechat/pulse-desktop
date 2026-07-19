#include "default-device.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string>

// Helper to get a CFString property from an audio object as a std::string
static std::string GetCFStringProperty(AudioObjectID objectId,
                                       AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress prop = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    CFStringRef cfStr = nullptr;
    UInt32 dataSize = sizeof(CFStringRef);

    OSStatus status = AudioObjectGetPropertyData(objectId, &prop, 0, nullptr, &dataSize, &cfStr);
    if (status != noErr || !cfStr) return "";

    char buf[256];
    bool ok = CFStringGetCString(cfStr, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(cfStr);

    return ok ? std::string(buf) : "";
}

Napi::Value GetDefaultOutputDevice(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioObjectID deviceId = kAudioObjectUnknown;
    UInt32 dataSize = sizeof(AudioObjectID);

    OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, &deviceId);

    if (status != noErr || deviceId == kAudioObjectUnknown) {
        Napi::Error::New(env, "Failed to get default output device").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string uid  = GetCFStringProperty(deviceId, kAudioDevicePropertyDeviceUID);
    std::string name = GetCFStringProperty(deviceId, kAudioObjectPropertyName);

    Napi::Object result = Napi::Object::New(env);
    result.Set("id", Napi::Number::New(env, static_cast<double>(deviceId)));
    result.Set("uid", Napi::String::New(env, uid));
    result.Set("name", Napi::String::New(env, name));
    return result;
}

Napi::Value SetDefaultOutputDevice(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected (deviceId: number)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    AudioObjectID deviceId = static_cast<AudioObjectID>(info[0].As<Napi::Number>().Uint32Value());

    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    OSStatus status = AudioObjectSetPropertyData(
        kAudioObjectSystemObject, &prop, 0, nullptr, sizeof(AudioObjectID), &deviceId);

    if (status != noErr) {
        Napi::Error::New(env, "Failed to set default output device (OSStatus: " +
            std::to_string(status) + ")").ThrowAsJavaScriptException();
    }

    return env.Undefined();
}
