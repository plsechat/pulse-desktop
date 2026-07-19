#include "plugin.h"
#include "device.h"
#include "types.h"
#include <CoreFoundation/CoreFoundation.h>

// ============================================================================
// Global state
// ============================================================================

static AudioServerPlugInHostRef     gHost = nullptr;
static PulseDevice*                 gDevice = nullptr;
static UInt32                       gRefCount = 0;

// Forward declarations for the vtable
static HRESULT   Plugin_QueryInterface(void* driver, REFIID iid, LPVOID* ppv);
static ULONG     Plugin_AddRef(void* driver);
static ULONG     Plugin_Release(void* driver);
static OSStatus  Plugin_Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host);
static OSStatus  Plugin_CreateDevice(AudioServerPlugInDriverRef driver,
                                     CFDictionaryRef description,
                                     const AudioServerPlugInClientInfo* clientInfo,
                                     AudioObjectID* outDeviceObjectID);
static OSStatus  Plugin_DestroyDevice(AudioServerPlugInDriverRef driver, AudioObjectID objectID);
static OSStatus  Plugin_AddDeviceClient(AudioServerPlugInDriverRef driver,
                                        AudioObjectID objectID,
                                        const AudioServerPlugInClientInfo* clientInfo);
static OSStatus  Plugin_RemoveDeviceClient(AudioServerPlugInDriverRef driver,
                                           AudioObjectID objectID,
                                           const AudioServerPlugInClientInfo* clientInfo);
static OSStatus  Plugin_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef driver,
                                                         AudioObjectID objectID,
                                                         UInt64 changeAction,
                                                         void* changeInfo);
static OSStatus  Plugin_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef driver,
                                                       AudioObjectID objectID,
                                                       UInt64 changeAction,
                                                       void* changeInfo);
static Boolean   Plugin_HasProperty(AudioServerPlugInDriverRef driver,
                                    AudioObjectID objectID,
                                    pid_t clientPID,
                                    const AudioObjectPropertyAddress* address);
static OSStatus  Plugin_IsPropertySettable(AudioServerPlugInDriverRef driver,
                                           AudioObjectID objectID,
                                           pid_t clientPID,
                                           const AudioObjectPropertyAddress* address,
                                           Boolean* outIsSettable);
static OSStatus  Plugin_GetPropertyDataSize(AudioServerPlugInDriverRef driver,
                                            AudioObjectID objectID,
                                            pid_t clientPID,
                                            const AudioObjectPropertyAddress* address,
                                            UInt32 qualifierDataSize,
                                            const void* qualifierData,
                                            UInt32* outDataSize);
static OSStatus  Plugin_GetPropertyData(AudioServerPlugInDriverRef driver,
                                        AudioObjectID objectID,
                                        pid_t clientPID,
                                        const AudioObjectPropertyAddress* address,
                                        UInt32 qualifierDataSize,
                                        const void* qualifierData,
                                        UInt32 inDataSize,
                                        UInt32* outDataSize,
                                        void* outData);
static OSStatus  Plugin_SetPropertyData(AudioServerPlugInDriverRef driver,
                                        AudioObjectID objectID,
                                        pid_t clientPID,
                                        const AudioObjectPropertyAddress* address,
                                        UInt32 qualifierDataSize,
                                        const void* qualifierData,
                                        UInt32 inDataSize,
                                        const void* inData);
static OSStatus  Plugin_StartIO(AudioServerPlugInDriverRef driver,
                                AudioObjectID objectID,
                                UInt32 clientID);
static OSStatus  Plugin_StopIO(AudioServerPlugInDriverRef driver,
                               AudioObjectID objectID,
                               UInt32 clientID);
static OSStatus  Plugin_GetZeroTimeStamp(AudioServerPlugInDriverRef driver,
                                         AudioObjectID objectID,
                                         UInt32 clientID,
                                         Float64* outSampleTime,
                                         UInt64* outHostTime,
                                         UInt64* outSeed);
