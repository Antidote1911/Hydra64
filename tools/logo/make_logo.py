#!/usr/bin/env python3
"""Draws Hydra64's logo (three-headed hydra over a chessboard) and writes:
  resources/hydra64.svg            square icon (application, Linux menu entry)
  resources/hydra64-logo.svg       full logo with the HYDRA64 wordmark
  resources/windows/hydra64.ico    Windows icon (16 to 256 px), needs rsvg-convert and Pillow
Only plain shapes and gradients (no filter, no font): Qt's SVG renderer draws it as it is.
Run again after a change: python3 tools/logo/make_logo.py"""
import io
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

BG, BG2 = "#0b0d12", "#1a1016"
DARK, MID, LIGHT = "#1c1e26", "#30333f", "#555a6a"
BLACK = "#0a0b0f"
RED, RED_D, RED_B = "#e11d2e", "#8a0f1b", "#ff4a3d"
CREAM = "#f4e7c9"
RIM = "#d3162a"  # outline around the whole creature


def mirror(pts):
    return [(-x, y) for x, y in pts]


def sym(pts):
    """Right half (x >= 0, top to bottom) -> closed symmetric outline."""
    left = [(-x, y) for x, y in reversed(pts) if x != 0]
    return pts + left


class Layer:
    """Shapes of one part, in local coordinates, placed by an SVG transform."""

    def __init__(self, transform=""):
        self.transform = transform
        self.items = []

    def poly(self, pts, fill, sil=True):
        self.items.append(("poly", pts, fill, sil))
        return self

    def both(self, pts, fill, sil=True):
        self.poly(pts, fill, sil)
        self.poly(mirror(pts), fill, sil)
        return self

    def stroke(self, d, color, width, sil=True, extra=""):
        self.items.append(("stroke", d, color, width, sil, extra))
        return self

    def raw(self, svg):
        self.items.append(("raw", svg))
        return self

    def render(self, outline=None, grow=0.0):
        out = []
        for it in self.items:
            if it[0] == "poly":
                _, pts, fill, sil = it
                p = " ".join(f"{x:g},{y:g}" for x, y in pts)
                if outline:
                    if sil:
                        out.append(f'<polygon points="{p}" fill="{outline}" stroke="{outline}" stroke-width="{grow:g}" stroke-linejoin="round"/>')
                else:
                    out.append(f'<polygon points="{p}" fill="{fill}"/>')
            elif it[0] == "stroke":
                _, d, color, width, sil, extra = it
                if outline:
                    if sil:
                        out.append(f'<path d="{d}" fill="none" stroke="{outline}" stroke-width="{width + grow:g}" stroke-linecap="round" stroke-linejoin="round"/>')
                else:
                    cap = "" if "stroke-linecap" in extra else 'stroke-linecap="round" '
                    out.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width:g}" {cap}stroke-linejoin="round" {extra}/>')
            elif not outline:
                out.append(it[1])
        body = "".join(out)
        return f'<g transform="{self.transform}">{body}</g>' if self.transform else body


