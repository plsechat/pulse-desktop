#include "device.h"
#include <mach/mach_time.h>
#include <cmath>

// Helper to convert mach_absolute_time to nanoseconds
static UInt64 HostTimeToNanos(UInt64 hostTime)
{
    static mach_timebase_info_data_t sTimebase = { 0, 0 };
    if (sTimebase.denom == 0) {
        mach_timebase_info(&sTimebase);
    }
    return (hostTime * sTimebase.numer) / sTimebase.denom;
}

static UInt64 NanosToHostTime(UInt64 nanos)
{
    static mach_timebase_info_data_t sTimebase = { 0, 0 };
    if (sTimebase.denom == 0) {
        mach_timebase_info(&sTimebase);
    }
    return (nanos * sTimebase.denom) / sTimebase.numer;
}

PulseDevice::PulseDevice()
    : mSampleRate(kDefaultSampleRate)
    , mVolume(kDefaultVolume)
    , mMuted(false)
    , mIORunning(false)
    , mIOStartCount(0)
    , mIOAnchorHostTime(0)
    , mIOAnchorSampleTime(0)
    , mTimestampSeed(0)
{
    mRingBuffer.Initialize(kRingBufferFrameCapacity, kBytesPerFrame);
}

PulseDevice::~PulseDevice()
{
}

// ============================================================================
// Property dispatch
// ============================================================================

Boolean PulseDevice::HasProperty(AudioObjectID objectID,
                                 const AudioObjectPropertyAddress* address)
{
    switch (objectID) {
        case kObjectID_Device:
            return HasDeviceProperty(address);
        case kObjectID_Stream_Output:
        case kObjectID_Stream_Input:
            return HasStreamProperty(objectID, address);
        case kObjectID_Volume:
            return HasVolumeProperty(address);
        default:
            return false;
    }
}

