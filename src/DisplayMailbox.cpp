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
        if (isCameraUpdate && !snapshot.overlay.hasCentroids &&
            snapshot_.overlay.hasCentroids)
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
    snapshot_ = std::move(snapshot);
    hasSnapshot_ = true;
}

void DisplayMailbox::publishAoiPatch(const cv::Mat &aoiMono8,
                                     const QRect &sourceRect,
                                     const QSize &fullFrameSize,
                                     DisplaySnapshot snapshot)
{
    if (aoiMono8.empty() || aoiMono8.type() != CV_8UC1 ||
        sourceRect.isEmpty() || fullFrameSize.isEmpty() ||
        sourceRect.width() != aoiMono8.cols ||
        sourceRect.height() != aoiMono8.rows)
        return;

    QMutexLocker lock(&mutex_);

    const int fullWidth = fullFrameSize.width();
    const int fullHeight = fullFrameSize.height();
    const QRect fullRect(0, 0, fullWidth, fullHeight);
    const QRect patchRect = sourceRect.intersected(fullRect);
    if (patchRect.isEmpty())
        return;

    cv::Mat fullFrame;
    if (hasSnapshot_ && !snapshot_.fullFrameMono8.empty() &&
        snapshot_.fullFrameMono8.type() == CV_8UC1 &&
        snapshot_.fullFrameMono8.cols == fullWidth &&
        snapshot_.fullFrameMono8.rows == fullHeight) {
        fullFrame = snapshot_.fullFrameMono8.clone();
    } else {
        fullFrame = cv::Mat::zeros(fullHeight, fullWidth, CV_8UC1);
    }

    const int sourceX = patchRect.x() - sourceRect.x();
    const int sourceY = patchRect.y() - sourceRect.y();
    const cv::Rect sourceCv(sourceX, sourceY,
                            patchRect.width(), patchRect.height());
    const cv::Rect destinationCv(patchRect.x(), patchRect.y(),
                                 patchRect.width(), patchRect.height());
    aoiMono8(sourceCv).copyTo(fullFrame(destinationCv));
    snapshot.fullFrameMono8 = std::move(fullFrame);

    if (hasSnapshot_) {
        // The measurement worker owns overlay/sequence/result. Preserve the
        // independently published camera statistics when this preview tick
        // only patches the latest full-frame canvas.
        if (snapshot.latestResult.sequence == 0)
            snapshot.latestResult = snapshot_.latestResult;
        if (snapshot.cameraStats.receivedFrames == 0)
            snapshot.cameraStats = snapshot_.cameraStats;
        if (snapshot.sequence == 0)
            snapshot.sequence = snapshot_.sequence;
    }

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
