"""CUDA interpretation of the shared Soup recipes and anamorphic effects.

This is original float32 GPU processing, NOT a bit-exact port of engine/look.c
or anamorphic.c. It preserves their stage order and controls, uses replicated
edges, interpolated curves, continuous hue math and frame-varying grain. The
input is nonlinear RGB in [0,1], already decoded/white-balanced by the caller;
RAW overrides and WB metadata do not reprocess a baked video. Spatial recipe
radii/grain cells scale relative to a 3840-pixel output width. No optical depth,
oval bokeh, extra field of view or global squeeze is reconstructed.

One call processes one frame. Only small LUTs/parameters and one geometry's 1D
coordinates are cached; no frames or full-resolution maps are retained.
GPL-2.0-only, consistent with the surrounding project.
"""
from __future__ import annotations

from bisect import bisect_right
import math
from pathlib import Path
import re
import warnings

import torch
import torch.nn.functional as F

_MAX_PIXELS = 8192 * 4320
_LUT_SIZE = 4096
_LUMA = (77.0 / 256.0, 150.0 / 256.0, 29.0 / 256.0)
_PRESETS = {
    "off": (0.0, 0.0, 0.0, 0.0, 0.0),
    "subtle": (.35, .40, .12, .12, .05),
    "cinema": (.65, .60, .25, .18, .10),
    "strong": (1.0, .80, .40, .30, .18),
}


def _numbers(text: str, count: int, key: str) -> list[float]:
    parts = re.split(r"[\s,]+", text.strip())
    if len(parts) != count:
        raise ValueError(f"{key} requires {count} numbers")
    try:
        values = [float(x) for x in parts]
    except ValueError as exc:
        raise ValueError(f"invalid number in {key}") from exc
    if any(not math.isfinite(x) or abs(x) > 1_000_000 for x in values):
        raise ValueError(f"non-finite or excessive value in {key}")
    return values


