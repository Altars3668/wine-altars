"""服务信息探针的显式隔离回归，不使用 Office prefix 或原生插件。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SppcServiceInformationIntegration(unittest.TestCase):
    def test_service_probe_and_forwarder(self):
        wine = os.environ.get('SPPC_TEST_WINE')
        scratch = os.environ.get('SPPC_TEST_TMP')
        if not wine or not scratch:
            self.skipTest('set SPPC_TEST_WINE and SPPC_TEST_TMP for isolated integration tests')
        wine = Path(wine).absolute()
        scratch = Path(scratch).resolve()
        default_server = wine.parent / 'server/wineserver' if (wine.parent / 'server/wineserver').is_file() else wine.parent / 'wineserver'
        server = Path(os.environ.get('SPPC_TEST_WINESERVER', default_server)).resolve()
        self.assertTrue(server.is_file(), 'matching wineserver is required')
        compiler = shutil.which('x86_64-w64-mingw32-gcc')
        self.assertIsNotNone(compiler, 'the service probe requires the x64 MinGW compiler')
        work = tempfile.TemporaryDirectory(prefix='sppc-service-', dir=scratch)
        self.addCleanup(work.cleanup)
        base = Path(work.name)
        prefix = base / 'prefix'
        drive = prefix / 'drive_c'
        drive.mkdir(parents=True, mode=0o700)
        devices = prefix / 'dosdevices'
        devices.mkdir(mode=0o700)
        devices.joinpath('c:').symlink_to('../drive_c', target_is_directory=True)
        sandbox = base / 'private-z'
        sandbox.mkdir(mode=0o700)
        devices.joinpath('z:').symlink_to(sandbox, target_is_directory=True)
        env = os.environ.copy()
        for key in ('WINE_WAM_CAPTURE_DIR', 'WINE_WAM_CAPTURE_ROOT', 'WINELOADER', 'WINEDLLPATH'):
            env.pop(key, None)
        env.update(WINEPREFIX=str(prefix), WINESERVER=str(server), WINEDEBUG='-all',
                   WINEDLLOVERRIDES='sppc,slc=b', TMPDIR=str(scratch))
        result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-municode',
                                 '-o', str(drive / 'service-info.exe'),
                                 str(ROOT / 'tools/sppcprobe/service-info.c'), '-lversion'],
                                env=env, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        def stop_server():
            result = subprocess.run([str(server), '-k'], env=env, capture_output=True, timeout=30)
            self.assertTrue(result.returncode == 0 or (result.returncode == 1 and not result.stderr.strip()),
                            'failed to stop isolated wineserver')
            subprocess.run([str(server), '-w'], env=env, capture_output=True, timeout=30, check=True)

        self.addCleanup(stop_server)
        result = subprocess.run([str(wine), 'wineboot.exe', '-u'], env=env,
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(devices.joinpath('z:').resolve(), sandbox)

        # 自测与错误命令行必须在加载服务 API 前完成。
        result = subprocess.run([str(wine), r'C:\service-info.exe', '--selftest'], env=env,
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('selftests=14 failures=0', result.stdout)
        self.assertNotIn('requested-module=', result.stdout)
        for args in (['sppc', 'active'], ['sppc', 'unknown-case', '--query'], ['unknown-dll', 'active', '--query']):
            result = subprocess.run([str(wine), r'C:\service-info.exe', *args], env=env,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertNotIn('requested-module=', result.stdout)

        # ActivePlugins 如实报告 Wine 这一侧唯一活动的许可对象提供者，也就是 sppc.dll
        # 自己；原生那两个 SPP 服务插件在这里并不存在，不能报告它们的路径。格式与原生
        # 对照一致：SL_DATA_MULTI_SZ、双 NUL 结尾、长度以字节计。两 DLL 必须给出同一
        # 结果，因为 slc 转发到 sppc。查询成功只表示许可平台可用，不表示已授权。
        # bytes=60 是 C:\windows\system32\sppc.dll 的 28 个字符加两个 NUL，共 30 个 WCHAR。
        for library in ('sppc', 'slc'):
            for case, expect_type in (('active', 'type=7'), ('active-no-type', 'type=unchanged')):
                with self.subTest(library=library, case=case):
                    result = subprocess.run([str(wine), r'C:\service-info.exe', library, case, '--query'],
                                            env=env, capture_output=True, text=True, timeout=90)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('resolved-query-module=sppc.dll', result.stdout.lower())
                    self.assertIn(f'query hr=0 {expect_type} bytes=60 data-unchanged=0 data-null=0', result.stdout)
                    self.assertIn('plugin-count=1', result.stdout)
                    self.assertIn('plugin[0].basename=sppc.dll', result.stdout)
                    self.assertIn('probe-failures=0', result.stdout)

        # 原生实测的失败契约：未知属性名、参数错误和句柄错误各自不同，且每条失败路径
        # 都不改写调用方的输出。不能把它们合并成同一个错误码。
        for library in ('sppc', 'slc'):
            for case, expect in (('missing', '0xc004f012'), ('null-handle', '0x80070057'),
                                 ('null-name', '0x80070057'), ('null-size', '0x80070057'),
                                 ('null-data', '0x80070057'), ('invalid-handle', '0xc0030005'),
                                 ('closed', '0xc0030005')):
                with self.subTest(library=library, case=case):
                    result = subprocess.run([str(wine), r'C:\service-info.exe', library, case, '--query'],
                                            env=env, capture_output=True, text=True, timeout=90)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(f'query hr={expect} type=unchanged bytes=unchanged data-unchanged=1 data-null=0',
                                  result.stdout)
                    self.assertIn('probe-failures=0', result.stdout)
                    self.assertNotIn('plugin-count=', result.stdout)
        print('SPPC service probe: 14 parser cases, 3 CLI guards, 18 forwarded queries; failures=0', flush=True)


if __name__ == '__main__':
    unittest.main()
