"""注册事务使用内存后端验证，不修改任何 Wine/Windows 注册表。"""
import copy
import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'sppc_registry.py'
spec = importlib.util.spec_from_file_location('sppc_registry_tested', SCRIPT)
registry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(registry)

ROOT = registry.APP_KEY.casefold()
SKU_KEY = ROOT + r'\Skus\{537ea5b5-7d50-4876-bd38-a53a77caca32}'
DATA = ('Windows Registry Editor Version 5.00\r\n\r\n[' + registry.APP_KEY +
        ']\r\n"CatalogVersion"=dword:00000001\r\n[' + SKU_KEY +
        ']\r\n"Name"="Test metadata only"\r\n').encode('utf-16')


class MemoryRegistry:
    def __init__(self, state=None, failure=None):
        self.state = copy.deepcopy(state)
        self.failure = failure
        self.backups = self.writes = self.deletes = 0

    def read(self):
        return copy.deepcopy(self.state)

    def backup_absence(self):
        self.backups += 1

    def write(self, data):
        self.writes += 1
        self.state = registry.parse_registry(data)
        if self.failure == 'partial':
            self.state = {ROOT: {}}
            raise OSError('controlled import failure')
        if self.failure == 'concurrent':
            self.state[ROOT + r'\unrelated'] = {'owner': ('string', 'another writer')}
            raise OSError('controlled concurrent change')
        if self.failure == 'silent':
            self.state = None

    def delete(self):
        self.deletes += 1
        self.state = None


class RegistryTransaction(unittest.TestCase):
    def test_new_catalog_has_backup_and_verified_state(self):
        backend = MemoryRegistry()
        self.assertEqual(registry.apply_metadata(backend, DATA), 'created')
        self.assertEqual(backend.state, registry.parse_registry(DATA))
        self.assertEqual((backend.backups, backend.writes, backend.deletes), (1, 1, 0))

    def test_idempotent_registration_does_not_write(self):
        backend = MemoryRegistry(registry.parse_registry(DATA))
        self.assertEqual(registry.apply_metadata(backend, DATA), 'unchanged')
        self.assertEqual((backend.backups, backend.writes, backend.deletes), (0, 0, 0))

    def test_unrelated_existing_products_remain(self):
        existing = registry.parse_registry(DATA)
        existing[ROOT + r'\skus\other-product'] = {'name': ('string', 'other metadata')}
        backend = MemoryRegistry(existing)
        self.assertEqual(registry.apply_metadata(backend, DATA), 'unchanged')
        self.assertEqual(backend.state, existing)
        self.assertEqual(backend.writes, 0)

    def test_existing_conflict_is_not_overwritten(self):
        existing = registry.parse_registry(DATA)
        existing[SKU_KEY.casefold()]['name'] = ('string', 'different registration')
        backend = MemoryRegistry(existing)
        with self.assertRaises(registry.RegistrationError):
            registry.apply_metadata(backend, DATA)
        self.assertEqual(backend.state, existing)
        self.assertEqual((backend.backups, backend.writes, backend.deletes), (0, 0, 0))

    def test_partial_new_catalog_is_rolled_back(self):
        backend = MemoryRegistry(failure='partial')
        with self.assertRaises(registry.RegistrationError):
            registry.apply_metadata(backend, DATA)
        self.assertIsNone(backend.state)
        self.assertEqual((backend.backups, backend.writes, backend.deletes), (1, 1, 1))

    def test_concurrent_change_refuses_automatic_deletion(self):
        backend = MemoryRegistry(failure='concurrent')
        with self.assertRaises(registry.RegistrationError):
            registry.apply_metadata(backend, DATA)
        self.assertIn(ROOT + r'\unrelated', backend.state)
        self.assertEqual(backend.deletes, 0)

    def test_silent_noop_is_not_reported_as_success(self):
        backend = MemoryRegistry(failure='silent')
        with self.assertRaises(registry.RegistrationError):
            registry.apply_metadata(backend, DATA)
        self.assertIsNone(backend.state)

    def test_keys_outside_office_namespace_are_rejected(self):
        backend = MemoryRegistry()
        outside = b'Windows Registry Editor Version 5.00\n[HKEY_LOCAL_MACHINE\\Software\\Unrelated]\n"Name"="no"\n'
        with self.assertRaises(registry.RegistrationError):
            registry.apply_metadata(backend, outside)
        self.assertEqual(backend.writes, 0)

    def test_unescaped_quote_is_not_accepted(self):
        with self.assertRaises(registry.RegistrationError):
            registry.unquote('"invalid"quote"')

    def test_unknown_registry_data_type_is_not_guessed(self):
        data = ('[' + registry.APP_KEY + ']\n"Other"=hex:00,01\n').encode()
        with self.assertRaises(registry.RegistrationError):
            registry.parse_registry(data)

    def test_quoted_metadata_roundtrip(self):
        self.assertEqual(registry.unquote(r'"A \\"'), 'A \\')
        self.assertEqual(registry.unquote(r'"A \"B\""'), 'A "B"')


if __name__ == '__main__':
    unittest.main()
