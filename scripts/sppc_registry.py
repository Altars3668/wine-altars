"""仅写 Office 的 Wine SPPC 元数据命名空间，保留备份并拒绝覆盖冲突目录。"""
import csv
from datetime import datetime, timezone
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import uuid

APP_KEY = r'HKEY_LOCAL_MACHINE\Software\Wine\SPPC\Applications\{0ff1ce15-a989-479d-af46-f275c6370663}'
OFFICE_PROCESSES = {'WINWORD.EXE', 'EXCEL.EXE', 'POWERPNT.EXE', 'OUTLOOK.EXE', 'MSACCESS.EXE',
                    'MSPUB.EXE', 'ONENOTE.EXE', 'OLICENSEHEARTBEAT.EXE', 'OFFICECLICKTORUN.EXE'}


class RegistrationError(RuntimeError):
    pass


def unquote(value):
    if len(value) < 2 or value[0] != '"' or value[-1] != '"':
        raise RegistrationError('unsupported registry string')
    text, result, index = value[1:-1], [], 0
    while index < len(text):
        character = text[index]
        if character == '"':
            raise RegistrationError('unescaped registry quote')
        if character == '\\':
            index += 1
            if index >= len(text) or text[index] not in ('\\', '"'):
                raise RegistrationError('unsupported registry escape')
            character = text[index]
        if ord(character) < 32:
            raise RegistrationError('registry control character')
        result.append(character)
        index += 1
    return ''.join(result)


def parse_registry(data):
    if len(data) > 2 * 1024 * 1024:
        raise RegistrationError('registry metadata too large')
    try:
        text = data.decode('utf-16') if data.startswith((b'\xff\xfe', b'\xfe\xff')) else data.decode('utf-8-sig')
    except UnicodeError:
        raise RegistrationError('registry metadata encoding') from None
    result, current = {}, None
    root = APP_KEY.casefold()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(';') or line in ('Windows Registry Editor Version 5.00', 'REGEDIT4'):
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].casefold()
            if current != root and not current.startswith(root + '\\'):
                raise RegistrationError('registry key outside the approved Office namespace')
            result.setdefault(current, {})
            parent = current
            while parent != root:
                parent = parent.rsplit('\\', 1)[0]
                result.setdefault(parent, {})
            continue
        if current is None:
            raise RegistrationError('registry value without a key')
        match = re.fullmatch(r'("(?:[^"\\]|\\.)*"|@)=(.*)', line)
        if not match:
            raise RegistrationError('unsupported registry metadata entry')
        name = '' if match.group(1) == '@' else unquote(match.group(1)).casefold()
        raw = match.group(2)
        if re.fullmatch(r'dword:[0-9a-fA-F]{8}', raw):
            value = ('dword', int(raw[6:], 16))
        else:
            value = ('string', unquote(raw))
        if name in result[current] and result[current][name] != value:
            raise RegistrationError('conflicting duplicate registry value')
        result[current][name] = value
    if root not in result:
        raise RegistrationError('Office metadata root missing')
    return result


def contained(part, whole):
    return all(key in whole and all(whole[key].get(name) == value for name, value in values.items())
               for key, values in part.items())


def apply_metadata(backend, data):
    """只创建新目录或确认幂等；已有不一致目录留给操作者审阅，不自动覆盖。"""
    expected = parse_registry(data)
    previous = backend.read()
    if previous is not None:
        if contained(expected, previous):
            return 'unchanged'
        raise RegistrationError('existing Office catalog differs; no changes made')
    backend.backup_absence()
    try:
        backend.write(data)
        current = backend.read()
        if current != expected:
            raise RegistrationError('registry read-back differs from the validated metadata')
    except Exception as error:
        # 只回滚本次创建且仍属于预期集合的键；遇到并发外来变化就保留现场。
        try:
            current = backend.read()
            if current is not None:
                if not contained(current, expected):
                    raise RegistrationError('unexpected registry change; automatic rollback refused')
                backend.delete()
                if backend.read() is not None:
                    raise RegistrationError('registry rollback verification failed')
        except Exception as rollback:
            raise RegistrationError('registration failed; inspect the preserved backup before recovery') from rollback
        raise RegistrationError('registration failed; original absent state restored') from error
    return 'created'


