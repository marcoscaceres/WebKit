/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(WEB_AUDIO) && USE(MEDIATOOLBOX)

#include "Test.h"
#include <WebCore/CAAudioStreamDescription.h>
#include <WebCore/CARingBuffer.h>
#include <atomic>
#include <wtf/MainThread.h>
#include <wtf/Scope.h>

using namespace WebCore;

namespace WebCore {
inline std::ostream& operator<<(std::ostream& os, const WebCore::CARingBuffer::TimeBounds& value)
{
    return os << "{ " << value.startFrame << ", " << value.endFrame << " }";
}
}
namespace TestWebKitAPI {

class CARingBufferTest : public testing::Test {
public:

    virtual void SetUp()
    {
        WTF::initializeMainThread();
    }

    // CAAudioStreamDescription(double sampleRate, UInt32 numChannels, PCMFormat format, IsInterleaved isInterleaved, size_t capacity)
    void setup(double sampleRate, UInt32 numChannels, CAAudioStreamDescription::PCMFormat format, bool isInterleaved, size_t capacity)
    {
        m_description = CAAudioStreamDescription(sampleRate, numChannels, format, isInterleaved ? CAAudioStreamDescription::IsInterleaved::Yes : CAAudioStreamDescription::IsInterleaved::No);
        m_capacity = capacity;
        size_t listSize = offsetof(AudioBufferList, mBuffers) + (sizeof(AudioBuffer) * std::max<uint32_t>(1, m_description->numberOfChannelStreams()));
        m_bufferList = std::unique_ptr<AudioBufferList>(static_cast<AudioBufferList*>(::operator new (listSize)));
        m_ringBuffer = InProcessCARingBuffer::allocate(*m_description, capacity);
    }

    template<typename T, size_t ArraySize>
    void setListDataBuffer(std::array<T, ArraySize>& buffer)
    {
        size_t bufferCount = m_description->numberOfChannelStreams();
        size_t channelCount = m_description->numberOfInterleavedChannels();
        size_t bytesPerChannel = buffer.size() * m_description->bytesPerFrame();

        m_bufferList->mNumberBuffers = bufferCount;
        auto bufferBytes = asMutableByteSpan(buffer);
        for (unsigned i = 0; i < bufferCount; ++i) {
            m_bufferList->mBuffers[i].mNumberChannels = channelCount;
            m_bufferList->mBuffers[i].mDataByteSize = bytesPerChannel;
            m_bufferList->mBuffers[i].mData = bufferBytes.data();
            if (!bufferBytes.empty())
                bufferBytes = bufferBytes.subspan(bytesPerChannel);
        }
    }

    const CAAudioStreamDescription& description() const { return *m_description; }
    AudioBufferList& bufferList() const { return *m_bufferList.get(); }
    InProcessCARingBuffer& ringBuffer() const { return *m_ringBuffer.get(); }
    size_t capacity() const { return m_capacity; }

private:

