// pulse-audio-helper: Standalone CLI for CoreAudio aggregate device management.
// Used by the Electron main process via execSync instead of a N-API addon.
//
// Commands:
//   detect                        — exits 0 if Pulse Audio device found, 1 otherwise
//   get-default                   — prints "id|uid|name" of default output device
//   set-default <device-id>       — sets default output device by AudioObjectID
//   create-aggregate <real-uid>   — creates aggregate device, prints "id|uid"
//   destroy-aggregate             — destroys the aggregate device
//   start-capture                 — full capture flow: save default, create aggregate, set default, print result
//   stop-capture <saved-device-id>— restore default, destroy aggregate
//   list-devices                  — list all audio devices (for debugging)

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static const char* kPulseDeviceUID = "com.pulse.audio.device";
static const char* kAggregateUID = "com.pulse.aggregate.screenshare";
static const char* kAggregateName = "Pulse Screen Share";

// ============================================================================
// Helpers
// ============================================================================

static CFStringRef ToCFString(const char* str) {
    return CFStringCreateWithCString(kCFAllocatorDefault, str, kCFStringEncodingUTF8);
}

static void CFStringToBuffer(CFStringRef cfStr, char* buf, size_t bufSize) {
    if (!cfStr || !CFStringGetCString(cfStr, buf, (CFIndex)bufSize, kCFStringEncodingUTF8)) {
        buf[0] = '\0';
    }
}

// Set the default output device using AudioObjectSetPropertyData.
// Returns true on success.
static bool setDefaultOutput(AudioObjectID deviceId) {
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    OSStatus err = AudioObjectSetPropertyData(
        kAudioObjectSystemObject, &prop, 0, nullptr, sizeof(deviceId), &deviceId);
    if (err != noErr) {
        fprintf(stderr, "AudioObjectSetPropertyData failed: %d\n", (int)err);
        return false;
    }

    // Give CoreAudio time to process
    usleep(200000); // 200ms

    // Verify
    AudioObjectID currentDefault = 0;
    UInt32 size = sizeof(currentDefault);
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &size, &currentDefault);
    if (err == noErr && currentDefault == deviceId) {
        return true;
    }

    fprintf(stderr, "AudioObjectSetPropertyData returned noErr but default is %u (wanted %u)\n",
            (unsigned)currentDefault, (unsigned)deviceId);
    return false;
}

// Get the current default output device ID and UID.
static bool getDefaultOutput(AudioObjectID& outDeviceId, char* outUID, size_t uidBufSize, char* outName, size_t nameBufSize) {
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = sizeof(outDeviceId);
    OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &size, &outDeviceId);
    if (err != noErr) return false;

    // UID
    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef uid = nullptr;
    UInt32 uidSize = sizeof(CFStringRef);
    err = AudioObjectGetPropertyData(outDeviceId, &uidProp, 0, nullptr, &uidSize, &uid);
    if (err != noErr) return false;
    CFStringToBuffer(uid, outUID, uidBufSize);
    CFRelease(uid);

    // Name
    AudioObjectPropertyAddress nameProp = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef name = nullptr;
    UInt32 nameSize = sizeof(CFStringRef);
    err = AudioObjectGetPropertyData(outDeviceId, &nameProp, 0, nullptr, &nameSize, &name);
    if (err != noErr) {
        outName[0] = '\0';
        return true; // UID is enough
    }
    CFStringToBuffer(name, outName, nameBufSize);
    CFRelease(name);
    return true;
}

