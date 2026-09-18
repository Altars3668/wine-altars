"""显式指定 Wine/prefix 才运行；探针不发送网络请求，不启动 Office。"""
import os
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ListenTimeoutParity(unittest.TestCase):
    def test_matches_recorded_windows_behavior(self):
        wine = os.environ.get('HTTP_OPTION_WINE')
        prefix = os.environ.get('HTTP_OPTION_PREFIX')
        if not wine or not prefix:
            self.skipTest('set HTTP_OPTION_WINE and HTTP_OPTION_PREFIX for this integration test')
        probe = ROOT / 'tools/httpopt/listen.exe'
        self.assertTrue(probe.is_file(), 'build tools/httpopt/listen.exe first')
        env = os.environ.copy()
        env.update(WINEPREFIX=prefix, WINEDEBUG='-all')
        result = subprocess.run([wine, str(probe)], env=env, text=True,
                                capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = (ROOT / 'tools/httpopt/listen-windows.txt').read_text().splitlines()
        self.assertEqual(result.stdout.splitlines(), expected)


if __name__ == '__main__':
    unittest.main()
