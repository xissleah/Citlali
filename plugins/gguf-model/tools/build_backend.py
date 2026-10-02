"""Build only upstream CPU libraries; no downloads or edits to upstream source."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--toolchain', type=Path, required=True, help='MinGW UCRT bin')
p.add_argument('--jobs', type=int, default=4)
a = p.parse_args()
a.source, a.output, a.toolchain = a.source.resolve(), a.output.resolve(), a.toolchain.resolve()
assert a.output != a.source and a.source not in a.output.parents, 'Output must be outside upstream source'
pin = json.loads((Path(__file__).resolve().parents[1]/'upstream.json').read_text())
for name, expected in pin['headers_sha256'].items():
    assert hashlib.sha256((a.source/name).read_bytes()).hexdigest() == expected, f'Header snapshot mismatch: {name}'
options = {'BUILD_SHARED_LIBS':True, 'GGML_CPU':True, 'GGML_NATIVE':False, 'GGML_BACKEND_DL':False,
           'GGML_CPU_ALL_VARIANTS':False, 'GGML_OPENMP':False, 'GGML_BLAS':False, 'GGML_CUDA':False,
           'GGML_VULKAN':False, 'GGML_METAL':False, 'GGML_OPENCL':False, 'GGML_SYCL':False,
           'GGML_RPC':False, 'GGML_SSE42':False, 'GGML_BMI2':False, 'GGML_AVX':False,
           'GGML_AVX2':False, 'GGML_AVX_VNNI':False, 'GGML_AVX512':False, 'GGML_AVX512_VBMI':False,
           'GGML_AVX512_VNNI':False, 'GGML_AVX512_BF16':False, 'GGML_FMA':False, 'GGML_F16C':False,
           'LLAMA_BUILD_COMMON':False, 'LLAMA_BUILD_TESTS':False, 'LLAMA_BUILD_EXAMPLES':False,
           'LLAMA_BUILD_TOOLS':False}
command = ['cmake', '-S', str(a.source), '-B', str(a.output), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
           '-DCMAKE_C_COMPILER='+str(a.toolchain/'gcc.exe'), '-DCMAKE_CXX_COMPILER='+str(a.toolchain/'g++.exe'),
           '-DCMAKE_C_FLAGS=', '-DCMAKE_CXX_FLAGS=']
command += [f'-D{k}={"ON" if v else "OFF"}' for k,v in options.items()]
subprocess.run(command, check=True)
subprocess.run(['cmake', '--build', str(a.output), '--config', 'Release', '-j', str(a.jobs)], check=True)
if (a.output/'bin/libllama.dll').exists():
    shutil.copy2(a.output/'bin/libllama.dll', a.output/'bin/llama.dll')
for dll in ('libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll'):
    shutil.copy2(a.toolchain/dll, a.output/'bin'/dll)
licenses = a.toolchain.parent/'share/licenses'
assert (licenses/'gcc-libs').is_dir(), 'GCC runtime licenses missing from toolchain'
shutil.copytree(licenses/'gcc-libs', a.output/'runtime-licenses/gcc-libs', dirs_exist_ok=True)
names = ('llama.dll','ggml.dll','ggml-base.dll','ggml-cpu.dll','libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll')
record = {'cpu_only':True, 'minimum_cpu':'Windows x86_64 baseline; optional ISA disabled',
          'source_revision':pin['revision'], 'headers_sha256':pin['headers_sha256'], 'cmake_options':options,
          'dll_sha256':{name:hashlib.sha256((a.output/'bin'/name).read_bytes()).hexdigest() for name in names}}
(a.output/'citlali-cpu.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print('CPU backend ready:', a.output)
