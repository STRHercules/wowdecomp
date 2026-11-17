"""Exercise the native preview bounds bridge without starting a graphics device."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'Extensions/ASECM3/client/UnitWorldBinding.cpp').read_text()
    bridge = 'bool PreviewModelBounds(' + source.split('bool PreviewModelBounds(', 1)[1].split('\nbool UnitCameraBounds(', 1)[0]
    # Compile the production bridge against a delayed M3 binding, not the full GPU host.
    harness = r'''
#include "Engine/Source/Retail/CosmeticPreviewFit.h"
#include <cassert>
#include <map>
struct C3Vector { float x=0,y=0,z=0; };
struct CAaBox { C3Vector b,t; };
struct CM2Model {};
std::map<CM2Model*,int> previewBindings;
bool loaded=false,hasBounds=false;
unsigned updates=0,queries=0;
void UpdatePreviewModel(CM2Model*) { ++updates; }
bool UnitModelBounds(CM2Model*,CAaBox& box) {
    ++queries;
    if (!loaded || !hasBounds) return false;
    box={{-12,-7,0},{8,9,18}};
    return true;
}
''' + bridge + r'''
int main() {
    CM2Model m2,m3;
    CAaBox box;
    bool pending=true;
    assert(!PreviewModelBounds(&m2,box,pending) && !pending);
    assert(!updates && !queries); // Ordinary M2s retain their original fitter.
    previewBindings[&m3]=1;
    assert(PreviewModelBounds(&m3,box,pending) && pending);
    loaded=true;
    assert(PreviewModelBounds(&m3,box,pending) && pending); // Loaded proxy alone is insufficient.
    hasBounds=true;
    assert(PreviewModelBounds(&m3,box,pending) && !pending);
    assert(box.b.x==-12 && box.t.z==18 && updates==3 && queries==3);
    const auto fit=Retail::FitCosmeticBounds({box.b.x,box.b.y,box.b.z},
        {box.t.x,box.t.y,box.t.z},3.14159265f,0.83f,0.9f);
    const auto cube=Retail::FitCosmeticBounds({-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f},0,0.83f,0.9f);
    assert(fit.distance>cube.distance*10 && fit.center[2]==9);
    hasBounds=false;
    assert(PreviewModelBounds(&m3,box,pending) && pending); // Never fall back to the ErrorCube.
}
'''
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    install = subprocess.check_output([str(vswhere), '-latest', '-products', '*',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not install:
        raise RuntimeError('MSVC C++ tools are required')
    with tempfile.TemporaryDirectory(prefix='mount-preview-') as tmp:
        work = Path(tmp)
        (work / 'test.cpp').write_text(harness)
        (work / 'test.cmd').write_text(
            f'@call "{install}\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
            f'@cl /nologo /std:c++17 /EHsc /I"{ROOT}" test.cpp /Fe:test.exe\n'
            '@if errorlevel 1 exit /b 1\n@test.exe\n')
        subprocess.run(['cmd', '/c', str(work / 'test.cmd')], cwd=work, check=True)
    print('PASS: M2 fallback, delayed M3 load/bounds, source framing, unavailable bounds')


if __name__ == '__main__':
    main()
