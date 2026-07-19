#pragma once

#include <CoreAudio/AudioServerPlugIn.h>
#include <mutex>
#include "ring-buffer.h"
#include "types.h"

// Virtual audio device implementation.
// Manages properties for the device, its streams, and volume control.
// Routes IO operations through the shared ring buffer.
class PulseDevice {
public:
    PulseDevice();
    ~PulseDevice();

    // Property dispatch — routes to the correct handler based on object ID
    Boolean HasProperty(AudioObjectID objectID,
                        const AudioObjectPropertyAddress* address);

    OSStatus IsPropertySettable(AudioObjectID objectID,
                                const AudioObjectPropertyAddress* address,
                                Boolean* outIsSettable);

    OSStatus GetPropertyDataSize(AudioObjectID objectID,
                                 const AudioObjectPropertyAddress* address,
                                 UInt32 qualifierDataSize,
                                 const void* qualifierData,
                                 UInt32* outDataSize);

    OSStatus GetPropertyData(AudioObjectID objectID,
                             const AudioObjectPropertyAddress* address,
                             UInt32 qualifierDataSize,
                             const void* qualifierData,
                             UInt32 inDataSize,
                             UInt32* outDataSize,
                             void* outData);

    OSStatus SetPropertyData(AudioObjectID objectID,
                             const AudioObjectPropertyAddress* address,
                             UInt32 qualifierDataSize,
                             const void* qualifierData,
                             UInt32 inDataSize,
                             const void* inData);

    // IO operations
    OSStatus StartIO();
    OSStatus StopIO();
    void     GetZeroTimeStamp(Float64* outSampleTime,
                              UInt64* outHostTime,
                              UInt64* outSeed);
    OSStatus DoIOOperation(AudioObjectID streamID,
                           UInt32 operationID,
                           UInt32 ioBufferFrameSize,
                           AudioBufferList* ioMainBuffer,
                           AudioBufferList* ioSecondaryBuffer);

    // Accessors
    Float64  GetSampleRate() const { return mSampleRate; }
    bool     IsIORunning() const { return mIORunning; }

private:
    // Device properties
    Boolean  HasDeviceProperty(const AudioObjectPropertyAddress* address);
    OSStatus IsDevicePropertySettable(const AudioObjectPropertyAddress* address, Boolean* outIsSettable);
    OSStatus GetDevicePropertyDataSize(const AudioObjectPropertyAddress* address, UInt32* outDataSize);
    OSStatus GetDevicePropertyData(const AudioObjectPropertyAddress* address,
                                   UInt32 inDataSize, UInt32* outDataSize, void* outData);
    OSStatus SetDevicePropertyData(const AudioObjectPropertyAddress* address,
                                   UInt32 inDataSize, const void* inData);

    // Stream properties
    Boolean  HasStreamProperty(AudioObjectID streamID, const AudioObjectPropertyAddress* address);
    OSStatus IsStreamPropertySettable(const AudioObjectPropertyAddress* address, Boolean* outIsSettable);
    OSStatus GetStreamPropertyDataSize(AudioObjectID streamID,
                                       const AudioObjectPropertyAddress* address, UInt32* outDataSize);
    OSStatus GetStreamPropertyData(AudioObjectID streamID,
                                   const AudioObjectPropertyAddress* address,
                                   UInt32 inDataSize, UInt32* outDataSize, void* outData);

    // Volume control properties
    Boolean  HasVolumeProperty(const AudioObjectPropertyAddress* address);
    OSStatus IsVolumePropertySettable(const AudioObjectPropertyAddress* address, Boolean* outIsSettable);
    OSStatus GetVolumePropertyDataSize(const AudioObjectPropertyAddress* address, UInt32* outDataSize);
    OSStatus GetVolumePropertyData(const AudioObjectPropertyAddress* address,
                                   UInt32 inDataSize, UInt32* outDataSize, void* outData);
    OSStatus SetVolumePropertyData(const AudioObjectPropertyAddress* address,
                                   UInt32 inDataSize, const void* inData);

    // State
    Float64         mSampleRate;
    Float32         mVolume;
    bool            mMuted;
    bool            mIORunning;
    UInt32          mIOStartCount;
    UInt64          mIOAnchorHostTime;
    UInt64          mIOAnchorSampleTime;
    UInt64          mTimestampSeed;
    RingBuffer      mRingBuffer;
    std::mutex      mIOMutex;
};
