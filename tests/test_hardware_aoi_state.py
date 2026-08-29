"""Pure-Python model of the hardware-AOI and relocalization contracts."""

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


def centered_window(center, size, frame_w, frame_h):
    x = int(round(center[0])) - size // 2
    y = int(round(center[1])) - size // 2
    x = max(0, min(x, max(0, frame_w - size)))
    y = max(0, min(y, max(0, frame_h - size)))
    return Rect(x, y, min(size, frame_w), min(size, frame_h))


def enclosing_aoi(star_a, star_b, window_size, margin, frame_w, frame_h):
    wa = centered_window(star_a, window_size, frame_w, frame_h)
    wb = centered_window(star_b, window_size, frame_w, frame_h)
    x = max(0, min(min(wa.x, wb.x) - margin, frame_w - 1))
    y = max(0, min(min(wa.y, wb.y) - margin, frame_h - 1))
    right = max(x + 1, min(max(wa.x + wa.width, wb.x + wb.width) + margin,
                            frame_w))
    bottom = max(y + 1, min(max(wa.y + wa.height, wb.y + wb.height) + margin,
                             frame_h))
    return Rect(x, y, right - x, bottom - y)


class HardwareAoiState:
    """AOI-only movement gate; there is no software-window recenter state."""

    def __init__(self, current, window_size=32, safety=32,
                 cooldown_ms=1000, min_shift=16.0,
                 frame_w=1920, frame_h=1200, margin=32):
        self.current = current
        self.window_size = window_size
        self.safety = safety
        self.cooldown_ms = cooldown_ms
        self.min_shift = min_shift
        self.frame_w = frame_w
        self.frame_h = frame_h
        self.margin = margin
        self.last_request_ms = None

    def needs_update(self, star_a, star_b):
        def safely_contained(star):
            window = centered_window(star, self.window_size,
                                     self.frame_w, self.frame_h)
            return (
                window.x >= self.current.x + self.safety
                and window.y >= self.current.y + self.safety
                and window.x + window.width <= self.current.x + self.current.width - self.safety
                and window.y + window.height <= self.current.y + self.current.height - self.safety
            )

        return not safely_contained(star_a) or not safely_contained(star_b)

    def update(self, star_a, star_b, now_ms):
        if not self.needs_update(star_a, star_b):
            return False
        if (self.last_request_ms is not None
                and now_ms - self.last_request_ms < self.cooldown_ms):
            return False
        requested = enclosing_aoi(
            star_a, star_b, self.window_size, self.margin,
            self.frame_w, self.frame_h)
        if self.current and max(
                abs(requested.x - self.current.x),
                abs(requested.y - self.current.y),
                abs(requested.width - self.current.width),
                abs(requested.height - self.current.height)) < self.min_shift:
            return False
        self.current = requested
        self.last_request_ms = now_ms
        return True


class LossCounter:
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


def test_centroid_motion_inside_aoi_never_repositions_or_reconfigures_camera():
    state = HardwareAoiState(Rect(300, 300, 300, 300))
    for frame in range(100):
        assert not state.update((400.0 + frame * 0.2, 400.0),
                                (500.0 + frame * 0.2, 450.0), frame * 10)


def test_centroid_window_near_aoi_edge_requests_hardware_aoi_update():
    state = HardwareAoiState(Rect(300, 300, 300, 300))
    assert state.update((315.0, 400.0), (500.0, 450.0), 1000)
    assert state.current.contains(centered_window(
        (315.0, 400.0), 32, 1920, 1200))


def test_hardware_aoi_update_has_cooldown_and_minimum_shift_guards():
    state = HardwareAoiState(Rect(300, 300, 300, 300))
    assert state.update((315.0, 400.0), (500.0, 450.0), 1000)
    assert not state.update((315.0, 410.0), (500.0, 450.0), 1500)
    assert state.update((270.0, 430.0), (500.0, 450.0), 2100)


def test_enclosing_aoi_contains_both_dynamic_centroid_windows():
    aoi = enclosing_aoi((300.0, 400.0), (500.0, 450.0),
                        window_size=32, margin=32,
                        frame_w=1920, frame_h=1200)
    assert aoi.contains(centered_window((300.0, 400.0), 32, 1920, 1200))
    assert aoi.contains(centered_window((500.0, 450.0), 32, 1920, 1200))


def test_ten_consecutive_lost_frames_request_relocalization_without_new_segment():
    counter = LossCounter(lost_frames=10)
    for _ in range(9):
        assert not counter.update(False)
    assert counter.update(False)
    assert not counter.update(True)
    for _ in range(9):
        assert not counter.update(False)
    assert counter.update(False)
