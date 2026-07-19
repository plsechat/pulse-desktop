#include "ring-buffer.h"
#include <algorithm>

RingBuffer::RingBuffer()
    : mBuffer(nullptr)
    , mCapacityFrames(0)
    , mBytesPerFrame(0)
    , mChannels(0)
    , mWriteHead(0)
    , mReadHead(0)
{
}

RingBuffer::~RingBuffer()
{
    delete[] mBuffer;
}

void RingBuffer::Initialize(UInt32 capacityFrames, UInt32 bytesPerFrame)
{
    delete[] mBuffer;

    mCapacityFrames = capacityFrames;
    mBytesPerFrame  = bytesPerFrame;
    mChannels       = bytesPerFrame / sizeof(float);

    // Allocate buffer in samples (frames * channels)
    mBuffer = new float[mCapacityFrames * mChannels];
    std::memset(mBuffer, 0, mCapacityFrames * mChannels * sizeof(float));

    mWriteHead.store(0, std::memory_order_relaxed);
    mReadHead.store(0, std::memory_order_relaxed);
}

void RingBuffer::Reset()
{
    if (mBuffer) {
        std::memset(mBuffer, 0, mCapacityFrames * mChannels * sizeof(float));
    }
    mWriteHead.store(0, std::memory_order_release);
    mReadHead.store(0, std::memory_order_release);
}

UInt32 RingBuffer::Store(const float* src, UInt32 numFrames)
{
    if (!mBuffer || !src || numFrames == 0) return 0;

    UInt64 writePos = mWriteHead.load(std::memory_order_relaxed);
    UInt64 readPos  = mReadHead.load(std::memory_order_acquire);

    // Don't overwrite unread data — cap at available space
    UInt64 used      = writePos - readPos;
    UInt32 available = (used < mCapacityFrames) ? (mCapacityFrames - (UInt32)used) : 0;
    UInt32 toWrite   = std::min(numFrames, available);

    if (toWrite == 0) return 0;

    UInt32 writeIndex = (UInt32)(writePos % mCapacityFrames);
    UInt32 samplesPerFrame = mChannels;

    // First chunk: from writeIndex to end of buffer (or less)
    UInt32 firstChunk = std::min(toWrite, mCapacityFrames - writeIndex);
    std::memcpy(
        mBuffer + (writeIndex * samplesPerFrame),
        src,
        firstChunk * samplesPerFrame * sizeof(float)
    );

    // Second chunk: wrap around to beginning
    if (toWrite > firstChunk) {
        UInt32 secondChunk = toWrite - firstChunk;
        std::memcpy(
            mBuffer,
            src + (firstChunk * samplesPerFrame),
            secondChunk * samplesPerFrame * sizeof(float)
        );
    }

    mWriteHead.store(writePos + toWrite, std::memory_order_release);
    return toWrite;
}

UInt32 RingBuffer::Fetch(float* dst, UInt32 numFrames)
{
    if (!mBuffer || !dst || numFrames == 0) {
        if (dst && numFrames > 0) {
            std::memset(dst, 0, numFrames * mChannels * sizeof(float));
        }
        return 0;
    }

    UInt64 writePos = mWriteHead.load(std::memory_order_acquire);
    UInt64 readPos  = mReadHead.load(std::memory_order_relaxed);

    UInt64 available64 = writePos - readPos;
    UInt32 available   = (UInt32)std::min(available64, (UInt64)mCapacityFrames);
    UInt32 toRead      = std::min(numFrames, available);

    UInt32 readIndex = (UInt32)(readPos % mCapacityFrames);
    UInt32 samplesPerFrame = mChannels;

    if (toRead > 0) {
        // First chunk: from readIndex to end of buffer
        UInt32 firstChunk = std::min(toRead, mCapacityFrames - readIndex);
        std::memcpy(
            dst,
            mBuffer + (readIndex * samplesPerFrame),
            firstChunk * samplesPerFrame * sizeof(float)
        );

        // Second chunk: wrap around
        if (toRead > firstChunk) {
            UInt32 secondChunk = toRead - firstChunk;
            std::memcpy(
                dst + (firstChunk * samplesPerFrame),
                mBuffer,
                secondChunk * samplesPerFrame * sizeof(float)
            );
        }
    }

    // Fill remaining with silence
    if (toRead < numFrames) {
        std::memset(
            dst + (toRead * samplesPerFrame),
            0,
            (numFrames - toRead) * samplesPerFrame * sizeof(float)
        );
    }

    mReadHead.store(readPos + toRead, std::memory_order_release);
    return toRead;
}

UInt32 RingBuffer::AvailableFrames() const
{
    UInt64 writePos = mWriteHead.load(std::memory_order_acquire);
    UInt64 readPos  = mReadHead.load(std::memory_order_acquire);
    UInt64 avail    = writePos - readPos;
    return (UInt32)std::min(avail, (UInt64)mCapacityFrames);
}
