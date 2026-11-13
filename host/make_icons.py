"""Regenerate block icons from the checked-in Illustrator SVG exports."""
from pathlib import Path
from make_ai_icons import generate

if __name__ == "__main__":
    generate(Path(__file__).resolve().parents[1])