static OSStatus  Plugin_WillDoIOOperation(AudioServerPlugInDriverRef driver,
                                          AudioObjectID objectID,
                                          UInt32 clientID,
                                          UInt32 operationID,
                                          Boolean* outWillDo,
                                          Boolean* outIsInput);
static OSStatus  Plugin_BeginIOOperation(AudioServerPlugInDriverRef driver,
                                         AudioObjectID objectID,
                                         UInt32 clientID,
                                         UInt32 operationID,
                                         UInt32 ioBufferFrameSize,
                                         const AudioServerPlugInIOCycleInfo* ioCycleInfo);
static OSStatus  Plugin_DoIOOperation(AudioServerPlugInDriverRef driver,
                                      AudioObjectID objectID,
                                      AudioObjectID streamID,
                                      UInt32 clientID,
                                      UInt32 operationID,
                                      UInt32 ioBufferFrameSize,
                                      const AudioServerPlugInIOCycleInfo* ioCycleInfo,
                                      void* ioMainBuffer,
                                      void* ioSecondaryBuffer);
static OSStatus  Plugin_EndIOOperation(AudioServerPlugInDriverRef driver,
                                       AudioObjectID objectID,
                                       UInt32 clientID,
                                       UInt32 operationID,
                                       UInt32 ioBufferFrameSize,
                                       const AudioServerPlugInIOCycleInfo* ioCycleInfo);

// ============================================================================
// vtable
// ============================================================================

static AudioServerPlugInDriverInterface gDriverInterface = {
    // IUnknown
    nullptr, // _reserved
    Plugin_QueryInterface,
    Plugin_AddRef,
    Plugin_Release,

    // AudioServerPlugIn
    Plugin_Initialize,
    Plugin_CreateDevice,
    Plugin_DestroyDevice,
    Plugin_AddDeviceClient,
    Plugin_RemoveDeviceClient,
    Plugin_PerformDeviceConfigurationChange,
    Plugin_AbortDeviceConfigurationChange,
    Plugin_HasProperty,
    Plugin_IsPropertySettable,
    Plugin_GetPropertyDataSize,
    Plugin_GetPropertyData,
    Plugin_SetPropertyData,
    Plugin_StartIO,
    Plugin_StopIO,
    Plugin_GetZeroTimeStamp,
    Plugin_WillDoIOOperation,
    Plugin_BeginIOOperation,
    Plugin_DoIOOperation,
    Plugin_EndIOOperation
};

static AudioServerPlugInDriverInterface* gDriverInterfacePtr = &gDriverInterface;

// ============================================================================
// COM Factory Function — entry point for coreaudiod
// ============================================================================

extern "C" void* PulseAudio_Create(CFAllocatorRef /*allocator*/, CFUUIDRef requestedTypeUUID)
{
    CFUUIDRef audioServerPlugInDriverTypeUID =
        CFUUIDGetConstantUUIDWithBytes(nullptr,
            0x44, 0x3A, 0xBA, 0xB8, 0xE7, 0xB3, 0x49, 0x1A,
            0xB9, 0x85, 0xBE, 0xB9, 0x18, 0x70, 0x30, 0xDB);

    if (!CFEqual(requestedTypeUUID, audioServerPlugInDriverTypeUID)) {
        return nullptr;
    }

    gRefCount = 1;
    return &gDriverInterfacePtr;
}

// ============================================================================
// IUnknown
// ============================================================================