def side_head(transform):
    """Head in profile, facing left, jaws open."""
    L = Layer(transform)
    # horns
    for horn, facet in (
        ([(118, 30), (204, -30), (152, 46)], [(118, 30), (204, -30), (138, 34)]),
        ([(140, 48), (226, 22), (158, 74)], [(140, 48), (226, 22), (150, 56)]),
        ([(90, 24), (126, -34), (124, 32)], [(90, 24), (126, -34), (106, 26)]),
    ):
        L.poly(horn, MID).poly(facet, LIGHT, False)
    # mouth, lower jaw
    L.poly([(26, 92), (60, 86), (100, 82), (136, 92), (100, 112), (60, 124), (20, 122)], RED_D)
    L.poly([(126, 98), (76, 102), (40, 114), (76, 114), (120, 106)], RED, False)
    L.poly([(136, 92), (158, 112), (126, 142), (70, 148), (34, 136), (18, 120), (60, 124), (100, 112)], DARK)
    L.poly([(34, 136), (70, 148), (126, 142), (140, 128), (100, 134), (60, 134)], BLACK, False)
    # upper head
    L.poly([(0, 70), (8, 56), (40, 48), (72, 30), (96, 20), (150, 38), (166, 74), (136, 92), (100, 82), (60, 86), (26, 92), (6, 86)], DARK)
    L.poly([(40, 48), (72, 30), (96, 20), (150, 38), (122, 42), (88, 38), (56, 52)], MID, False)
    L.poly([(72, 30), (96, 20), (150, 38), (118, 32), (96, 26)], LIGHT, False)
    L.poly([(8, 56), (40, 48), (52, 54), (30, 64), (4, 68)], LIGHT, False)
    L.poly([(12, 62), (22, 59), (18, 66)], BLACK, False)
    L.poly([(112, 60), (152, 50), (160, 72), (132, 82)], RED_D, False)
    L.poly([(118, 62), (150, 55), (154, 66), (132, 74)], RED, False)
    # eye under an angry brow
    L.raw('<circle cx="84" cy="50" r="30" fill="url(#glow)"/>')
    L.poly([(62, 52), (102, 40), (94, 58), (74, 60)], RED_B, False)
    L.poly([(72, 53), (96, 46), (90, 55)], "#ffe1d2", False)
    L.poly([(54, 50), (106, 30), (104, 41), (62, 54)], BLACK, False)
    # teeth
    for t in ([(8, 86), (20, 90), (15, 106)], [(27, 91), (41, 89), (35, 112)], [(47, 88), (57, 86), (53, 100)],
              [(63, 86), (73, 85), (69, 97)], [(80, 84), (90, 83), (86, 94)],
              [(22, 122), (33, 123), (26, 104)], [(39, 124), (49, 124), (44, 109)], [(57, 124), (67, 122), (61, 110)],
              [(75, 119), (85, 116), (79, 106)]):
        L.poly(t, CREAM, False)
    return L


def front_head(transform):
    """Central head, facing the viewer, crowned with spikes."""
    L = Layer(transform)
    L.poly([(0, -156), (-24, -70), (24, -70)], MID).poly([(0, -156), (0, -70), (24, -70)], LIGHT, False)
    for spike, facet in (
        ([(18, -76), (48, -140), (58, -62)], [(48, -140), (40, -70), (58, -62)]),
        ([(50, -64), (96, -116), (82, -42)], [(96, -116), (68, -52), (82, -42)]),
        ([(70, -40), (130, -68), (86, -10)], [(130, -68), (80, -24), (86, -10)]),
        ([(64, -6), (118, -8), (66, 24)], [(118, -8), (66, 10), (66, 24)]),
    ):
        L.both(spike, MID).both(facet, LIGHT, False)
    # lower jaw and mouth
    L.poly(sym([(0, 20), (48, 20), (42, 88), (20, 114), (0, 122)]), DARK)
    L.poly(sym([(0, 36), (35, 36), (31, 82), (13, 102), (0, 106)]), RED_D, False)
    L.poly([(-13, 100), (0, 68), (13, 100), (0, 105)], RED, False)
    L.poly([(0, 70), (3, 98), (0, 102), (-3, 98)], RED_D, False)
    L.both([(15, 102), (28, 90), (23, 70)], CREAM, False).both([(3, 105), (11, 103), (7, 90)], CREAM, False)
    # skull
    L.poly(sym([(0, -82), (38, -68), (72, -34), (68, 4), (48, 30), (30, 42), (0, 46)]), DARK)
    L.poly(sym([(0, -82), (38, -68), (30, -40), (0, -26)]), MID, False)
    L.both([(38, -68), (72, -34), (68, 4), (60, -22)], MID, False)
    L.both([(48, 30), (68, 4), (58, 2), (40, 24)], RED_D, False)
    L.poly([(0, -68), (11, -50), (0, -32), (-11, -50)], RED, False).poly([(0, -68), (11, -50), (0, -50)], RED_B, False)
    L.poly(sym([(0, -22), (17, 0), (23, 26), (0, 40)]), MID, False)
    L.poly([(0, 6), (15, 22), (0, 35), (-15, 22)], LIGHT, False)
    L.both([(6, 25), (13, 20), (11, 29)], BLACK, False)
    # eyes
    L.raw('<circle cx="36" cy="-16" r="34" fill="url(#glow)"/><circle cx="-36" cy="-16" r="34" fill="url(#glow)"/>')
    L.both([(14, -12), (56, -32), (48, -8), (26, -2)], RED_B, False)
    L.both([(24, -11), (48, -22), (43, -11)], "#ffe1d2", False)
    L.both([(6, -24), (60, -48), (68, -32), (52, -31), (12, -10)], BLACK, False)
    # upper fangs
    L.both([(21, 39), (35, 37), (30, 70)], CREAM, False).both([(8, 43), (17, 41), (13, 57)], CREAM, False)
    return L


