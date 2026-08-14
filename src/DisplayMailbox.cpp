#include "DisplayMailbox.h"

#include <QMutexLocker>

void DisplayMailbox::publish(DisplaySnapshot snapshot)
{
    QMutexLocker lock(&mutex_);
    if (hasSnapshot_) {
        const bool isCameraUpdate = !snapshot.fullFrameMono8.empty();
        // CameraWorker and MeasurementWorker publish different fields at
        // different rates. Merge rather than letting an empty field from one
        // producer erase the latest image from the other producer.
        if (snapshot.fullFrameMono8.empty())
            snapshot.fullFrameMono8 = snapshot_.fullFrameMono8;
        if (snapshot.roiAMono8.empty())
            snapshot.roiAMono8 = snapshot_.roiAMono8;
        if (snapshot.roiBMono8.empty())
            snapshot.roiBMono8 = snapshot_.roiBMono8;
        if (isCameraUpdate && !snapshot.overlay.hasRois &&
            snapshot_.overlay.hasRois)
            snapshot.overlay = snapshot_.overlay;
        if (snapshot.latestResult.sequence == 0)
            snapshot.latestResult = snapshot_.latestResult;
        if (snapshot.cameraStats.receivedFrames == 0)
            snapshot.cameraStats = snapshot_.cameraStats;
        if (snapshot.sequence == 0)
            snapshot.sequence = snapshot_.sequence;
    }
    // The snapshot may reference a worker-owned cv::Mat. Deep-copy all image
    // buffers so the mailbox owns its copy and no worker buffer can be
    // released while the GUI still displays it.
    if (!snapshot.fullFrameMono8.empty())
        snapshot.fullFrameMono8 = snapshot.fullFrameMono8.clone();
    if (!snapshot.roiAMono8.empty())
        snapshot.roiAMono8 = snapshot.roiAMono8.clone();
    if (!snapshot.roiBMono8.empty())
        snapshot.roiBMono8 = snapshot.roiBMono8.clone();
    snapshot_ = std::move(snapshot);
    hasSnapshot_ = true;
}

bool DisplayMailbox::tryTake(DisplaySnapshot &snapshot) const
{
    QMutexLocker lock(&mutex_);
    if (!hasSnapshot_)
        return false;
    // Non-consuming copy: the image buffers are shared through cv::Mat's
    // reference counting, so the GUI may render this snapshot even while the
    // next publish replaces the mailbox contents.
    snapshot = snapshot_;
    return true;
}

void DisplayMailbox::clear()
{
    QMutexLocker lock(&mutex_);
    snapshot_ = DisplaySnapshot{};
    hasSnapshot_ = false;
}