static HRESULT Plugin_QueryInterface(void* /*driver*/, REFIID iid, LPVOID* ppv)
{
    CFUUIDRef interfaceID = CFUUIDCreateFromUUIDBytes(nullptr, iid);
    CFUUIDRef unknownUUID = CFUUIDGetConstantUUIDWithBytes(nullptr,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
    CFUUIDRef pluginUUID = CFUUIDGetConstantUUIDWithBytes(nullptr,
        0xEE, 0xA5, 0x77, 0x3D, 0xCC, 0x43, 0x49, 0xF1,
        0x8E, 0x00, 0x8F, 0x96, 0xE7, 0xD2, 0x3B, 0x17);

    if (CFEqual(interfaceID, unknownUUID) || CFEqual(interfaceID, pluginUUID)) {
        CFRelease(interfaceID);
        gRefCount++;
        *ppv = &gDriverInterfacePtr;
        return S_OK;
    }

    CFRelease(interfaceID);
    *ppv = nullptr;
    return E_NOINTERFACE;
}

static ULONG Plugin_AddRef(void* /*driver*/)
{
    return ++gRefCount;
}

static ULONG Plugin_Release(void* /*driver*/)
{
    UInt32 count = --gRefCount;
    if (count == 0) {
        delete gDevice;
        gDevice = nullptr;
    }
    return count;
}

// ============================================================================
// Plugin lifecycle
// ============================================================================

static OSStatus Plugin_Initialize(AudioServerPlugInDriverRef /*driver*/,
                                  AudioServerPlugInHostRef host)
{
    gHost = host;
    gDevice = new PulseDevice();
    return kAudioHardwareNoError;
}

static OSStatus Plugin_CreateDevice(AudioServerPlugInDriverRef /*driver*/,
                                    CFDictionaryRef /*description*/,
                                    const AudioServerPlugInClientInfo* /*clientInfo*/,
                                    AudioObjectID* outDeviceObjectID)
{
    // We create our device in Initialize, so just return it here
    *outDeviceObjectID = kObjectID_Device;
    return kAudioHardwareNoError;
}

static OSStatus Plugin_DestroyDevice(AudioServerPlugInDriverRef /*driver*/,
                                     AudioObjectID /*objectID*/)
{
    return kAudioHardwareNoError;
}

static OSStatus Plugin_AddDeviceClient(AudioServerPlugInDriverRef /*driver*/,
                                       AudioObjectID /*objectID*/,
                                       const AudioServerPlugInClientInfo* /*clientInfo*/)
{
    return kAudioHardwareNoError;
}

static OSStatus Plugin_RemoveDeviceClient(AudioServerPlugInDriverRef /*driver*/,
                                          AudioObjectID /*objectID*/,
                                          const AudioServerPlugInClientInfo* /*clientInfo*/)
{
    return kAudioHardwareNoError;
}

static OSStatus Plugin_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef /*driver*/,
                                                        AudioObjectID /*objectID*/,
                                                        UInt64 /*changeAction*/,
                                                        void* /*changeInfo*/)
{
    return kAudioHardwareNoError;
}

static OSStatus Plugin_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef /*driver*/,
                                                      AudioObjectID /*objectID*/,
                                                      UInt64 /*changeAction*/,
                                                      void* /*changeInfo*/)
{
    return kAudioHardwareNoError;
}

// ============================================================================
// Plugin-level property handlers
// ============================================================================

static Boolean Plugin_HasProperty(AudioServerPlugInDriverRef /*driver*/,
                                  AudioObjectID objectID,
                                  pid_t /*clientPID*/,
                                  const AudioObjectPropertyAddress* address)
{
    if (objectID == kAudioObjectPlugInObject) {
        switch (address->mSelector) {
            case kAudioObjectPropertyBaseClass:
            case kAudioObjectPropertyClass:
            case kAudioObjectPropertyOwner:
            case kAudioObjectPropertyManufacturer:
            case kAudioObjectPropertyOwnedObjects:
            case kAudioPlugInPropertyDeviceList:
            case kAudioPlugInPropertyTranslateUIDToDevice:
            case kAudioPlugInPropertyResourceBundle:
                return true;
            default:
                return false;
        }
    }

    if (!gDevice) return false;
    return gDevice->HasProperty(objectID, address);
}

static OSStatus Plugin_IsPropertySettable(AudioServerPlugInDriverRef /*driver*/,
                                          AudioObjectID objectID,
                                          pid_t /*clientPID*/,
                                          const AudioObjectPropertyAddress* address,
                                          Boolean* outIsSettable)
{
    if (objectID == kAudioObjectPlugInObject) {
        *outIsSettable = false;
        return kAudioHardwareNoError;
    }

    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->IsPropertySettable(objectID, address, outIsSettable);
}

