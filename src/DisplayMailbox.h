#pragma once

#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QMutex>

// Latest-only mailbox for display. Each publish replaces the previous
// snapshot; the GUI timer takes a clone/immutable snapshot and immediately
// releases the lock. This prevents GUI backlog and never steals measurement
// frames from FrameQueue.
struct DisplaySnapshot {
    cv::Mat fullFrameMono8;
    DisplayOverlay overlay;
    CameraStatistics cameraStats;
    MeasurementResult latestResult;
    std::uint64_t sequence = 0;
};

class DisplayMailbox final {
public:
    void publish(DisplaySnapshot snapshot);
    // Merge an AOI camera frame into the latest full-frame preview. The
    // preview remains full-frame sized; pixels outside sourceRect retain the
    // most recent full-frame image until another full-frame frame arrives.
    void publishAoiPatch(const cv::Mat &aoiMono8,
                        const QRect &sourceRect,
                        const QSize &fullFrameSize,
                        DisplaySnapshot snapshot);
    bool tryTake(DisplaySnapshot &snapshot) const;
    void clear();

private:
    mutable QMutex mutex_;
    DisplaySnapshot snapshot_;
    bool hasSnapshot_ = false;
};
