#!/usr/bin/env python3
"""
1) Ejecuta el flujo de calibración existente (captura frame, clic en 4 puntos,
   calcula la homografía con homography_calibrator.py).
2) Usa la homografía resultante para procesar la webcam del portátil con el
   mismo algoritmo de color/homografía que corre en vision/vision.c.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import Dict, Tuple

import cv2
import numpy as np

sys.path.append(str(Path(__file__).resolve().parent))
from capture_and_calibrate import pick_pixels, prompt_world_points
from homography_calibrator import (
    PixelPoint,
    WorldPoint,
    solve_homography,
    _print_results,
)

COLOR_RANGES: Dict[str, Tuple[Tuple[int, int, int], Tuple[int, int, int]]] = {
    "ORANGE": ((10, 80, 80), (30, 255, 255)),
    "GREEN": ((40, 60, 60), (80, 255, 255)),
}


def capture_webcam_frame(cap: cv2.VideoCapture, output: Path) -> np.ndarray:
    ok, frame = cap.read()
    if not ok:
        raise RuntimeError("No se pudo capturar un frame de la webcam.")
    if not cv2.imwrite(str(output), frame):
        raise RuntimeError(f"No se pudo escribir el frame en {output}")
    print(f"[calib] Frame de webcam guardado en {output}")
    return frame


def _ensure_pixel_points(items):
    normalized = []
    for item in items:
        if isinstance(item, PixelPoint):
            normalized.append(item)
        else:
            normalized.append(PixelPoint(*item))
    return normalized


def _ensure_world_points(items):
    normalized = []
    for item in items:
        if isinstance(item, WorldPoint):
            normalized.append(item)
        else:
            normalized.append(WorldPoint(*item))
    return normalized


def calibrate_from_webcam(
    cap: cv2.VideoCapture, capture_path: Path, coeffs_path: Path | None
) -> np.ndarray:
    capture_webcam_frame(cap, capture_path)

    pixels = _ensure_pixel_points(pick_pixels(capture_path))
    worlds = _ensure_world_points(prompt_world_points())

    H = solve_homography(pixels, worlds)
    _print_results(H, pixels, worlds)

    if coeffs_path:
        coeffs_path.write_text(
            "\n".join(",".join(f"{val:.9f}" for val in row) for row in H),
            encoding="utf-8",
        )
        np.save(coeffs_path.with_suffix(".npy"), H)
        print(f"[calib] Coeficientes guardados en {coeffs_path} (+ .npy)")
    return H


def load_coeffs(path: Path) -> np.ndarray:
    if path.suffix == ".npy":
        return np.load(path)
    rows = [
        [float(value) for value in line.split(",") if value.strip()]
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if len(rows) != 3 or any(len(row) != 3 for row in rows):
        raise ValueError("Archivo de coeficientes debe contener 3 filas de 3 valores.")
    return np.array(rows, dtype=np.float32)


def homography_transform(H: np.ndarray, u: float, v: float) -> Tuple[float, float]:
    vec = H @ np.array([u, v, 1.0], dtype=np.float32)
    denom = vec[2] if vec[2] != 0 else 1e-6
    return float(vec[0] / denom), float(vec[1] / denom)


def detect_color(
    frame: np.ndarray, H: np.ndarray, color_name: str, min_area: int
) -> dict | None:
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    lower, upper = COLOR_RANGES[color_name]
    mask = cv2.inRange(
        hsv, np.array(lower, dtype=np.uint8), np.array(upper, dtype=np.uint8)
    )
    mask = cv2.erode(mask, None, iterations=2)
    mask = cv2.dilate(mask, None, iterations=2)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None

    contour = max(contours, key=cv2.contourArea)
    area = cv2.contourArea(contour)
    if area < min_area:
        return None

    M = cv2.moments(contour)
    if M["m00"] == 0:
        return None

    cx = int(M["m10"] / M["m00"])
    cy = int(M["m01"] / M["m00"])
    world_x, world_y = homography_transform(H, cx, cy)

    return {
        "centroid": (cx, cy),
        "world": (world_x, world_y),
        "area": area,
        "mask": mask,
        "contour": contour,
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Calibra y corre el pipeline de visión usando solo la webcam."
    )
    parser.add_argument(
        "--capture",
        type=Path,
        help="Ruta temporal para la imagen de calibración.",
    )
    parser.add_argument(
        "--coeffs",
        type=Path,
        help="Archivo con la matriz (CSV o .npy). Si no existe, se recalibra.",
    )
    parser.add_argument(
        "--color",
        choices=list(COLOR_RANGES.keys()),
        default="GREEN",
        help="Color a seguir en HSV.",
    )
    parser.add_argument(
        "--min-area",
        type=int,
        default=800,
        help="Área mínima en píxeles para aceptar una detección.",
    )
    parser.add_argument(
        "--device", type=int, default=0, help="Índice de la webcam (VideoCapture)."
    )
    args = parser.parse_args()

    tools_dir = Path(__file__).resolve().parent
    capture_path = args.capture or (tools_dir / "capture_webcam.jpg")
    capture_path = capture_path.expanduser().resolve()
    capture_path.parent.mkdir(parents=True, exist_ok=True)

    cap = cv2.VideoCapture(args.device)
    if not cap.isOpened():
        raise RuntimeError(f"No se pudo abrir la webcam (device={args.device}).")

    coeffs_arg = args.coeffs.expanduser().resolve() if args.coeffs else None

    if coeffs_arg and coeffs_arg.exists():
        print(f"[calib] Cargando matriz desde {coeffs_arg}")
        target = coeffs_arg if coeffs_arg.suffix else coeffs_arg.with_suffix(".csv")
        H = load_coeffs(target)
    else:
        coeffs_path = coeffs_arg or (tools_dir / "homography_coeffs.csv")
        coeffs_path = coeffs_path.expanduser().resolve()
        coeffs_path.parent.mkdir(parents=True, exist_ok=True)
        H = calibrate_from_webcam(cap, capture_path, coeffs_path)

    print(f"[webcam] Procesando webcam con color {args.color}. Pulsa 'q' para salir.")

    fps_timer = time.time()
    frames = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        detection = detect_color(frame, H, args.color, args.min_area)
        if detection:
            (cx, cy) = detection["centroid"]
            (wx, wy) = detection["world"]
            cv2.drawContours(frame, [detection["contour"]], -1, (0, 255, 0), 2)
            cv2.circle(frame, (cx, cy), 5, (0, 0, 255), -1)
            cv2.putText(
                frame,
                f"{args.color} pix=({cx},{cy}) world=({wx:.1f},{wy:.1f}) cm",
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (255, 255, 255),
                2,
            )

        cv2.imshow("Webcam + Homografia", frame)
        frames += 1
        if time.time() - fps_timer >= 1:
            print(f"[webcam] FPS: {frames}")
            frames = 0
            fps_timer = time.time()

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
