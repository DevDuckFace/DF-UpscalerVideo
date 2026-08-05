#pragma once

#include "core/FrameBuffer.h"

#include <cstddef>
#include <mutex>
#include <vector>

namespace dfu {

// Recycles frame storage. Thread-safe: the decoder, the processor and the
// encoder all acquire and release from their own threads.
class FramePool
{
public:
    FramePool(int width, int height, std::size_t softLimit);

    // Never blocks. Reuses a free block when one is available, otherwise
    // allocates. Backpressure is the ring buffers' job, not the pool's.
    FrameBuffer acquire();

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    int stride() const noexcept { return m_stride; }
    std::size_t frameBytes() const noexcept { return m_frameBytes; }

    std::size_t freeCount() const;
    std::size_t allocatedCount() const;

private:
    friend class FrameBuffer;
    void release(std::vector<uint8_t>&& storage);

    mutable std::mutex m_mutex;
    std::vector<std::vector<uint8_t>> m_free;
    int m_width;
    int m_height;
    int m_stride;
    std::size_t m_frameBytes;
    // Blocks beyond this are dropped instead of pooled, so a transient burst
    // does not pin memory for the rest of the job.
    std::size_t m_softLimit;
    std::size_t m_allocated = 0;
};

} // namespace dfu
