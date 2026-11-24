#!/usr/bin/env python3
"""Draws the default board "Hydra64", compiled into the program (two squares in the greys of the
interface, softly shaded):
  resources/board/hydra64/light.svg, dark.svg
The default piece set of the same name (resources/pieces/hydra64/) is not drawn by a script: its
twelve SVG files are their own source.
Run again after a change: python3 tools/sets/make_hydra64.py"""
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

SQUARES = {
    # each square shades softly from its top left corner to its bottom right one
    # the interface's neutral graphite greys
    "light": ("#a3a5ac", "#95979e"),
    "dark": ("#4c4d54", "#414248"),
}

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main():
    board = os.path.join(ROOT, "resources", "board", "hydra64")
    for name, (a, b) in SQUARES.items():
        write(os.path.join(board, f"{name}.svg"),
              f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><defs><linearGradient id="a" x1="0" y1="0" x2="1" y2="1">'
              f'<stop offset="0" stop-color="{a}"/><stop offset="1" stop-color="{b}"/></linearGradient></defs>'
              f'<rect width="100" height="100" fill="url(#a)"/></svg>\n')


if __name__ == "__main__":
    main()