OSStatus PulseDevice::IsPropertySettable(AudioObjectID objectID,
                                         const AudioObjectPropertyAddress* address,
                                         Boolean* outIsSettable)
{
    switch (objectID) {
        case kObjectID_Device:
            return IsDevicePropertySettable(address, outIsSettable);
        case kObjectID_Stream_Output:
        case kObjectID_Stream_Input:
            return IsStreamPropertySettable(address, outIsSettable);
        case kObjectID_Volume:
            return IsVolumePropertySettable(address, outIsSettable);
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::GetPropertyDataSize(AudioObjectID objectID,
                                           const AudioObjectPropertyAddress* address,
                                           UInt32 /*qualifierDataSize*/,
                                           const void* /*qualifierData*/,
                                           UInt32* outDataSize)
{
    switch (objectID) {
        case kObjectID_Device:
            return GetDevicePropertyDataSize(address, outDataSize);
        case kObjectID_Stream_Output:
        case kObjectID_Stream_Input:
            return GetStreamPropertyDataSize(objectID, address, outDataSize);
        case kObjectID_Volume:
            return GetVolumePropertyDataSize(address, outDataSize);
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::GetPropertyData(AudioObjectID objectID,
                                       const AudioObjectPropertyAddress* address,
                                       UInt32 /*qualifierDataSize*/,
                                       const void* /*qualifierData*/,
                                       UInt32 inDataSize,
                                       UInt32* outDataSize,
                                       void* outData)
{
    switch (objectID) {
        case kObjectID_Device:
            return GetDevicePropertyData(address, inDataSize, outDataSize, outData);
        case kObjectID_Stream_Output:
        case kObjectID_Stream_Input:
            return GetStreamPropertyData(objectID, address, inDataSize, outDataSize, outData);
        case kObjectID_Volume:
            return GetVolumePropertyData(address, inDataSize, outDataSize, outData);
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::SetPropertyData(AudioObjectID objectID,
                                       const AudioObjectPropertyAddress* address,
                                       UInt32 /*qualifierDataSize*/,
                                       const void* /*qualifierData*/,
                                       UInt32 inDataSize,
                                       const void* inData)
{
    switch (objectID) {
        case kObjectID_Device:
            return SetDevicePropertyData(address, inDataSize, inData);
        case kObjectID_Volume:
            return SetVolumePropertyData(address, inDataSize, inData);
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

// ============================================================================
// IO Operations
// ============================================================================

OSStatus PulseDevice::StartIO()
{
    std::lock_guard<std::mutex> lock(mIOMutex);

    if (mIOStartCount == 0) {
        mRingBuffer.Reset();
        mIOAnchorHostTime   = mach_absolute_time();
        mIOAnchorSampleTime = 0;
        mTimestampSeed++;
        mIORunning = true;
    }

    mIOStartCount++;
    return kAudioHardwareNoError;
}

OSStatus PulseDevice::StopIO()
{
    std::lock_guard<std::mutex> lock(mIOMutex);

    if (mIOStartCount == 0) {
        return kAudioHardwareNotRunningError;
    }

    mIOStartCount--;
    if (mIOStartCount == 0) {
        mIORunning = false;
    }

    return kAudioHardwareNoError;
}

void PulseDevice::GetZeroTimeStamp(Float64* outSampleTime,
                                   UInt64* outHostTime,
                                   UInt64* outSeed)
{
    // Calculate the current zero timestamp based on the IO anchor
    UInt64 currentHostTime = mach_absolute_time();
    UInt64 elapsedNanos    = HostTimeToNanos(currentHostTime - mIOAnchorHostTime);
    Float64 elapsedSeconds = (Float64)elapsedNanos / 1000000000.0;
    Float64 elapsedSamples = elapsedSeconds * mSampleRate;

    // Align to period boundaries
    UInt64 periodsElapsed  = (UInt64)(elapsedSamples / kFramesPerPeriod);
    Float64 sampleTime     = (Float64)(periodsElapsed * kFramesPerPeriod);
    Float64 periodSeconds  = (Float64)(periodsElapsed * kFramesPerPeriod) / mSampleRate;
    UInt64  periodNanos    = (UInt64)(periodSeconds * 1000000000.0);

    *outSampleTime = sampleTime;
    *outHostTime   = mIOAnchorHostTime + NanosToHostTime(periodNanos);
    *outSeed       = mTimestampSeed;
}

OSStatus PulseDevice::DoIOOperation(AudioObjectID streamID,
                                     UInt32 operationID,
                                     UInt32 ioBufferFrameSize,
                                     AudioBufferList* ioMainBuffer,
                                     AudioBufferList* /*ioSecondaryBuffer*/)
{
    if (!ioMainBuffer || ioMainBuffer->mNumberBuffers == 0) {
        return kAudioHardwareNoError;
    }

    float* buffer = (float*)ioMainBuffer->mBuffers[0].mData;
    if (!buffer) return kAudioHardwareNoError;

    switch (operationID) {
        case kAudioServerPlugInIOOperationWriteMix:
            // Output stream: apps writing audio → store in ring buffer
            if (streamID == kObjectID_Stream_Output) {
                // Apply volume
                if (mMuted || mVolume <= 0.0f) {
                    // Don't store silence, just skip
                } else if (mVolume < 1.0f) {
                    // Scale in-place then store
                    UInt32 totalSamples = ioBufferFrameSize * kNumChannels;
                    for (UInt32 i = 0; i < totalSamples; i++) {
                        buffer[i] *= mVolume;
                    }
                    mRingBuffer.Store(buffer, ioBufferFrameSize);
                } else {
                    mRingBuffer.Store(buffer, ioBufferFrameSize);
                }
            }
            break;

        case kAudioServerPlugInIOOperationReadInput:
            // Input stream: Electron reading audio → fetch from ring buffer
            if (streamID == kObjectID_Stream_Input) {
                mRingBuffer.Fetch(buffer, ioBufferFrameSize);
            }
            break;

        default:
            break;
    }

    return kAudioHardwareNoError;
}

// ============================================================================
// Device Properties
// ============================================================================

Boolean PulseDevice::HasDeviceProperty(const AudioObjectPropertyAddress* address)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyStreams:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyIcon:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertySafetyOffset:
            return true;
        default:
            return false;
    }
}

OSStatus PulseDevice::IsDevicePropertySettable(const AudioObjectPropertyAddress* address,
                                               Boolean* outIsSettable)
{
    switch (address->mSelector) {
        case kAudioDevicePropertyNominalSampleRate:
            *outIsSettable = true;
            return kAudioHardwareNoError;
        default:
            *outIsSettable = false;
            return kAudioHardwareNoError;
    }
}

OSStatus PulseDevice::GetDevicePropertyDataSize(const AudioObjectPropertyAddress* address,
                                                UInt32* outDataSize)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
            *outDataSize = sizeof(CFStringRef);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyClockDomain:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyRelatedDevices:
            *outDataSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyIsHidden:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyZeroTimeStampPeriod:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyStreams: {
            // One stream per direction
            UInt32 count = 0;
            if (address->mScope == kAudioObjectPropertyScopeGlobal ||
                address->mScope == kAudioObjectPropertyScopeOutput) {
                count++;
            }
            if (address->mScope == kAudioObjectPropertyScopeGlobal ||
                address->mScope == kAudioObjectPropertyScopeInput) {
                count++;
            }
            *outDataSize = count * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }

        case kAudioObjectPropertyOwnedObjects: {
            // Output stream + Input stream + Volume control
            *outDataSize = 3 * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }

        case kAudioObjectPropertyControlList:
            *outDataSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyNominalSampleRate:
            *outDataSize = sizeof(Float64);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyAvailableNominalSampleRates:
            *outDataSize = kNumSupportedSampleRates * sizeof(AudioValueRange);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyIcon:
            *outDataSize = sizeof(CFURLRef);
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::GetDevicePropertyData(const AudioObjectPropertyAddress* address,
                                            UInt32 /*inDataSize*/,
                                            UInt32* outDataSize,
                                            void* outData)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioObjectClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioDeviceClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            *(AudioObjectID*)outData = kObjectID_Plugin;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyName:
            *outDataSize = sizeof(CFStringRef);
            *(CFStringRef*)outData = CFStringCreateWithCString(kCFAllocatorDefault,
                kDeviceName, kCFStringEncodingUTF8);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyManufacturer:
            *outDataSize = sizeof(CFStringRef);
            *(CFStringRef*)outData = CFStringCreateWithCString(kCFAllocatorDefault,
                kDeviceManufacturer, kCFStringEncodingUTF8);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceUID:
            *outDataSize = sizeof(CFStringRef);
            *(CFStringRef*)outData = CFStringCreateCopy(kCFAllocatorDefault, kDeviceUID);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyModelUID:
            *outDataSize = sizeof(CFStringRef);
            *(CFStringRef*)outData = CFStringCreateCopy(kCFAllocatorDefault, kDeviceModelUID);
            return kAudioHardwareNoError;

        case kAudioDevicePropertyTransportType:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kAudioDeviceTransportTypeVirtual;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyRelatedDevices:
            *outDataSize = sizeof(AudioObjectID);
            *(AudioObjectID*)outData = kObjectID_Device;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyClockDomain:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 0;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceIsAlive:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 1;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceIsRunning:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = mIORunning ? 1 : 0;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 1;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 0; // Don't hijack system sounds
            return kAudioHardwareNoError;

        case kAudioDevicePropertyIsHidden:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 0; // Not hidden — visible in Audio MIDI Setup
            return kAudioHardwareNoError;

        case kAudioDevicePropertyLatency:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kDeviceLatencyFrames;
            return kAudioHardwareNoError;

        case kAudioDevicePropertySafetyOffset:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kSafetyOffsetFrames;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyZeroTimeStampPeriod:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kFramesPerPeriod;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyStreams: {
            AudioObjectID* ids = (AudioObjectID*)outData;
            UInt32 count = 0;
            if (address->mScope == kAudioObjectPropertyScopeGlobal ||
                address->mScope == kAudioObjectPropertyScopeOutput) {
                ids[count++] = kObjectID_Stream_Output;
            }
            if (address->mScope == kAudioObjectPropertyScopeGlobal ||
                address->mScope == kAudioObjectPropertyScopeInput) {
                ids[count++] = kObjectID_Stream_Input;
            }
            *outDataSize = count * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }

        case kAudioObjectPropertyOwnedObjects: {
            AudioObjectID* ids = (AudioObjectID*)outData;
            ids[0] = kObjectID_Stream_Output;
            ids[1] = kObjectID_Stream_Input;
            ids[2] = kObjectID_Volume;
            *outDataSize = 3 * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }

        case kAudioObjectPropertyControlList: {
            *outDataSize = sizeof(AudioObjectID);
            *(AudioObjectID*)outData = kObjectID_Volume;
            return kAudioHardwareNoError;
        }

        case kAudioDevicePropertyNominalSampleRate:
            *outDataSize = sizeof(Float64);
            *(Float64*)outData = mSampleRate;
            return kAudioHardwareNoError;

        case kAudioDevicePropertyAvailableNominalSampleRates: {
            AudioValueRange* ranges = (AudioValueRange*)outData;
            for (UInt32 i = 0; i < kNumSupportedSampleRates; i++) {
                ranges[i].mMinimum = kSupportedSampleRates[i];
                ranges[i].mMaximum = kSupportedSampleRates[i];
            }
            *outDataSize = kNumSupportedSampleRates * sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }

        case kAudioDevicePropertyIcon:
            *outDataSize = sizeof(CFURLRef);
            *(CFURLRef*)outData = nullptr;
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::SetDevicePropertyData(const AudioObjectPropertyAddress* address,
                                            UInt32 /*inDataSize*/,
                                            const void* inData)
{
    switch (address->mSelector) {
        case kAudioDevicePropertyNominalSampleRate: {
            Float64 newRate = *(const Float64*)inData;
            bool valid = false;
            for (UInt32 i = 0; i < kNumSupportedSampleRates; i++) {
                if (fabs(newRate - kSupportedSampleRates[i]) < 0.1) {
                    valid = true;
                    break;
                }
            }
            if (!valid) return kAudioHardwareIllegalOperationError;
            mSampleRate = newRate;
            mRingBuffer.Reset();
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

// ============================================================================
// Stream Properties
// ============================================================================

Boolean PulseDevice::HasStreamProperty(AudioObjectID /*streamID*/,
                                       const AudioObjectPropertyAddress* address)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            return true;
        default:
            return false;
    }
}

OSStatus PulseDevice::IsStreamPropertySettable(const AudioObjectPropertyAddress* address,
                                               Boolean* outIsSettable)
{
    switch (address->mSelector) {
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            *outIsSettable = true;
            return kAudioHardwareNoError;
        default:
            *outIsSettable = false;
            return kAudioHardwareNoError;
    }
}

OSStatus PulseDevice::GetStreamPropertyDataSize(AudioObjectID /*streamID*/,
                                                const AudioObjectPropertyAddress* address,
                                                UInt32* outDataSize)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;

        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            *outDataSize = sizeof(AudioStreamBasicDescription);
            return kAudioHardwareNoError;

        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            *outDataSize = kNumSupportedSampleRates * sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::GetStreamPropertyData(AudioObjectID streamID,
                                            const AudioObjectPropertyAddress* address,
                                            UInt32 /*inDataSize*/,
                                            UInt32* outDataSize,
                                            void* outData)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioObjectClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioStreamClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            *(AudioObjectID*)outData = kObjectID_Device;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyIsActive:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 1;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyDirection:
            *outDataSize = sizeof(UInt32);
            // 0 = output (apps write to it), 1 = input (apps read from it)
            *(UInt32*)outData = (streamID == kObjectID_Stream_Output) ? 0 : 1;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyTerminalType:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = (streamID == kObjectID_Stream_Output)
                ? kAudioStreamTerminalTypeLine
                : kAudioStreamTerminalTypeMicrophone;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyStartingChannel:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = 1;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyLatency:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kStreamLatencyFrames;
            return kAudioHardwareNoError;

        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: {
            AudioStreamBasicDescription* desc = (AudioStreamBasicDescription*)outData;
            desc->mSampleRate       = mSampleRate;
            desc->mFormatID         = kAudioFormatLinearPCM;
            desc->mFormatFlags      = kAudioFormatFlagIsFloat
                                    | kAudioFormatFlagIsPacked;
            desc->mFramesPerPacket  = 1;
            desc->mChannelsPerFrame = kNumChannels;
            desc->mBitsPerChannel   = kBitsPerChannel;
            desc->mBytesPerFrame    = kBytesPerFrame;
            desc->mBytesPerPacket   = kBytesPerFrame;
            *outDataSize = sizeof(AudioStreamBasicDescription);
            return kAudioHardwareNoError;
        }

        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            AudioStreamRangedDescription* descs = (AudioStreamRangedDescription*)outData;
            for (UInt32 i = 0; i < kNumSupportedSampleRates; i++) {
                descs[i].mFormat.mSampleRate       = kSupportedSampleRates[i];
                descs[i].mFormat.mFormatID         = kAudioFormatLinearPCM;
                descs[i].mFormat.mFormatFlags      = kAudioFormatFlagIsFloat
                                                   | kAudioFormatFlagIsPacked;
                descs[i].mFormat.mFramesPerPacket  = 1;
                descs[i].mFormat.mChannelsPerFrame = kNumChannels;
                descs[i].mFormat.mBitsPerChannel   = kBitsPerChannel;
                descs[i].mFormat.mBytesPerFrame    = kBytesPerFrame;
                descs[i].mFormat.mBytesPerPacket   = kBytesPerFrame;
                descs[i].mSampleRateRange.mMinimum = kSupportedSampleRates[i];
                descs[i].mSampleRateRange.mMaximum = kSupportedSampleRates[i];
            }
            *outDataSize = kNumSupportedSampleRates * sizeof(AudioStreamRangedDescription);
            return kAudioHardwareNoError;
        }

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

// ============================================================================
// Volume Control Properties
// ============================================================================

Boolean PulseDevice::HasVolumeProperty(const AudioObjectPropertyAddress* address)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyElementName:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
        case kAudioLevelControlPropertyScalarValue:
        case kAudioLevelControlPropertyDecibelValue:
        case kAudioLevelControlPropertyDecibelRange:
        case kAudioBooleanControlPropertyValue:
            return true;
        default:
            return false;
    }
}

OSStatus PulseDevice::IsVolumePropertySettable(const AudioObjectPropertyAddress* address,
                                               Boolean* outIsSettable)
{
    switch (address->mSelector) {
        case kAudioLevelControlPropertyScalarValue:
        case kAudioLevelControlPropertyDecibelValue:
        case kAudioBooleanControlPropertyValue:
            *outIsSettable = true;
            return kAudioHardwareNoError;
        default:
            *outIsSettable = false;
            return kAudioHardwareNoError;
    }
}

OSStatus PulseDevice::GetVolumePropertyDataSize(const AudioObjectPropertyAddress* address,
                                                UInt32* outDataSize)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            return kAudioHardwareNoError;

        case kAudioObjectPropertyElementName:
            *outDataSize = sizeof(CFStringRef);
            return kAudioHardwareNoError;

        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        case kAudioLevelControlPropertyScalarValue:
            *outDataSize = sizeof(Float32);
            return kAudioHardwareNoError;

        case kAudioLevelControlPropertyDecibelValue:
            *outDataSize = sizeof(Float32);
            return kAudioHardwareNoError;

        case kAudioLevelControlPropertyDecibelRange:
            *outDataSize = sizeof(AudioValueRange);
            return kAudioHardwareNoError;

        case kAudioBooleanControlPropertyValue:
            *outDataSize = sizeof(UInt32);
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::GetVolumePropertyData(const AudioObjectPropertyAddress* address,
                                            UInt32 /*inDataSize*/,
                                            UInt32* outDataSize,
                                            void* outData)
{
    switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioObjectClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyClass:
            *outDataSize = sizeof(AudioClassID);
            *(AudioClassID*)outData = kAudioVolumeControlClassID;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyOwner:
            *outDataSize = sizeof(AudioObjectID);
            *(AudioObjectID*)outData = kObjectID_Device;
            return kAudioHardwareNoError;

        case kAudioObjectPropertyElementName:
            *outDataSize = sizeof(CFStringRef);
            *(CFStringRef*)outData = CFStringCreateWithCString(kCFAllocatorDefault,
                "Volume", kCFStringEncodingUTF8);
            return kAudioHardwareNoError;

        case kAudioControlPropertyScope:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kAudioObjectPropertyScopeOutput;
            return kAudioHardwareNoError;

        case kAudioControlPropertyElement:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = kAudioObjectPropertyElementMain;
            return kAudioHardwareNoError;

        case kAudioLevelControlPropertyScalarValue:
            *outDataSize = sizeof(Float32);
            *(Float32*)outData = mVolume;
            return kAudioHardwareNoError;

        case kAudioLevelControlPropertyDecibelValue: {
            // Convert scalar to dB: 0 = -96dB, 1 = 0dB
            Float32 dB = (mVolume > 0.0f) ? (20.0f * log10f(mVolume)) : -96.0f;
            *outDataSize = sizeof(Float32);
            *(Float32*)outData = dB;
            return kAudioHardwareNoError;
        }

        case kAudioLevelControlPropertyDecibelRange: {
            AudioValueRange* range = (AudioValueRange*)outData;
            range->mMinimum = -96.0;
            range->mMaximum = 0.0;
            *outDataSize = sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }

        case kAudioBooleanControlPropertyValue:
            *outDataSize = sizeof(UInt32);
            *(UInt32*)outData = mMuted ? 1 : 0;
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}

OSStatus PulseDevice::SetVolumePropertyData(const AudioObjectPropertyAddress* address,
                                            UInt32 /*inDataSize*/,
                                            const void* inData)
{
    switch (address->mSelector) {
        case kAudioLevelControlPropertyScalarValue: {
            Float32 newVolume = *(const Float32*)inData;
            mVolume = fmaxf(kMinVolume, fminf(kMaxVolume, newVolume));
            return kAudioHardwareNoError;
        }

        case kAudioLevelControlPropertyDecibelValue: {
            Float32 dB = *(const Float32*)inData;
            mVolume = (dB <= -96.0f) ? 0.0f : powf(10.0f, dB / 20.0f);
            mVolume = fmaxf(kMinVolume, fminf(kMaxVolume, mVolume));
            return kAudioHardwareNoError;
        }

        case kAudioBooleanControlPropertyValue:
            mMuted = (*(const UInt32*)inData) != 0;
            return kAudioHardwareNoError;

        default:
            return kAudioHardwareUnknownPropertyError;
    }
}
