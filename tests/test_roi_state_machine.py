"""Pure-Python model of the TwoStarTracker recentering / relocalization state
machine, mirroring the logic in src/TwoStarTracker.cpp.

The model tests the contract the C++ implementation follows. It requires no Qt,
pylon, camera, or compiled binary.
"""

import math


class Rect:
    def __init__(self, x, y, width, height):
        self.x = x
        self.y = y
        self.width = width
        self.height = height

    def contains(self, other):
        return (
            other.x >= self.x
            and other.y >= self.y
            and other.x + other.width <= self.x + self.width
            and other.y + other.height <= self.y + self.height
        )


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def make_roi_centered(center, width, height, frame_w, frame_h):
    x = int(round(center[0])) - width // 2
    y = int(round(center[1])) - height // 2
    x = max(0, min(x, max(0, frame_w - width)))
    y = max(0, min(y, max(0, frame_h - height)))
    return Rect(x, y, width, height)


def enclosing_aoi(star_a, star_b, roi_w, roi_h, margin, frame_w, frame_h):
    """One hardware AOI containing both 64x64 software ROIs plus the margin,
    clipped to the full-frame bounds."""
    ra = make_roi_centered(star_a, roi_w, roi_h, frame_w, frame_h)
    rb = make_roi_centered(star_b, roi_w, roi_h, frame_w, frame_h)
    x = min(ra.x, rb.x) - margin
    y = min(ra.y, rb.y) - margin
    right = max(ra.x + ra.width, rb.x + rb.width) + margin
    bottom = max(ra.y + ra.height, rb.y + rb.height) + margin
    x = max(0, min(x, frame_w - 1))
    y = max(0, min(y, frame_h - 1))
    right = max(x + 1, min(right, frame_w))
    bottom = max(y + 1, min(bottom, frame_h))
    return Rect(x, y, right - x, bottom - y)


class RecenterStateMachine:
    """Near-edge accumulation + cooldown + minimum-shift recentering gate.

    roi_a/roi_b are the current software ROIs in full-frame coordinates.
    update() receives full-frame star positions.
    """

    def __init__(self, roi_w=64, roi_h=64, frame_w=1920, frame_h=1200,
                 edge=16, consecutive=5, cooldown_ms=3000, min_shift=8.0):
        self.roi_w = roi_w
        self.roi_h = roi_h
        self.frame_w = frame_w
        self.frame_h = frame_h
        self.edge = edge
        self.consecutive = consecutive
        self.cooldown_ms = cooldown_ms
        self.min_shift = min_shift
        self.roi_a = Rect(0, 0, roi_w, roi_h)
        self.roi_b = Rect(0, 0, roi_w, roi_h)
        self.near_edge_a = 0
        self.near_edge_b = 0
        self.last_change_ms = None

    def is_near_edge(self, full_star, roi):
        # Near-edge is evaluated in ROI-local coordinates, exactly like the
        # C++ centroidPx comparison against roi.width/roi.height.
        local_x = full_star[0] - roi.x
        local_y = full_star[1] - roi.y
        return (
            local_x < self.edge
            or local_x > (roi.width - self.edge)
            or local_y < self.edge
            or local_y > (roi.height - self.edge)
        )

    def candidate_shift(self, star_a, star_b):
        center_a = (self.roi_a.x + self.roi_a.width / 2.0,
                    self.roi_a.y + self.roi_a.height / 2.0)
        center_b = (self.roi_b.x + self.roi_b.width / 2.0,
                    self.roi_b.y + self.roi_b.height / 2.0)
        return max(distance(star_a, center_a), distance(star_b, center_b))

    def update(self, star_a, star_b, now_ms):
        """Returns True exactly when a recenter request fires for this frame."""
        self.near_edge_a = (
            self.near_edge_a + 1 if self.is_near_edge(star_a, self.roi_a)
            else 0)
        self.near_edge_b = (
            self.near_edge_b + 1 if self.is_near_edge(star_b, self.roi_b)
            else 0)
        if self.near_edge_a < self.consecutive and \
                self.near_edge_b < self.consecutive:
            return False
        self.near_edge_a = 0
        self.near_edge_b = 0
        if self.last_change_ms is not None and \
                now_ms - self.last_change_ms < self.cooldown_ms:
            return False
        if self.candidate_shift(star_a, star_b) < self.min_shift:
            return False
        self.roi_a = make_roi_centered(star_a, self.roi_w, self.roi_h,
                                       self.frame_w, self.frame_h)
        self.roi_b = make_roi_centered(star_b, self.roi_w, self.roi_h,
                                       self.frame_w, self.frame_h)
        self.last_change_ms = now_ms
        return True


class LossCounter:
    """Counts consecutive lost-pair frames before relocalization."""

    def __init__(self, lost_frames=10):
        self.lost_frames = lost_frames
        self.count = 0

    def update(self, valid_pair):
        if valid_pair:
            self.count = 0
            return False
        self.count += 1
        if self.count >= self.lost_frames:
            self.count = 0
            return True
        return False


