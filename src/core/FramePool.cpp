#include "core/FramePool.h"

#include <utility>

namespace dfu {

FramePool::FramePool(int width, int height, std::size_t softLimit)
    : m_width(width)
    , m_height(height)
    , m_stride(width * 3) // rgb24, tightly packed: this is what ffmpeg emits
    , m_frameBytes(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u)
    , m_softLimit(softLimit)
{
}

FrameBuffer FramePool::acquire()
{
    std::vector<uint8_t> storage;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_free.empty()) {
            storage = std::move(m_free.back());
            m_free.pop_back();
        } else {
            ++m_allocated;
        }
    }

    if (storage.size() != m_frameBytes) {
        storage.assign(m_frameBytes, 0);
    }

    return FrameBuffer(this, std::move(storage), m_width, m_height, m_stride);
}

void FramePool::release(std::vector<uint8_t>&& storage)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_free.size() >= m_softLimit) {
        // Let it go rather than hold memory the job no longer needs.
        if (m_allocated > 0) {
            --m_allocated;
        }
        return;
    }
    m_free.push_back(std::move(storage));
}

std::size_t FramePool::freeCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_free.size();
}

std::size_t FramePool::allocatedCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocated;
}

} // namespace dfu
