"""Optional QA helper (Pillow); not a dependency of the application.
Input: the lossless native-renderer atlas from preview-clipboard-liquid.ps1.
No desktop capture, clipboard reads, network access, or generated UI glyphs.
"""
from pathlib import Path
import argparse
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("atlas", nargs="?", type=Path, default=Path("build/clipboard-liquid/liquid-frames.png"))
parser.add_argument("--output", type=Path, default=Path("build/clipboard-liquid/clipboard-liquid-preview.gif"))
args = parser.parse_args()
with Image.open(args.atlas) as atlas:
    if atlas.size != (7200, 11700):
        raise ValueError("Expected 10 x 15 native frames, each 720 x 780")
    # One global palette prevents the stationary background changing color.
    palette = atlas.resize((900, 1463), Image.Resampling.BOX).convert("RGB").quantize(colors=256)
    frames = []
    for index in range(150):
        x, y = (index % 10) * 720, (index // 10) * 780
        image = atlas.crop((x, y, x + 720, y + 780)).convert("RGB")
        frames.append(image.quantize(palette=palette, dither=Image.Dither.NONE))
args.output.parent.mkdir(parents=True, exist_ok=True)
frames[0].save(args.output, save_all=True, append_images=frames[1:],
               duration=[30, 30, 40] * 50, loop=0, disposal=1, optimize=True)
print(f"Native synthetic preview: {args.output} ({args.output.stat().st_size:,} bytes), 5 seconds")
