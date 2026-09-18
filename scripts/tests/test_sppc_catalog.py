"""纯合成安装元数据测试；不把 XML 作为许可证安装或执行。"""
import copy
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'sppc-catalog.py'
spec = importlib.util.spec_from_file_location('sppc_catalog', SCRIPT)
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

SKU = '537ea5b5-7d50-4876-bd38-a53a77caca32'
FILE_ID = '11111111-2222-3333-4444-555555555555'
EDITION = 'Office16O365HomePremR_Subscription1'
PPD_NAME = 'O365HomePremR_Subscription1-ppd.xrm-ms'
UL_NAME = 'O365HomePremR_Subscription1-ul-oob.xrm-ms'
BITMAP = '0x0001F1BB'


def ppd(sku=SKU, app=catalog.OFFICE_APP, repeated=None, edition=EDITION):
    if repeated is None:
        repeated = app
    return (f'<r:license xmlns:r="{catalog.R[1:-1]}" xmlns:sl="{catalog.SL[1:-1]}" '
            f'xmlns:tm="{catalog.TM[1:-1]}" licenseId="{FILE_ID}">'
            f'<r:title>Office{sku.upper()} PPD License</r:title>'
            f'<sl:appId><sl:guid>{{{app}}}</sl:guid></sl:appId>'
            f'<tm:editionId value="{edition}"/>'
            f'<tm:infoStr name="applicationId">{{{repeated}}}</tm:infoStr>'
            '</r:license>').encode('utf-8')


def ul(sku=SKU, bitmap=BITMAP, extra=''):
    """合成 UL 许可文件：位图与 productSkuId 必须落在同一个 license 块内。"""
    block = ''
    if sku is not None and bitmap is not None:
        block = (f'<r:license xmlns:r="{catalog.R[1:-1]}" xmlns:tm="{catalog.TM[1:-1]}" licenseId="{FILE_ID}">'
                 f'<tm:infoStr name="productSkuId">{{{sku.upper()}}}</tm:infoStr>'
                 f'<tm:infoStr name="ApplicationBitmap">{bitmap}</tm:infoStr>'
                 '</r:license>')
    return (f'<rg:licenseGroup xmlns:rg="{catalog.R[1:-1]}">{block}{extra}'
            '</rg:licenseGroup>').encode('utf-8')