    std::unique_ptr<AudioBufferList> m_bufferList;
    std::unique_ptr<InProcessCARingBuffer> m_ringBuffer;
    std::optional<CAAudioStreamDescription> m_description;
    size_t m_capacity = { 0 };
};

static CARingBuffer::TimeBounds makeBounds(uint64_t start, uint64_t end)
{
    return { start, end };
}

TEST_F(CARingBufferTest, Basics)
{
    const size_t capacity = 32;

    setup(44100, 1, CAAudioStreamDescription::PCMFormat::Float32, true, capacity);

    auto fetchBounds = ringBuffer().getFetchTimeBounds();
    EXPECT_EQ(fetchBounds, makeBounds(0, 0));

    std::array<float, capacity> sourceBuffer;
    for (size_t i = 0; i < capacity; i++)
        sourceBuffer[i] = i + 0.5;

    setListDataBuffer(sourceBuffer);

    // Fill the first half of the buffer ...
    uint64_t sampleCount = capacity / 2;
    CARingBuffer::Error err = ringBuffer().store(&bufferList(), sampleCount, 0);
    EXPECT_EQ(err, CARingBuffer::Error::Ok);

    fetchBounds = ringBuffer().getFetchTimeBounds();
    EXPECT_EQ(fetchBounds, makeBounds(0, sampleCount));

    std::array<float, capacity> scratchBuffer;
    setListDataBuffer(scratchBuffer);

    ringBuffer().fetch(&bufferList(), sampleCount, 0);
    EXPECT_TRUE(!memcmp(sourceBuffer.data(), scratchBuffer.data(), sampleCount * description().sampleWordSize()));

    // ... and the second half.
    err = ringBuffer().store(&bufferList(), capacity / 2, capacity / 2);
    EXPECT_EQ(err, CARingBuffer::Error::Ok);

    fetchBounds = ringBuffer().getFetchTimeBounds();
    EXPECT_EQ(fetchBounds, makeBounds(0, capacity));

    zeroSpan(std::span { scratchBuffer }.first(sampleCount));
    ringBuffer().fetch(&bufferList(), sampleCount, 0);
    EXPECT_TRUE(!memcmp(sourceBuffer.data(), scratchBuffer.data(), sampleCount * description().sampleWordSize()));

    // Force the buffer to wrap around
    err = ringBuffer().store(&bufferList(), capacity, capacity - 1);
    EXPECT_EQ(err, CARingBuffer::Error::Ok);

    fetchBounds = ringBuffer().getFetchTimeBounds();
    EXPECT_EQ(fetchBounds, makeBounds(capacity - 1, capacity - 1 + capacity));

    // Make sure it returns an error when asked to store too much ...
    err = ringBuffer().store(&bufferList(), capacity * 3, capacity / 2);
    EXPECT_EQ(err, CARingBuffer::Error::TooMuch);

    // ... and doesn't modify the buffer
    fetchBounds = ringBuffer().getFetchTimeBounds();
    EXPECT_EQ(fetchBounds, makeBounds(capacity - 1, capacity - 1 + capacity));
}

TEST_F(CARingBufferTest, SmallBufferListForFetch)
{
    const int capacity = 32;
    const int halfCapacity = capacity / 2;
    setup(44100, 1, CAAudioStreamDescription::PCMFormat::Float32, true, capacity);

    std::array<float, capacity> sourceBuffer;
    for (int i = 0; i < capacity; i++)
        sourceBuffer[i] = i + 0.5;
    setListDataBuffer(sourceBuffer);
    CARingBuffer::Error err = ringBuffer().store(&bufferList(), capacity, 0);
    EXPECT_EQ(err, CARingBuffer::Error::Ok);

    std::array<float, halfCapacity> destinationBuffer;
    setListDataBuffer(destinationBuffer);
    int bufferCount = bufferList().mNumberBuffers;
    EXPECT_GE(bufferCount, 1);
    size_t listDataByteSizeBeforeFetch = bufferList().mBuffers[0].mDataByteSize;

    ringBuffer().fetch(&bufferList(), capacity, 0);
    EXPECT_LE(bufferList().mBuffers[0].mDataByteSize, listDataByteSizeBeforeFetch);
}

TEST_F(CARingBufferTest, FetchTimeBoundsInMiddleCorrect)
{
    const size_t capacity = 32;
    setup(44100, 1, CAAudioStreamDescription::PCMFormat::Float32, true, capacity);
    std::array<float, capacity> sourceBuffer = { };
    setListDataBuffer(sourceBuffer);
    EXPECT_EQ(makeBounds(0u, 0u), ringBuffer().getFetchTimeBounds());

    ringBuffer().store(&bufferList(), 32, 55);
    EXPECT_EQ(makeBounds(55u, 55u + 32u), ringBuffer().getFetchTimeBounds());

    ringBuffer().store(&bufferList(), 5, 57);
    EXPECT_EQ(makeBounds(55u, 57u + 5u), ringBuffer().getFetchTimeBounds());

    ringBuffer().store(&bufferList(), 32, 60);
    EXPECT_EQ(makeBounds(60u, 60u + 32u), ringBuffer().getFetchTimeBounds());
}

TEST_F(CARingBufferTest, FetchTimeBoundsInvalid)
{
    const size_t capacity = 32;
    setup(44100, 1, CAAudioStreamDescription::PCMFormat::Float32, true, capacity);
    std::array<float, capacity> sourceBuffer = { };
    setListDataBuffer(sourceBuffer);
    EXPECT_EQ(makeBounds(0u, 0u), ringBuffer().getFetchTimeBounds());

    ringBuffer().store(&bufferList(), 8, 0);
    EXPECT_EQ(makeBounds(0u, 8u), ringBuffer().getFetchTimeBounds());

    auto& boundsBuffer = ringBuffer().timeBoundsBufferForTesting();
    boundsBuffer.store({ 1u, 3u });
    EXPECT_EQ(makeBounds(1u, 3u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 3u, 3u });
    EXPECT_EQ(makeBounds(3u, 3u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 4u, 3u });
    EXPECT_EQ(makeBounds(4u, 4u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 5u, 5u + 32u });
    EXPECT_EQ(makeBounds(5u, 5u + 32u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 5u, 5u + 33u });
    EXPECT_EQ(makeBounds(5u, 5u + 32u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 5u, 5u + 34u });
    EXPECT_EQ(makeBounds(5u, 5u + 32u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ 5u, std::numeric_limits<uint64_t>::max() - 1u });
    EXPECT_EQ(makeBounds(5u, 5u + 32u), ringBuffer().getFetchTimeBounds());

    boundsBuffer.store({ std::numeric_limits<uint64_t>::max() - 1u, std::numeric_limits<uint64_t>::max() });
    EXPECT_EQ(makeBounds(std::numeric_limits<uint64_t>::max() - 32u, std::numeric_limits<uint64_t>::max()), ringBuffer().getFetchTimeBounds());
}

// FIXME when rdar://151622059 is resolved.
#if PLATFORM(IOS)
TEST_F(CARingBufferTest, DISABLED_FetchTimeBoundsConsistent)
#else
TEST_F(CARingBufferTest, FetchTimeBoundsConsistent)
#endif
{
    const size_t capacity = 32;
    setup(44100, 1, CAAudioStreamDescription::PCMFormat::Float32, true, capacity);
    std::array<float, capacity> sourceBuffer = { };
    setListDataBuffer(sourceBuffer);

    std::atomic<bool> done = false;
    auto thread = Thread::create("FetchTimeBoundsConsistent test"_s, [&] {
        uint64_t i = 0;
        while (!done) {
            ringBuffer().store(&bufferList(), 1, i);
            i += 1;
        }
    }, ThreadType::Audio, Thread::QOS::UserInteractive);
    auto threadCleanup = makeScopeExit([&] {
        thread->waitForCompletion();
    });
    CARingBuffer::TimeBounds maxBounds { };
#if !defined(NDEBUG)
    for (int i = 0; i < 100000; ++i) {
#else
    for (int i = 0; i < 10000000; ++i) {
#endif
        auto fetchBounds = ringBuffer().getFetchTimeBounds();
        EXPECT_LE(fetchBounds.startFrame, fetchBounds.endFrame);
        EXPECT_LE(maxBounds.startFrame, fetchBounds.startFrame);
        EXPECT_LE(maxBounds.endFrame, fetchBounds.endFrame);
        maxBounds = fetchBounds;
    }
    done = true;
}

template <typename type>
class MixingTest {
public:
    static void run(CARingBufferTest& test)
    {
        const int sampleCount = 441;

        CAAudioStreamDescription::PCMFormat format;
        if (std::is_same<type, float>::value)
            format = CAAudioStreamDescription::PCMFormat::Float32;
        else if (std::is_same<type, double>::value)
            format = CAAudioStreamDescription::PCMFormat::Float64;
        else if (std::is_same<type, int32_t>::value)
            format = CAAudioStreamDescription::PCMFormat::Int32;
        else if (std::is_same<type, int16_t>::value)
            format = CAAudioStreamDescription::PCMFormat::Int16;
        else
            ASSERT_NOT_REACHED();

        test.setup(44100, 1, format, true, sampleCount);

        std::array<type, sampleCount> referenceBuffer;
        std::array<type, sampleCount> sourceBuffer;
        std::array<type, sampleCount> readBuffer;

        for (int i = 0; i < sampleCount; i++) {
            sourceBuffer[i] = i * 0.5;
            referenceBuffer[i] = sourceBuffer[i];
        }

        test.setListDataBuffer(sourceBuffer);
        CARingBuffer::Error err = test.ringBuffer().store(&test.bufferList(), sampleCount, 0);
        EXPECT_EQ(err, CARingBuffer::Error::Ok);

        readBuffer.fill(0);
        test.setListDataBuffer(readBuffer);
        auto mixFetchMode = CARingBuffer::fetchModeForMixing(test.description().format());
        test.ringBuffer().fetch(&test.bufferList(), sampleCount, 0, mixFetchMode);

        for (int i = 0; i < sampleCount; i++)
            EXPECT_EQ(readBuffer[i], referenceBuffer[i]) << "Ring buffer value differs at index " << i;

        test.ringBuffer().fetch(&test.bufferList(), sampleCount, 0, mixFetchMode);
        test.ringBuffer().fetch(&test.bufferList(), sampleCount, 0, mixFetchMode);
        test.ringBuffer().fetch(&test.bufferList(), sampleCount, 0, mixFetchMode);

        for (int i = 0; i < sampleCount; i++)
            referenceBuffer[i] += sourceBuffer[i] * 3;

        for (int i = 0; i < sampleCount; i++)
            EXPECT_EQ(readBuffer[i], referenceBuffer[i]) << "Ring buffer value differs at index " << i;

        test.ringBuffer().fetch(&test.bufferList(), sampleCount, 0, CARingBuffer::FetchMode::Copy);
        err = test.ringBuffer().store(&test.bufferList(), sampleCount, sampleCount);
        EXPECT_EQ(err, CARingBuffer::Error::Ok);

        test.ringBuffer().fetch(&test.bufferList(), sampleCount, sampleCount, CARingBuffer::FetchMode::Copy);
        test.ringBuffer().fetch(&test.bufferList(), sampleCount, sampleCount, mixFetchMode);
        test.ringBuffer().fetch(&test.bufferList(), sampleCount, sampleCount, mixFetchMode);

        for (int i = 0; i < sampleCount; i++)
            referenceBuffer[i] = sourceBuffer[i] * 3;

        for (int i = 0; i < sampleCount; i++)
            EXPECT_EQ(readBuffer[i], referenceBuffer[i]) << "Ring buffer value differs at index " << i;
    }
};

TEST_F(CARingBufferTest, FloatMixing)
{
    MixingTest<float>::run(*this);
}

TEST_F(CARingBufferTest, DoubleMixing)
{
    MixingTest<double>::run(*this);
}

TEST_F(CARingBufferTest, Int32Mixing)
{
    MixingTest<int32_t>::run(*this);
}

TEST_F(CARingBufferTest, Int16Mixing)
{
    MixingTest<int16_t>::run(*this);
}

}

#endif
