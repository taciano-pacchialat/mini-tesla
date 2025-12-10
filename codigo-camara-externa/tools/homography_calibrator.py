from __future__ import annotations
import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple
import numpy as np


@dataclass(frozen=True)
class PixelPoint:
    u: float
    v: float


@dataclass(frozen=True)
class WorldPoint:
    x: float
    y: float


def _parse_coord(raw: str) -> Tuple[float, float]:
    try:
        lhs, rhs = raw.split(",")
        return float(lhs.strip()), float(rhs.strip())
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"Formato inválido '{raw}'. Usa 'x,y' con coma decimal."
        ) from exc


def prompt_points(label: str) -> List[Tuple[float, float]]:
    pts: List[Tuple[float, float]] = []
    print(f"\nIngresa 4 puntos para: {label}")
    for idx in range(4):
        while True:
            raw = input(f"  Punto {idx + 1} ({label}) [x,y]: ").strip()
            try:
                pts.append(_parse_coord(raw))
                break
            except argparse.ArgumentTypeError as err:
                print(f"    ✗ {err}")
    return pts


def _build_dlt_system(
    pixels: Sequence[PixelPoint], worlds: Sequence[WorldPoint]
) -> Tuple[np.ndarray, np.ndarray]:
    if len(pixels) != 4 or len(worlds) != 4:
        raise ValueError("Se requieren exactamente 4 correspondencias.")
    A = np.zeros((8, 8), dtype=np.float64)
    b = np.zeros((8,), dtype=np.float64)

    for i, (pix, real) in enumerate(zip(pixels, worlds)):
        u, v = pix.u, pix.v
        x, y = real.x, real.y

        row = 2 * i
        A[row, 0:3] = [u, v, 1.0]
        A[row, 6:8] = [-u * x, -v * x]
        b[row] = x

        row += 1
        A[row, 3:6] = [u, v, 1.0]
        A[row, 6:8] = [-u * y, -v * y]
        b[row] = y
    return A, b


def solve_homography(
    pixels: Sequence[PixelPoint], worlds: Sequence[WorldPoint]
) -> np.ndarray:
    A, b = _build_dlt_system(pixels, worlds)
    h_vec, *_ = np.linalg.lstsq(A, b, rcond=None)
    return np.append(h_vec, 1.0).reshape(3, 3)


def format_c_array(matrix: np.ndarray) -> str:
    flat = matrix.flatten()
    rows = []
    for i in range(0, 9, 3):
        rows.append("    " + ", ".join(f"{flat[i + j]:.6f}f" for j in range(3)))
    return "{\n" + ",\n".join(rows) + "\n}"


def _print_results(
    matrix: np.ndarray,
    pixels: Sequence[PixelPoint],
    worlds: Sequence[WorldPoint],
) -> None:
    print("\n=== Resultados ===")
    for idx, (pix, real) in enumerate(zip(pixels, worlds), 1):
        print(
            f"  #{idx}: Pixel({pix.u:.2f}, {pix.v:.2f}) -> Mundo({real.x:.2f} cm, {real.y:.2f} cm)"
        )
    print("\nMatriz H:")
    print(matrix)
    print("\nArreglo C:")
    print(format_c_array(matrix))
    coeffs = ", ".join(f"{val:.6f}f" for val in matrix.flatten())
    print(f"\nSnippet:\nhomography_init(&H, (float[]){{ {coeffs} }});")


def _run_interactive() -> None:
    pixels = [PixelPoint(*xy) for xy in prompt_points("Píxeles (u,v)")]
    worlds = [WorldPoint(*xy) for xy in prompt_points("Mundo (x_cm,y_cm)")]
    H = solve_homography(pixels, worlds)
    _print_results(H, pixels, worlds)


def _run_from_json(path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    pixels = [PixelPoint(**pt) for pt in data["pixels"]]
    worlds = [WorldPoint(**pt) for pt in data["world"]]
    H = solve_homography(pixels, worlds)
    _print_results(H, pixels, worlds)


def main(argv: Iterable[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="Calcula la homografía a partir de cuatro puntos."
    )
    parser.add_argument(
        "--json", type=Path, help="Archivo JSON con las correspondencias."
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    try:
        if args.json:
            _run_from_json(args.json)
        else:
            _run_interactive()
    except Exception as exc:
        print(f"\n✗ Error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