def _parse_recipe(text: str) -> dict:
    """Parse common/JPEG recipe fields; RAW sections are deliberately skipped."""
    recipe = {
        "NAME": "", "DESC": "", "WB": "AS_SHOT", "VERSION": 1,
        "EXPOSURE": 0.0, "SAT": 100.0, "GRAIN": 0.0, "GRAIN_SIZE": 0.0,
        "HUE_SAT": [100.0] * 6, "HUE_ROT": [0.0] * 6,
        "HUE_LUM": [100.0] * 6, "SPLIT_SH": [0.0] * 3,
        "SPLIT_HI": [0.0] * 3, "HALATION": [0.0, 228.0, 16.0],
        "HAL_TINT": [255.0, 64.0, 24.0],
    }
    identity = [(0.0, 0.0), (1.0, 1.0)]
    for key in ("CURVE", "CURVE_R", "CURVE_G", "CURVE_B"):
        recipe[key] = identity
    section = "COMMON"
    unknown = set()
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = re.split(r"[#;]", raw, maxsplit=1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            section = line.strip("[]").upper()
            if section not in ("RAW", "JPEG", "COMMON"):
                raise ValueError(f"unsupported section on line {line_number}")
            continue
        if section == "RAW":
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            raise ValueError(f"missing recipe value on line {line_number}")
        key, value = parts[0].upper(), parts[1]
        if key in ("NAME", "DESC", "WB"):
            recipe[key] = value.strip()
        elif key.startswith("CURVE") and key in recipe:
            tokens = value.split()
            if not 2 <= len(tokens) <= 32:
                raise ValueError(f"{key} requires 2..32 points")
            points = [_numbers(token, 2, key) for token in tokens]
            if any(not 0 <= v <= 255 for point in points for v in point):
                raise ValueError(f"{key} points must lie in 0..255")
            if any(a[0] >= b[0] for a, b in zip(points, points[1:])):
                raise ValueError(f"{key} x coordinates must strictly increase")
            recipe[key] = [(x / 255.0, y / 255.0) for x, y in points]
        elif key in ("HUE_SAT", "HUE_ROT", "HUE_LUM"):
            recipe[key] = _numbers(value, 6, key)
        elif key in ("SPLIT_SH", "SPLIT_HI", "HALATION", "HAL_TINT"):
            recipe[key] = _numbers(value, 3, key)
        elif key in ("VERSION", "EXPOSURE", "SAT", "GRAIN", "GRAIN_SIZE"):
            recipe[key] = _numbers(value, 1, key)[0]
        else:
            unknown.add(key)
    if not recipe["NAME"]:
        raise ValueError("recipe has no NAME")
    if recipe["VERSION"] not in (1, 2, 3):
        raise ValueError("only shared recipe versions 1..3 are supported")
    if not -32 <= recipe["EXPOSURE"] <= 32:
        raise ValueError("EXPOSURE must lie in -32..32")
    bounds = {"SAT": (0, 400), "GRAIN": (0, 100), "GRAIN_SIZE": (0, 100)}
    for key, (low, high) in bounds.items():
        if not low <= recipe[key] <= high:
            raise ValueError(f"{key} out of supported range")
    for key, low, high in (("HUE_SAT", 0, 400), ("HUE_LUM", 0, 400),
                           ("HUE_ROT", -120, 120), ("SPLIT_SH", -255, 255),
                           ("SPLIT_HI", -255, 255), ("HAL_TINT", 0, 255)):
        if any(not low <= v <= high for v in recipe[key]):
            raise ValueError(f"{key} out of supported range")
    amount, threshold, radius = recipe["HALATION"]
    if not (0 <= amount <= 400 and 0 <= threshold <= 255 and 0 <= radius <= 48):
        raise ValueError("HALATION requires amount 0..400, threshold 0..255, radius 0..48")
    if unknown:
        warnings.warn("ignored recipe keys: " + ", ".join(sorted(unknown)), stacklevel=2)
    return recipe


def _curve_function(points):
    """Monotone cubic Hermite, matching the C curve's interpolation approach."""
    xs, ys = zip(*points)
    slopes = [(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(len(xs) - 1)]
    tangents = [slopes[0]] + [0.0] * (len(xs) - 2) + [slopes[-1]]
    for i in range(1, len(xs) - 1):
        tangents[i] = 0.0 if slopes[i - 1] * slopes[i] <= 0 else (slopes[i - 1] + slopes[i]) * .5
    for i, slope in enumerate(slopes):
        if slope == 0:
            tangents[i] = tangents[i + 1] = 0.0
        else:
            a, b = tangents[i] / slope, tangents[i + 1] / slope
            if a * a + b * b > 9:
                scale = 3.0 / math.sqrt(a * a + b * b)
                tangents[i], tangents[i + 1] = scale * a * slope, scale * b * slope

    def evaluate(value):
        if value <= xs[0]:
            return ys[0]
        if value >= xs[-1]:
            return ys[-1]
        i = min(len(xs) - 2, max(0, bisect_right(xs, value) - 1))
        width = xs[i + 1] - xs[i]
        t = (value - xs[i]) / width
        return ((2*t**3 - 3*t*t + 1)*ys[i] + (t**3 - 2*t*t + t)*width*tangents[i]
                + (-2*t**3 + 3*t*t)*ys[i + 1] + (t**3 - t*t)*width*tangents[i + 1])
    return evaluate


def _box_axis(image: torch.Tensor, radius: int, axis: int) -> torch.Tensor:
    """O(pixels) edge-clamped box mean; no radius-sized convolution kernel."""
    if radius <= 0:
        return image
    padding = (radius, radius, 0, 0) if axis == 1 else (0, 0, radius, radius)
    padded = F.pad(image[None, None], padding, mode="replicate")[0, 0]
    prefix = torch.cumsum(padded, dim=axis)
    window = 2 * radius + 1
    if axis == 1:
        prefix = F.pad(prefix, (1, 0, 0, 0))
        result = prefix[:, window:] - prefix[:, :-window]
    else:
        prefix = F.pad(prefix, (0, 0, 1, 0))
        result = prefix[window:, :] - prefix[:-window, :]
    return result.mul_(1.0 / window).clamp_min_(0)


def _luma(image):
    return image[0] * _LUMA[0] + image[1] * _LUMA[1] + image[2] * _LUMA[2]


class GpuLook:
    """Shared-stock film grade plus optional anamorphic-inspired CUDA effects.

    ``strength='off'`` disables only the anamorphic stage. ``grain=False``
    disables stock grain. Same frame/index/settings reproduce the same output;
    different frame indices produce fresh grain without changing global RNG.
    Calls are serialized; the geometry cache stores only the latest geometry.
    """
    def __init__(self, stock_path: Path, strength: str = "cinema", tint: str = "blue", grain: bool = True):
        if not isinstance(strength, str) or strength.lower() not in _PRESETS:
            raise ValueError("strength must be off, subtle, cinema or strong")
        if not isinstance(tint, str) or tint.lower() not in ("blue", "amber"):
            raise ValueError("tint must be blue or amber")
        if type(grain) is not bool:
            raise TypeError("grain must be bool")
        self.stock_path = Path(stock_path).resolve(strict=True)
        if self.stock_path.stat().st_size > 65536:
            raise ValueError("recipe exceeds 64 KiB")
        self.recipe = _parse_recipe(self.stock_path.read_text(encoding="utf-8"))
        self.stock_name = self.recipe["NAME"]
        self.strength, self.tint, self.grain = strength.lower(), tint.lower(), grain
        master = _curve_function(self.recipe["CURVE"])
        gain = 2.0 ** self.recipe["EXPOSURE"]
        tables = []
        for channel in ("CURVE_R", "CURVE_G", "CURVE_B"):
            curve = _curve_function(self.recipe[channel])
            table = []
            for i in range(_LUT_SIZE):
                value = i / (_LUT_SIZE - 1)
                if gain != 1:
                    linear = value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4
                    linear = min(1.0, linear * gain)
                    value = 12.92 * linear if linear <= .0031308 else 1.055 * linear ** (1 / 2.4) - .055
                table.append(min(1.0, max(0.0, curve(master(value)))))
            tables.append(table)
        self._cpu_lut = torch.tensor(tables, dtype=torch.float32)
        self._device = None
        self._parameters = {}
        self._geometry_key = None
        self._geometry = {}

    def _prepare(self, frame):
        device = frame.device
        if self._device != device:
            self._device = device
            self._parameters = {"lut": self._cpu_lut.to(device)}
            for key in ("HUE_SAT", "HUE_ROT", "HUE_LUM", "SPLIT_SH", "SPLIT_HI", "HAL_TINT"):
                divisor = 100.0 if key in ("HUE_SAT", "HUE_LUM") else 255.0
                values = self.recipe[key]
                if key == "HUE_ROT":
                    values, divisor = [v * math.pi / 180 for v in values], 1.0
                self._parameters[key] = torch.tensor(values, device=device, dtype=torch.float32) / divisor
            self._geometry_key = None
        height, width = frame.shape[1:]
        key = (device, height, width)
        if self._geometry_key != key:
            x = torch.linspace(-1, 1, width, device=device) if width > 1 else torch.zeros(1, device=device)
            y = torch.linspace(-1, 1, height, device=device) if height > 1 else torch.zeros(1, device=device)
            self._geometry = {"x": x, "y": y}
            self._geometry_key = key

    def _grade(self, frame):
        channels = []
        for channel in range(3):
            position = frame[channel] * (_LUT_SIZE - 1)
            low = position.to(torch.int64)
            high = (low + 1).clamp_max_(_LUT_SIZE - 1)
            table = self._parameters["lut"][channel]
            channels.append(torch.lerp(table[low], table[high], position - low))
        red, green, blue = channels
        maximum, minimum = torch.maximum(torch.maximum(red, green), blue), torch.minimum(torch.minimum(red, green), blue)
        chroma = maximum - minimum
        denominator = chroma.clamp_min(1e-7)
        hue = torch.where(maximum == red, (green - blue) / denominator,
              torch.where(maximum == green, (blue - red) / denominator + 2, (red - green) / denominator + 4))
        hue = hue.remainder_(6)
        sector = hue.to(torch.int64)
        fraction = hue - sector
        following = (sector + 1).remainder_(6)
        fade = (chroma * (255.0 / 40.0)).clamp_(0, 1)
        controls = []
        for key in ("HUE_SAT", "HUE_LUM", "HUE_ROT"):
            table = self._parameters[key]
            value = torch.lerp(table[sector], table[following], fraction)
            controls.append(value * fade if key == "HUE_ROT" else 1 + (value - 1) * fade)
        saturation, luminance, rotation = controls
        luma = red * _LUMA[0] + green * _LUMA[1] + blue * _LUMA[2]
        u, v = blue - luma, red - luma
        cosine, sine = torch.cos(rotation), torch.sin(rotation)
        rotated_u = (u * cosine - v * sine) * saturation
        rotated_v = (u * sine + v * cosine) * saturation
        luma = luma * luminance
        image = torch.stack((luma + rotated_v, luma - (77*rotated_v + 29*rotated_u)/150, luma + rotated_u))
        luma = _luma(image.clamp(0, 1))
        image = luma[None] + (image - luma[None]) * (self.recipe["SAT"] / 100.0)
        luma = _luma(image.clamp(0, 1))
        image = image + self._parameters["SPLIT_SH"][:, None, None] * (1 - luma)[None] + self._parameters["SPLIT_HI"][:, None, None] * luma[None]
        luma = _luma(image).clamp_(0, 1)
        level = luma * 255
        rolloff = torch.where(level < 24, 150 + level * (106/24),
                  torch.where(level <= 112, torch.full_like(level, 256), 256 - (level - 112) * (140/143)))
        image = luma[None] + (image - luma[None]) * (rolloff.clamp_(0, 255) / 256)[None]
        # Fit chroma around luminance instead of independently clipping channels.
        luma = _luma(image).clamp_(0, 1)
        delta = image - luma[None]
        allowed = torch.where(delta >= 0, (1 - luma)[None] / delta.clamp_min(1e-7),
                              luma[None] / (-delta).clamp_min(1e-7))
        scale = allowed.amin(dim=0).clamp_(0, 1)
        return (luma[None] + delta * scale[None]).clamp_(0, 1)

    def _halation(self, image):
        amount, threshold, radius = self.recipe["HALATION"]
        if amount <= 0 or threshold >= 255:
            return image
        luma = _luma(image)
        mask = ((luma - threshold/255) / (1 - threshold/255)).clamp_(0, 1)
        radius = max(1, round(radius * image.shape[2] / 3840))
        glow = _box_axis(_box_axis(mask, radius, 1), radius, 0)
        glow.mul_(amount / 100).mul_(1 - luma)
        return (image + self._parameters["HAL_TINT"][:, None, None] * glow[None]).clamp_(0, 1)

    def _grain(self, image, frame_index):
        if not self.grain or self.recipe["GRAIN"] <= 0:
            return image
        height, width = image.shape[1:]
        cell = max(1, round((1 + self.recipe["GRAIN_SIZE"] / 12) * width / 3840))
        x = torch.arange(width, device=image.device, dtype=torch.int64).div_(cell, rounding_mode="floor")[None]
        y = torch.arange(height, device=image.device, dtype=torch.int64).div_(cell, rounding_mode="floor")[:, None]
        seed = (0x5EED + frame_index * 0x9E3779B9) & 0xFFFFFFFF
        hashed = (x * 374761393 + y * 668265263 + ((seed * 2246822519) & 0xFFFFFFFF)).bitwise_and_(0xFFFFFFFF)
        hashed = ((hashed ^ (hashed >> 13)) * 1274126177).bitwise_and_(0xFFFFFFFF)
        hashed.bitwise_xor_(hashed >> 16)
        noise = (hashed.bitwise_and_(65535).to(torch.float32) / 32768 - 1)
        noise.mul_(self.recipe["GRAIN"] * .38 / 255)
        return (image + noise[None] * (4 * image * (1 - image))).clamp_(0, 1)

    def _anamorphic(self, image):
        strength, streak, bloom, vignette, chromatic = _PRESETS[self.strength]
        if strength == 0:
            return image
        width = image.shape[2]
        radius = max(8, min(2048, width // 14))
        mask = ((image.amax(dim=0) - 220/255) / (35/255)).clamp_(0, 1)
        flare = _box_axis(mask, max(1, radius//16), 1).mul_(2)
        flare.add_(_box_axis(mask, max(1, radius//4), 1), alpha=3)
        flare.add_(_box_axis(mask, radius, 1), alpha=5).clamp_(0, 1).mul_(strength * streak)
        vertical = 3 if self.strength == "strong" else 2
        glow = _box_axis(_box_axis(mask, max(1, radius//8, vertical*2), 1), vertical, 0)
        glow.mul_(strength * bloom)
        x, y = self._geometry["x"], self._geometry["y"]
        offset = x * (2 * strength * chromatic)
        columns = (x + 1) * ((width - 1) / 2)
        shifted = []
        for channel, direction in ((0, 1), (2, -1)):
            coordinate = (columns + direction * offset).clamp(0, width - 1)
            low = coordinate.to(torch.int64)
            high = (low + 1).clamp_max_(width - 1)
            shifted.append(torch.lerp(image[channel][:, low], image[channel][:, high], coordinate - low))
        image = torch.stack((shifted[0], image[1], shifted[1]))
        tint = image.new_tensor((85, 160, 255) if self.tint == "blue" else (255, 174, 75)).div_(255)
        room = (1 - _luma(image)).clamp_(0, 1)
        image = (image + (tint[:, None, None] * flare[None] + ((tint + 2)/3)[:, None, None] * glow[None]) * room[None]).clamp_(0, 1)
        edge = x.square()[None] + y.square()[:, None]
        dark = edge.square_().mul_(.17578125 * strength * vignette).clamp_(0, .8)
        return image.mul_(1 - dark[None]).clamp_(0, 1)

    @torch.inference_mode()
    def __call__(self, frame: torch.Tensor, frame_index: int) -> torch.Tensor:
        if not isinstance(frame, torch.Tensor) or frame.dtype != torch.float32:
            raise TypeError("frame must be a float32 torch.Tensor")
        if frame.device.type != "cuda":
            raise ValueError("frame must be on CUDA")
        if frame.ndim != 3 or frame.shape[0] != 3 or min(frame.shape[1:]) < 1:
            raise ValueError("frame must have shape 3xHxW with positive dimensions")
        if max(frame.shape[1:]) > 16384 or frame.shape[1] * frame.shape[2] > _MAX_PIXELS:
            raise ValueError("frame exceeds the one-frame geometry bound")
        if type(frame_index) is not int or frame_index < 0:
            raise ValueError("frame_index must be a nonnegative int")
        low, high = torch.aminmax(frame)
        if not (math.isfinite(low.item()) and math.isfinite(high.item()) and low.item() >= 0 and high.item() <= 1):
            raise ValueError("frame must contain finite RGB values in 0..1")
        self._prepare(frame)
        image = self._grain(self._halation(self._grade(frame)), frame_index)
        return self._anamorphic(image).contiguous()


__all__ = ["GpuLook"]
