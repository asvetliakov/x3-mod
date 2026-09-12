"""Game-process guard classification on synthetic process-table lines."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verification/probe'))
from game_guard import game_lines, is_game_line

GHIDRA = ('4242 /usr/bin/java -Xmx4G -cp /opt/ghidra/support/../Ghidra/Framework/Utility/lib/Utility.jar '
          'ghidra.GhidraLauncher ghidra.app.util.headless.AnalyzeHeadless /tmp/x3-ghidra-research X3Render '
          '-process X3AP.exe -noanalysis -postScript X3LoadingOrchestration.java')


class GameGuardTests(unittest.TestCase):
    def test_game_process_forms_are_detected(self):
        for line in ('123 C:\\X3\\X3AP.exe',
                     '124 C:\\X3\\X3AP.exe -noabout',
                     '125 X3AP.exe',
                     '126 /Users/me/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/X3AP.exe',
                     '127 X3AP',
                     '128 /Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle Steam '
                     '--no-update --dll d3d9=b --workdir C:\\X3 C:\\X3\\X3AP.exe',
                     '129 /Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/'
                     'winewrapper.exe --enable-alt-loader macdrv --wait-children --start -- C:/X3/X3AP.exe',
                     '130 /opt/wine/bin/wine-preloader /opt/wine/bin/wine64 ../drive_c/X3/x3ap.EXE'.replace(
                         '/opt/wine/bin/wine-preloader ', 'wine-preloader ')):
            self.assertTrue(is_game_line(line), line)

    def test_analysis_tooling_is_ignored(self):
        for line in (GHIDRA,
                     '4243 /Applications/Ghidra/support/analyzeHeadless /tmp/x3 X3Render -process X3AP.exe',
                     '4244 pgrep -ifl X3AP.exe',
                     '4245 java -jar tool.jar X3AP.exe'):
            self.assertFalse(is_game_line(line), line)

    def test_other_wine_processes_are_not_the_game(self):
        for line in ('200 C:\\windows\\system32\\winedevice.exe CX_GRAPHICS_BACKEND=dxmt',
                     '201 /Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wineserver-x86',
                     '202 /Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle Steam '
                     '--no-update --dll d3d9=b Z:/Users/me/x3-mod/build/motion_output_fixture.exe Z:/tmp/X3AP.exe.log',
                     '203 wine --bottle Steam --start -- C:/X3/Steam.lnk',
                     '204 /bin/sh -c grep X3AP.exe results.txt',
                     '205 python3 verification/probe/run_motion_output.py',
                     '',
                     '206'):
            self.assertFalse(is_game_line(line), line)

    def test_game_lines_filters_inventory(self):
        text = '\n'.join([GHIDRA, '200 winedevice.exe', '  777 C:\\X3\\X3AP.exe', '4244 pgrep -ifl X3AP.exe'])
        self.assertEqual(game_lines(text), ['777 C:\\X3\\X3AP.exe'])
        self.assertEqual(game_lines(GHIDRA + '\n'), [])


if __name__ == '__main__':
    unittest.main()