// Create the multi-output aggregate device. Returns the device ID (0 on failure).
static AudioObjectID createAggregate(const char* realOutputUID) {
    CFStringRef aggUID = ToCFString(kAggregateUID);
    CFStringRef aggName = ToCFString(kAggregateName);
    CFStringRef realUID = ToCFString(realOutputUID);
    CFStringRef pulseUID = ToCFString(kPulseDeviceUID);

    // Build sub-device list
    CFMutableArrayRef subDevices = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);

    // Real output device
    CFMutableDictionaryRef realDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(realDict, CFSTR(kAudioSubDeviceUIDKey), realUID);
    CFArrayAppendValue(subDevices, realDict);
    CFRelease(realDict);

    // Pulse Audio virtual device
    CFMutableDictionaryRef pulseDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(pulseDict, CFSTR(kAudioSubDeviceUIDKey), pulseUID);
    CFArrayAppendValue(subDevices, pulseDict);
    CFRelease(pulseDict);

    // Aggregate device description
    CFMutableDictionaryRef desc = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceUIDKey), aggUID);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceNameKey), aggName);
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices);

    // Use the real device as the clock source
    CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceMainSubDeviceKey), realUID);

    // Multi-output mode (NOT stacked, NOT private).
    // Multi-output routes audio to all sub-devices simultaneously — audio plays
    // through the real speakers AND gets sent to the Pulse virtual device for capture.

    // Create the aggregate device
    AudioObjectID aggregateId = 0;
    OSStatus err = AudioHardwareCreateAggregateDevice(desc, &aggregateId);

    CFRelease(desc);
    CFRelease(subDevices);
    CFRelease(aggUID);
    CFRelease(aggName);
    CFRelease(realUID);
    CFRelease(pulseUID);

    if (err != noErr) {
        fprintf(stderr, "Failed to create aggregate device: %d\n", (int)err);
        return 0;
    }

    // Wait for CoreAudio to fully initialize the aggregate device
    usleep(500000); // 500ms

    return aggregateId;
}

// Destroy the aggregate device by finding it by UID.
static bool destroyAggregate() {
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize);
    if (err != noErr) return false;

    UInt32 deviceCount = dataSize / sizeof(AudioObjectID);
    auto* devices = new AudioObjectID[deviceCount];
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, devices);
    if (err != noErr) { delete[] devices; return false; }

    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    CFStringRef targetUID = ToCFString(kAggregateUID);
    AudioObjectID aggregateId = 0;

    for (UInt32 i = 0; i < deviceCount; i++) {
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(CFStringRef);
        err = AudioObjectGetPropertyData(devices[i], &uidProp, 0, nullptr, &uidSize, &uid);
        if (err == noErr && uid) {
            if (CFStringCompare(uid, targetUID, 0) == kCFCompareEqualTo) {
                aggregateId = devices[i];
            }
            CFRelease(uid);
            if (aggregateId) break;
        }
    }

    CFRelease(targetUID);
    delete[] devices;

    if (aggregateId == 0) {
        return true; // Already gone
    }

    err = AudioHardwareDestroyAggregateDevice(aggregateId);
    return err == noErr;
}

// ============================================================================
// detect — check if Pulse Audio device exists
// ============================================================================

static int cmd_detect() {
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize);
    if (err != noErr) return 1;

    UInt32 deviceCount = dataSize / sizeof(AudioObjectID);
    auto* devices = new AudioObjectID[deviceCount];
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, devices);
    if (err != noErr) { delete[] devices; return 1; }

    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    CFStringRef targetUID = ToCFString(kPulseDeviceUID);
    bool found = false;

    for (UInt32 i = 0; i < deviceCount; i++) {
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(CFStringRef);
        err = AudioObjectGetPropertyData(devices[i], &uidProp, 0, nullptr, &uidSize, &uid);
        if (err == noErr && uid) {
            if (CFStringCompare(uid, targetUID, 0) == kCFCompareEqualTo) {
                found = true;
            }
            CFRelease(uid);
            if (found) break;
        }
    }

    CFRelease(targetUID);
    delete[] devices;
    return found ? 0 : 1;
}

// ============================================================================
// get-default — print default output device info
// ============================================================================

static int cmd_get_default() {
    AudioObjectID deviceId;
    char uidBuf[256], nameBuf[256];
    if (!getDefaultOutput(deviceId, uidBuf, sizeof(uidBuf), nameBuf, sizeof(nameBuf))) {
        fprintf(stderr, "Failed to get default output device\n");
        return 1;
    }
    printf("%u|%s|%s\n", (unsigned)deviceId, uidBuf, nameBuf);
    return 0;
}

// ============================================================================
// set-default <device-id>
// ============================================================================

static int cmd_set_default(const char* idStr) {
    AudioObjectID deviceId = (AudioObjectID)atoi(idStr);
    return setDefaultOutput(deviceId) ? 0 : 1;
}

// ============================================================================
// create-aggregate <real-output-uid>
// ============================================================================

