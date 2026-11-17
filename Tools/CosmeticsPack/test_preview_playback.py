"""Run the production character-frame sequence/reset methods against a minimal model."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def main():
    source=(ROOT/'WoW/Source/Ui/CharacterModelBase.cpp').read_text()
    methods='void CGCharacterModelBase::SetSequence('+source.split('void CGCharacterModelBase::SetSequence(',1)[1].split('// VERIFY:',1)[0]
    dressup=(ROOT/'WoW/Source/Ui/DressUpModelFrame.cpp').read_text()
    factory='CM2Model* CGDressUpModelFrame::GetDuplicateUnitCharacterModel('+dressup.split(
        'CM2Model* CGDressUpModelFrame::GetDuplicateUnitCharacterModel(',1)[1].split('// VERIFY:',1)[0]
    header=(ROOT/'Engine/Source/Frame/CSimpleModel.h').read_text()
    assert 'virtual void SetSequence(uint32_t sequence);' in header
    assert 'virtual int32_t SetSequenceTime(uint32_t sequence, int32_t time);' in header
    harness=r'''
#include <cassert>
#include <cstdint>
uint32_t OsGetAsyncTimeMs(){return 1000;}
struct Model {
    uint32_t sequence=0;
    int32_t time=0;
    uint32_t GetBoneSequenceId(int){return sequence;}
    void SetBoneSequence(int,uint32_t value,int,int32_t clock,float,int,int){sequence=value;time=clock;}
};
using CM2Model=Model;
constexpr uint32_t TYPEMASK_PLAYER=0x10;
struct Object {uint32_t m_type=TYPEMASK_PLAYER;};
struct Unit {uint8_t m_unitFieldBytes0[4]={1,0,0,0};uint32_t m_unitFieldDisplayid=49,m_unitFieldNativedisplayid=49;};
struct CGUnit_C {Object* m_obj;Unit* m_unit;};
bool RetailRaceMapped(uint32_t race){return race==1;}
Model liveRetailBody,stockBody;
unsigned nativeLookups=0,copies=0,creates=0;
struct CreatureDisplayInfoRec {uint32_t m_modelID=1;} display;
struct CreatureModelDataRec {const char* m_modelName="Character/Human/Male/HumanMale.m2";} modelData;
struct DisplayDB {bool missing=false;CreatureDisplayInfoRec* GetRecord(uint32_t){++nativeLookups;return missing?nullptr:&display;}} g_creatureDisplayInfoDB;
struct ModelDB {CreatureModelDataRec* GetRecord(uint32_t){return &modelData;}} g_creatureModelDataDB;
struct Scene {CM2Model* CreateModel(const char*,int){++creates;return &stockBody;}} scene;
struct CSimpleModel {
    Model* m_model=nullptr;
    virtual void SetSequence(uint32_t value){if(m_model)m_model->sequence=value;}
    virtual int32_t SetSequenceTime(uint32_t value,int32_t clock){if(m_model){m_model->sequence=value;m_model->time=clock;}return 1;}
    virtual void UpdateModel(){}
};
struct CGCharacterModelBase:CSimpleModel {
    uint32_t m_sequenceSet=0,m_sequenceResetTime=0;
    void SetSequence(uint32_t) override;
    int32_t SetSequenceTime(uint32_t,int32_t) override;
    void UpdateModel() override;
    CM2Model* GetDuplicateUnitCharacterModel(CGUnit_C*){++copies;return &liveRetailBody;}
};
struct CGDressUpModelFrame:CGCharacterModelBase {
    CM2Model* GetDuplicateUnitCharacterModel(CGUnit_C*);
    Scene* GetScene(){return &scene;}
};
''' + methods + factory + r'''
int main(){
    Object object;Unit unit;CGUnit_C player{&object,&unit};CGDressUpModelFrame dressed;
    assert(dressed.GetDuplicateUnitCharacterModel(nullptr)==nullptr);
    assert(dressed.GetDuplicateUnitCharacterModel(&player)==&liveRetailBody);
    assert(copies==1 && !nativeLookups && !creates); // Preserve the emote-capable body, not the native Wrath record.
    unit.m_unitFieldDisplayid=100;
    assert(dressed.GetDuplicateUnitCharacterModel(&player)==&stockBody); // Transformed players still dress their native body.
    unit.m_unitFieldDisplayid=49;unit.m_unitFieldBytes0[0]=2;
    assert(dressed.GetDuplicateUnitCharacterModel(&player)==&stockBody); // Unmapped races keep the stock path.
    unit.m_unitFieldBytes0[0]=1;object.m_type=0x8;
    assert(dressed.GetDuplicateUnitCharacterModel(&player)==&stockBody); // NPCs are not mapped player previews.
    g_creatureDisplayInfoDB.missing=true;
    assert(dressed.GetDuplicateUnitCharacterModel(&player)==nullptr);
    assert(copies==1 && creates==3);
    Model model;
    CGCharacterModelBase character;
    character.m_model=&model;
    CSimpleModel* frame=&character; // The Lua and hearth preview callers hold the base type.
    for(int time: {100,200,300}){
        assert(frame->SetSequenceTime(3000,time)==1);
        frame->UpdateModel();
        assert(model.sequence==3000 && model.time==time && !character.m_sequenceSet);
    }
    frame->SetSequence(3001);frame->UpdateModel();assert(model.sequence==3001);
    frame->UpdateModel();assert(model.sequence==0); // Idle rotation/reset behavior is retained.
    frame->SetSequenceTime(51,500);frame->UpdateModel();assert(model.sequence==51 && model.time==500);
    Model plainModel;CSimpleModel plain;plain.m_model=&plainModel;
    plain.SetSequenceTime(52,250);plain.UpdateModel();assert(plainModel.sequence==52);
    character.m_model=nullptr;assert(frame->SetSequenceTime(3000,0)==1);frame->SetSequence(3000);
}
'''
    harness=harness.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    if not install:raise RuntimeError('MSVC C++ tools are required')
    with tempfile.TemporaryDirectory(prefix='preview-playback-') as tmp:
        work=Path(tmp);(work/'test.cpp').write_text(harness)
        (work/'test.cmd').write_text(f'@call "{install}\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
            '@cl /nologo /std:c++17 /EHsc test.cpp /Fe:test.exe\n@if errorlevel 1 exit /b 1\n@test.exe\n')
        subprocess.run(['cmd','/c',str(work/'test.cmd')],cwd=work,check=True)
    print('PASS: Retail dress-up body selection, transformed/unmapped/NPC fallback, missing records, emote frames, virtual dispatch, hearth casting, idle reset')


if __name__=='__main__':main()
