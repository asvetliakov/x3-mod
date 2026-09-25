# PyInstaller spec: one-file console executable x3m-regenerate (macOS/Linux) or x3m-regenerate.exe (Windows).
# Build through tools/regenerate/build.py (it pins the output and work directories and runs the smoke test).
# The tools hash their own sources at run time (lod_overlay.tool_sha256 decides --sync reuse; the fog record
# carries baker and recipe hashes), so those .py files are bundled as data beside the compiled modules: the
# frozen tool then records the same hashes as a source checkout and a later run from either reuses the bake.
from pathlib import Path

ROOT = Path(SPECPATH).resolve().parents[1]
ANALYSIS = ROOT / 'tools' / 'analysis'
PATHS = [ROOT / 'tools' / 'regenerate', ANALYSIS, ROOT / 'tools', ROOT / 'tools' / 'build', ROOT / 'verification' / 'probe']
HASHED = [ANALYSIS / n for n in ('lod_atlas.py', 'lod_overlay.py', 'bob1.py', 'lod_batch_census.py', 'lod_recipes.py',
                                 'fog_families.py')] + [ROOT / 'tools' / 'build' / 'bake_fog_fields.py',
                                                        ROOT / 'tools' / 'fog_field_recipe.py']
TOOLS = ['fog_families', 'fog_family_inputs', 'bake_fog_fields', 'fog_field_recipe', 'lod_overlay', 'lod_overlay_check',
         'lod_atlas', 'lod_batch_census', 'lod_recipes', 'bob1', 'body_materials', 'sector_fog_census', 'inspect_x3',
         'game_guard', 'PIL.Image', 'PIL.JpegImagePlugin', 'PIL.TgaImagePlugin', 'PIL.PngImagePlugin']

a = Analysis(
    [str(ROOT / 'tools' / 'regenerate' / 'x3m_regenerate.py')],
    pathex=[str(p) for p in PATHS],
    datas=[(str(p), '.') for p in HASHED],
    hiddenimports=TOOLS,
    excludes=['tkinter', 'matplotlib', 'scipy', 'pandas', 'IPython', 'pytest', 'unittest.mock'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='x3m-regenerate',
    console=True,
    upx=False,
    strip=False,
    debug=False,
    runtime_tmpdir=None,
)