static OSStatus Plugin_GetPropertyDataSize(AudioServerPlugInDriverRef /*driver*/,
                                           AudioObjectID objectID,
                                           pid_t /*clientPID*/,
                                           const AudioObjectPropertyAddress* address,
                                           UInt32 qualifierDataSize,
                                           const void* qualifierData,
                                           UInt32* outDataSize)
{
    if (objectID == kAudioObjectPlugInObject) {
        switch (address->mSelector) {
            case kAudioObjectPropertyBaseClass:
            case kAudioObjectPropertyClass:
                *outDataSize = sizeof(AudioClassID);
                return kAudioHardwareNoError;
            case kAudioObjectPropertyOwner:
                *outDataSize = sizeof(AudioObjectID);
                return kAudioHardwareNoError;
            case kAudioObjectPropertyManufacturer:
                *outDataSize = sizeof(CFStringRef);
                return kAudioHardwareNoError;
            case kAudioObjectPropertyOwnedObjects:
            case kAudioPlugInPropertyDeviceList:
                *outDataSize = sizeof(AudioObjectID);
                return kAudioHardwareNoError;
            case kAudioPlugInPropertyTranslateUIDToDevice:
                *outDataSize = sizeof(AudioObjectID);
                return kAudioHardwareNoError;
            case kAudioPlugInPropertyResourceBundle:
                *outDataSize = sizeof(CFStringRef);
                return kAudioHardwareNoError;
            default:
                return kAudioHardwareUnknownPropertyError;
        }
    }

    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->GetPropertyDataSize(objectID, address, qualifierDataSize, qualifierData, outDataSize);
}

static OSStatus Plugin_GetPropertyData(AudioServerPlugInDriverRef /*driver*/,
                                       AudioObjectID objectID,
                                       pid_t /*clientPID*/,
                                       const AudioObjectPropertyAddress* address,
                                       UInt32 qualifierDataSize,
                                       const void* qualifierData,
                                       UInt32 inDataSize,
                                       UInt32* outDataSize,
                                       void* outData)
{
    if (objectID == kAudioObjectPlugInObject) {
        switch (address->mSelector) {
            case kAudioObjectPropertyBaseClass:
                *outDataSize = sizeof(AudioClassID);
                *(AudioClassID*)outData = kAudioObjectClassID;
                return kAudioHardwareNoError;

            case kAudioObjectPropertyClass:
                *outDataSize = sizeof(AudioClassID);
                *(AudioClassID*)outData = kAudioPlugInClassID;
                return kAudioHardwareNoError;

            case kAudioObjectPropertyOwner:
                *outDataSize = sizeof(AudioObjectID);
                *(AudioObjectID*)outData = kAudioObjectPlugInObject;
                return kAudioHardwareNoError;

            case kAudioObjectPropertyManufacturer:
                *outDataSize = sizeof(CFStringRef);
                *(CFStringRef*)outData = CFStringCreateWithCString(kCFAllocatorDefault,
                    kDeviceManufacturer, kCFStringEncodingUTF8);
                return kAudioHardwareNoError;

            case kAudioObjectPropertyOwnedObjects:
            case kAudioPlugInPropertyDeviceList:
                *outDataSize = sizeof(AudioObjectID);
                *(AudioObjectID*)outData = kObjectID_Device;
                return kAudioHardwareNoError;

            case kAudioPlugInPropertyTranslateUIDToDevice: {
                CFStringRef uid = *(CFStringRef*)qualifierData;
                if (uid && CFStringCompare(uid, kDeviceUID, 0) == kCFCompareEqualTo) {
                    *(AudioObjectID*)outData = kObjectID_Device;
                } else {
                    *(AudioObjectID*)outData = kAudioObjectUnknown;
                }
                *outDataSize = sizeof(AudioObjectID);
                return kAudioHardwareNoError;
            }

            case kAudioPlugInPropertyResourceBundle:
                *outDataSize = sizeof(CFStringRef);
                *(CFStringRef*)outData = CFSTR("");
                return kAudioHardwareNoError;

            default:
                return kAudioHardwareUnknownPropertyError;
        }
    }

    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->GetPropertyData(objectID, address, qualifierDataSize, qualifierData,
                                    inDataSize, outDataSize, outData);
}

