#!/usr/bin/env python3
"""
Spellcross strategic map helper:
- Given two screenshots of the SAME strategic map (same resolution/crop),
  produce a mask of the territory that changed state (e.g. stripes disappeared).
- This lets you map Territory IDs deterministically from gameplay evidence.

Usage:
  python spell_territory_mapper.py before.png after.png out_prefix

Outputs:
  out_prefix_change_mask.png        (green overlay showing changed pixels)
  out_prefix_change_mask_raw.png    (raw binary mask)
  out_prefix_change_mask_marked.png (overlay with centroid marker)
  out_prefix_change_centroid.txt    (centroid coords in image space)
"""

import sys
from pathlib import Path
import numpy as np
import cv2
from PIL import Image, ImageDraw


def compute_change_mask(before_bgr: np.ndarray, after_bgr: np.ndarray, red_drop_thresh: int = 20) -> np.ndarray:
    # Focus on disappearance of red stripes: pixels where R channel drops.
    diff = before_bgr.astype(np.int16) - after_bgr.astype(np.int16)
    red_drop = diff[:, :, 2]  # OpenCV is BGR, index 2 is R
    mask = (red_drop > red_drop_thresh).astype(np.uint8) * 255

    # Clean up: open then close to form a coherent blob
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN,
                            cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)),
                            iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE,
                            cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (21, 21)),
                            iterations=1)
    return mask


def centroid_of_largest_blob(mask: np.ndarray):
    num, labels, stats, centroids = cv2.connectedComponentsWithStats(mask, connectivity=8)
    best = None
    best_area = 0
    for i in range(1, num):
        area = int(stats[i, cv2.CC_STAT_AREA])
        if area > best_area:
            best_area = area
            best = i
    if best is None:
        return None, None, 0
    cx, cy = centroids[best]
    return float(cx), float(cy), best_area


def overlay_mask(before_bgr: np.ndarray, mask: np.ndarray, alpha: float = 0.2) -> np.ndarray:
    overlay = before_bgr.copy()
    overlay[mask == 255] = (0, 255, 0)  # green
    blended = cv2.addWeighted(before_bgr, 1.0 - alpha, overlay, alpha, 0)
    return blended


def main():
    if len(sys.argv) < 4:
        print("Usage: python spell_territory_mapper.py before.png after.png out_prefix")
        sys.exit(2)

    before_path = Path(sys.argv[1])
    after_path = Path(sys.argv[2])
    out_prefix = Path(sys.argv[3])

    before = cv2.imread(str(before_path), cv2.IMREAD_COLOR)
    after = cv2.imread(str(after_path), cv2.IMREAD_COLOR)
    if before is None or after is None:
        raise SystemExit("Failed to read input images.")

    # Ensure same crop/size
    h = min(before.shape[0], after.shape[0])
    w = min(before.shape[1], after.shape[1])
    before = before[:h, :w]
    after = after[:h, :w]

    mask = compute_change_mask(before, after)

    cx, cy, area = centroid_of_largest_blob(mask)
    print(f"Centroid: ({cx:.2f}, {cy:.2f}), area={area}")

    # Save raw mask
    cv2.imwrite(str(out_prefix.with_name(out_prefix.name + "_change_mask_raw.png")), mask)

    # Save overlay
    blended = overlay_mask(before, mask)
    cv2.imwrite(str(out_prefix.with_name(out_prefix.name + "_change_mask.png")), blended)

    # Mark centroid on overlay
    pil = Image.fromarray(cv2.cvtColor(blended, cv2.COLOR_BGR2RGB)).convert("RGBA")
    draw = ImageDraw.Draw(pil)
    if cx is not None:
        draw.ellipse((cx-12, cy-12, cx+12, cy+12), outline=(255, 0, 0, 255), width=3)
        draw.text((cx+14, cy-10), "centroid", fill=(255, 0, 0, 255))
        out_prefix.with_name(out_prefix.name + "_change_centroid.txt").write_text(f"{cx:.4f},{cy:.4f}\n")
    pil.save(out_prefix.with_name(out_prefix.name + "_change_mask_marked.png"))


if __name__ == "__main__":
    main()
