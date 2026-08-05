#include "core/FrameBuffer.h"

#include "core/FramePool.h"

#include <utility>

namespace dfu {

FrameBuffer::FrameBuffer(FramePool* pool, std::vector<uint8_t>&& storage, int width, int height,
                         int stride)
    : m_pool(pool)
    , m_storage(std::move(storage))
    , m_width(width)
    , m_height(height)
    , m_stride(stride)
{
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
    : m_pool(other.m_pool)
    , m_storage(std::move(other.m_storage))
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_stride(other.m_stride)
    , m_frameIndex(other.m_frameIndex)
    , m_ptsSeconds(other.m_ptsSeconds)
    , m_synthetic(other.m_synthetic)
{
    other.m_pool = nullptr;
    other.m_storage.clear();
    other.m_width = 0;
    other.m_height = 0;
    other.m_stride = 0;
    other.m_frameIndex = -1;
}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();

    m_pool = other.m_pool;
    m_storage = std::move(other.m_storage);
    m_width = other.m_width;
    m_height = other.m_height;
    m_stride = other.m_stride;
    m_frameIndex = other.m_frameIndex;
    m_ptsSeconds = other.m_ptsSeconds;
    m_synthetic = other.m_synthetic;

    other.m_pool = nullptr;
    other.m_storage.clear();
    other.m_width = 0;
    other.m_height = 0;
    other.m_stride = 0;
    other.m_frameIndex = -1;

    return *this;
}

FrameBuffer::~FrameBuffer()
{
    reset();
}

void FrameBuffer::reset()
{
    if (m_pool && !m_storage.empty()) {
        m_pool->release(std::move(m_storage));
    }
    m_storage.clear();
    m_pool = nullptr;
    m_width = 0;
    m_height = 0;
    m_stride = 0;
    m_frameIndex = -1;
    m_ptsSeconds = 0.0;
    m_synthetic = false;
}

} // namespace dfu
