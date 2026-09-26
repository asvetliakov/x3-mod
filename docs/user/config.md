# Settings: x3m.ini

The mod's settings live in one text file, `x3m.ini`, shipped next to `d3d9.dll`.

## Where it goes

Put `x3m.ini` in the game folder, next to `d3d9.dll` and `X3AP.exe` (with CrossOver:
`~/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c/X3`). The mod reads it once when the game
starts. The mod also works without the file: every setting has a built-in default, and the shipped file only
shows those defaults.

## Changing a setting

Open `x3m.ini` in a text editor. Each setting has a short explanation above it and looks like this:

```ini
; Sharpening applied after the anti-aliasing, against its slight softness. 0 = none, 1 = strongest.
; Needs: taa.
;taa_sharpen = 0.75
```

A line starting with `;` is a comment, so the setting above is not active and the mod uses the value shown.
To change it, delete the `;` in front of the setting and edit the value:

```ini
taa_sharpen = 0.5
```

Save the file and restart the game. To return to the default, put the `;` back or delete the line. On/off
settings accept `1`/`0` as well as `on`/`off`, `true`/`false` and `yes`/`no`. The `[sections]` only group the
settings for reading; a setting works in any section. Nothing may follow the value on the same line (no
trailing comments).

## Checking what was loaded

The mod writes `x3m.log` next to `d3d9.dll`. Near its top:

- `config_open` names the file it read and counts the settings it took, and the lines it could not use
  (`unknown=` a misspelt setting, `invalid=` a value out of range, `duplicate=` a setting given twice: the last
  one counts);
- one `config_key` line per line it could not use, with the setting name and the problem;
- `config_file` lists every setting the file set that the mod uses; a setting the file set but something
  outside the file overrode (an environment variable, as a developer launcher sends) is named after
  `overridden_by_env=` instead;
- `proxy_options` lists every setting in force with where it came from: `@file` (your file), `@default`
  (the built-in default) or `@env` (the environment).

A line the mod cannot use is skipped and that setting keeps its default; the game always starts. The mod never
writes to `x3m.ini`. When you update the mod, keep your edited `x3m.ini` (do not let the new one overwrite
it) and compare it with the new file for new settings: a newer mod version ignores settings it no longer has
(they appear as `unknown` in the log) and uses defaults for settings your file does not mention.

## Reporting a problem

Set `debug = 1` in `x3m.ini`, reproduce the problem, quit the game and send `x3m.log` (and `x3m.ini` if you
changed it). Set `debug` back afterwards: the detailed log grows by about 8 KB per frame, about 1.7 GB per hour at
60 fps (`perf = 1` writes about 0.36 GB per hour).
