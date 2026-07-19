#pragma once

#include <CoreAudio/AudioServerPlugIn.h>

// Entry point — the COM interface factory function
extern "C" void* PulseAudio_Create(CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID);
