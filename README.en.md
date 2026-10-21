# IIDX 33 NOTES RADAR Cap Unlock

> ## ⚠️ IMPORTANT — ONLINE SERVERS
>
> **Never disable `EnablePlayerCap` while connected to an online server.**
> With it disabled the player radar (PNR) may exceed 200.00 and be recorded by the
> server as an abnormal value, **which can get your account banned**.
> Always keep `EnablePlayerCap=1` when playing online; only consider changing it in a
> fully offline setup.

[简体中文](README.md) | **English** | [日本語](README.ja.md) | [한국어](README.ko.md)

A cap-unlock plugin for **beatmania IIDX 33 Sparkle Shower (LDJ)** NOTES RADAR.

The game hard-clamps the six chart-radar axes to **200.00**. This plugin removes that cap so
the song-select screen and the sub screen show the real values (which can exceed 200), while
**the player radar (PNR) is still capped at 200.00 when it is settled and reported**.

---

## Features

| Feature | Description | Option |
|---|---|---|
| Chart radar cap unlock | Song select / sub screen show real values (>200.00) | `EnableCap` |
| Player radar cap | PNR settlement and reporting stay capped at 200.00 | `EnablePlayerCap` |
| Manual recalc | `Ctrl+Alt+R` calls the game's own full-chart radar recalculation | `EnableRecalcKey` |

## How it works

Each chart radar axis is produced by a **4-segment piecewise-linear normaliser** applied to a
raw metric; the result is in `[0, 2.0]` and is multiplied by `10000` into a 1/100 fixed-point
integer. The 200.00 limit is **not** a `min(v, 200)` instruction — it is the endpoint of the
last segment (`1.75 + 0.25 × 1.0 = 2.0`). NOP-ing the "clamp ratio to 1.0" branch of that last
segment lets the value extrapolate linearly, so it exceeds 200.00 while staying monotonic.

The full reverse-engineering write-up (parameter table, data structures, verification) is in
**[docs/algorithm.md](docs/algorithm.md)**.

## Compatibility

- Supports **IIDX 33 Sparkle Shower** (bm2dx.dll 18,467,992 bytes) and
  **IIDX 34 ZINRAI** (bm2dx.dll 18,200,216 bytes); the build is detected at
  runtime via the module `SizeOfImage`.
- The DLL byte-verifies every patch site; on a mismatch it skips that change and logs it
  instead of corrupting the game.
- Coexists with spice2x plugins; loaded through spice2x's `-k` argument.

## Build

Any C compiler works (MSVC / clang / zig):

```bat
zig cc -target x86_64-windows-gnu -shared -O2 -o notesradar.dll src\notesradar.c
```

or just run `build.bat`.

## Install

1. Put `notesradar.dll` and `notesradar.ini` next to `bm2dx.dll`;
2. Add this to the spice2x command line:

   ```
   -k notesradar.dll
   ```

3. Start the game. `notesradar.log` should contain:

   ```
   notesradar loaded (build ...; cap=1 pnr=1 2dx=0)
   cap removal: 3/3 sites patched
   addBinary hook installed ...
   all done
   ```

## Configuration

`notesradar.ini`

```ini
EnableCap=1          ; chart radar cap unlock
EnablePlayerCap=1    ; PNR settlement cap 200.00 (must be 1 online)
Enable2dxCompat=0    ; 2dxtra compatibility (experimental)
EnableRecalcKey=1    ; Ctrl+Alt+R full recalculation
```

## Known limitations

- **2dxtra special chart generation** (kichiku / kiraku / all scratch) reads the chart radar
  and assumes `<= 200.00`. With `EnableCap` on, switching to those chart types may crash —
  this is an inherent conflict. Set `EnableCap=0` and restart if you need them.
- `Enable2dxCompat` is experimental (intercepts 2dxtra's write-back). 2dxtra's code does not
  follow the Windows x64 ABI (it relies on r9 being preserved and calls with an unaligned
  stack), so repeated attempts remain unstable. **Disabled by default.**
- With radar values above 200 the radar hexagon is drawn outside its frame (the game
  normalises by `/200`); this is expected.

## Disclaimer

For **local / offline research and personal use only**. You must legally own the game.
The author is not responsible for any data loss, account ban, or other consequences.
**Do not use this in online matches or in any way that affects other players.**

## License

MIT