class CatalogValidation(unittest.TestCase):
    def setUp(self):
        directory = os.environ.get('SPPC_TEST_TMP') or os.environ.get('TMPDIR')
        self.work = tempfile.TemporaryDirectory(prefix='sppc-catalog-test-', dir=directory)
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.path = self.root / PPD_NAME
        self.path.write_bytes(ppd())
        self.ul_path = self.root / UL_NAME
        self.ul_path.write_bytes(ul())
        self.document = {
            'schema_version': 1,
            'source': {'kind': 'windows-cim-office-registration', 'office_version': '16.0.20430.20000',
                       'product_release_ids': ['O365HomePremRetail'], 'captured_at': '2026-09-07T00:00:00Z'},
            'products': [{'sku_id': SKU, 'application_id': catalog.OFFICE_APP,
                          'name': 'Office 16, ' + EDITION + ' edition'}],
        }

    def validate(self, document=None, products=None):
        return catalog.validate_catalog(self.document if document is None else document, self.root,
                                        ['O365HomePremRetail'] if products is None else products,
                                        '16.0.20208.20000')

    def test_bitmap_comes_from_the_same_licence_block(self):
        # 位图按块内显式的 productSkuId 归属，与 PPD 的身份校验各自独立。
        result = self.validate()
        self.assertEqual(result['products'][0]['application_bitmap'], BITMAP)
        self.assertIn('"ApplicationBitmap"="' + BITMAP + '"',
                      catalog.registry_bytes(result).decode('utf-16le'))

    def test_a_registered_sku_without_a_bitmap_is_rejected(self):
        # Office 会对每个已注册 SKU 查询这个值，缺失时不能注册不完整的目录。
        self.ul_path.unlink()
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_conflicting_bitmaps_are_rejected(self):
        # 同一 SKU 出现两个取值时必须拒绝，不能挑一个用。
        (self.root / 'Other-ul-oob.xrm-ms').write_bytes(ul(bitmap='0x00000112'))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_repeated_identical_bitmap_is_accepted(self):
        # 同一取值在多个文件里重复出现是正常的，不算冲突。
        (self.root / 'Other-ul-phn.xrm-ms').write_bytes(ul())
        self.assertEqual(self.validate()['products'][0]['application_bitmap'], BITMAP)

    def test_malformed_bitmap_is_rejected(self):
        for value in ('0x112', '0001F1BB', '0xZZZZZZZZ', '0x0001F1BBB'):
            with self.subTest(value=value):
                self.ul_path.write_bytes(ul(bitmap=value))
                with self.assertRaises(catalog.CatalogError):
                    self.validate()

    def test_a_bitmap_without_a_sku_is_not_borrowed(self):
        # 没有 productSkuId 的块不能从邻块借身份，结果应是"缺位图"而不是错配。
        orphan = (f'<r:license xmlns:r="{catalog.R[1:-1]}" xmlns:tm="{catalog.TM[1:-1]}" licenseId="{FILE_ID}">'
                  f'<tm:infoStr name="ApplicationBitmap">{BITMAP}</tm:infoStr></r:license>')
        self.ul_path.write_bytes(ul(sku=None, bitmap=None, extra=orphan))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_bitmap_files_outside_the_installation_are_not_read(self):
        # 目录扫描不跟随符号链接到安装目录之外。
        outside = Path(self.work.name).parent / 'sppc-outside-bitmap.xrm-ms'
        outside.write_bytes(ul(bitmap='0x00000112'))
        self.addCleanup(outside.unlink)
        link = self.root / 'Linked-ul-oob.xrm-ms'
        try:
            link.symlink_to(outside)
        except OSError:
            self.skipTest('symlinks unavailable')
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_identity_is_from_title_not_root_license_id(self):
        result = self.validate()
        self.assertEqual(result['products'][0]['sku_id'], SKU)
        self.assertNotEqual(result['products'][0]['sku_id'], FILE_ID)
        self.assertTrue(result['version_mismatch'])
        self.assertEqual(result['products'][0]['edition_id'], EDITION)

    def test_render_is_metadata_only_and_deterministic(self):
        result = self.validate()
        output = catalog.registry_bytes(result)
        self.assertEqual(output, catalog.registry_bytes(self.validate()))
        self.assertTrue(output.startswith(b'\xff\xfe'))
        text = output.decode('utf-16')
        self.assertIn('"CatalogVersion"=dword:00000001', text)
        self.assertIn(SKU, text)
        for name in ('LicenseStatus', 'HardwareId', 'ProductKey', 'GraceTime', 'Expiry', 'Token'):
            self.assertNotIn('"' + name + '"=', text)

    def test_rejects_authorization_fields(self):
        for section, key in ((self.document, 'License'), (self.document['source'], 'status'),
                             (self.document['products'][0], 'LicenseStatus')):
            section[key] = 'TEST-NOT-A-CREDENTIAL'
            with self.assertRaises(catalog.CatalogError):
                self.validate()
            del section[key]

    def test_file_distribution_is_not_registration(self):
        self.document['source']['kind'] = 'all-files-in-licenses16'
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_different_product_selection(self):
        with self.assertRaises(catalog.CatalogError):
            self.validate(products=['O365ProPlusRetail'])

    def test_rejects_duplicate_registration(self):
        self.document['products'].append(copy.deepcopy(self.document['products'][0]))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_root_file_id_as_sku(self):
        self.document['products'][0]['sku_id'] = FILE_ID
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_zero_sku(self):
        self.document['products'][0]['sku_id'] = '00000000-0000-0000-0000-000000000000'
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_foreign_application(self):
        self.document['products'][0]['application_id'] = FILE_ID
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_ppd_application_disagreement(self):
        self.path.write_bytes(ppd(repeated=FILE_ID))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_ppd_edition_disagreement(self):
        self.path.write_bytes(ppd(edition='Office16DifferentProduct'))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_missing_local_definition(self):
        self.path.unlink()
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_case_colliding_filenames(self):
        self.root.joinpath(PPD_NAME.upper().replace('.XRM-MS', '.xrm-ms').replace('-PPD.', '-ppd.')).write_bytes(ppd())
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_uppercase_ppd_filename_is_supported(self):
        self.path.rename(self.root / PPD_NAME.upper())
        self.assertEqual(self.validate()['products'][0]['sku_id'], SKU)

    def test_rejects_oversized_ppd(self):
        self.path.write_bytes(b' ' * (1024 * 1024 + 1))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_definition_outside_installation(self):
        outside = self.root.parent / (self.root.name + '-outside.xrm-ms')
        outside.write_bytes(ppd())
        self.addCleanup(outside.unlink)
        self.path.unlink()
        self.path.symlink_to(outside)
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_xml_encoding_is_explicit(self):
        self.path.write_bytes(b'\xef\xbb\xbf' + ppd())
        self.assertEqual(self.validate()['products'][0]['sku_id'], SKU)
        self.path.write_bytes(ppd().decode().encode('utf-16'))
        self.assertEqual(self.validate()['products'][0]['sku_id'], SKU)

    def test_rejects_dtd(self):
        self.path.write_bytes(b'<!DOCTYPE license [<!ENTITY test "x">]>' + ppd())
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_namespace_substitution(self):
        self.path.write_bytes(ppd().replace(catalog.R[1:-1].encode(), b'urn:untrusted'))
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_ambiguous_timestamp(self):
        self.document['source']['captured_at'] = '2026-09-07T00:00:00'
        with self.assertRaises(catalog.CatalogError):
            self.validate()

    def test_rejects_name_path_injection(self):
        self.document['products'][0]['name'] = 'Office 16, Office16../../outside edition'
        with self.assertRaises(catalog.CatalogError):
            self.validate()


if __name__ == '__main__':
    unittest.main()
