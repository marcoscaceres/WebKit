//
// Copyright 2020 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <array>
#include <atomic>
#include <type_traits>
#include <wtf/Compiler.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/ParsingUtilities.h>

namespace WTF {

// A SequenceLock implements lock-free reads. A sequence counter is incremented
// before and after each write, and readers access the counter before and after
// accessing the protected data. If the counter is verified to not change during
// the access, and the sequence counter value was even, then the reader knows
// that the read was race-free and valid.
//
// Works best with types T that are initialized to zeros.
//
// The memory reads and writes protected by this lock must use the provided
// `load()` and `store()` functions.
// As an implementation detail, the protected data must be an array of
// `std::atomic<uint64_t>`. This is to comply with the C++ standard, which
// considers data races on non-atomic objects to be undefined behavior. See "Can
// Seqlocks Get Along With Programming Language Memory Models?"[1] by Hans J.
// Boehm for more details.
//
// [1] https://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf
template<typename T>
class SequenceLocked {
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<T>);

    // Copy T from store and return it, protected as a read-side
    // critical section of the sequence lock.
    T load() const
    {
        T result;
        for (;;) {
            // Acquire barrier ensures that no loads are reordered
            // above the first load of the sequence counter.
            uint64_t versionBefore = m_version.load(std::memory_order_acquire);
            if ((versionBefore & 1) == 1) [[unlikely]]
                continue;
            relaxedCopyFromAtomic(asMutableByteSpan(result), std::span { m_storage });
            // Another acquire fence ensures that the load of 'm_version' below is
            // strictly ordered after the RelaxedCopyToAtomic call above.
            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t versionAfter = m_version.load(std::memory_order_relaxed);
            if (versionBefore == versionAfter) [[likely]]
                break;
        }
        return result;
    }

    // Copy value to store as a write-side critical section
    // of the sequence lock. Any concurrent readers will be forced to retry
    // until they get a read that does not conflict with this write.
    //
    // This call must be externally synchronized against other calls to write,
    // but may proceed concurrently with reads.
    void store(const T& value)
    {
        // We can use relaxed instructions to increment the counter since we
        // are extenally synchronized. The std::atomic_thread_fence below
        // ensures that the counter updates don't get interleaved with the
        // copy to the data.
        uint64_t version = m_version.load(std::memory_order_relaxed);
        m_version.store(version + 1, std::memory_order_relaxed);

        // We put a release fence between update to m_version and writes to shared data.
        // Thus all stores to shared data are effectively release operations and
        // update to m_version above cannot be re-ordered past any of them. Note that
        // this barrier is not for the fetch_add above. A release barrier for the
        // fetch_add would be before it, not after.
        std::atomic_thread_fence(std::memory_order_release);
        relaxedCopyToAtomic(std::span { m_storage }, asByteSpan(value));
        // "Release" semantics ensure that none of the writes done by
        // relaxedCopyToAtomic() can be reordered after the following modification.
        m_version.store(version + 2, std::memory_order_release);
    }

private:
    // Perform the equivalent of "memcpy(dst, src, size)", but using relaxed
    // atomics.
    static constexpr size_t storageSize = (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(std::atomic<uint64_t>);
    static void relaxedCopyFromAtomic(std::span<uint8_t> destination, std::span<const std::atomic<uint64_t>, storageSize> source)
    {
        size_t sourceIndex = 0;
        while (destination.size() >= sizeof(uint64_t)) {
            uint64_t word = source[sourceIndex++].load(std::memory_order_relaxed);
            memcpySpan(consumeSpan(destination, sizeof(word)), asByteSpan(word));
        }
        ASSERT(destination.empty());
    }

    // Perform the equivalent of "memcpy(dst, src, size)", but using relaxed
    // atomics.
    static void relaxedCopyToAtomic(std::span<std::atomic<uint64_t>, storageSize> destination, std::span<const uint8_t> source)
    {
        size_t destinationIndex = 0;
        while (source.size() >= sizeof(uint64_t)) {
            uint64_t word;
            memcpySpan(asMutableByteSpan(word), consumeSpan(source, sizeof(word)));
            destination[destinationIndex++].store(word, std::memory_order_relaxed);
        }
        ASSERT(source.empty());
    }

    std::atomic<uint64_t> m_version { 0 };
    using Storage = std::array<std::atomic<uint64_t>, storageSize>;
    Storage m_storage { };
};

}

using WTF::SequenceLocked;
