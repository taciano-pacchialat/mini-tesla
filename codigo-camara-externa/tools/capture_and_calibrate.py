#!/usr/bin/env python3
"""
1) Se conecta al WebSocket del ESP32-S3 (192.168.4.1/ws) y guarda el primer
   frame JPEG del nodo maestro.
2) Muestra la imagen para que hagas clic en los cuatro puntos de referencia.
3) Pide las coordenadas reales (cm) de esos puntos y llama al solver de
   homografía de homography_calibrator.py.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.image as mpimg
from websocket import create_connection  # pip install websocket-client

sys.path.append(str(Path(__file__).resolve().parent))
from homography_calibrator import PixelPoint, WorldPoint, solve_homography, print_results  # noqa: E402


def capture_frame(ws_url: str, outfile: Path) -> None:
    ws = create_connection(ws_url, timeout=10)
    ws.send(json.dumps({"type": "register", "role": "dashboard"}))

    pending_source = "esp32s3"
    while True:
        msg = ws.recv()
        if isinstance(msg, str):
            data = json.loads(msg)
            if data.get("type") == "frame":
                pending_source = data.get("source", "esp32s3")
            continue

        if pending_source != "esp32s3":
            continue

        outfile.write_bytes(msg)
        ws.close()
        return


def pick_pixels(image_path: Path):
    img = mpimg.imread(image_path)
    plt.imshow(img)
    plt.title("Haz clic en los 4 puntos de referencia (orden reloj)")
    pts = plt.ginput(4, timeout=-1)
    plt.close()
    return [PixelPoint(u, v) for u, v in pts]


def prompt_world_points():
    coords = []
    print("\nIngresa las coordenadas reales (x_cm,y_cm) en el mismo orden:")
    for idx in range(4):
        raw = input(f"Punto #{idx+1} (cm): ")
        x_str, y_str = raw.split(",")
        coords.append(WorldPoint(float(x_str), float(y_str)))
    return coords


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ws", default="ws://192.168.4.1/ws",
                        help="WebSocket del ESP32-S3")
    parser.add_argument("--output", default="capture.jpg",
                        help="Ruta donde guardar la foto")
    args = parser.parse_args()

    out = Path(args.output).resolve()
    print(f"Capturando frame desde {args.ws} …")
    capture_frame(args.ws, out)
    print(f"Imagen guardada en {out}")

    pixels = pick_pixels(out)
    worlds = prompt_world_points()

    H = solve_homography(pixels, worlds)
    print_results(H, pixels, worlds)


if __name__ == "__main__":
    main()