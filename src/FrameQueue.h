#pragma once

#include "CameraTypes.h"

#include <QMutex>

#include <atomic>
#include <deque>

// Bounded FIFO for measurement frames. Fixed capacity of 8; when full, the
// oldest frame is discarded and an atomic drop counter is incremented. The
// queue never grows without bound.
class FrameQueue final {
public:
    explicit FrameQueue(std::size_t capacity = 8);

    void push(CameraFrame frame);
    bool tryPopOldest(CameraFrame &frame);
    bool tryPopLatest(CameraFrame &frame);
    void close();
    void reopen();
    void clear();
    std::size_t size() const;
    std::uint64_t droppedCount() const;

private:
    const std::size_t capacity_;
    mutable QMutex mutex_;
    std::deque<CameraFrame> frames_;
    bool closed_ = false;
    std::atomic<std::uint64_t> dropped_{0};
};
