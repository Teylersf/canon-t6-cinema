"""Focused CUDA behavior tests; no private media, dependencies or file writes."""
from pathlib import Path
import unittest

import torch

from effects import GpuLook, _box_axis, _parse_recipe

ROOT = Path(__file__).resolve().parents[2]
STOCKS = ROOT / "src" / "stocks"


class RecipeTests(unittest.TestCase):
    def test_shared_recipes_and_raw_override(self):
        for path in STOCKS.glob("*.fs"):
            with self.subTest(path=path.name):
                look = GpuLook(path, strength="off", grain=False)
                self.assertTrue(look.stock_name)
                self.assertEqual(tuple(look._cpu_lut.shape), (3, 4096))
                self.assertTrue(torch.isfinite(look._cpu_lut).all())
        recipe = _parse_recipe("NAME TEST\nSAT 86\n[RAW]\nSAT nonsense\nEXPOSURE 17\n[JPEG]\nGRAIN 12\n")
        self.assertEqual(recipe["SAT"], 86)
        self.assertEqual(recipe["EXPOSURE"], 0)
        self.assertEqual(recipe["GRAIN"], 12)

    def test_malformed_and_options(self):
        for text in ("SAT 100", "NAME X\nVERSION 4", "NAME X\nSAT nan",
                     "NAME X\nEXPOSURE 33", "NAME X\nHUE_ROT 1,2,3",
                     "NAME X\nCURVE 0,0 0,255", "NAME X\nHALATION 1 256 3",
                     "NAME X\n[UNSUPPORTED]\nGRAIN 2"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                _parse_recipe(text)
        for options in ({"strength": "invalid"}, {"tint": "purple"}, {"grain": 1}):
            with self.subTest(options=options), self.assertRaises((ValueError, TypeError)):
                GpuLook(STOCKS / "portra.fs", **options)


@unittest.skipUnless(torch.cuda.is_available(), "CUDA required for GPU behavior tests")
class CudaTests(unittest.TestCase):
    def assert_valid(self, image, shape):
        self.assertEqual(tuple(image.shape), shape)
        self.assertEqual(image.dtype, torch.float32)
        self.assertEqual(image.device.type, "cuda")
        self.assertTrue(bool(torch.isfinite(image).all()))
        self.assertGreaterEqual(float(image.min()), 0)
        self.assertLessEqual(float(image.max()), 1)

    def test_off_still_grades_and_preserves_input(self):
        image = torch.linspace(0, 1, 96, device="cuda")[None, None].expand(3, 48, 96).clone()
        original = image.clone()
        look = GpuLook(STOCKS / "portra.fs", strength="off", grain=False)
        output = look(image, 0)
        self.assert_valid(output, (3, 48, 96))
        self.assertTrue(torch.equal(image, original))
        self.assertGreater(float((output - image).abs().mean()), .01)
        self.assertFalse(output.requires_grad)

    def test_temporal_grain_determinism_and_rng(self):
        image = torch.full((3, 48, 96), .4, device="cuda")
        look = GpuLook(STOCKS / "soup800.fs", strength="off")
        before = torch.cuda.get_rng_state()
        one, repeated, next_frame = look(image, 9), look(image, 9), look(image, 10)
        self.assertTrue(torch.equal(one, repeated))
        self.assertFalse(torch.equal(one, next_frame))
        self.assertTrue(torch.equal(before, torch.cuda.get_rng_state()))
        without = GpuLook(STOCKS / "soup800.fs", strength="off", grain=False)
        self.assertTrue(torch.equal(without(image, 9), without(image, 10)))

    def test_box_edges_and_degenerate_geometry(self):
        uniform = torch.ones((3, 7), device="cuda")
        for axis in (0, 1):
            self.assertTrue(torch.allclose(_box_axis(uniform, 97, axis), uniform, atol=1e-6))
        look = GpuLook(STOCKS / "eterna.fs", strength="strong")
        for height, width in ((1, 1), (1, 7), (7, 1), (19, 37), (7, 11)):
            image = torch.full((3, height, width), .7, device="cuda")
            self.assert_valid(look(image, 0), (3, height, width))
            self.assertEqual(look._geometry_key[1:], (height, width))
            self.assertEqual(set(look._geometry), {"x", "y"})
            self.assertEqual(look._geometry["x"].numel() + look._geometry["y"].numel(), height + width)

    def test_highlight_streak_direction_and_tint(self):
        image = torch.zeros((3, 96, 512), device="cuda")
        image[:, 48, 256] = 1
        plain = GpuLook(STOCKS / "portra.fs", strength="off", grain=False)(image, 0)
        blue = GpuLook(STOCKS / "portra.fs", strength="strong", tint="blue", grain=False)(image, 0)
        amber = GpuLook(STOCKS / "portra.fs", strength="strong", tint="amber", grain=False)(image, 0)
        delta = (blue - plain).clamp_min(0)
        horizontal = float(delta[:, 48, 240:251].sum())
        vertical = float(delta[:, 32:43, 256].sum())
        self.assertGreater(horizontal, vertical * 2 + 1e-5)
        self.assertGreater(float(delta[2, 48, 240:251].sum()), float(delta[0, 48, 240:251].sum()))
        warm = (amber - plain).clamp_min(0)
        self.assertGreater(float(warm[0, 48, 240:251].sum()), float(warm[2, 48, 240:251].sum()))

    def test_input_validation(self):
        look = GpuLook(STOCKS / "portra.fs")
        good = torch.full((3, 8, 8), .5, device="cuda")
        bad = [torch.zeros(3, 8, 8), good.half(), good[:2], good[0], good[:, :0]]
        for value in (float("nan"), float("inf"), -.01, 1.01):
            frame = good.clone()
            frame[0, 0, 0] = value
            bad.append(frame)
        for frame in bad:
            with self.subTest(shape=frame.shape, dtype=frame.dtype), self.assertRaises((TypeError, ValueError)):
                look(frame, 0)
        for index in (-1, .5, True):
            with self.subTest(index=index), self.assertRaises(ValueError):
                look(good, index)

    def test_4k_one_frame_smoke(self):
        look = GpuLook(STOCKS / "soup800.fs", strength="strong")
        look(torch.full((3, 16, 16), .3, device="cuda"), 0)
        image = torch.full((3, 2160, 3840), .3, device="cuda")
        image[:, 1080:1088, 1920:1928] = 1
        torch.cuda.synchronize()
        baseline = torch.cuda.memory_allocated()
        torch.cuda.reset_peak_memory_stats()
        start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        start.record()
        output = look(image, 1)
        end.record()
        torch.cuda.synchronize()
        self.assert_valid(output, (3, 2160, 3840))
        print(f"CUDA 4K one-frame smoke: {start.elapsed_time(end):.2f}ms; "
              f"incremental allocated peak {(torch.cuda.max_memory_allocated()-baseline)/2**20:.1f}MiB; "
              "not an end-to-end video rate", flush=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
