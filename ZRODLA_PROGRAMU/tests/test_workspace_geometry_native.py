"""The native footprint kernel must retain the established Python decisions."""
import copy
import math
import random
import unittest

import native_model
from workspace_worktops import polygons_overlap


@unittest.skipUnless(native_model.available(), 'C++ layout library is required')
class NativeWorkspaceGeometryTests(unittest.TestCase):
    def compare(self, first, second):
        before = copy.deepcopy((first, second))
        expected = polygons_overlap.__wrapped__(first, second)
        self.assertIs(polygons_overlap(first, second), expected)
        self.assertEqual((first, second), before)

    def test_real_kernel_routes_and_does_not_count_touching_as_collision(self):
        self.assertTrue(getattr(polygons_overlap, '_native_cpp', False))
        first = [(0, 0), (600, 0), (600, 400), (0, 400)]
        touching = [(600, 0), (1200, 0), (1200, 400), (600, 400)]
        vertex = [(600, 400), (800, 400), (800, 600), (600, 600)]
        self.assertFalse(polygons_overlap(first, touching))
        self.assertFalse(polygons_overlap(first, vertex))
        self.compare(first, touching)
        self.compare(first, vertex)

    def test_concave_notch_and_both_windings_preserve_old_decisions(self):
        corner = [(0, 0), (900, 0), (900, 300), (300, 300), (300, 900), (0, 900)]
        cavity = [(350, 350), (800, 350), (800, 800), (350, 800)]
        crossing = [(250, 250), (700, 250), (700, 700), (250, 700)]
        self.assertFalse(polygons_overlap(corner, cavity))
        self.assertTrue(polygons_overlap(corner, crossing))
        for other in (cavity, crossing):
            for first in (corner, corner[::-1]):
                for second in (other, other[::-1]):
                    self.compare(first, second)
                    self.compare(second, first)

    def test_boundary_probe_and_nearly_parallel_tolerances_match(self):
        for scale in (1e-5, .01, 1., 1000.):
            first = [(0, 0), (scale, 0), (scale, scale), (0, scale)]
            for delta in (-1e-4, -1e-6, -1e-7, -1e-9, 0., 1e-9, 1e-7, 1e-6, 1e-4):
                second = [(scale + delta, 0), (2 * scale + delta, 1e-10),
                          (2 * scale + delta, scale), (scale + delta, scale)]
                self.compare(first, second)
                self.compare(first[::-1], second[::-1])

    def test_legacy_empty_and_degenerate_polygons_are_not_reinterpreted(self):
        polygons = [[], [(0, 0)], [(0, 0), (0, 0)], [(0, 0), (1, 0)],
                    [(0, 0), (1e-8, 1e-8), (2e-8, 2e-8)],
                    [(-2, -2), (2, -2), (2, 2), (-2, 2)]]
        for first in polygons:
            for second in polygons:
                self.compare(first, second)

    def test_compensated_winding_retains_probe_side_after_large_cancellation(self):
        outline = [(0., 0.), (100000., 0.), (0., 100000.), (-1e-13, 0.),
                   (0., -100000.), (-100000., 0.)]
        probe = [(49999, -.0002), (50001, -.0002),
                 (50001, -.00005), (49999, -.00005)]
        self.assertFalse(polygons_overlap.__wrapped__(outline, probe))
        self.assertFalse(polygons_overlap(outline, probe))
        self.compare(outline, probe)

    def test_mixed_integer_float_sum_keeps_legacy_winding_transition(self):
        outline = [(0, 0), (100000, 0), (0, 100000), (-1e-13, 0),
                   (0, -100000), (-100000, 0)]
        probe = [(49999, -.0002), (50001, -.0002),
                 (50001, -.00005), (49999, -.00005)]
        self.assertTrue(polygons_overlap.__wrapped__(outline, probe))
        self.assertTrue(polygons_overlap(outline, probe))
        self.compare(outline, probe)

    def test_seeded_rotated_footprints_match_without_mutating_inputs(self):
        randomizer = random.Random(202610302)
        for _ in range(1000):
            polygons = []
            for _ in range(2):
                width, depth = randomizer.uniform(.01, 2000), randomizer.uniform(.01, 2000)
                x, y = randomizer.uniform(-1500, 1500), randomizer.uniform(-1500, 1500)
                angle = randomizer.uniform(-math.pi, math.pi)
                cosine, sine = math.cos(angle), math.sin(angle)
                outline = [(-width / 2, -depth / 2), (width / 2, -depth / 2),
                           (width / 2, depth / 2), (-width / 2, depth / 2)]
                polygons.append([(x + a * cosine - b * sine, y + a * sine + b * cosine)
                                 for a, b in outline])
            self.compare(*polygons)


if __name__ == '__main__':
    unittest.main()