class WineRegistry:
    def __init__(self, wine, prefix):
        self.wine = str(Path(wine).resolve())
        self.prefix = Path(prefix).resolve()
        if not (self.prefix / 'system.reg').is_file() or not (self.prefix / 'drive_c').is_dir():
            raise RegistrationError('an already initialized target prefix is required')
        self.env = os.environ.copy()
        self.env.update(WINEPREFIX=str(self.prefix), WINEDEBUG='-all')
        for name in ('WINE_WAM_CAPTURE_DIR', 'WINE_WAM_CAPTURE_ROOT', 'WINEDLLPATH', 'WINELOADER', 'WINESERVER'):
            self.env.pop(name, None)
        self.backup_dir = None
        self.sequence = 0

    def run(self, arguments):
        # 不等后台服务关闭继承的 PIPE；临时命令输出不进入持久备份。
        scratch = Path(os.environ['CLAUDE_JOB_DIR']) / 'tmp' if os.environ.get('CLAUDE_JOB_DIR') else None
        with tempfile.TemporaryFile(dir=scratch) as output:
            try:
                result = subprocess.run([self.wine, *arguments], env=self.env, stdin=subprocess.DEVNULL,
                                        stdout=output, stderr=subprocess.STDOUT, timeout=60)
            except subprocess.SubprocessError as error:
                raise RegistrationError('Wine command did not complete; inspect the preserved registry state') from error
            output.seek(0)
            return result.returncode, output.read().decode('utf-8', 'replace')

    def target_metadata(self):
        code, output = self.run(['tasklist', '/fo', 'csv', '/nh'])
        if code:
            raise RegistrationError('cannot verify target process state')
        if any(row and row[0].upper() in OFFICE_PROCESSES for row in csv.reader(io.StringIO(output))):
            raise RegistrationError('close Office consumers before registering metadata')
        values = []
        key = r'HKLM\Software\Microsoft\Office\ClickToRun\Configuration'
        for name in ('ProductReleaseIds', 'VersionToReport'):
            code, output = self.run(['reg', 'query', key, '/v', name, '/reg:64'])
            matches = re.findall(r'^\s*' + re.escape(name) + r'\s+REG_SZ\s+(.+?)\s*$', output, re.M)
            if code or len(matches) != 1:
                raise RegistrationError('cannot read the actual target installation identity')
            values.append(matches[0])
        return values[0].split(','), values[1]

    def prepare_backup(self):
        if self.backup_dir is None:
            name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]
            self.backup_dir = self.prefix / 'drive_c/ProgramData/Wine/SPPC/Backups' / name
            self.backup_dir.mkdir(parents=True, mode=0o700)

    def windows_path(self, path):
        return 'C:\\' + str(path.relative_to(self.prefix / 'drive_c')).replace('/', '\\')

    def read(self):
        self.prepare_backup()
        self.sequence += 1
        path = self.backup_dir / f'registry-{self.sequence}.reg'
        code, output = self.run(['reg', 'export', APP_KEY, self.windows_path(path), '/reg:64'])
        if code == 0 and path.is_file():
            return parse_registry(path.read_bytes())
        # 本版本 Wine reg.exe 的明确缺键消息才被视作 absent；其他失败一律拒绝写入。
        missing = ('Unable to find the specified registry key', 'The system was unable to find the specified registry key')
        if code == 1 and not path.exists() and any(message in output for message in missing):
            return None
        raise RegistrationError('registry export failed or absence could not be established')

    def backup_absence(self):
        self.prepare_backup()
        path = self.backup_dir / 'before.json'
        with path.open('x') as output:
            json.dump({'registry_key': APP_KEY, 'existed': False}, output, indent=2)

    def write(self, data):
        path = self.backup_dir / 'validated-metadata.reg'
        with path.open('xb') as output:
            output.write(data)
        code, _ = self.run(['regedit', '/S', self.windows_path(path)])
        if code:
            raise RegistrationError('metadata registry import failed')

    def delete(self):
        code, _ = self.run(['reg', 'delete', APP_KEY, '/f', '/reg:64'])
        if code:
            raise RegistrationError('metadata rollback deletion failed')
