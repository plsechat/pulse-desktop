#pragma once

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>

// Static object IDs — we have exactly one device with fixed topology
enum ObjectID : AudioObjectID {
    kObjectID_Plugin        = kAudioObjectPlugInObject, // always 1
    kObjectID_Device        = 2,
    kObjectID_Stream_Output = 3,
    kObjectID_Stream_Input  = 4,
    kObjectID_Volume        = 5,
};

// String constants
static const char* kDeviceName          = "Pulse Audio";
static const char* kDeviceManufacturer  = "Pulse";

// CFString UIDs (created lazily)
#define kPluginBundleID     CFSTR("com.pulse.audio.driver")
#define kDeviceUID          CFSTR("com.pulse.audio.device")
#define kDeviceModelUID     CFSTR("com.pulse.audio.device.model")

// Audio format constants
static const Float64 kSupportedSampleRates[]    = { 44100.0, 48000.0 };
static const UInt32  kNumSupportedSampleRates    = 2;
static const Float64 kDefaultSampleRate          = 48000.0;
static const UInt32  kNumChannels                = 2;
static const UInt32  kBitsPerChannel             = 32;
static const UInt32  kBytesPerFrame              = kNumChannels * (kBitsPerChannel / 8);
static const UInt32  kBytesPerSample             = kBitsPerChannel / 8;

// IO timing
static const UInt32  kFramesPerPeriod            = 480;  // 10ms at 48kHz
static const UInt32  kRingBufferFrameCapacity    = 48000; // 1 second

// Latency
static const UInt32  kDeviceLatencyFrames        = 0;
static const UInt32  kStreamLatencyFrames        = 0;
static const UInt32  kSafetyOffsetFrames         = 0;

// Volume
static const Float32 kDefaultVolume              = 1.0f;
static const Float32 kMinVolume                  = 0.0f;
static const Float32 kMaxVolume                  = 1.0f;
