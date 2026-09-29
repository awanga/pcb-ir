#!/usr/bin/env python3
"""Loads a .kicad_pcb file with real KiCad (pcbnew) and reports success/failure.

Used by pcbnew_verify_test.cpp's optional, environment-gated real-KiCad
verification check (docs/rfcs/0003-kicad-importer-exporter.md --
"Verification against real KiCad"): the literal test of Phase 7's "exported
board re-opens in KiCad" acceptance criterion, which no amount of format-spec
reading can substitute for. Invoked as:

    $KICAD_PYTHON verify_with_pcbnew.py <path-to-.kicad_pcb>

Exits 0 if pcbnew loads the file without error, 1 otherwise. Not run by
default CI (no KiCad installed there); run locally wherever KiCad is
available, via pcbnew_verify_test.cpp.
"""

import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_with_pcbnew.py <path-to-.kicad_pcb>", file=sys.stderr)
        return 2

    import pcbnew  # noqa: PLC0415 -- only importable via KiCad's own bundled Python.

    # pcbnew.LoadBoard asserts ("create wxApp before calling this") without a
    # wx application context, even in a headless script -- verified on this
    # device (KiCad 10.0.6): a plain wx.App() with no event loop run is
    # enough to satisfy it.
    import wx  # noqa: PLC0415

    _app = wx.App()

    path = sys.argv[1]
    try:
        board = pcbnew.LoadBoard(path)
    except Exception as exc:  # pcbnew raises on a malformed/unparseable file.
        print(f"pcbnew failed to load '{path}': {exc}", file=sys.stderr)
        return 1

    if board is None:
        print(f"pcbnew.LoadBoard returned no board for '{path}'", file=sys.stderr)
        return 1

    print(f"OK: pcbnew {pcbnew.GetBuildVersion()} loaded '{path}' successfully")
    return 0


if __name__ == "__main__":
    sys.exit(main())
