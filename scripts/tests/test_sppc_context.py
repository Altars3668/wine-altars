"""显式 opt-in 的 SPPC winetest；仅使用新 prefix 和合成产品目录。"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SppcContextIntegration(unittest.TestCase):
    def test_context_and_catalog_contract(self):
        wine = os.environ.get('SPPC_TEST_WINE')
        scratch = os.environ.get('SPPC_TEST_TMP')
        if not wine or not scratch:
            self.skipTest('set SPPC_TEST_WINE and SPPC_TEST_TMP for isolated integration tests')
        wine = Path(wine).absolute()
        default_server = wine.parent / 'server/wineserver' if (wine.parent / 'server/wineserver').is_file() else wine.parent / 'wineserver'
        server = Path(os.environ.get('SPPC_TEST_WINESERVER', default_server)).resolve()
        self.assertTrue(server.is_file(), 'matching wineserver is required')
        build = Path(os.environ.get('SPPC_TEST_BUILD', ROOT / 'wine-src/build64-cx')).resolve()
        artifacts = {
            'sppc.dll': build / 'dlls/sppc/x86_64-windows/sppc.dll',
            'slc.dll': build / 'dlls/slc/x86_64-windows/slc.dll',
            'sppc_test.exe': build / 'dlls/sppc/tests/x86_64-windows/sppc_test.exe',
        }
        for source in artifacts.values():
            self.assertTrue(source.is_file(), f'build {source} first')
        work = tempfile.TemporaryDirectory(prefix='sppc-context-', dir=Path(scratch).resolve())
        self.addCleanup(work.cleanup)
        base = Path(work.name)
        prefix = base / 'prefix'
        drive = prefix / 'drive_c'
        drive.mkdir(parents=True, mode=0o700)
        devices = prefix / 'dosdevices'
        devices.mkdir(mode=0o700)
        devices.joinpath('c:').symlink_to('../drive_c', target_is_directory=True)
        sandbox = base / 'virtual-z'
        sandbox.mkdir(mode=0o700)
        devices.joinpath('z:').symlink_to(sandbox, target_is_directory=True)
        env = os.environ.copy()
        for key in ('WINE_WAM_CAPTURE_DIR', 'WINE_WAM_CAPTURE_ROOT', 'WINELOADER', 'WINESERVER', 'WINEDLLPATH'):
            env.pop(key, None)
        env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all', WINESERVER=str(server),
                   WINEDLLOVERRIDES='sppc,slc=b', WINETEST_DEBUG='1')

        def stop_server():
            result = subprocess.run([str(server), '-k'], env=env,
                                    capture_output=True, timeout=30)
            self.assertTrue(result.returncode == 0 or (result.returncode == 1 and not result.stderr.strip()),
                            'failed to stop isolated wineserver')
            subprocess.run([str(server), '-w'], env=env,
                           capture_output=True, timeout=30, check=True)

        self.addCleanup(stop_server)
        result = subprocess.run([str(wine), 'wineboot.exe', '-u'], env=env,
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(devices.joinpath('z:').resolve(), sandbox)
        for name, source in artifacts.items():
            target = drive / name if name.endswith('.exe') else drive / 'windows/system32' / name
            shutil.copy2(source, target)
        result = subprocess.run([str(wine), r'C:\sppc_test.exe', 'context'], env=env,
                                capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        totals = re.findall(r'(\d+) tests executed.*?(\d+) failures.*?(\d+) skipped', output)
        self.assertTrue(totals, output)
        self.assertTrue(any(int(tests) >= 50 for tests, _, _ in totals), output)
        self.assertTrue(all(int(failures) == 0 and int(skipped) == 0 for _, failures, skipped in totals), output)
        self.assertNotIn('Test failed', output)
        print('SPPC winetest:', totals[-1], flush=True)

        # 继续在同一个一次性 prefix 中验证真实 reg.exe 导入及幂等行为。
        from test_sppc_catalog import ppd, ul, SKU, EDITION, PPD_NAME, UL_NAME, catalog
        licenses = drive / 'Program Files/Microsoft Office/root/Licenses16'
        licenses.mkdir(parents=True)
        licenses.joinpath(PPD_NAME).write_bytes(ppd())
        # 位图与身份分处两个文件，和真实安装一样：注册需要两者都在。
        licenses.joinpath(UL_NAME).write_bytes(ul())
        installation = r'HKLM\Software\Microsoft\Office\ClickToRun\Configuration'
        for name, value in (('ProductReleaseIds', 'O365HomePremRetail'), ('VersionToReport', '16.0.20208.20000')):
            result = subprocess.run([str(wine), 'reg', 'add', installation, '/v', name, '/t', 'REG_SZ',
                                     '/d', value, '/f', '/reg:64'], env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        document = {'schema_version': 1,
                    'source': {'kind': 'windows-cim-office-registration', 'office_version': '16.0.20430.20000',
                               'product_release_ids': ['O365HomePremRetail'], 'captured_at': '2026-09-07T00:00:00Z'},
                    'products': [{'sku_id': SKU, 'application_id': catalog.OFFICE_APP,
                                  'name': 'Office 16, ' + EDITION + ' edition'}]}
        source = base / 'synthetic-catalog.json'
        source.write_text(json.dumps(document))
        command = [sys.executable, str(ROOT / 'scripts/sppc-catalog.py'), str(source), '--licenses-dir',
                   str(licenses), '--register', '--wine', str(wine), '--prefix', str(prefix)]
        for expected in ('created', 'unchanged'):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('registration=' + expected, result.stdout)
            self.assertIn('authorization-fields-written=0', result.stdout)


if __name__ == '__main__':
    unittest.main()
