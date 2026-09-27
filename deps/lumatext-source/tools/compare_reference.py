#!/usr/bin/env python3
import argparse
import json
import math
import pathlib
import sys

import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity


def coverage(mask_path: pathlib.Path) -> np.ndarray:
    image = np.asarray(Image.open(mask_path).convert("RGBA"), dtype=np.float32) / 255.0
    return image[:, :, 3]


def shifted(source: np.ndarray, dx: int, dy: int) -> np.ndarray:
    output = np.zeros_like(source)
    source_y0 = max(0, -dy)
    source_y1 = min(source.shape[0], source.shape[0] - dy)
    source_x0 = max(0, -dx)
    source_x1 = min(source.shape[1], source.shape[1] - dx)
    if source_y1 > source_y0 and source_x1 > source_x0:
        output[source_y0 + dy:source_y1 + dy, source_x0 + dx:source_x1 + dx] = \
            source[source_y0:source_y1, source_x0:source_x1]
    return output


def align(reference: np.ndarray, candidate: np.ndarray):
    best = None
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            moved = shifted(candidate, dx, dy)
            error = float(np.mean(np.square(reference - moved)))
            if best is None or error < best[0]:
                best = (error, dx, dy, moved)
    return best[1], best[2], best[3]


def record_key(record):
    return record["name"], record["background"], round(float(record["scale"]), 2)


def glyph_origin_errors(reference, candidate, scale):
    reference_glyphs = reference.get("glyphs", [])
    candidate_glyphs = candidate.get("glyphs", [])
    count = min(len(reference_glyphs), len(candidate_glyphs))
    if count == 0:
        return math.inf, math.inf
    errors = []
    for index in range(count):
        left = reference_glyphs[index]
        right = candidate_glyphs[index]
        errors.append(math.hypot(float(left["x"]) - float(right["x"]),
                                 float(left["y"]) - float(right["y"])) * scale)
    if len(reference_glyphs) != len(candidate_glyphs):
        errors.extend([1.0] * abs(len(reference_glyphs) - len(candidate_glyphs)))
    values = np.asarray(errors, dtype=np.float64)
    return float(np.sqrt(np.mean(values * values))), float(np.max(values))


