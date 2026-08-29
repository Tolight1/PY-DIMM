import math


def test_pixel_scale():
    pixel_scale_rad = (5.86e-6) / 2.5
    assert math.isclose(pixel_scale_rad, 2.344e-6, rel_tol=1e-12)


def test_population_variance_uses_n():
    values = [1.0, 2.0, 3.0]
    mean = sum(values) / len(values)
    variance = sum((x - mean) ** 2 for x in values) / len(values)
    assert math.isclose(variance, 2.0 / 3.0)


def test_dimm_coefficients_are_positive_for_defaults():
    lam = 550e-9
    diameter = 80e-3
    baseline = 170e-3
    longitudinal = 2 * lam**2 * (
        0.179 * diameter ** (-1 / 3) - 0.0968 * baseline ** (-1 / 3)
    )
    transverse = 2 * lam**2 * (
        0.179 * diameter ** (-1 / 3) - 0.145 * baseline ** (-1 / 3)
    )
    assert longitudinal > 0
    assert transverse > 0
