"""Unpack rexglue resources output and inspect selected dashboard XUR scenes.

python dashboard_resources.py PACKAGES OUTPUT --xuihelper XUIHelper.CLI.exe

XUIHelper is an optional, separately installed converter (SGCSam/XUIHelper).
Without it this command extracts packages and writes an inventory. With it,
selected XURs become readable XUI XML plus a JSON property report. Defaults to
MPDashSkin.xur and MiniGamercard.xur; repeat --scene to inspect other scenes.
Use --scene '*' to attempt every scene. Conversion failures are reported and
produce a nonzero exit code; no guessed properties are substituted.

Output contains user-supplied dashboard assets: keep it outside tracked files.
"""

import argparse
from collections import Counter
import fnmatch
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

import xzp


def scene_properties(path):
    """Preserve explicit properties and ancestry, without inventing defaults.

    XUI dimensions/positions are local coordinates. Parent transforms, anchors,
    visual inheritance and timelines must be resolved before rendering them.
    """
    root = ET.parse(path).getroot()
    elements = []

    def visit(node, parent):
        props = node.find('Properties')
        if props is not None:
            values = {
                p.tag: p.text if len(p) == 0 else ET.tostring(p, encoding='unicode')
                for p in props
            }
            label = values.get('Id') or node.tag
            current = f'{parent}/{label}'
            elements.append({'path': current, 'class': node.tag, 'properties': values})
            for child in node:
                if child.tag not in ('Properties', 'Timelines'):
                    visit(child, current)

    visit(root, '')
    return elements


def inspect(packages, output, converter=None, patterns=None):
    packages, output = packages.resolve(), output.resolve()
    if packages == output or packages.is_relative_to(output) or output.is_relative_to(packages):
        raise ValueError('Input and output directories must be separate, non-nested paths')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Choose an empty output directory to avoid mixing extraction runs')
    output.mkdir(parents=True, exist_ok=True)
    (output / '.gitignore').write_text('*\n', encoding='utf-8')
    report = {'packages': [], 'scenes': [], 'errors': []}
    patterns = patterns or ['MPDashSkin.xur', 'MiniGamercard.xur']
    candidates = sorted(p for p in packages.iterdir() if p.is_file())
    for package in candidates:
        with package.open('rb') as stream:
            if stream.read(4) != b'XUIZ':
                continue
        version, data, entries = xzp.read(package)
        folder = output / 'files' / package.name
        extracted = xzp.extract(package, folder, virtual_parents=True)
        inventory = []
        for entry, target in zip(entries, extracted):
            blob = data[entry.offset:entry.offset + entry.size]
            item = {'name': entry.name, 'size': entry.size, 'type': xzp.describe(entry, data),
                    'extracted': target.relative_to(output).as_posix()}
            if blob[:8] == b'\x89PNG\r\n\x1a\n' and len(blob) >= 24:
                item['dimensions'] = list(struct.unpack_from('>II', blob, 16))
            inventory.append(item)
            if target.suffix.lower() != '.xur' or not any(
                fnmatch.fnmatchcase(target.name.lower(), pattern.lower()) for pattern in patterns
            ):
                continue
            scene = {'package': package.name, 'name': entry.name,
                     'sha256': hashlib.sha256(blob).hexdigest(),
                     'xur_version': int.from_bytes(blob[4:8], 'big')}
            report['scenes'].append(scene)
            if converter is None:
                scene['status'] = 'extracted; converter not supplied'
                continue
            destination = output / 'xml' / package.name / target.relative_to(folder).with_suffix('.xui')
            destination.parent.mkdir(parents=True, exist_ok=True)
            if scene['xur_version'] not in (5, 8):
                scene['status'] = 'unsupported XUR version'
                report['errors'].append(f'{package.name}/{entry.name}: {scene["status"]}')
                continue
            command = [str(converter), 'conv', '-s', str(target), '-f', 'xuiv12',
                       '-o', str(destination), '-g', f'V{scene["xur_version"]}']
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                # XUIHelper can return zero on failure: require a parseable output too.
                if result.returncode != 0 or not destination.is_file():
                    raise ValueError((result.stdout + result.stderr).strip())
                scene['elements'] = scene_properties(destination)
                scene['xml'] = destination.relative_to(output).as_posix()
                scene['status'] = 'converted'
                print(f'Converted {package.name}/{entry.name}', flush=True)
            except (OSError, ValueError, ET.ParseError, subprocess.TimeoutExpired) as error:
                scene['status'] = 'conversion failed'
                report['errors'].append(f'{package.name}/{entry.name}: {error}')
        report['packages'].append({'name': package.name, 'version': version,
                                   'sha256': hashlib.sha256(data).hexdigest(), 'files': inventory})
    if not report['packages']:
        report['errors'].append('No XUIZ packages found in the input directory')
    if not report['scenes']:
        report['errors'].append('No scenes matched the requested patterns')
    (output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    lines = ['# Dashboard scene inspection', '',
             'Explicit local properties only. These are not resolved screen coordinates.',
             'A dashboard skin is not evidence of the in-game guide layout.', '',
             f'Packages: {len(report["packages"])}; selected scenes: {len(report["scenes"])}.', '']
    for scene in report['scenes']:
        lines += [f'## {scene["package"]}/{scene["name"]}', '', scene['status'], '']
        elements = scene.get('elements', [])
        sizes = Counter(e['properties']['PointSize'] for e in elements if 'PointSize' in e['properties'])
        fonts = sorted({v for e in elements for k, v in e['properties'].items() if 'font' in k.lower() or 'typeface' in k.lower()})
        images = sorted({e['properties']['ImagePath'] for e in elements if 'ImagePath' in e['properties']})
        lines += [f'Elements: {len(elements)}', '', f'Explicit point sizes: {dict(sizes)}', '',
                  f'Explicit font properties: {fonts or "none; inherited/default font unresolved"}', '',
                  f'Image references: {images}', '']
        if scene.get('xml'):
            lines += [f'[Converted scene]({scene["xml"]})', '']
    if report['errors']:
        lines += ['## Errors', ''] + report['errors']
    (output / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packages', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--xuihelper', type=Path)
    parser.add_argument('--scene', action='append')
    args = parser.parse_args()
    try:
        report = inspect(args.packages, args.output,
                         args.xuihelper.resolve() if args.xuihelper else None, args.scene)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print(f'{len(report["packages"])} packages; {len(report["scenes"])} selected scenes; '
          f'{len(report["errors"])} errors. Report: {args.output / "report.md"}')
    return bool(report['errors'])


if __name__ == '__main__':
    sys.exit(main())
