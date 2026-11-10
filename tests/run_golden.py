#!/usr/bin/env python3
"""Check the decoder against rtl_433's output for the same payloads.

Builds tests/decoder_cli.cpp with g++, feeds it every payload in golden/bresser_golden.jsonl
and compares each result with the recorded rtl_433 output: every measurement must match in
presence and value. Exits non-zero on any mismatch.
"""

import json
import subprocess
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
GOLDEN = HERE / "golden" / "bresser_golden.jsonl"
BUILD = ROOT / "build" / "tests"

# rtl_433 key -> decoder_cli key. light_klx (= lux / 1000) and mic are not compared.
FIELDS = {
    "temperature_C": "temperature",
    "humidity": "humidity",
    "wind_max_m_s": "wind_gust",
    "wind_avg_m_s": "wind_speed",
    "wind_dir_deg": "wind_direction",
    "rain_mm": "rain",
    "light_lux": "illuminance_lux",
    "uvi": "uv_index",
    "temperature_1_C": "globe_temperature",
    "pm2_5_ug_m3": "pm2_5",
    "pm10_0_ug_m3": "pm10",
    "co2_ppm": "co2",
    "hcho_ppb": "hcho",
    "voc_level": "voc_level",
}
MODELS = {1: "Bresser-7in1", 8: "Bresser-7in1", 12: "Bresser-7in1", 13: "Bresser-7in1"}
MODELS |= {10: "Bresser-CO2", 11: "Bresser-HCHOVOC"}
TOLERANCE = 1e-3


def build() -> Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / "decoder_cli"
    cmd = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O2",
        f"-I{ROOT / 'components' / 'bresser'}",
        str(HERE / "decoder_cli.cpp"),
        str(ROOT / "components" / "bresser" / "bresser_decoder.cpp"),
        "-o",
        str(exe),
    ]
    subprocess.run(cmd, check=True)
    return exe


def compare(expect: dict | None, got: dict | None) -> list[str]:
    if expect is None or got is None:
        return [] if expect is got else [f"expected {expect}, got {got}"]
    errors = []

    def check(name, want, have):
        if have is None:
            errors.append(f"{name}: expected {want}, missing")
        elif abs(float(want) - float(have)) > TOLERANCE:
            errors.append(f"{name}: expected {want}, got {have}")

    check("id", expect["id"], got["id"])
    check("battery_ok", expect["battery_ok"], int(got["battery_ok"]))
    check("startup", expect.get("startup", 0), int(got["startup"]))
    if "channel" in expect:
        check("channel", expect["channel"], got["channel"])
    if MODELS.get(got["type"]) != expect["model"]:
        errors.append(f"model: expected {expect['model']}, got type {got['type']}")
    for rtl_key, key in FIELDS.items():
        if rtl_key in expect:
            check(key, expect[rtl_key], got.get(key))
        elif key in got:
            errors.append(f"{key}: expected absent, got {got[key]}")
    return errors


def main() -> int:
    vectors = [
        json.loads(line)
        for line in GOLDEN.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    ]
    exe = build()
    stdin = "".join(v["hex"] + "\n" for v in vectors)
    out = subprocess.run([exe], input=stdin, capture_output=True, text=True, check=True)
    results = [json.loads(line) for line in out.stdout.splitlines()]
    if len(results) != len(vectors):
        print(f"decoder_cli printed {len(results)} lines for {len(vectors)} vectors")
        return 1

    passed, failed = Counter(), Counter()
    for vector, got in zip(vectors, results, strict=True):
        errors = compare(vector["expect"], got)
        if errors:
            failed[vector["source"]] += 1
            if sum(failed.values()) <= 20:
                print(f"FAIL {vector['source']} {vector['hex']}")
                for e in errors:
                    print(f"     {e}")
        else:
            passed[vector["source"]] += 1

    for source in sorted(passed.keys() | failed.keys()):
        print(f"{passed[source]:5d} pass {failed[source]:5d} fail  {source}")
    total_failed = sum(failed.values())
    print(f"{sum(passed.values())}/{len(vectors)} vectors match rtl_433")
    return 1 if total_failed else 0


if __name__ == "__main__":
    sys.exit(main())
