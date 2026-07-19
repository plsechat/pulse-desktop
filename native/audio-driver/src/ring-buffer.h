#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include "types.h"

// Lock-free ring buffer for audio loopback.
// Output stream calls Store() to write audio data.
// Input stream calls Fetch() to read it back.
// Uses atomic frame counters for thread safety without locks.
class RingBuffer {
public:
    RingBuffer();
    ~RingBuffer();

    // Initialize with the given capacity in frames.
    void Initialize(UInt32 capacityFrames, UInt32 bytesPerFrame);

    // Reset the buffer (clear all data and counters).
    void Reset();

    // Store frames from the output stream into the ring buffer.
    // Returns the number of frames actually stored.
    UInt32 Store(const float* src, UInt32 numFrames);

    // Fetch frames from the ring buffer for the input stream.
    // If not enough data is available, fills with silence.
    // Returns the number of frames actually fetched (non-silent).
    UInt32 Fetch(float* dst, UInt32 numFrames);

    // Get the number of frames currently available for reading.
    UInt32 AvailableFrames() const;

private:
    float*              mBuffer;
    UInt32              mCapacityFrames;
    UInt32              mBytesPerFrame;
    UInt32              mChannels;
    std::atomic<UInt64> mWriteHead;  // total frames written (monotonic)
    std::atomic<UInt64> mReadHead;   // total frames read (monotonic)
};
