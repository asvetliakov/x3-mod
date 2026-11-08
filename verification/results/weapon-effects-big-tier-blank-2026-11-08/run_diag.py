import sys, pathlib
sys.path.insert(0, '/home/asvetl/x3-mod/verification/probe')
import native_host
B = pathlib.Path(sys.argv[1])
d3dx = native_host.windows_path(native_host.GAME_DIR / 'd3dx9_37.dll')
done, text = native_host.run_native(B / 'weapon_effects_fixture.exe', [d3dx], B, 900,
                                    env={'X3M_WEAPON_EFFECTS_BOOM_ONLY': '1'}, log=B / 'fixture')
print('exit', done.returncode, 'lines', text.count('\n'))
