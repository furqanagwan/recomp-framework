"""Synthetic parser/extraction tests; no Microsoft assets required."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

import dashboard_resources
import xzp


def package(entries, version=3):
    table = bytearray(b'\0' if version == 1 else b'')
    content = bytearray()
    for name, blob in entries:
        if version == 1:
            encoded = name.encode('utf-16-le')
            table += len(blob).to_bytes(3, 'big') + struct.pack('>I', len(content))
            table += struct.pack('<H', len(encoded) // 2) + encoded
        else:
            encoded = name.encode('ascii')
            table += struct.pack('>II', len(blob), len(content)) + bytes([len(encoded)]) + encoded
        content += blob
    header = b'XUIZ' + struct.pack('>IIIIH', version, 22 + len(table) + len(content),
                                 0, len(table), len(entries))
    return header + table + content


class ResourceTests(unittest.TestCase):
    def test_record_names_match_their_own_payloads(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for version in (1, 3):
                source = root / f'package{version}'
                source.write_bytes(package([('A.png', b'first'), ('B.png', b'second')], version))
                targets = xzp.extract(source, root / f'out{version}')
                self.assertEqual([(p.name, p.read_bytes()) for p in targets],
                                 [('A.png', b'first'), ('B.png', b'second')])

    def test_truncated_header_and_table(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'bad'
            data = package([('A.png', b'first')])
            for length in (4, 20, 25):
                source.write_bytes(data[:length])
                with self.assertRaises(ValueError):
                    xzp.read(source)

    def test_traversal_rejected_before_any_file_is_written(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'bad'
            for name in ('../escape', '..\\escape', '/absolute', 'C:/absolute'):
                source.write_bytes(package([('ok', b'ok'), (name, b'bad')]))
                with self.assertRaises(ValueError):
                    xzp.extract(source, root / 'out')
                self.assertFalse((root / 'out').exists())

    def test_virtual_parent_is_mapped_inside_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'controlp'
            source.write_bytes(package([('..\\handles\\VScrollHandle.xur', b'XUIB')]))
            targets = xzp.extract(source, root / 'out', virtual_parents=True)
            self.assertEqual(targets[0], (root / 'out/__parent__/handles/VScrollHandle.xur').resolve())

    def test_scene_reports_local_properties_without_inferred_font(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.xui'
            source.write_text('<XuiCanvas><Properties><Width>400</Width></Properties>'
                              '<XuiVisual><Properties><Id>Tab</Id><Width>420</Width>'
                              '</Properties><XuiTextPresenter><Properties><Id>Text</Id>'
                              '<PointSize>20</PointSize></Properties></XuiTextPresenter>'
                              '</XuiVisual></XuiCanvas>')
            elements = dashboard_resources.scene_properties(source)
            self.assertEqual(elements[2]['path'], '/XuiCanvas/Tab/Text')
            self.assertEqual(elements[2]['properties'], {'Id': 'Text', 'PointSize': '20'})

    def test_extensionless_package_inventory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'input').mkdir()
            (root / 'input/SharedUI').write_bytes(package([
                ('MPDashSkin.xur', b'XUIB' + struct.pack('>I', 8))]))
            report = dashboard_resources.inspect(root / 'input', root / 'out')
            self.assertEqual(len(report['packages']), 1)
            self.assertEqual(report['scenes'][0]['xur_version'], 8)
            self.assertEqual(json.loads((root / 'out/report.json').read_text()), report)
            with self.assertRaises(ValueError):
                dashboard_resources.inspect(root / 'input', root / 'out')


if __name__ == '__main__':
    unittest.main()