def raster_metrics(mask: np.ndarray):
    """Return stable grayscale edge/halo/stem measurements for A/B reports."""
    ink = mask > (1.0 / 255.0)
    if not np.any(ink):
        return {"edgeMaxAlpha": 0.0, "edgeMeanAlpha": 0.0,
                "haloPixels": 0, "horizontalStrokePx": 0.0,
                "verticalStrokePx": 0.0, "strokeAxisDeltaPx": 0.0}

    padded = np.pad(ink, 1, mode="constant", constant_values=False)
    interior = padded[1:-1, 1:-1]
    edge = interior & (~padded[:-2, 1:-1] | ~padded[2:, 1:-1] |
                       ~padded[1:-1, :-2] | ~padded[1:-1, 2:])
    edge_alpha = mask[edge]
    core = mask >= 0.5

    horizontal = []
    for row in core:
        columns = np.flatnonzero(row)
        if columns.size >= 2:
            horizontal.append(float(columns[-1] - columns[0] + 1))
    vertical = []
    for column in core.T:
        rows = np.flatnonzero(column)
        if rows.size >= 2:
            vertical.append(float(rows[-1] - rows[0] + 1))
    horizontal_mean = float(np.mean(horizontal)) if horizontal else 0.0
    vertical_mean = float(np.mean(vertical)) if vertical else 0.0
    return {
        "edgeMaxAlpha": float(np.max(edge_alpha)),
        "edgeMeanAlpha": float(np.mean(edge_alpha)),
        "haloPixels": int(np.count_nonzero((mask > 0.0) & (mask < 0.08))),
        "horizontalStrokePx": horizontal_mean,
        "verticalStrokePx": vertical_mean,
        "strokeAxisDeltaPx": abs(horizontal_mean - vertical_mean),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coretext", required=True, type=pathlib.Path)
    parser.add_argument("--lumatext", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--allow-font-mismatch", action="store_true",
                        help="compare different font builds and record both identities")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    overlays = args.output / "overlays"
    overlays.mkdir(exist_ok=True)

    core_manifest = json.loads((args.coretext / "manifest.json").read_text("utf-8"))
    luma_manifest = json.loads((args.lumatext / "manifest.json").read_text("utf-8"))
    font_identity_matches = (
        core_manifest["regularSHA256"] == luma_manifest["regularSHA256"] and
        core_manifest["boldSHA256"] == luma_manifest["boldSHA256"])
    if not font_identity_matches and not args.allow_font_mismatch:
        raise RuntimeError("font hashes differ between CoreText and LumaText artifacts")

    core_records = {record_key(value): value for value in core_manifest["records"]
                    if not value.get("fallbackCase", False)}
    luma_records = {record_key(value): value for value in luma_manifest["records"]
                    if not value.get("fallbackCase", False)}
    results = []
    failed = False
    for key in sorted(core_records.keys() & luma_records.keys()):
        core = core_records[key]
        luma = luma_records[key]
        reference = coverage(args.coretext / core["mask"])
        candidate = coverage(args.lumatext / luma["mask"])
        if reference.shape != candidate.shape:
            raise RuntimeError(f"image dimensions differ for {key}")
        dx, dy, candidate = align(reference, candidate)
        ssim = float(structural_similarity(reference, candidate, data_range=1.0))
        reference_metrics = raster_metrics(reference)
        candidate_metrics = raster_metrics(candidate)
        reference_total = max(float(np.sum(reference)), 1e-6)
        coverage_difference = abs(float(np.sum(candidate)) - reference_total) / reference_total
        scale = float(core["scale"])
        line_width_error = abs(float(core["width"]) - float(luma["width"])) * scale
        origin_rms, origin_max = glyph_origin_errors(core, luma, scale)
        passed = (line_width_error <= 0.5 and origin_rms <= 0.2 and
                  origin_max <= 0.5 and coverage_difference <= 0.03 and ssim >= 0.97)
        failed = failed or not passed
        result = {
            "name": core["name"], "background": core["background"], "scale": scale,
            "alignment": {"dx": dx, "dy": dy}, "lineWidthErrorPx": line_width_error,
            "glyphOriginRMSPx": origin_rms, "glyphOriginMaxPx": origin_max,
            "coverageDifference": coverage_difference, "ssim": ssim, "passed": passed,
            "referenceMetrics": reference_metrics,
            "candidateMetrics": candidate_metrics,
            "metricDelta": {
                "edgeMaxAlpha": candidate_metrics["edgeMaxAlpha"] -
                    reference_metrics["edgeMaxAlpha"],
                "edgeMeanAlpha": candidate_metrics["edgeMeanAlpha"] -
                    reference_metrics["edgeMeanAlpha"],
                "haloPixels": candidate_metrics["haloPixels"] -
                    reference_metrics["haloPixels"],
                "strokeAxisDeltaPx": candidate_metrics["strokeAxisDeltaPx"] -
                    reference_metrics["strokeAxisDeltaPx"],
            },
        }
        results.append(result)
        overlay = np.zeros((*reference.shape, 3), dtype=np.float32)
        overlay[:, :, 0] = reference
        overlay[:, :, 1] = candidate
        overlay[:, :, 2] = candidate
        Image.fromarray(np.uint8(np.clip(overlay, 0, 1) * 255), "RGB").save(
            overlays / core["image"])

    report = {
        "thresholds": {"lineWidthPx": 0.5, "glyphOriginRMSPx": 0.2,
                       "glyphOriginMaxPx": 0.5, "coverageDifference": 0.03,
                       "ssim": 0.97},
        "coretext": {"osBuild": core_manifest["osBuild"],
                     "regularSHA256": core_manifest["regularSHA256"],
                     "boldSHA256": core_manifest["boldSHA256"]},
        "lumatext": {"osBuild": luma_manifest["osBuild"],
                     "regularSHA256": luma_manifest["regularSHA256"],
                     "boldSHA256": luma_manifest["boldSHA256"]},
        "fontIdentityMatches": font_identity_matches,
        "results": results,
        "passed": not failed and bool(results),
    }
    (args.output / "comparison.json").write_text(
        json.dumps(report, indent=2, sort_keys=True), "utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
