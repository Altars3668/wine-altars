"""只在全新 Wine prefix 中使用合成控制字符串；不启动 Office，不访问真实 broker。"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DLL_NAME = 'windows.security.authentication.onlineid.dll'
SSL_SCOPE = 'service::ssl.live.com::MBI_SSL_SHORT openid profile'
OFFICE_SCOPE = 'service::officeapps.live.com::MBI_SSL_SHORT openid profile'
CLIENT_ID = '00000000480728C5'
CONTROL = 'TEST-ONLY-CAPTURE-CONTROL-NOT-A-CREDENTIAL'
LEGACY_CONTROL = 'TEST-ONLY-LEGACY-CONTROL-NOT-A-CREDENTIAL'


def scope_directory(scope):
    return re.sub(r'[^A-Za-z0-9._-]', '_', scope)


@unittest.skipUnless(os.environ.get('WAM_CAPTURE_TEST_WINE'),
                     'set WAM_CAPTURE_TEST_WINE and WAM_CAPTURE_TEST_TMP for isolated tests')
class CaptureContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wine = Path(os.environ['WAM_CAPTURE_TEST_WINE']).resolve()
        scratch = Path(os.environ['WAM_CAPTURE_TEST_TMP']).resolve()
        cls.work = tempfile.TemporaryDirectory(prefix='wam-contract-', dir=scratch)
        cls.addClassCleanup(cls.work.cleanup)
        cls.base = Path(cls.work.name)
        cls.prefix = cls.base / 'prefix'
        cls.env = os.environ.copy()
        for key in ('WINE_WAM_CAPTURE_DIR', 'WINE_WAM_CAPTURE_ROOT',
                    'WINE_WAM_TOKEN_FILE', 'WINELOADER', 'WINESERVER', 'WINEDLLPATH'):
            cls.env.pop(key, None)
        cls.env.update(WINEPREFIX=str(cls.prefix), WINEDEBUG='-all',
                       WINEDLLOVERRIDES='windows.security.authentication.onlineid=b')
        cls.drive = cls.prefix / 'drive_c'
        cls.drive.mkdir(parents=True, mode=0o700)
        cls.sandbox = cls.base / 'virtual-z'
        cls.legacy = cls.sandbox / 'tmp/office-wam-tokens'
        cls.legacy.mkdir(parents=True, mode=0o700)
        devices = cls.prefix / 'dosdevices'
        devices.mkdir(mode=0o700)
        devices.joinpath('c:').symlink_to('../drive_c', target_is_directory=True)
        zdrive = devices / 'z:'
        # 包括首次 wineboot 在内，任何 Wine 进程都不能经 Z: 访问宿主根目录。
        zdrive.symlink_to(cls.sandbox, target_is_directory=True)
        if zdrive.resolve() != cls.sandbox:
            raise AssertionError('private Z: mapping was not established')
        # cleanup 只作用于本测试刚建立的 prefix，不能终止用户的 Office。
        cls.addClassCleanup(cls.stop_server)
        result = subprocess.run([str(cls.wine), 'wineboot.exe', '-u'], env=cls.env,
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(f'isolated wineboot failed: {result.returncode}\n{result.stderr}')
        if not zdrive.is_symlink() or zdrive.resolve() != cls.sandbox:
            raise AssertionError('wineboot changed the private Z: mapping')
        source = Path(os.environ.get('WAM_CAPTURE_TEST_DLL',
                     str(cls.wine.parent.parent / 'lib/wine/x86_64-windows' / DLL_NAME)))
        shutil.copy2(source, cls.drive / 'windows/system32' / DLL_NAME)
        shutil.copy2(ROOT / 'tools/wamprobe/wamprobe.exe', cls.drive / 'wamprobe.exe')

    @classmethod
    def stop_server(cls):
        result = subprocess.run([str(cls.wine.parent / 'wineserver'), '-k'], env=cls.env,
                                capture_output=True, timeout=30)
        # server/main.c: -k 在已无 server 时返回 1；仍需 -w 确认退出。
        if result.returncode != 0 and (result.returncode != 1 or result.stderr.strip()):
            raise AssertionError(f'isolated server stop failed: {result.returncode}\n{result.stderr!r}')
        subprocess.run([str(cls.wine.parent / 'wineserver'), '-w'], env=cls.env,
                       capture_output=True, timeout=30, check=True)

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix='capture-', dir=self.drive))
        self.primary = self.case / 'primary'
        self.primary.mkdir()
        self.scoped = self.case / 'scopes'
        self.scoped.mkdir()
        self.primary.joinpath('response-0-account-id').write_text('TEST-ONLY-ACCOUNT', encoding='utf-8')
        self.primary.joinpath('response-0-account-name').write_text('test@example.invalid', encoding='utf-8')
        self.primary.joinpath('response-0-token').write_text(CONTROL, encoding='utf-8')
        self.env = type(self).env.copy()
        self.env['WINE_WAM_CAPTURE_DIR'] = self.windows_path(self.primary)
        self.env['WINE_WAM_CAPTURE_ROOT'] = self.windows_path(self.scoped)
        # Z: 已映射到本测试的私有目录，这些文件不会触及宿主 /tmp 中的凭据。
        for file in self.legacy.iterdir():
            if not file.is_file() or file.is_symlink():
                self.fail('unexpected legacy test fixture')
            file.unlink()
        self.legacy.joinpath('account-id').write_text('TEST-ONLY-ACCOUNT', encoding='utf-8')
        self.legacy.joinpath('account-username').write_text('test@example.invalid', encoding='utf-8')

    def windows_path(self, path):
        return 'C:\\' + str(path.relative_to(self.drive)).replace('/', '\\')

    def capture(self, scope=SSL_SCOPE):
        directory = self.scoped / scope_directory(scope)
        directory.mkdir()
        for name, text in {
            'request-scope': scope, 'request-client-id': CLIENT_ID,
            'request-profile': 'office-silent',
            'response-0-account-id': 'TEST-ONLY-ACCOUNT',
            'response-0-token': CONTROL,
        }.items():
            directory.joinpath(name).write_text(text, encoding='utf-8')
        return directory

    def legacy_token(self, scope):
        self.legacy.joinpath(scope_directory(scope) + '.txt').write_text(LEGACY_CONTROL, encoding='utf-8')

    def probe(self, mode='office-silent', scope=SSL_SCOPE, success=True, token=CONTROL):
        result = subprocess.run([str(self.wine), r'C:\wamprobe.exe', mode, '-', scope],
                                env=self.env, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        self.assertIn(f'response-status={0 if success else 3} mode={mode}', result.stdout)
        self.assertIn(f'failures={0 if success else 1}\n', result.stdout)
        if success:
            self.assertIn(f'token chars={len(token)} kind=opaque', result.stdout)
            if mode == 'office-silent':
                self.assertIn('request-property-roundtrips=10', result.stdout)
        else:
            self.assertNotIn('token chars=', result.stdout)
        self.assertNotIn(CONTROL, result.stdout + result.stderr)
        self.assertNotIn(LEGACY_CONTROL, result.stdout + result.stderr)

    def test_scoped_ssl(self):
        self.capture()
        self.probe()

    def test_scoped_officeapps(self):
        self.capture(OFFICE_SCOPE)
        self.probe(scope=OFFICE_SCOPE)

    def test_unknown_scope_does_not_read_legacy(self):
        scope = 'https://example.invalid/.default openid profile'
        self.legacy_token(scope)
        self.probe(scope=scope, success=False)

    def test_plain_request_does_not_read_legacy(self):
        self.capture()
        self.legacy_token(SSL_SCOPE)
        self.probe(mode='plain', success=False)

    def test_colliding_directory_names_do_not_match_scope(self):
        requested = 'https://example.invalid/a?b openid profile'
        captured = 'https://example.invalid/a:b openid profile'
        self.assertEqual(scope_directory(requested), scope_directory(captured))
        self.capture(captured)
        self.legacy_token(requested)
        self.probe(scope=requested, success=False)

    def test_wrong_client_manifest(self):
        self.capture().joinpath('request-client-id').write_text('TEST-OTHER-CLIENT', encoding='utf-8')
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_wrong_profile_manifest(self):
        self.capture().joinpath('request-profile').write_text('plain', encoding='utf-8')
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_wrong_account_manifest(self):
        self.capture().joinpath('response-0-account-id').write_text('TEST-OTHER-ACCOUNT', encoding='utf-8')
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_missing_manifest(self):
        self.capture().joinpath('request-client-id').unlink()
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_empty_token(self):
        self.capture().joinpath('response-0-token').write_bytes(b'')
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_invalid_utf8_token(self):
        self.capture().joinpath('response-0-token').write_bytes(b'\xff')
        self.legacy_token(SSL_SCOPE)
        self.probe(success=False)

    def test_root_without_primary_does_not_read_legacy(self):
        self.capture()
        self.legacy_token(SSL_SCOPE)
        self.env.pop('WINE_WAM_CAPTURE_DIR')
        self.probe(success=False)

    def test_single_capture_remains_compatible(self):
        self.env.pop('WINE_WAM_CAPTURE_ROOT')
        self.probe()

    def test_single_capture_wrong_scope_does_not_read_legacy(self):
        self.env.pop('WINE_WAM_CAPTURE_ROOT')
        self.legacy_token(OFFICE_SCOPE)
        self.probe(scope=OFFICE_SCOPE, success=False)

    def test_capture_disabled_preserves_legacy_mode(self):
        self.env.pop('WINE_WAM_CAPTURE_ROOT')
        self.env.pop('WINE_WAM_CAPTURE_DIR')
        self.legacy_token(SSL_SCOPE)
        self.probe(mode='plain', token=LEGACY_CONTROL)


if __name__ == '__main__':
    unittest.main()