def board():
    """Chessboard seen in perspective, with red circuit lines."""
    top, bottom, x0t, x1t, x0b, x1b = 392, 474, 136, 376, 62, 450
    cols, rows = 6, 3
    out = []
    ys = [top + (bottom - top) * (r / rows) ** 1.25 for r in range(rows + 1)]

    def xat(c, y):
        f = (y - top) / (bottom - top)
        left = x0t + (x0b - x0t) * f
        right = x1t + (x1b - x1t) * f
        return left + (right - left) * c / cols

    out.append(f'<polygon points="{x0t - 5},{top - 4} {x1t + 5},{top - 4} {x1b + 8},{bottom + 5} {x0b - 8},{bottom + 5}" fill="{BLACK}" stroke="{RIM}" stroke-width="3" stroke-linejoin="round"/>')
    for r in range(rows):
        for c in range(cols):
            fill = "url(#sq)" if (r + c) % 2 == 0 else "#22242d"
            pts = [(xat(c, ys[r]), ys[r]), (xat(c + 1, ys[r]), ys[r]), (xat(c + 1, ys[r + 1]), ys[r + 1]), (xat(c, ys[r + 1]), ys[r + 1])]
            out.append('<polygon points="%s" fill="%s"/>' % (" ".join(f"{x:.1f},{y:.1f}" for x, y in pts), fill))
    for c in range(1, cols):
        out.append(f'<path d="M{xat(c, top):.1f} {top} L{xat(c, bottom):.1f} {bottom}" stroke="{RED_D}" stroke-width="1.6"/>')
    for y in ys[1:-1]:
        out.append(f'<path d="M{xat(0, y):.1f} {y:.1f} L{xat(cols, y):.1f} {y:.1f}" stroke="{RED_D}" stroke-width="1.6"/>')
    return "".join(out)


DEFS = f"""<defs>
<radialGradient id="glow"><stop offset="0" stop-color="#ff2a2a" stop-opacity="0.75"/><stop offset="1" stop-color="#ff2a2a" stop-opacity="0"/></radialGradient>
<radialGradient id="halo" cx="0.5" cy="0.42" r="0.6"><stop offset="0" stop-color="#5a0f1a"/><stop offset="0.55" stop-color="{BG2}"/><stop offset="1" stop-color="{BG}"/></radialGradient>
<linearGradient id="sq" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#6d7180"/><stop offset="1" stop-color="#b9bcc6"/></linearGradient>
<linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ff4a3d"/><stop offset="1" stop-color="#7a0c18"/></linearGradient>
<linearGradient id="ink" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff5a4a"/><stop offset="1" stop-color="#b3101f"/></linearGradient>
</defs>"""


def emblem():
    necks = Layer()
    for d in ("M172 244 C212 248 222 296 236 352", "M340 244 C300 248 290 296 276 352"):
        necks.stroke(d, DARK, 46)
    necks.stroke("M256 300 L256 372", DARK, 74)
    belly = Layer()
    for d in ("M176 250 C208 258 216 300 228 350", "M336 250 C304 258 296 300 284 350"):
        belly.stroke(d, RED, 15, False, 'stroke-dasharray="9 5" stroke-linecap="butt"')
    belly.stroke("M256 318 L256 372", RED, 30, False, 'stroke-dasharray="9 5" stroke-linecap="butt"')
    coil_d = "M256 388 C312 326 420 336 412 386 C404 434 318 436 256 388 C194 340 108 342 100 390 C92 440 200 450 256 388 Z"
    coil = Layer().stroke(coil_d, DARK, 36)
    coil_hi = Layer()  # armour plates, with glowing seams between them
    coil_hi.stroke(coil_d, MID, 22, False, 'stroke-dasharray="16 5" stroke-linecap="butt"')
    coil_hi.stroke(coil_d, LIGHT, 5, False, 'stroke-dasharray="10 11" stroke-dashoffset="-3" stroke-linecap="butt"')
    coil_hi.stroke(coil_d, RED, 22, False, 'stroke-dasharray="2.5 18.5" stroke-dashoffset="-17.2" stroke-linecap="butt"')
    heads = [side_head("translate(40 160) scale(0.84)"), side_head("translate(472 160) scale(-0.84 0.84)")]
    centre = front_head("translate(256 196) scale(1.06)")
    back = [necks, coil] + heads + [centre]
    parts = [board()]
    parts += [l.render(RIM, 13) for l in back]      # red rim around the whole creature
    parts += [l.render(BLACK, 5) for l in back]     # then a black contour
    parts += [necks.render(), belly.render(), coil.render(), coil_hi.render()]
    parts += [h.render() for h in heads]
    parts.append(centre.render())
    return "".join(parts)