static OSStatus Plugin_SetPropertyData(AudioServerPlugInDriverRef /*driver*/,
                                       AudioObjectID objectID,
                                       pid_t /*clientPID*/,
                                       const AudioObjectPropertyAddress* address,
                                       UInt32 qualifierDataSize,
                                       const void* qualifierData,
                                       UInt32 inDataSize,
                                       const void* inData)
{
    if (objectID == kAudioObjectPlugInObject) {
        return kAudioHardwareUnknownPropertyError;
    }

    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->SetPropertyData(objectID, address, qualifierDataSize, qualifierData,
                                    inDataSize, inData);
}

// ============================================================================
// IO Operations
// ============================================================================

static OSStatus Plugin_StartIO(AudioServerPlugInDriverRef /*driver*/,
                               AudioObjectID /*objectID*/,
                               UInt32 /*clientID*/)
{
    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->StartIO();
}

static OSStatus Plugin_StopIO(AudioServerPlugInDriverRef /*driver*/,
                              AudioObjectID /*objectID*/,
                              UInt32 /*clientID*/)
{
    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->StopIO();
}

static OSStatus Plugin_GetZeroTimeStamp(AudioServerPlugInDriverRef /*driver*/,
                                        AudioObjectID /*objectID*/,
                                        UInt32 /*clientID*/,
                                        Float64* outSampleTime,
                                        UInt64* outHostTime,
                                        UInt64* outSeed)
{
    if (!gDevice) return kAudioHardwareBadObjectError;
    gDevice->GetZeroTimeStamp(outSampleTime, outHostTime, outSeed);
    return kAudioHardwareNoError;
}

static OSStatus Plugin_WillDoIOOperation(AudioServerPlugInDriverRef /*driver*/,
                                         AudioObjectID /*objectID*/,
                                         UInt32 /*clientID*/,
                                         UInt32 operationID,
                                         Boolean* outWillDo,
                                         Boolean* outIsInput)
{
    switch (operationID) {
        case kAudioServerPlugInIOOperationWriteMix:
            *outWillDo  = true;
            *outIsInput = false;
            break;
        case kAudioServerPlugInIOOperationReadInput:
            *outWillDo  = true;
            *outIsInput = true;
            break;
        default:
            *outWillDo  = false;
            *outIsInput = false;
            break;
    }
    return kAudioHardwareNoError;
}

static OSStatus Plugin_BeginIOOperation(AudioServerPlugInDriverRef /*driver*/,
                                        AudioObjectID /*objectID*/,
                                        UInt32 /*clientID*/,
                                        UInt32 /*operationID*/,
                                        UInt32 /*ioBufferFrameSize*/,
                                        const AudioServerPlugInIOCycleInfo* /*ioCycleInfo*/)
{
    return kAudioHardwareNoError;
}

static OSStatus Plugin_DoIOOperation(AudioServerPlugInDriverRef /*driver*/,
                                     AudioObjectID /*objectID*/,
                                     AudioObjectID streamID,
                                     UInt32 /*clientID*/,
                                     UInt32 operationID,
                                     UInt32 ioBufferFrameSize,
                                     const AudioServerPlugInIOCycleInfo* /*ioCycleInfo*/,
                                     void* ioMainBuffer,
                                     void* ioSecondaryBuffer)
{
    if (!gDevice) return kAudioHardwareBadObjectError;
    return gDevice->DoIOOperation(streamID, operationID, ioBufferFrameSize,
                                  (AudioBufferList*)ioMainBuffer,
                                  (AudioBufferList*)ioSecondaryBuffer);
}

static OSStatus Plugin_EndIOOperation(AudioServerPlugInDriverRef /*driver*/,
                                      AudioObjectID /*objectID*/,
                                      UInt32 /*clientID*/,
                                      UInt32 /*operationID*/,
                                      UInt32 /*ioBufferFrameSize*/,
                                      const AudioServerPlugInIOCycleInfo* /*ioCycleInfo*/)
{
    return kAudioHardwareNoError;
}
