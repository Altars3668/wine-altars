#!/usr/bin/env python3
"""校验显式 Office 注册目录并生成元数据注册表文件；不导入权利或凭据。"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import uuid
import xml.etree.ElementTree as ET

R = '{urn:mpeg:mpeg21:2003:01-REL-R-NS}'
SL = '{http://www.microsoft.com/DRM/XrML2/SL/v2}'
TM = '{http://www.microsoft.com/DRM/XrML2/TM/v2}'
OFFICE_APP = '0ff1ce15-a989-479d-af46-f275c6370663'
REGISTRY_ROOT = r'HKEY_LOCAL_MACHINE\Software\Wine\SPPC\Applications'
GUID_PATTERN = re.compile(r'[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}')
VERSION_PATTERN = re.compile(r'\d+\.\d+\.\d+\.\d+')
BITMAP_PATTERN = re.compile(r'0x[0-9A-Fa-f]{8}')


class CatalogError(ValueError):
    pass


def require_fields(value, required, optional=()):
    if not isinstance(value, dict) or not set(required) <= value.keys() or value.keys() - set(required) - set(optional):
        raise CatalogError('missing or unexpected metadata fields')


def safe_string(value, limit=512):
    if not isinstance(value, str) or not value or len(value) > limit or any(ord(c) < 32 for c in value):
        raise CatalogError('invalid metadata string')
    return value


def guid(value):
    value = safe_string(value, 38)
    if value.startswith('{') and value.endswith('}'):
        value = value[1:-1]
    if not GUID_PATTERN.fullmatch(value):
        raise CatalogError('invalid GUID shape')
    parsed = uuid.UUID(value)
    if not parsed.int:
        raise CatalogError('zero GUID is not a registered product identifier')
    return str(parsed)


def product_ids(values):
    if not isinstance(values, list) or not values or len(values) > 32:
        raise CatalogError('explicit product selection required')
    normalized = []
    for value in values:
        value = safe_string(value, 128)
        if not re.fullmatch(r'[A-Za-z0-9._-]+', value):
            raise CatalogError('invalid product release ID')
        normalized.append(value)
    if len(set(value.casefold() for value in normalized)) != len(normalized):
        raise CatalogError('duplicate product release ID')
    return sorted(normalized, key=str.casefold)


def parse_ppd(data):
    if not isinstance(data, bytes) or not data or len(data) > 1024 * 1024:
        raise CatalogError('invalid PPD size')
    try:
        if data.startswith((b'\xff\xfe', b'\xfe\xff')):
            text = data.decode('utf-16')
        else:
            text = data.decode('utf-8-sig')
        if re.search(r'<!\s*(?:DOCTYPE|ENTITY)\b', text, re.I):
            raise CatalogError('DTD and entity declarations are not accepted')
        root = ET.fromstring(text)
    except (UnicodeError, ET.ParseError):
        raise CatalogError('invalid PPD XML') from None
    if root.tag != R + 'license':
        raise CatalogError('unsupported PPD namespace')
    titles = {(node.text or '').strip() for node in root.iter(R + 'title')}
    if len(titles) != 1:
        raise CatalogError('ambiguous PPD title')
    match = re.fullmatch(r'Office([0-9A-Fa-f-]{36}) PPD License', titles.pop())
    if not match:
        raise CatalogError('PPD title does not identify a SKU')
    sku = guid(match.group(1))
    apps = {guid(node.text or '') for app in root.iter(SL + 'appId') for node in app if node.tag == SL + 'guid'}
    repeated = {guid(node.text or '') for node in root.iter(TM + 'infoStr') if node.get('name') == 'applicationId'}
    editions = {safe_string(node.get('value'), 128) for node in root.iter(TM + 'editionId')}
    if len(apps) != 1 or apps != repeated or len(editions) != 1:
        raise CatalogError('inconsistent PPD application or edition metadata')
    # 根 licenseId 是许可证文件标识，不能拿来当产品 SKU。
    return {'sku_id': sku, 'application_id': apps.pop(), 'edition_id': editions.pop()}


def parse_bitmap_document(data):
    """从一个许可文件收集 SKU 与 ApplicationBitmap 的配对。

    位图说明某个 SKU 覆盖哪些应用，是安装自带的产品定义，不是许可状态、密钥
    或设备绑定。归属只认同一个 license 块内显式的 productSkuId，不按文件名推断；
    块内缺少任何一项就跳过该块，而不是拿邻近的值补齐。
    """
    # 上限比 PPD 宽：安装目录里还有密钥配置一类的大文件，按大小跳过会漏掉 SKU。
    if not isinstance(data, bytes) or not data or len(data) > 4 * 1024 * 1024:
        raise CatalogError('invalid licence size')
    try:
        if data.startswith((b'\xff\xfe', b'\xfe\xff')):
            text = data.decode('utf-16')
        else:
            text = data.decode('utf-8-sig')
        if re.search(r'<!\s*(?:DOCTYPE|ENTITY)\b', text, re.I):
            raise CatalogError('DTD and entity declarations are not accepted')
        root = ET.fromstring(text)
    except (UnicodeError, ET.ParseError):
        raise CatalogError('invalid licence XML') from None
    found = {}
    for licence in (node for node in root.iter() if node.tag == R + 'license'):
        info = {}
        for node in licence.iter(TM + 'infoStr'):
            if node.get('name') in ('productSkuId', 'ApplicationBitmap'):
                info.setdefault(node.get('name'), []).append((node.text or '').strip())
        skus, bitmaps = info.get('productSkuId', []), info.get('ApplicationBitmap', [])
        if not skus or not bitmaps:
            continue
        # 同一块内重复出现可以，取值分歧不可以。
        if len({item.casefold() for item in skus}) != 1 or len(set(bitmaps)) != 1:
            raise CatalogError('ambiguous SKU or bitmap inside one licence block')
        if not BITMAP_PATTERN.fullmatch(bitmaps[0]):
            raise CatalogError('application bitmap is not a 32-bit hex value')
        found.setdefault(guid(skus[0]), set()).add(bitmaps[0])
    return found


def collect_bitmaps(licenses_dir):
    """扫描安装目录的许可文件得到 SKU 到位图的映射；取值冲突一律拒绝，不做挑选。"""
    root = Path(licenses_dir).resolve(strict=True)
    merged = {}
    for path in sorted(root.iterdir()):
        if not path.name.casefold().endswith('.xrm-ms'):
            continue
        if not path.is_file() or not path.resolve().is_relative_to(root):
            raise CatalogError('licence file outside the installation directory')
        with path.open('rb') as stream:
            data = stream.read(4 * 1024 * 1024 + 1)
        for sku, values in parse_bitmap_document(data).items():
            merged.setdefault(sku, set()).update(values)
    conflicting = sorted(sku for sku, values in merged.items() if len(values) != 1)
    if conflicting:
        raise CatalogError('conflicting application bitmaps for ' + conflicting[0])
    return {sku: values.pop() for sku, values in merged.items()}


def validate_catalog(document, licenses_dir, target_products, target_version):
    require_fields(document, ('schema_version', 'source', 'products'))
    if type(document['schema_version']) is not int or document['schema_version'] != 1:
        raise CatalogError('unsupported catalog schema')
    source = document['source']
    require_fields(source, ('kind', 'office_version', 'product_release_ids', 'captured_at'))
    if source['kind'] != 'windows-cim-office-registration':
        raise CatalogError('registered-product provenance required')
    source_version = safe_string(source['office_version'], 64)
    if not VERSION_PATTERN.fullmatch(source_version) or not VERSION_PATTERN.fullmatch(safe_string(target_version, 64)):
        raise CatalogError('invalid Office version')
    try:
        captured = datetime.fromisoformat(safe_string(source['captured_at'], 64).replace('Z', '+00:00'))
        if captured.tzinfo is None:
            raise ValueError
    except ValueError:
        raise CatalogError('capture time needs an explicit timezone') from None
    selection = product_ids(target_products)
    if {s.casefold() for s in selection} != {s.casefold() for s in product_ids(source['product_release_ids'])}:
        raise CatalogError('source registration belongs to a different product selection')
    products = document['products']
    if not isinstance(products, list) or not products or len(products) > 512:
        raise CatalogError('explicit nonempty registered-product list required')
    root = Path(licenses_dir).resolve(strict=True)
    if not root.is_dir():
        raise CatalogError('PPD directory required')
    names = {}
    for path in root.iterdir():
        if not path.name.casefold().endswith('-ppd.xrm-ms'):
            continue
        key = path.name.casefold()
        if key in names:
            raise CatalogError('case-colliding PPD filenames')
        names[key] = path
    bitmaps = collect_bitmaps(root)
    result, seen = [], set()
    for record in products:
        require_fields(record, ('sku_id', 'application_id', 'name'))
        sku, app = guid(record['sku_id']), guid(record['application_id'])
        if app != OFFICE_APP:
            raise CatalogError('non-Office registration in Office catalog')
        if sku in seen:
            raise CatalogError('duplicate SKU registration')
        seen.add(sku)
        name = safe_string(record['name'])
        match = re.fullmatch(r'Office 16, (Office16[A-Za-z0-9_]+) edition', name)
        if not match:
            raise CatalogError('registration name lacks an unambiguous Office edition')
        edition = match.group(1)
        filename = edition[len('Office16'):] + '-ppd.xrm-ms'
        path = names.get(filename.casefold())
        if path is None or not path.is_file() or not path.resolve().is_relative_to(root):
            raise CatalogError('matching local PPD is missing or outside the installation')
        with path.open('rb') as stream:
            data = stream.read(1024 * 1024 + 1)
        local = parse_ppd(data)
        if local != {'sku_id': sku, 'application_id': app, 'edition_id': edition}:
            raise CatalogError('registered identity disagrees with the local PPD')
        # Office 会对每个已注册 SKU 查询这个值，缺一项就不注册不完整的目录。
        bitmap = bitmaps.get(sku)
        if bitmap is None:
            raise CatalogError('no application bitmap for a registered SKU')
        result.append({'sku_id': sku, 'application_id': app, 'name': name,
                       'edition_id': edition, 'ppd_file': path.name,
                       'ppd_sha256': hashlib.sha256(data).hexdigest(),
                       'application_bitmap': bitmap})
    return {'schema_version': 1, 'source': dict(source), 'target_office_version': target_version,
            'target_product_release_ids': selection,
            'version_mismatch': source_version != target_version,
            'products': sorted(result, key=lambda item: (item['application_id'], item['sku_id']))}


def registry_string(value):
    return '"' + safe_string(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def registry_bytes(catalog):
    lines = ['Windows Registry Editor Version 5.00', '']
    apps = sorted({row['application_id'] for row in catalog['products']})
    for app in apps:
        app_key = REGISTRY_ROOT + '\\{' + app + '}'
        for row in catalog['products']:
            if row['application_id'] != app:
                continue
            lines.append('[' + app_key + '\\Skus\\{' + row['sku_id'] + '}]')
            fields = {'Name': row['name'], 'Edition': row['edition_id'],
                      'ApplicationBitmap': row['application_bitmap'],
                      'PpdFile': row['ppd_file'], 'PpdSha256': row['ppd_sha256'],
                      'RegistrationSource': catalog['source']['kind'],
                      'SourceOfficeVersion': catalog['source']['office_version'],
                      'TargetOfficeVersion': catalog['target_office_version']}
            lines.extend(registry_string(key) + '=' + registry_string(value) for key, value in fields.items())
            lines.append('')
        # 完整 SKU 元数据之后才写应用目录版本标记；仍须停用消费者后导入、读回及回滚。
        lines.extend(['[' + app_key + ']', '"CatalogVersion"=dword:00000001',
                      '"ProductReleaseIds"=' + registry_string(','.join(catalog['target_product_release_ids'])), ''])
    return ('﻿' + '\r\n'.join(lines) + '\r\n').encode('utf-16le')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalog', type=Path)
    parser.add_argument('--licenses-dir', type=Path, required=True)
    parser.add_argument('--target-products', help='validation-only: actual target ProductReleaseIds')
    parser.add_argument('--target-version', help='validation-only: actual target version')
    parser.add_argument('--output-reg', type=Path, help='create a new metadata-only .reg file')
    parser.add_argument('--register', action='store_true', help='explicitly register verified metadata in the target prefix')
    parser.add_argument('--wine', type=Path)
    parser.add_argument('--prefix', type=Path)
    args = parser.parse_args()
    from sppc_registry import RegistrationError, WineRegistry, apply_metadata
    if args.register:
        if not args.wine or not args.prefix or args.target_products or args.target_version:
            parser.error('--register requires --wine/--prefix and reads target identity itself')
    elif not args.target_products or not args.target_version:
        parser.error('validation requires --target-products and --target-version')
    backend = None
    try:
        with args.catalog.open('rb') as stream:
            data = stream.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            raise CatalogError('catalog file too large')
        document = json.loads(data)
        if args.register:
            prefix = args.prefix.resolve()
            if not args.licenses_dir.resolve().is_relative_to(prefix / 'drive_c'):
                raise CatalogError('registration requires PPDs from the target prefix')
            backend = WineRegistry(args.wine, prefix)
            products, version = backend.target_metadata()
        else:
            products, version = args.target_products.split(','), args.target_version
        result = validate_catalog(document, args.licenses_dir, products, version)
        rendered = registry_bytes(result)
        if args.output_reg:
            with args.output_reg.open('xb') as output:
                output.write(rendered)
        if backend:
            outcome = apply_metadata(backend, rendered)
            print('registration=' + outcome)
            print('metadata-backup=' + str(backend.backup_dir))
    except (CatalogError, RegistrationError) as error:
        parser.exit(1, 'catalog operation failed: ' + str(error) + '\n')
    except (OSError, ValueError) as error:
        # 非受控错误只输出类型，不把原始输入或授权材料放入日志。
        parser.exit(1, 'catalog operation failed: ' + type(error).__name__ + '\n')
    print('validated-products=' + str(len(result['products'])))
    print('source-target-version-mismatch=' + str(result['version_mismatch']).lower())
    print('authorization-fields-written=0')


if __name__ == '__main__':
    main()
