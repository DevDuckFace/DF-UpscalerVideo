#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dfu {

class FramePool;

// Move-only handle to one frame's pixel data. Never copied. Storage is owned
// by a FramePool and returns to it when the handle dies, so a running job does
// not allocate per frame -- a 4K RGB24 frame is ~25 MB, and at even 3 fps that
// would be 75 MB/s of churn.
class FrameBuffer
{
public:
    FrameBuffer() = default;
    ~FrameBuffer();

    FrameBuffer(FrameBuffer&& other) noexcept;
    FrameBuffer& operator=(FrameBuffer&& other) noexcept;

    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    uint8_t* data() noexcept { return m_storage.data(); }
    const uint8_t* data() const noexcept { return m_storage.data(); }

    std::size_t sizeBytes() const noexcept { return m_storage.size(); }

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    int stride() const noexcept { return m_stride; }

    int64_t frameIndex() const noexcept { return m_frameIndex; }
    double ptsSeconds() const noexcept { return m_ptsSeconds; }
    bool isSynthetic() const noexcept { return m_synthetic; }

    void setFrameIndex(int64_t index) noexcept { m_frameIndex = index; }
    void setPtsSeconds(double pts) noexcept { m_ptsSeconds = pts; }
    void setSynthetic(bool synthetic) noexcept { m_synthetic = synthetic; }

    bool valid() const noexcept { return !m_storage.empty(); }

    // Hands the storage back to the pool early. The handle becomes invalid.
    void reset();

private:
    friend class FramePool;
    FrameBuffer(FramePool* pool, std::vector<uint8_t>&& storage, int width, int height, int stride);

    FramePool* m_pool = nullptr;
    std::vector<uint8_t> m_storage;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    int64_t m_frameIndex = -1;
    double m_ptsSeconds = 0.0;
    bool m_synthetic = false;
};

} // namespace dfu
