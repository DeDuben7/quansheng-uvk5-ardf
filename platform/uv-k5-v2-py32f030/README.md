# scout — UV-K5 V2 (Puya PY32F030)

**Status: placeholder. Not started.**

This folder is reserved for the port of the scout ARDF firmware to the
**Quansheng UV-K5 hardware version 2**, which uses the Puya **PY32F030**
microcontroller (Cortex-M0+) — a completely different chip from the V1's
Nyquest DP32G030.

Nothing here is built or flashable yet. The active porting effort targets
V3 first (see `../uv-k5-v3-py32f071/`); the V2 PY32F030 differs from the V3
PY32F071 mainly in flash/RAM size and peripheral set, so most of the V3 port
is expected to carry over here once V3 boots and is verified on hardware.

Reference firmware for V2/PY32F030: muzkr's forks (see project notes).