static int cmd_create_aggregate(const char* realOutputUID) {
    AudioObjectID aggregateId = createAggregate(realOutputUID);
    if (aggregateId == 0) return 1;

    // Get the UID of the created device
    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef createdUID = nullptr;
    UInt32 uidSize = sizeof(CFStringRef);
    AudioObjectGetPropertyData(aggregateId, &uidProp, 0, nullptr, &uidSize, &createdUID);

    char uidBuf[256];
    CFStringToBuffer(createdUID, uidBuf, sizeof(uidBuf));
    if (createdUID) CFRelease(createdUID);

    printf("%u|%s\n", (unsigned)aggregateId, uidBuf);
    return 0;
}

// ============================================================================
// destroy-aggregate
// ============================================================================

static int cmd_destroy_aggregate() {
    return destroyAggregate() ? 0 : 1;
}

// ============================================================================
// start-capture — full capture flow in a single process
//   1. Get current default output
//   2. Destroy any leftover aggregate
//   3. Create multi-output aggregate (real output + Pulse Audio)
//   4. Set aggregate as default output
//   5. Print "savedDeviceId|realOutputName" for the caller
// ============================================================================

static int cmd_start_capture() {
    // 1. Get current default output
    AudioObjectID savedDeviceId;
    char savedUID[256], savedName[256];
    if (!getDefaultOutput(savedDeviceId, savedUID, sizeof(savedUID), savedName, sizeof(savedName))) {
        fprintf(stderr, "Failed to get current default output\n");
        return 1;
    }
    fprintf(stderr, "[audio-capture] Current default: %s (%s, ID %u)\n",
            savedName, savedUID, (unsigned)savedDeviceId);

    // 2. Destroy any leftover aggregate
    destroyAggregate();
    usleep(200000); // 200ms

    // Re-read default (ID may change after destroying aggregate)
    if (!getDefaultOutput(savedDeviceId, savedUID, sizeof(savedUID), savedName, sizeof(savedName))) {
        fprintf(stderr, "Failed to re-read default output after cleanup\n");
        return 1;
    }

    // 3. Create multi-output aggregate
    AudioObjectID aggregateId = createAggregate(savedUID);
    if (aggregateId == 0) {
        fprintf(stderr, "Failed to create aggregate device\n");
        return 1;
    }
    fprintf(stderr, "[audio-capture] Created aggregate device (ID %u)\n", (unsigned)aggregateId);

    // 4. Set aggregate as default output
    if (!setDefaultOutput(aggregateId)) {
        fprintf(stderr, "Failed to set aggregate as default output, cleaning up\n");
        AudioHardwareDestroyAggregateDevice(aggregateId);
        return 1;
    }
    fprintf(stderr, "[audio-capture] Set aggregate as default output\n");

    // 5. Print result: savedDeviceId|savedName
    // The caller needs savedDeviceId to restore later
    printf("%u|%s\n", (unsigned)savedDeviceId, savedName);
    return 0;
}

// ============================================================================
// stop-capture <saved-device-id> — restore default and destroy aggregate
// ============================================================================

static int cmd_stop_capture(const char* savedIdStr) {
    AudioObjectID savedDeviceId = (AudioObjectID)atoi(savedIdStr);

    fprintf(stderr, "[audio-capture] Restoring default output to device %u\n", (unsigned)savedDeviceId);

    // First destroy the aggregate (this may change device IDs)
    destroyAggregate();
    usleep(200000); // 200ms

    // Now restore the default
    // After destroying the aggregate, we need to find the real device again
    // because the AudioObjectID may have changed
    if (savedDeviceId > 0) {
        // Try setting by the saved ID first
        if (!setDefaultOutput(savedDeviceId)) {
            fprintf(stderr, "[audio-capture] Warning: could not restore saved device %u (ID may have changed)\n",
                    (unsigned)savedDeviceId);
            // Not a fatal error — the system will fall back to its own default
        }
    }

    return 0;
}

// ============================================================================
// list-devices — print all audio devices (for debugging)
// ============================================================================

