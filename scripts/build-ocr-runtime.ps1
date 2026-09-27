# Requires Python 3.12, Git, CMake and MSVC C++ build tools.
# Uses full ONNX-format runtime support; this is not a minimal/ORT-format build.
param(
    [string]$Python,
    [ValidateSet('Release','MinSizeRel')][string]$Configuration = 'Release',
    [ValidateRange(1,4)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if (!$Python) {
    $candidates = @('python3.12','python', (Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'))
    foreach ($candidate in $candidates) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            $version = & $command.Source -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>$null
            if ($version -eq '3.12') { $Python = $command.Source; break }
        }
    }
}
if (!$Python) { throw 'Python 3.12 was not found. Pass -Python with its executable path.' }
$pythonVersion = & $Python -c 'import sys; print("%d.%d" % sys.version_info[:2])'
if ($LASTEXITCODE -or $pythonVersion -ne '3.12') { throw 'This reproducible build requires Python 3.12; pass -Python with a compatible executable.' }
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $root 'build/runtime-size'
$source = Join-Path $work 'source'
New-Item -ItemType Directory -Force $work | Out-Null
if (!(Test-Path "$source/build.bat")) {
    git clone --depth 1 --branch v1.22.0 https://github.com/microsoft/onnxruntime.git $source
    if ($LASTEXITCODE) { throw 'Official source download failed.' }
}
$revision = git -C $source rev-parse HEAD
if ($revision -ne 'f217402897f40ebba457e2421bc0a4702771968e') { throw 'Source is not the expected ONNX Runtime v1.22.0 revision.' }
$revision | Set-Content "$work/source-revision.txt"
# GitLab's archive endpoint can reject requests or regenerate ZIP bytes. Fetch the
# exact original commit through official Git instead, without changing Eigen code.
$eigen = Join-Path $work 'eigen'
if (!(Test-Path "$eigen/.git")) {
    New-Item -ItemType Directory -Force $eigen | Out-Null
    git -C $eigen init
    git -C $eigen fetch --depth 1 https://gitlab.com/libeigen/eigen.git 1d8b82b0740839c0de7f1242a3585e3390ff5f33
    if ($LASTEXITCODE) { throw 'Pinned official Eigen Git download failed.' }
    git -C $eigen checkout --detach FETCH_HEAD
}
$eigenRevision = git -C $eigen rev-parse HEAD
if ($eigenRevision -ne '1d8b82b0740839c0de7f1242a3585e3390ff5f33') { throw 'Eigen revision mismatch.' }
git -C $eigen rev-parse 'HEAD^{tree}' | Set-Content "$work/eigen-tree.txt"
if (!(Test-Path "$work/venv/Scripts/python.exe")) { & $Python -m venv "$work/venv" }
$py = "$work/venv/Scripts/python.exe"
& $py -m pip install onnx==1.17.0 onnxruntime==1.22.0
if ($LASTEXITCODE) { throw 'Python inspection dependency installation failed.' }
# Only temporary copies/optimized analysis graphs are written. Shipped models stay unchanged.
@'
import pathlib, sys, shutil, hashlib
import onnx, onnxruntime as ort
root=pathlib.Path(sys.argv[1]); work=root/'build/runtime-size'; src=work/'source'
sys.path.insert(0,str(src/'tools/python'))
from create_reduced_build_config import create_config_from_onnx_models
models=work/'analysis-models'; models.mkdir(exist_ok=True)
for name in ('det','rec'):
    original=root/'build/ocr'/f'{name}.onnx'
    shutil.copy2(original,models/original.name)
    print(name,'sha256',hashlib.sha256(original.read_bytes()).hexdigest())
    m=onnx.load(original)
    assert all(n.domain != 'ai.onnx.ml' for n in m.graph.node)
    print(name,'input shapes',[[d.dim_param or d.dim_value for d in i.type.tensor_type.shape.dim] for i in m.graph.input])
    for label,level in [('basic',ort.GraphOptimizationLevel.ORT_ENABLE_BASIC),('extended',ort.GraphOptimizationLevel.ORT_ENABLE_EXTENDED),('all',ort.GraphOptimizationLevel.ORT_ENABLE_ALL)]:
        opts=ort.SessionOptions(); opts.intra_op_num_threads=2; opts.inter_op_num_threads=1; opts.log_severity_level=3
        opts.graph_optimization_level=level; opts.optimized_model_filepath=str(models/f'{name}-{label}.onnx')
        ort.InferenceSession(str(original),opts,providers=['CPUExecutionProvider'])
create_config_from_onnx_models(list(models.glob('*.onnx')),work/'required-operators.config')
'@ | Set-Content "$work/analyze-models.py"
& $py "$work/analyze-models.py" $root *> "$work/model-analysis.log"
if ($LASTEXITCODE) { throw 'Model analysis failed; see model-analysis.log.' }
# Full ONNX loading, graph optimizations, contrib fusion operators, exceptions and MLAS remain enabled.
# Only unused kernels and ML map types are removed; Release preserves speed-focused compilation.
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC build tools not found.' }
@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
rem VS18 deprecates ORT 1.22's numeric gsl::suppress attribute (no codegen effect).
set "_CL_=/wd4875"
"$py" "$source/tools/ci_build/build.py" --build_dir "$work/ninja-output" --config $Configuration --update --build --target onnxruntime --parallel $Jobs --build_shared_lib --skip_tests --skip_submodule_sync --cmake_generator Ninja --include_ops_by_config "$work/required-operators.config" --disable_ml_ops --enable_lto --cmake_extra_defines "FETCHCONTENT_SOURCE_DIR_EIGEN3=$($eigen.Replace('\','/'))"
exit /b %errorlevel%
"@ | Set-Content "$work/build-runtime.cmd"
& "$work/build-runtime.cmd" *> "$work/build-$Configuration.log"
if ($LASTEXITCODE) { throw "Runtime build failed; see build-$Configuration.log." }
$dll = "$work/ninja-output/$Configuration/onnxruntime.dll"
if (!(Test-Path $dll)) { throw "Expected DLL missing: $dll" }
Get-FileHash $dll -Algorithm SHA256 | Format-List | Out-File "$work/candidate-sha256.txt"
Write-Output $dll
