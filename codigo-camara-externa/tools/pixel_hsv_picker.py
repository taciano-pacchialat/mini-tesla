#!/usr/bin/env python3
"""Herramienta interactiva para obtener valores HSV desde una imagen.

Uso:
    python3 tools/pixel_hsv_picker.py ruta/a/imagen.jpg

Se abrirá la imagen y podrás hacer clic en cualquier píxel. El script imprimirá
las coordenadas escogidas junto con los valores RGB/HSV (0-255) de ese punto.
Pulsa la tecla Esc o cierra la ventana para finalizar.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Tuple

import cv2
import matplotlib.pyplot as plt
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Selecciona un píxel y obtiene su valor HSV"
    )
    parser.add_argument(
        "image",
        type=Path,
        help="Ruta de la imagen (PNG, JPG, etc.)",
    )
    parser.add_argument(
        "--scale",
        type=float,
        default=1.0,
        help="Factor de escala para visualizar la imagen (solo para la vista)",
    )
    return parser.parse_args()


def load_image(path: Path) -> np.ndarray:
    img = cv2.imread(str(path))
    if img is None:
        raise FileNotFoundError(f"No se pudo abrir la imagen: {path}")
    return img


def on_click(event, img_bgr: np.ndarray) -> None:
    if event.inaxes is None or event.xdata is None or event.ydata is None:
        return

    x = int(round(event.xdata))
    y = int(round(event.ydata))

    if x < 0 or y < 0 or y >= img_bgr.shape[0] or x >= img_bgr.shape[1]:
        return

    b, g, r = img_bgr[y, x]
    hsv = cv2.cvtColor(np.uint8([[[b, g, r]]]), cv2.COLOR_BGR2HSV)[0][0]
    h, s, v = hsv

    print(f"Pixel ({x}, {y}) -> RGB=({r}, {g}, {b}) HSV=({h}, {s}, {v})")


def main() -> None:
    args = parse_args()
    img_bgr = load_image(args.image)

    if args.scale != 1.0:
        img_bgr = cv2.resize(
            img_bgr,
            None,
            fx=args.scale,
            fy=args.scale,
            interpolation=cv2.INTER_AREA,
        )

    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)

    fig, ax = plt.subplots()
    ax.imshow(img_rgb)
    ax.set_title("Haz clic para obtener HSV (cerrar ventana para salir)")
    ax.axis("off")

    fig.canvas.mpl_connect("button_press_event", lambda e: on_click(e, img_bgr))

    print("Haz clic en la imagen para ver los valores HSV del píxel seleccionado…")
    plt.show()


if __name__ == "__main__":
    main()
