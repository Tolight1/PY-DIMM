#include "FrameQueue.h"

#include <QMutexLocker>

FrameQueue::FrameQueue(std::size_t capacity)
    : capacity_(capacity)
{
}

void FrameQueue::push(CameraFrame frame)
{
    QMutexLocker lock(&mutex_);
    if (closed_)
        return;
    if (frames_.size() >= capacity_) {
        // Discard the oldest frame to keep the queue bounded.
        frames_.pop_front();
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    frames_.push_back(std::move(frame));
}

bool FrameQueue::tryPopOldest(CameraFrame &frame)
{
    QMutexLocker lock(&mutex_);
    if (frames_.empty())
        return false;
    frame = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

bool FrameQueue::tryPopLatest(CameraFrame &frame)
{
    QMutexLocker lock(&mutex_);
    if (frames_.empty())
        return false;
    // The newest frame is at the back. Return it and drop all older queued
    // frames so measurement never processes stale images.
    frame = std::move(frames_.back());
    frames_.clear();
    return true;
}

void FrameQueue::close()
{
    QMutexLocker lock(&mutex_);
    closed_ = true;
}

void FrameQueue::reopen()
{
    QMutexLocker lock(&mutex_);
    closed_ = false;
    frames_.clear();
    dropped_.store(0, std::memory_order_relaxed);
}

void FrameQueue::clear()
{
    QMutexLocker lock(&mutex_);
    frames_.clear();
}

std::size_t FrameQueue::size() const
{
    QMutexLocker lock(&mutex_);
    return frames_.size();
}

std::uint64_t FrameQueue::droppedCount() const
{
    return dropped_.load(std::memory_order_relaxed);
}
