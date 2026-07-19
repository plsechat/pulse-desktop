#include "aggregate-device.h"
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

static const CFStringRef kAggregateUID  = CFSTR("com.pulse.aggregate.screenshare");
static const CFStringRef kAggregateName = CFSTR("Pulse Screen Share");

Napi::Value CreateAggregateDevice(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected (realOutputUID: string, pulseAudioUID: string)")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string realOutputUIDStr = info[0].As<Napi::String>().Utf8Value();
    std::string pulseAudioUIDStr = info[1].As<Napi::String>().Utf8Value();

    CFStringRef realOutputUID = CFStringCreateWithCString(kCFAllocatorDefault,
        realOutputUIDStr.c_str(), kCFStringEncodingUTF8);
    CFStringRef pulseAudioUID = CFStringCreateWithCString(kCFAllocatorDefault,
        pulseAudioUIDStr.c_str(), kCFStringEncodingUTF8);

    // Build the sub-device list
    // Real output device — this is where audio actually plays
    CFMutableDictionaryRef realDeviceDict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(realDeviceDict, CFSTR(kAudioSubDeviceUIDKey), realOutputUID);

    // Pulse Audio virtual device — this captures the audio for screen share
    CFMutableDictionaryRef pulseDeviceDict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(pulseDeviceDict, CFSTR(kAudioSubDeviceUIDKey), pulseAudioUID);

    // Sub-device array
    CFMutableArrayRef subDevices = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(subDevices, realDeviceDict);
    CFArrayAppendValue(subDevices, pulseDeviceDict);

    // Aggregate device description
    CFMutableDictionaryRef aggDesc = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceUIDKey), kAggregateUID);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceNameKey), kAggregateName);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices);

    // Stacked mode — both sub-devices receive the same audio
    int isStacked = 1;
    CFNumberRef stackedRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &isStacked);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceIsStackedKey), stackedRef);

    // Private — hidden from Audio MIDI Setup and System Preferences
    int isPrivate = 1;
    CFNumberRef privateRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &isPrivate);
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceIsPrivateKey), privateRef);

    // Clock source = real hardware device (to avoid drift)
    CFDictionarySetValue(aggDesc, CFSTR(kAudioAggregateDeviceMainSubDeviceKey), realOutputUID);

    // Create the aggregate device
    AudioObjectID aggregateDeviceID = kAudioObjectUnknown;
    OSStatus status = AudioHardwareCreateAggregateDevice(aggDesc, &aggregateDeviceID);

    // Cleanup CF objects
    CFRelease(aggDesc);
    CFRelease(subDevices);
    CFRelease(realDeviceDict);
    CFRelease(pulseDeviceDict);
    CFRelease(stackedRef);
    CFRelease(privateRef);
    CFRelease(realOutputUID);
    CFRelease(pulseAudioUID);

    if (status != noErr) {
        Napi::Error::New(env, "Failed to create aggregate device (OSStatus: " +
            std::to_string(status) + ")").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("id", Napi::Number::New(env, static_cast<double>(aggregateDeviceID)));
    result.Set("uid", Napi::String::New(env, "com.pulse.aggregate.screenshare"));
    return result;
}

Napi::Value DestroyAggregateDevice(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected (aggregateUID: string)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Find the aggregate device by UID
    std::string uidStr = info[0].As<Napi::String>().Utf8Value();
    CFStringRef uid = CFStringCreateWithCString(kCFAllocatorDefault,
        uidStr.c_str(), kCFStringEncodingUTF8);

    AudioObjectPropertyAddress translateProp = {
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioObjectID deviceId = kAudioObjectUnknown;
    UInt32 deviceIdSize = sizeof(AudioObjectID);
    UInt32 uidSize = sizeof(CFStringRef);

    OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &translateProp, uidSize, &uid, &deviceIdSize, &deviceId);
    CFRelease(uid);

    if (status != noErr || deviceId == kAudioObjectUnknown) {
        // Device may already be destroyed — not an error
        return env.Undefined();
    }

    status = AudioHardwareDestroyAggregateDevice(deviceId);
    if (status != noErr) {
        Napi::Error::New(env, "Failed to destroy aggregate device (OSStatus: " +
            std::to_string(status) + ")").ThrowAsJavaScriptException();
    }

    return env.Undefined();
}
