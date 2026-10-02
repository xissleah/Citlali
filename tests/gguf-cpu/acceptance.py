"""CPU GGUF acceptance under a clean Windows PATH; no GPU/model shipped."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import hashlib
import json
import threading

# Exercise the user-facing README recipe, without a generated deployment template.
exe, readme, model_path, plugins = (Path(v).resolve() for v in sys.argv[1:5])
libraries = {p.relative_to(plugins).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in plugins.rglob('*.dll')}
recipes = re.findall(r'```toml\n(.*?)\n```', readme.read_text(encoding='utf-8'), re.S)
assert len(recipes) == 1, 'Expected one documented CPU deployment recipe'
source = recipes[0].replace('./plugins/', plugins.as_posix()+'/')
source = re.sub(r'^model = ".*"$', lambda _: 'model = '+json.dumps(model_path.as_posix()), source, flags=re.M)

env = dict(os.environ)
env['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')

def run(config, data='', ok=True):
    p = subprocess.run([str(exe),'run',str(config)], input=data, capture_output=True, encoding='utf-8', env=env, timeout=90)
    assert (p.returncode == 0) == ok, p.stdout+p.stderr
    return p.stdout, p.stderr

def response(out):
    body = out.split('Citlali CLI: Enter to generate; Ctrl+C to exit. :plan / :quit\n',1)[1]
    body = body.split('dynamic check OK:',1)[0]
    return re.sub(r'^> ', '', body, flags=re.M).rstrip()


def interrupt(config, generating):
    # A hidden independent console lets us send actual Ctrl+C without touching
    # the test runner/user terminal. Pipes still exercise UTF-8 output and EOF.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    p = subprocess.Popen([str(exe), 'run', str(config)], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                         creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
    output, errors = bytearray(), bytearray()
    ready = threading.Event()
    banner = b'Citlali CLI: Enter to generate; Ctrl+C to exit. :plan / :quit\n> '
    def drain(stream, buffer, watch=False):
        while True:
            c = stream.read(1)
            if not c:
                return
            buffer.extend(c)
            if watch:
                normalized = buffer.replace(b'\r\n', b'\n')
                offset = normalized.find(banner)
                if offset >= 0 and (not generating or len(normalized) > offset + len(banner)):
                    ready.set()
    readers = [threading.Thread(target=drain,args=(p.stdout,output,True)),
               threading.Thread(target=drain,args=(p.stderr,errors))]
    for reader in readers:
        reader.start()
    try:
        if generating:
            p.stdin.write(b'Write a very long story about a journey.\n')
            p.stdin.flush()
        assert ready.wait(45), 'CLI did not become ready: '+errors.decode('utf-8',errors='replace')
        # Attach a separate sender to only this child console. Ignore Ctrl+C in
        # that helper while the Citlali host receives the real console event.
        sender = r"""
import ctypes, sys
k = ctypes.WinDLL('kernel32',use_last_error=True)
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])): raise ctypes.WinError(ctypes.get_last_error())
if not k.SetConsoleCtrlHandler(None, True): raise ctypes.WinError(ctypes.get_last_error())
if not k.GenerateConsoleCtrlEvent(0, 0): raise ctypes.WinError(ctypes.get_last_error())
k.FreeConsole()
"""
        sent = subprocess.run([sys.executable,'-c',sender,str(p.pid)],capture_output=True,timeout=10)
        assert sent.returncode == 0, sent.stderr
        p.wait(timeout=15)
        for reader in readers:
            reader.join(timeout=5)
            assert not reader.is_alive(), 'Output reader did not terminate'
        out, err = output.decode('utf-8'), errors.decode('utf-8')
        assert p.returncode == 0, out+err
        assert 'cli-basic@0.1.0] destroyed' in err, err
        assert 0 <= err.find('gguf-runtime@0.1.0] destroyed') < err.find('gguf-model@0.1.0] destroyed'), err
        if generating:
            assert '[cancelled]' in out, out+err
        print('Ctrl+C '+('during generation' if generating else 'while idle')+' cleaned up',flush=True)
    finally:
        if p.poll() is None:
            p.kill()
            p.wait(timeout=10)
        for reader in readers:
            reader.join(timeout=5)
        p.stdin.close()
        p.stdout.close()
        p.stderr.close()

with tempfile.TemporaryDirectory() as tmp:
    config = Path(tmp)/'test.citlali'
    config.write_text(source,encoding='utf-8')
    out,err=run(config,'What is two plus two? Give the numeric result. /no_think\nWhat is one plus one? Give the numeric result. /no_think\n:quit\n')
    result=response(out)
    assert re.search(r'\b4\b|\bfour\b',result,re.I), out+err
    assert err.count('generated_tokens=') == 2 and 'CPU-only' in err, err
    assert 0 <= err.find('gguf-runtime@0.1.0] destroyed') < err.find('gguf-model@0.1.0] destroyed'),err
    assert err.count('generated_tokens=') == 2, err
    architecture = re.search(r'loaded (\S+) GGUF',err).group(1)
    print('Two requests:',repr(result),flush=True)
    data='请用中文用一句话描述春天。 /no_think\n:quit\n'
    out,err=run(config,data)
    assert re.search(r'[\u4e00-\u9fff]',response(out)),out+err
    assert '\ufffd' not in response(out), out
    print('Chinese:',ascii(response(out)),flush=True)
    chinese_response = response(out)
    print('Stats:',re.findall(r'prompt_tokens=.*',err),flush=True)
    stats = re.findall(r'prompt_tokens=.*',err)
    interrupt(config, generating=False)
    interrupt(config, generating=True)
    config.write_text(source.replace('context_size = 2048','context_size = 0'),encoding='utf-8')
    out,err=run(config,ok=False)
    assert 'invalid context_size' in err and 'gguf-model@0.1.0] destroyed' in err,err
    config.write_text(source.replace('max_tokens = 256','max_tokens = 4096'),encoding='utf-8')
    out,err=run(config,'hello\n',ok=False)
    assert 'exceeds context_size' in err,out+err
    config.write_text(source.replace('[used_plugins.config]\nmodel =', '[used_plugins.config]\ngpu_layers = -1\nmodel =',1),encoding='utf-8')
    out,err=run(config,ok=False)
    assert 'unknown model config field' in err,err
    for path in plugins.rglob('*'):
        assert not any(v in path.name.lower() for v in ('cuda','cublas','vulkan')),path
print('CPU model template, multiple requests, Chinese UTF-8, Ctrl+C shutdown, rollback, context bounds, clean PATH passed')
if model_path:
    record={'model':model_path.name,'architecture':architecture,'plugin_dlls_sha256':libraries,
            'same_prompts_for_all_models':True,'prompt_hint':'/no_think appears in both families test prompts; no model-specific code branch',
            'math_response':result,'chinese_response':chinese_response,'stats':stats,'passed':True}
    folder=exe.parent/'acceptance-records'
    folder.mkdir(exist_ok=True)
    (folder/(model_path.stem+'.json')).write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