static int cmd_list_devices() {
    AudioObjectPropertyAddress prop = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize);
    if (err != noErr) {
        fprintf(stderr, "Failed to get device list: %d\n", (int)err);
        return 1;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioObjectID);
    auto* devices = new AudioObjectID[deviceCount];
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, devices);
    if (err != noErr) {
        fprintf(stderr, "Failed to get devices: %d\n", (int)err);
        delete[] devices;
        return 1;
    }

    printf("Found %u audio devices:\n", (unsigned)deviceCount);

    AudioObjectPropertyAddress uidProp = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectPropertyAddress nameProp = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectPropertyAddress outStreamsProp = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    AudioObjectPropertyAddress inStreamsProp = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMain
    };
    AudioObjectPropertyAddress transportProp = {
        kAudioDevicePropertyTransportType,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    for (UInt32 i = 0; i < deviceCount; i++) {
        AudioObjectID devId = devices[i];

        // UID
        CFStringRef uid = nullptr;
        UInt32 uidSize = sizeof(CFStringRef);
        char uidBuf[256] = "?";
        err = AudioObjectGetPropertyData(devId, &uidProp, 0, nullptr, &uidSize, &uid);
        if (err == noErr && uid) { CFStringToBuffer(uid, uidBuf, sizeof(uidBuf)); CFRelease(uid); }

        // Name
        CFStringRef name = nullptr;
        UInt32 nameSize = sizeof(CFStringRef);
        char nameBuf[256] = "?";
        err = AudioObjectGetPropertyData(devId, &nameProp, 0, nullptr, &nameSize, &name);
        if (err == noErr && name) { CFStringToBuffer(name, nameBuf, sizeof(nameBuf)); CFRelease(name); }

        // Output streams
        UInt32 outSize = 0;
        AudioObjectGetPropertyDataSize(devId, &outStreamsProp, 0, nullptr, &outSize);
        UInt32 outCount = outSize / sizeof(AudioObjectID);

        // Input streams
        UInt32 inSize = 0;
        AudioObjectGetPropertyDataSize(devId, &inStreamsProp, 0, nullptr, &inSize);
        UInt32 inCount = inSize / sizeof(AudioObjectID);

        // Transport type
        UInt32 transport = 0;
        UInt32 tSize = sizeof(transport);
        AudioObjectGetPropertyData(devId, &transportProp, 0, nullptr, &tSize, &transport);
        char fourCC[5] = {0};
        fourCC[0] = (char)((transport >> 24) & 0xFF);
        fourCC[1] = (char)((transport >> 16) & 0xFF);
        fourCC[2] = (char)((transport >> 8) & 0xFF);
        fourCC[3] = (char)(transport & 0xFF);

        printf("  [%u] %s  uid=%s  out=%u in=%u  transport='%s'\n",
               (unsigned)devId, nameBuf, uidBuf, (unsigned)outCount, (unsigned)inCount, fourCC);
    }

    delete[] devices;

    // Also show default
    AudioObjectID defaultOut = 0;
    UInt32 defSize = sizeof(defaultOut);
    AudioObjectPropertyAddress defProp = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &defProp, 0, nullptr, &defSize, &defaultOut);
    printf("Default output device ID: %u\n", (unsigned)defaultOut);

    return 0;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pulse-audio-helper <command> [args...]\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  detect                    — exits 0 if Pulse Audio device found\n");
        fprintf(stderr, "  get-default               — prints \"id|uid|name\" of default output\n");
        fprintf(stderr, "  set-default <id>          — sets default output device\n");
        fprintf(stderr, "  create-aggregate <uid>    — creates aggregate device\n");
        fprintf(stderr, "  destroy-aggregate         — destroys aggregate device\n");
        fprintf(stderr, "  start-capture             — full capture flow (create + activate)\n");
        fprintf(stderr, "  stop-capture <saved-id>   — restore default + destroy aggregate\n");
        fprintf(stderr, "  list-devices              — list all audio devices\n");
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "detect") == 0) {
        return cmd_detect();
    } else if (strcmp(cmd, "get-default") == 0) {
        return cmd_get_default();
    } else if (strcmp(cmd, "set-default") == 0 && argc >= 3) {
        return cmd_set_default(argv[2]);
    } else if (strcmp(cmd, "create-aggregate") == 0 && argc >= 3) {
        return cmd_create_aggregate(argv[2]);
    } else if (strcmp(cmd, "destroy-aggregate") == 0) {
        return cmd_destroy_aggregate();
    } else if (strcmp(cmd, "start-capture") == 0) {
        return cmd_start_capture();
    } else if (strcmp(cmd, "stop-capture") == 0 && argc >= 3) {
        return cmd_stop_capture(argv[2]);
    } else if (strcmp(cmd, "list-devices") == 0) {
        return cmd_list_devices();
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return 1;
    }
}
