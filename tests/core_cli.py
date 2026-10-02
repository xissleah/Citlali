"""Core-only package management acceptance, with an isolated loading fixture."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe, deployment, fixture = map(Path, sys.argv[1:])

def invoke(*args, ok=True, contains=None):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, timeout=10)
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, output
    if contains is not None:
        assert contains in output, output
    return output

invoke('--version', contains='Citlali v0.1')
invoke('check', deployment, contains='0 enabled plugins')
invoke('check', deployment, '--probe', contains='0 enabled plugins')
invoke('run', deployment, ok=False, contains='run requires one runtime')
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    packages = root / 'packages'
    package = packages / 'fixture'
    shutil.copytree(fixture, package)
    recipe = root / 'list.citlali'
    recipe.write_text('all_plugins = []\nused_plugins = []\n', encoding='utf-8')
    invoke('inspect', package, contains='test.lifecycle@0.1.0')
    invoke('scan', packages, '--deployment', recipe)
    invoke('scan', packages, '--deployment', recipe)
    assert invoke('list', '--deployment', recipe).count('test.lifecycle@0.1.0') == 1
    invoke('enable', 'test.lifecycle@0.1.0', '--deployment', recipe)
    invoke('check', recipe, ok=False, contains='not connected')
    invoke('disable', 'test.lifecycle@0.1.0', '--deployment', recipe)
    invoke('check', recipe, contains='0 enabled plugins')
    manifest = package / 'info.toml'
    original = manifest.read_text(encoding='utf-8')
    assert 'citlali.native/v1' in original
    for rejected_abi in ('citlali.native/v2', 'citlali.native/v99'):
        manifest.write_text(original.replace('citlali.native/v1', rejected_abi), encoding='utf-8')
        invoke('inspect', package, ok=False, contains='incompatible native ABI')
    manifest.write_text(original, encoding='utf-8')
    recipe.write_text('all_plugins = []\nused_plugins = []\nextra = 1\n', encoding='utf-8')
    invoke('check', recipe, ok=False, contains='unknown field')
print('core-only management contracts passed')
