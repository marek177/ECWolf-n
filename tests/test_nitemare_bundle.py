import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('bundle', Path(__file__).parents[1] / 'tools/nitemare_bundle.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)

class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in bundle.REQUIRED:
            (self.root/name.lower()).write_bytes(('synthetic '+name).encode())
        self.manifest = {'schema': 1, 'files': bundle.inventory(self.root)}

    def test_exact_case_insensitive_bundle(self):
        bundle.verify(self.root, self.manifest)

    def test_every_role_is_bound(self):
        for name in bundle.REQUIRED:
            with self.subTest(name=name):
                path = self.root/name.lower()
                old = path.read_bytes()
                path.write_bytes(old+b'changed')
                with self.assertRaisesRegex(ValueError, name):
                    bundle.verify(self.root, self.manifest)
                path.write_bytes(old)

    def test_swapped_episodes(self):
        a,b=self.root/'img.1',self.root/'img.2'
        original=a.read_bytes()
        a.write_bytes(b.read_bytes()); b.write_bytes(original)
        with self.assertRaises(ValueError): bundle.verify(self.root,self.manifest)

    def test_missing_and_incomplete_manifest(self):
        del self.manifest['files']['MAP.1']
        with self.assertRaises(ValueError): bundle.verify(self.root,self.manifest)
        (self.root/'map.1').unlink()
        with self.assertRaises(ValueError): bundle.inventory(self.root)

    def test_duplicate_case(self):
        (self.root/'MAP.1').write_bytes(b'ambiguous')
        with self.assertRaises(ValueError): bundle.inventory(self.root)

    def test_optional_files_cannot_silently_change(self):
        (self.root/'SND.DAT').write_bytes(b'synthetic optional')
        with self.assertRaises(ValueError): bundle.verify(self.root,self.manifest)

if __name__ == '__main__': unittest.main()
