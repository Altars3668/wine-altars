#!/usr/bin/env python3
"""给已注册的 Office SKU 目录补写 ApplicationBitmap，只写这一个值。

位图说明某个 SKU 覆盖哪些应用，取自安装自带的许可文件，属于产品定义而不是授权。
脚本不创建新的 SKU 条目、不改动身份字段、不导入任何许可状态、密钥或设备绑定；
写入后读回核对，取值与已有内容不一致时拒绝，不做覆盖。
"""
import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / 'sppc-catalog.py'
spec = importlib.util.spec_from_file_location('sppc_catalog', SCRIPT)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

KEY = re.compile(
    r'\[Software\\\\Wine\\\\SPPC\\\\Applications\\\\(\{[0-9a-fA-F-]{36}\})'
    r'\\\\Skus\\\\(\{[0-9a-fA-F-]{36}\})\]([^\[]*)', re.S)


def registered_skus(prefix):
    """只读地列出已注册的 (应用, SKU, 现有位图)。不新建任何条目。"""
    text = Path(prefix, 'system.reg').read_text(encoding='utf-8', errors='replace')
    rows = []
    for app, sku, body in KEY.findall(text):
        existing = re.search(r'"ApplicationBitmap"="([^"]*)"', body)
        rows.append((app, sku, existing.group(1) if existing else None))
    if not rows:
        raise SystemExit('no registered Office SKUs found; register the catalog first')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--licenses-dir', type=Path, required=True)
    parser.add_argument('--wine', type=Path)
    parser.add_argument('--output-reg', type=Path)
    parser.add_argument('--apply', action='store_true', help='explicitly import the generated values')
    args = parser.parse_args()
    if args.apply and not args.wine:
        parser.error('--apply needs --wine')

    bitmaps = catalog.collect_bitmaps(args.licenses_dir)
    rows = registered_skus(args.prefix)
    lines, pending, kept = ['Windows Registry Editor Version 5.00', ''], 0, 0
    for app, sku, existing in rows:
        value = bitmaps.get(catalog.guid(sku))
        if value is None:
            raise SystemExit(f'no application bitmap in the installation for {sku}')
        if existing is not None:
            # 幂等：相同取值跳过；不同取值一律拒绝，由人判断哪边是对的。
            if existing != value:
                raise SystemExit(f'refusing to overwrite a different bitmap for {sku}')
            kept += 1
            continue
        lines.append(f'[HKEY_LOCAL_MACHINE\\Software\\Wine\\SPPC\\Applications\\{app}\\Skus\\{sku}]')
        lines.append(f'"ApplicationBitmap"="{value}"')
        lines.append('')
        pending += 1
    print(f'registered SKUs: {len(rows)}; already correct: {kept}; to write: {pending}')
    if not pending:
        return 0
    data = ('﻿' + '\r\n'.join(lines) + '\r\n').encode('utf-16le')
    target = args.output_reg or Path(args.prefix, 'sppc-bitmaps.reg')
    target.write_bytes(data)
    print(f'wrote {target}')
    if not args.apply:
        print('dry run: pass --apply to import')
        return 0
    env = {'WINEPREFIX': str(Path(args.prefix).resolve()), 'WINEDEBUG': '-all',
           'PATH': '/usr/bin:/bin', 'HOME': str(Path.home())}
    result = subprocess.run([str(args.wine), 'reg', 'import', str(target)],
                            env=env, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise SystemExit('reg import failed: ' + (result.stderr or result.stdout))
    # 读回核对：每个待写 SKU 都必须出现正确取值。
    after = {(app, sku): existing for app, sku, existing in registered_skus(args.prefix)}
    wrong = [sku for app, sku, _ in rows if after.get((app, sku)) != bitmaps[catalog.guid(sku)]]
    if wrong:
        raise SystemExit(f'read-back mismatch for {len(wrong)} SKUs, first {wrong[0]}')
    print(f'imported and verified {pending} bitmaps')
    return 0


if __name__ == '__main__':
    sys.exit(main())
