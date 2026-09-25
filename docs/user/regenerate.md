# x3m-regenerate: rebuild the mod-dependent data after installing mods

x3-modern ships two kinds of data that depend on the game files, including any
mods: the **fog families** (volumetric fog colours for nebula types the renderer
does not know, `x3m/fog-families.bin`) and the **merged-LOD overlay** (simplified
distant versions of ships and stations, the `addon/NN.cat/.dat` catalogues with an
`.x3m-lod.json` marker, plus `addon/mods/<mod>-x3m-lod.cat/.dat` for a mod
selected in the start menu). After you install, update or remove a mod, run
`x3m-regenerate` once. It regenerates both and overwrites the previous results.

## Use

1. Install your mods into the X3 directory as usual, and select the mod package
   in the game's start menu if the mod uses one (the tool bakes the selected one).
2. Quit the game.
3. Copy `x3m-regenerate.exe` (Windows) or `x3m-regenerate` (macOS) into the game
   directory, next to `X3AP.exe`. With CrossOver that is
   `~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3`.
4. Double-click it. It starts at once and asks nothing. A console window shows
   the progress; at the end it says `all done` (or which step failed) and waits
   for any key before it closes.
5. If a mod package was selected, select `<mod>-x3m-lod` instead of `<mod>` in
   the start menu (the summary line says so).

The same console text, with timestamps, goes to `x3m-regenerate.log` next to the
executable. The file is rewritten on each run.

## What the log says

- `processing fog <family> (i/n) baked`: a nebula family the renderer does not
  know got its fog colours. `palette checked (built-in profile)` means the family
  is one of the 14 the renderer already knows; nothing is added for it.
- `processing model <body> (i/n)`: one ship or station was baked. `[mod <name>]`
  marks a body from the selected mod package. On later runs only bodies whose
  files changed are baked again; the rest are reported as `unchanged` in the
  summary.
- `refused model <body>: <reason>` / `refused fog <family>: <reason>`: that item
  is left as the game has it (the original LOD models or the plain nebula
  cards). This is normal: some bodies cannot be simplified safely (for example
  `texel_floor`: the merged texture would be too blurry at the switch distance;
  `text_parse_error`: a text body outside the supported grammar; `texture_missing`
  or `no_dust_bodies` for a fog family). Refusals never stop the run.
- `slot plan: addon/NN ...`: where the overlay went. A mod that adds numbered
  catalogues pushes the overlay to the next free numbers; old slots become empty
  catalogues (`retired`) or are removed.
- `LOD summary` / `fog summary`: the counts.
- `recovered interrupted write of addon/NN.cat`: the previous run was killed
  while writing (window closed, power loss); the previous overlay was put back
  and the run continued normally.
- `fog layers: ...`: the catalogues the fog step read; a selected mod package is
  included, so its own nebula types get fog colours too.
- `FAILED`: a step could not finish; its previous results stay in place and the
  log holds the details (a traceback for an unexpected error). The exit code is 1.
- `refused: the game is running` / `holds no X3AP.exe`: nothing was done.

## How long it takes

The first run bakes every eligible ship and station: the vanilla game (620
bodies) took about 50 minutes on the development Mac (measured);
a large mod set is expected to take 1.5 to 2 hours (inferred). The fog step
takes seconds to a few minutes. Later runs only rebake bodies whose inputs
changed, so they are much shorter. Updating x3-modern itself can make every body
count as changed once (the baker records its own version). The program unpacks
itself to a temporary folder at each start: on macOS the first line appears
after about 8 seconds and the fog step starts about 6 seconds later; the Windows
build starts in about 1.5 seconds under CrossOver (measured; not measured on a
Windows PC).

## Removing the results

With the game closed, delete in the game directory:

- `x3m/fog-families.bin`, `x3m/fog-families.json` and their `.previous` copies
  (the renderer falls back to its 14 built-in fog families);
- every `addon/NN.cat` / `addon/NN.dat` that has an `addon/NN.x3m-lod.json` marker
  beside it, the marker itself, and `addon/x3m-lod-batch.json`,
  `addon/x3m-lod-batch-summary.txt` and `addon/x3m-lod-batch-bodies.txt`;
- `addon/mods/<mod>-x3m-lod.cat/.dat/.x3m-lod.json` (then select `<mod>` again
  in the start menu);
- `x3m-regenerate.log`.

Only delete overlay slots that are the highest `addon` numbers; if a mod
installed later added higher numbers, the game needs the numbering without gaps,
so keep the file pair (or rerun the tool, which retires it cleanly).

## Building the executable

From the repository: `python3 tools/regenerate/build.py` builds
`dist/x3m-regenerate` for the host (PyInstaller one-file; needs Python with NumPy
2.0.2, Pillow and `pip install --user pyinstaller`) and runs a smoke test on a
synthetic game directory. `python3 tools/regenerate/build.py --windows` builds
`dist/x3m-regenerate.exe` on the Mac under Wine in a dedicated CrossOver bottle
`X3M-Build` (created from the `win10_64` template; Python 3.12.10 x64 from
python.org installed quietly into it; `pip install pyinstaller numpy==2.0.2
pillow`), never in the game bottle, and smoke-tests it there; `--windows --dry`
only reports the bottle state. On a Windows PC: install Python 3.12 (64-bit,
python.org), then from the repository root `py -m pip install pyinstaller
numpy==2.0.2 pillow` and `py -m PyInstaller --noconfirm --distpath dist
--workpath build\regenerate-win tools\regenerate\x3m_regenerate.spec`.

Developers can run the same flow from source:
`python3 tools/regenerate/x3m_regenerate.py --game-dir <game> --no-wait`
(`--jobs N` sets the parallel jobs, default CPU count minus one; the LOD bake
also keeps its memory cap of about 7 GB per job).