def test_five_consecutive_near_edge_frames_trigger_one_recenter():
    sm = RecenterStateMachine(edge=30)
    sm.roi_a = Rect(68, 68, 64, 64)      # center (100,100)
    sm.roi_b = Rect(868, 368, 64, 64)    # center (900,400)
    near_a = (105.0, 132.0)              # local (5,32): near-edge, shift ~27
    near_b = (905.0, 432.0)              # local (5,32): near-edge, shift ~27
    requests = 0
    for frame in range(5):
        if sm.update(near_a, near_b, 1000 * frame):
            requests += 1
    assert requests == 1


def test_four_near_edge_frames_do_not_trigger():
    sm = RecenterStateMachine(edge=30)
    sm.roi_a = Rect(68, 68, 64, 64)
    sm.roi_b = Rect(868, 368, 64, 64)
    near_a = (105.0, 132.0)
    near_b = (905.0, 432.0)
    for frame in range(4):
        assert not sm.update(near_a, near_b, 1000 * frame)


def test_shift_smaller_than_minimum_does_not_trigger():
    sm = RecenterStateMachine(edge=30, min_shift=8.0)
    # ROI center at (132,132); star at local (29,32) is near the edge but the
    # candidate ROI shift is only 3 px, below the 8 px minimum.
    sm.roi_a = Rect(100, 100, 64, 64)
    sm.roi_b = Rect(400, 100, 64, 64)
    a = (129.0, 132.0)
    b = (429.0, 132.0)
    assert sm.is_near_edge(a, sm.roi_a)
    assert sm.candidate_shift(a, b) < 8.0
    for frame in range(6):
        assert not sm.update(a, b, 1000 * frame)


def test_cooldown_suppresses_second_request_for_3000ms():
    sm = RecenterStateMachine(edge=30)
    sm.roi_a = Rect(68, 68, 64, 64)      # center (100,100)
    sm.roi_b = Rect(868, 368, 64, 64)    # center (900,400)

    p1a, p1b = (120.0, 120.0), (920.0, 420.0)   # local (20,20), shift ~28
    first_fire_ms = None
    for t in range(5):
        if sm.update(p1a, p1b, t):
            first_fire_ms = t
    assert first_fire_ms == 4

    # Keep the pair near-edge; every post-fire request inside [0, 3000) after
    # the first fire must be suppressed, and one must fire once >= 3000 ms.
    p2a, p2b = (140.0, 140.0), (940.0, 440.0)
    suppressed_inside = False
    fired_after_cooldown = None
    for t in range(1000, 6000):
        if sm.update(p2a, p2b, t):
            assert t >= 3000, f"second request fired during cooldown at {t}"
            fired_after_cooldown = t
        if not suppressed_inside and t >= 1000 and t < 3000:
            suppressed_inside = True  # marker: cooldown region was active
    assert fired_after_cooldown is not None
    assert fired_after_cooldown >= 3000


def test_ten_consecutive_lost_frames_request_relocalization():
    counter = LossCounter(lost_frames=10)
    for frame in range(9):
        assert not counter.update(False)
    assert counter.update(False)
    assert not counter.update(True)  # a valid pair resets the counter
    for frame in range(9):
        assert not counter.update(False)
    assert counter.update(False)


def test_enclosing_aoi_contains_both_rois_plus_margin():
    frame_w, frame_h = 1920, 1200
    roi_w, roi_h, margin = 64, 64, 64
    star_a = (300.0, 400.0)
    star_b = (900.0, 500.0)
    aoi = enclosing_aoi(star_a, star_b, roi_w, roi_h, margin, frame_w, frame_h)
    ra = make_roi_centered(star_a, roi_w, roi_h, frame_w, frame_h)
    rb = make_roi_centered(star_b, roi_w, roi_h, frame_w, frame_h)
    assert aoi.contains(ra)
    assert aoi.contains(rb)
    # The configured margin is honored on the outer sides that stay in frame.
    assert ra.x - margin >= aoi.x - 1
    assert rb.y + rb.height + margin >= aoi.y + aoi.height - 1


def test_frame_without_roi_containment_is_rejected():
    frame_w, frame_h = 1920, 1200
    roi = make_roi_centered((300.0, 400.0), 64, 64, frame_w, frame_h)
    # The AOI frame's sourceRect is the AOI's full-frame origin. A software ROI
    # expressed in full-frame coordinates that lies outside this AOI must be
    # rejected, never cropped with wrong coordinates.
    aoi_source = Rect(0, 0, 256, 256)
    assert not aoi_source.contains(roi)
    contained = aoi_source.contains(
        make_roi_centered((100.0, 100.0), 64, 64, frame_w, frame_h))
    assert contained