# Wordmark: letters as broken lines on a 4 x 8 grid.
LETTERS = {
    "H": [[(0, 0), (0, 8)], [(4, 0), (4, 8)], [(0, 4.6), (4, 3.4)]],
    "Y": [[(0, 0), (2, 4.2), (4, 0)], [(2, 4.2), (2, 8)]],
    "D": [[(0, 0), (2.6, 0), (4, 2.4), (4, 5.6), (2.6, 8), (0, 8), (0, 0)]],
    "R": [[(0, 8), (0, 0), (3, 0), (4, 1.4), (4, 3), (3, 4.4), (0, 4.4)], [(1.8, 4.4), (4, 8)]],
    "A": [[(0, 8), (2, 0), (4, 8)], [(0.8, 5.4), (3.2, 5.4)]],
    "6": [[(3.8, 0), (1.6, 0), (0, 2.2), (0, 6.4), (1.3, 8), (2.7, 8), (4, 6.4), (4, 5.2), (2.8, 3.9), (0, 3.9)]],
    "4": [[(3.1, 8), (3.1, 0), (0, 5.7), (4, 5.7)]],
}


def wordmark(cx, cy, unit=10.5):
    text = "HYDRA64"
    scales = [1.24, 1.1, 1.0, 0.94, 1.0, 1.1, 1.24]
    gap = unit * 1.75
    widths = [4 * unit * s for s in scales]
    x = cx - (sum(widths) + gap * (len(text) - 1)) / 2
    paths = []
    for ch, s, w in zip(text, scales, widths):
        u = unit * s
        for line in LETTERS[ch]:
            pts = [(x + px * u, cy + (py - 4) * u) for px, py in line]
            paths.append("M" + " L".join(f"{a:.1f} {b:.1f}" for a, b in pts) + (" Z" if line[0] == line[-1] else ""))
        x += w + gap
    d = " ".join(paths)
    out = []
    for color, width, join in ((BLACK, 34, "round"), (CREAM, 22, "miter"), ("url(#ink)", 13, "miter")):
        out.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="{join}" stroke-miterlimit="2.6" stroke-linecap="square"/>')
    return f'<g transform="translate({cx} {cy}) skewX(-9) translate({-cx} {-cy})">{"".join(out)}</g>'


def icon_svg():
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{DEFS}'
            f'<rect x="10" y="10" width="492" height="492" rx="104" fill="url(#halo)" stroke="url(#rim)" stroke-width="12"/>'
            f'<g transform="translate(256 262) scale(0.93) translate(-256 -256)">{emblem()}</g></svg>\n')


def logo_svg():
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 560 640">{DEFS}'
            f'<rect width="560" height="640" rx="40" fill="url(#halo)"/>'
            f'<g transform="translate(24 6)">{emblem()}</g>{wordmark(280, 566)}</svg>\n')


def small_svg():
    """Central head alone: the icon below 48 px, where three heads would blur."""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">{DEFS}'
            f'<rect x="10" y="10" width="492" height="492" rx="104" fill="url(#halo)" stroke="url(#rim)" stroke-width="20"/>'
            + "".join(front_head("translate(256 268) scale(1.62)").render(*a) for a in ((RIM, 12), (BLACK, 4), ()))
            + "</svg>\n")


def png(svg, size):
    return subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size)], input=svg.encode(), capture_output=True, check=True).stdout


def main():
    res = os.path.join(ROOT, "resources")
    files = {"hydra64.svg": icon_svg(), "hydra64-logo.svg": logo_svg(), "hydra64-small.svg": small_svg()}
    for name, svg in files.items():
        with open(os.path.join(res, name), "w", encoding="utf-8") as f:
            f.write(svg)
    try:
        from PIL import Image
        sizes = [16, 24, 32, 48, 64, 128, 256]
        imgs = [Image.open(io.BytesIO(png(files["hydra64-small.svg" if s < 48 else "hydra64.svg"], s))).convert("RGBA") for s in sizes]
        imgs[-1].save(os.path.join(res, "windows", "hydra64.ico"), format="ICO", append_images=imgs[:-1], sizes=[(s, s) for s in sizes])
    except (ImportError, OSError, subprocess.CalledProcessError) as e:
        print(f"hydra64.ico not written: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
