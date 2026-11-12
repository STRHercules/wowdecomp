#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/MountOverrides.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/ClientTables.h"
#include "Engine/Source/Retail/RetailShader.h"
#include "Engine/Source/Retail/Equipment.h"
#include "Engine/Source/Retail/Compose.h"
#include "Engine/Source/Retail/RaceGeometry.h"
#include "Engine/Source/Retail/RacePolicy.h"
#include "Engine/Source/Retail/LegacyAppearance.h"
#include "Engine/Source/Retail/DecodedCache.h"
#include "Engine/Source/Retail/Db2.h"
#include "Engine/Source/Retail/CreationPolicy.h"
#include "Engine/Source/Retail/EyeEffects.h"
#include "Engine/Source/Retail/RetailLookWire.h"
#include "Engine/Source/Retail/SlimeGiant.h"
#include "Engine/Source/Retail/Centaur.h"
#include "Engine/Source/Retail/CentaurAppearance.h"
#include "Engine/Source/Retail/EtherealModel.h"
#include "Engine/Source/Model2/M2Data.h"
#include <chrono>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
void Put(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (i * 8));
}
uint32_t Get(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
}
void Chunk(std::vector<uint8_t>& b, const char* tag, const std::vector<uint8_t>& data) {
    const size_t p = b.size();
    b.resize(p + 8);
    for (size_t i = 0; i < 4; ++i) b[p + i] = tag[i];
    Put(b, p + 4, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}
std::vector<uint8_t> Word(uint32_t v) { std::vector<uint8_t> b(4); Put(b, 0, v); return b; }

// A small valid wire model with one bone, sequence, vertex, texture and material.
// Nonzero offsets exercise MD21's payload-relative addressing and appended TXID paths.
std::vector<uint8_t> Body() {
    std::vector<uint8_t> b(312);
    Put(b, 0, 0x3032444d); Put(b, 4, 274); Put(b, 0x44, 1);
    auto array = [&](size_t field, uint32_t count, size_t stride) {
        size_t offset = b.size(); Put(b, field, count); Put(b, field + 4, uint32_t(offset)); b.resize(offset + count * stride); return offset;
    };
    size_t seq = array(0x1c, 1, 64); Put(b, seq + 12, 0x20); Put(b, seq + 28, 150 | (250 << 16));
    size_t bone = array(0x2c, 1, 88); b[bone + 8] = b[bone + 9] = 0xff;
    for (size_t p : {bone + 16, bone + 36, bone + 56}) b[p + 2] = b[p + 3] = 0xff;
    size_t vertex = array(0x3c, 1, 48); b[vertex + 12] = 255;
    array(0x50, 1, 16);
    array(0x70, 1, 4);
    array(0x78, 1, 2);
    array(0x80, 1, 2);
    array(0x88, 1, 2);
    size_t weight = array(0x90, 1, 2); b[weight] = b[weight + 1] = 0xff;
    size_t transform = array(0x98, 1, 2); b[transform] = b[transform + 1] = 0xff;
    return b;
}
std::vector<uint8_t> Model(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> b;
    Chunk(b, "MD21", body); Chunk(b, "SFID", Word(123)); Chunk(b, "TXID", Word(456));
    return b;
}
std::vector<uint8_t> Skin() {
    std::vector<uint8_t> b(48);
    Put(b, 0, 0x4e494b53); Put(b, 44, 1);
    auto array = [&](size_t field, uint32_t count, size_t stride) {
        size_t offset = b.size(); Put(b, field, count); Put(b, field + 4, uint32_t(offset)); b.resize(offset + count * stride); return offset;
    };
    array(4, 1, 2); array(12, 3, 2); array(20, 1, 4);
    size_t section = array(28, 1, 48);
    b[section + 6] = 1; b[section + 10] = 3; b[section + 12] = 1; b[section + 16] = 1;
    size_t batch = array(36, 1, 24);
    b[batch + 8] = b[batch + 9] = 0xff; b[batch + 14] = 1;
    return b;
}
}


// The look wire format: version 1 is a default-model look, version 2 names a Two Forms form in the sex byte's high nibble.
static void TestLookWire() {
    namespace W = CharacterRetailLook;
    uint8_t payload[W::PayloadSize];
    W::Look in;
    in.race = 12; in.sex = 1; in.count = 3; in.index[0] = 5; in.index[1] = 65535; in.index[2] = 2;
    W::WritePayload(payload, in);
    Check(payload[0] == W::Version && payload[2] == 1, "default-form look stays version 1 with a plain sex byte");
    W::Look out;
    Check(W::ReadPayload(payload, W::PayloadSize, out) && out.form == 0 && out.sex == 1 && out.index[1] == 65535, "version 1 round trip");
    for (uint8_t form = 1; form <= W::MaxForm; ++form) {
        in.form = form;
        W::WritePayload(payload, in);
        Check(payload[0] == W::VersionForm && payload[2] == (1 | form << 4), "form look is version 2 with the form in the high nibble");
        Check(W::ReadPayload(payload, W::PayloadSize, out) && out.form == form && out.sex == 1 && out.race == 12 && out.count == 3, "version 2 round trip");
    }
    payload[2] = 1;          // version 2 without a form
    Check(!W::ReadPayload(payload, W::PayloadSize, out), "version 2 needs a form");
    in.form = 6; W::WritePayload(payload, in); payload[2] = 0x71; // form 7
    Check(!W::ReadPayload(payload, W::PayloadSize, out), "form beyond MaxForm rejected");
    in.form = 0; W::WritePayload(payload, in); payload[2] = 0x11;   // a version 1 reader must not accept a form nibble as sex
    Check(!W::ReadPayload(payload, W::PayloadSize, out), "version 1 with a form nibble rejected");
    payload[0] = 3;
    Check(!W::ReadPayload(payload, W::PayloadSize, out), "unknown version rejected");
}

static void TestEthereal() {
    namespace E = Retail::Ethereal;
    namespace A = Retail::Db2::appearance;
    Check(E::CreatorCount(E::Profile) == 6 && E::CreatorCount(E::Profile+1) == 6
        && E::CreatorCount(E::Profile+2) == 3,"Ethereal creator exposes exactly six body and three Dreadnaught controls");
    const char* bodyLabels[] = {"Cloth Color","Glow Color","Head","Arms","Chest","Legs"};
    const char* mechLabels[] = {"Plating Color","Core Color","Lightning Color"};
    {
        Retail::Image image{16,4,std::vector<uint8_t>(16*4*4,0)};
        for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 8; ++x)
            for (uint32_t channel = 0; channel < 4; ++channel)
                image.rgba[(y*16+x)*4+channel]=uint8_t(y*33+x*3+channel);
        const auto original=image.rgba;
        Check(E::RepeatEquipmentAtlas(image),"equipment atlas can repeat its body tile without rescaling artwork");
        for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 16; ++x)
            for (uint32_t channel = 0; channel < 4; ++channel) {
                const uint32_t expected=x>=8 && x<12 ? x-4 : x;
                Check(image.rgba[(y*16+x)*4+channel]==original[(y*16+expected)*4+channel],
                    "repeat gutter preserves every original image pixel and only fills its reserved neighbor");
            }
        Retail::Image malformed{16,4,{0}};
        Check(!E::RepeatEquipmentAtlas(malformed),"malformed gutter input fails closed");
    }
    for (uint32_t race = 0; race < 129; ++race)
        for (uint32_t flags = 0; flags < 16; ++flags)
            Check(Retail::BareFeet(race,flags) == (race != 77 && race != 78 && (flags & 2) != 0),
                "only Ethereal bypasses the inherited bare-foot rule; Troll and other races retain their flags");
    {
        A::TextureSection section{1,0,256,256,256,0};
        E::FitGloveSection(section);
        Check(section.x == 0 && section.width == 256 && section.y == 384 && section.height == 128
            && section.y+section.height == 512,"glove cuffs keep the source artwork aspect ratio and extend half way up the forearm");
    }
    Check(E::Count(E::Option(E::Profile,0)) == 19 && E::Count(E::Option(E::Profile+1,0)) == 17,
        "ten shared cloth choices append after each gender's existing choices");
    Check(E::Count(E::Option(E::Profile,3)) == 12 && E::Count(E::Option(E::Profile+1,3)) == 12
        && E::Count(E::Option(E::Profile+2,0)) == 16 && E::Count(E::Option(E::Profile+2,1)) == 16
        && E::Count(E::Option(E::Profile+2,7)) == 17,"all five creator color controls gain exactly ten choices");
    Check(E::TexturePath(E::LocalTexture-1).empty() && E::TexturePath(E::LocalTexture+40).empty()
        && E::TexturePath(E::LocalTexture+39) == "custom/ethereal/colors/39.blp",
        "only the forty reserved Ethereal texture IDs resolve to local archive paths");
    {
        auto model = Body();
        const auto before = model;
        std::string error;
        Check(E::PrepareArmorModel(model,error),"Ethereal body gains independent clothing and cloak texture slots");
        Check(Get(model,0x50) == 3 && Get(model,Get(model,0x54)+16) == 14
            && Get(model,Get(model,0x54)+32) == 2,"new material slots use the established armor/cape bindings");
        Check(std::equal(before.begin()+Get(before,0x54),before.begin()+Get(before,0x54)+16,
            model.begin()+Get(model,0x54)),"original texture record remains byte-identical");
        for (size_t at : {0x2cu,0x30u,0x3cu,0x40u,0x48u,0x4cu,0xf0u,0xf4u,0x128u,0x12cu})
            Check(Get(model,at) == Get(before,at),"armor adapter preserves anatomy, rig, animation, sockets and particles");
        const auto once = model;
        Check(!E::PrepareArmorModel(model,error) && model == once,"double armor adaptation fails without modifying input");
        model.resize(24);
        Check(!E::PrepareArmorModel(model,error),"malformed armor input fails closed");
    }
    const uint32_t originalMale[] = {6190261,6190257,6190260,6190262,6190263,6190264,6190900,6190902,6190903};
    const uint32_t originalFemale[] = {6190902,6190261,6190900,6190901,6190903,6190904,6190905};
    const uint32_t originalGlow[] = {6190256,6190255}, originalPlating[] = {6403197,6403194,6403195,6403196,6403198,6403199};
    const uint32_t originalCore[] = {6435135,6435132,6435133,6435134,6435136,6435137};
    Check(std::equal(std::begin(originalMale),std::end(originalMale),std::begin(E::MaleCloth))
        && std::equal(std::begin(originalFemale),std::end(originalFemale),std::begin(E::FemaleCloth))
        && std::equal(std::begin(originalGlow),std::end(originalGlow),std::begin(E::ExtraColors))
        && std::equal(std::begin(originalPlating),std::end(originalPlating),std::begin(E::MechColors))
        && std::equal(std::begin(originalCore),std::end(originalCore),std::begin(E::MechGlowColors)),
        "existing saved color indexes retain exactly the same texture IDs");
    struct ColorCase { uint32_t sex, form, slot, first, textureType, localOffset; };
    const ColorCase colors[] = {
        {0,A::FormDarkweaver,0,9,11,0}, {1,A::FormDarkweaver,0,7,11,0},
        {0,A::FormDarkweaver,3,2,5,10}, {1,A::FormDarkweaver,3,2,5,10},
        {0,A::FormDreadnaught,0,6,11,20}, {1,A::FormDreadnaught,0,6,11,20},
        {0,A::FormDreadnaught,1,6,12,30}, {1,A::FormDreadnaught,1,6,12,30}
    };
    for (const auto& color : colors) {
        const uint32_t model = E::ModelForForm(color.sex,color.form);
        for (uint32_t custom = 0; custom < 10; ++custom) {
            A::ChrChoice selected{};
            Check(E::Choice(E::Option(model,color.slot),color.first+custom,selected),"new creator color can be selected");
            A::Recipe recipe;
            Check(E::Build(color.sex,&selected.id,1,recipe,color.form),"new color resolves for either gender and form");
            const auto layer = std::find_if(recipe.layers.begin(),recipe.layers.end(),[&](const auto& candidate) {
                return candidate.textureType == color.textureType;
            });
            Check(layer != recipe.layers.end() && layer->fileDataId == E::LocalTexture+color.localOffset+custom,
                "new color binds its own intended material rather than a cloth, energy or plating neighbor");
            Check(E::ChoiceName(selected.id) == E::CustomColorNames[custom],"new color exposes its authored theme name");
            for (uint8_t race : {uint8_t(77),uint8_t(78)}) {
                CharacterRetailLook::Look saved{}, loaded{};
                saved.race=race; saved.sex=uint8_t(color.sex); saved.form=uint8_t(color.form);
                saved.count=uint8_t(E::OptionCount(model)); saved.index[color.slot]=uint16_t(color.first+custom);
                uint8_t payload[CharacterRetailLook::PayloadSize];
                CharacterRetailLook::WritePayload(payload,saved);
                Check(CharacterRetailLook::ReadPayload(payload,sizeof(payload),loaded)
                    && loaded.index == saved.index && loaded.form == saved.form && loaded.sex == saved.sex,
                    "new color indexes survive the unchanged faction/gender/form appearance wire format");
            }
        }
        A::ChrChoice invalid{};
        Check(!E::Choice(E::Option(model,color.slot),color.first+10,invalid),"one-past-last color is rejected");
    }
    {
        struct Child { uint32_t m_attachmentId; Child* m_attachNext; };
        struct Model { Child* m_attachList; };
        struct Item { uint32_t m_inventoryType; };
        struct Unit {
            uint32_t m_sheatheState=1, m_animationState=0, transfers=0;
            Child right{26,nullptr},left{28,nullptr};
            Model model{&right}; Model* m_worldModel=&model;
            Item items[2]={{13},{14}};
            const Item* GetVirtualItem(uint32_t hand,bool) { return &items[hand]; }
            void SheatheHandItem(uint32_t hand,int32_t sheathe) {
                Check(sheathe == 0,"wield synchronization must never request a sheath transition");
                (hand ? left : right).m_attachmentId = !hand ? 1 : items[hand].m_inventoryType == 14 ? 0 : 2;
                ++transfers;
            }
        } unit;
        unit.right.m_attachNext=&unit.left;
        E::SyncWieldedHands(unit);
        Check(unit.right.m_attachmentId == 1 && unit.left.m_attachmentId == 0 && unit.transfers == 2,
            "weapon and shield transfer from sheath to wielded sockets without requiring combat");
        E::SyncWieldedHands(unit);
        Check(unit.transfers == 2 && (unit.m_animationState & 0x300000) == 0x300000,
            "repeated drawn state is idempotent and retains both wielded hand flags");
        unit.right.m_attachmentId=26; unit.left.m_attachmentId=28;
        E::SyncWieldedHands(unit);
        Check(unit.transfers == 4 && unit.right.m_attachmentId == 1 && unit.left.m_attachmentId == 0,
            "combat/server resynchronization repairs stale sheath attachments even when logical state is already drawn");
        unit.items[1].m_inventoryType=13; unit.left.m_attachmentId=27;
        E::SyncWieldedHands(unit);
        Check(unit.left.m_attachmentId == 2,"an offhand weapon uses the left hand rather than the shield attachment");
        unit.m_sheatheState=0; const auto count=unit.transfers;
        E::SyncWieldedHands(unit);
        Check(unit.transfers == count,"sheathed state is not forced into hands");
    }
    for (uint32_t i = 0; i < 6; ++i) Check(!std::strcmp(E::CreatorName(E::Profile,i),bodyLabels[i]),"body creator label/order");
    for (uint32_t i = 0; i < 3; ++i) Check(!std::strcmp(E::CreatorName(E::Profile+2,i),mechLabels[i]),"Dreadnaught creator label/order");
    std::vector<uint32_t> state(38,0);
    state[0]=4; state[3]=1; state[2]=5;
    for (uint32_t group = 0; group < 4; ++group) for (uint32_t value = 0; value < 3; ++value) {
        E::SetPreset(state,group,value);
        Check(E::PresetValue(state,group) == value,"preset reads back from existing wire slots");
        uint32_t enabled=0;
        for (uint32_t slot : E::PresetSlots[group]) enabled += E::PresetOn(slot,state[slot]);
        Check(enabled == 1 && state[32] == 1,"presets are exclusive and never select the legless geoset 501");
        Check(state[0] == 4 && state[3] == 1 && state[2] == 5,"presets retain saved colors and hidden legacy values");
    }
    for (uint32_t sex : {0u,1u}) {
        A::Recipe recipe;
        Check(E::Build(sex,nullptr,0,recipe,A::FormDarkweaver),"fresh Ethereal creator look resolves");
        for (uint16_t geoset : recipe.geosets)
            Check(geoset < 6106 || geoset > 6117,"fresh creator does not restore the removed 301-312 costume meshes");
        for (uint32_t style = 0; style < 12; ++style) {
            const uint32_t slot = 17 + style;
            const uint32_t selected = E::FirstChoice(E::Profile+sex,slot) + (style ? 1 : 0);
            Check(E::Build(sex,&selected,1,recipe,A::FormDarkweaver),"legacy costume selection remains decodable");
            Check(std::none_of(recipe.geosets.begin(),recipe.geosets.end(),[](uint16_t geo) {
                return geo >= 6106 && geo <= 6117;
            }),"removed baked collar/belt/boots cannot reappear from hidden saved costume values");
        }
    }
    for (uint32_t race = 0; race < 129; ++race) {
        Check(Retail::HelmetModelRace(race) == (race == 77 || race == 78 ? 4u : race),
            "Ethereal aliases the installed anatomy helmet family without changing Nightborne or any other race");
        for (uint32_t form = 0; form <= A::FormDreadnaught; ++form)
            Check(E::HidesArmor(race, form) == ((race == 77 || race == 78) && form == A::FormDreadnaught),
                "armor visibility is suppressed only for the Ethereal Dreadnaught form");
    }
    for (uint32_t socket : {0u,1u,2u,26u,27u,28u,30u,31u,32u,33u})
        Check(std::find(std::begin(E::ArmorSockets), std::end(E::ArmorSockets), socket) == std::end(E::ArmorSockets),
            "Dreadnaught armor removal cannot detach weapon/shield hand and sheath sockets");
    Check(CharacterRetailLook::FirstFormForRace(77) == A::FormDarkweaver
        && CharacterRetailLook::FirstFormForRace(78) == A::FormDarkweaver
        && CharacterRetailLook::FirstFormForRace(76) == 0, "Ethereal form admission preserves neighboring races");
    for (uint32_t sex : {0u, 1u}) for (uint32_t form : {uint32_t(A::FormDarkweaver), uint32_t(A::FormDreadnaught)}) {
        A::Recipe recipe;
        const uint32_t model = E::ModelForForm(sex, form);
        Check(E::Build(sex, nullptr, 0, recipe, form), "both sexes resolve both native Ethereal forms");
        Check(E::OptionCount(model) <= CharacterRetailLook::MaxOptions, "all Ethereal controls fit the saved full look");
        Check(recipe.modelFileDataId == E::Bodies[model - E::Profile], "form model is the verified NPC body");
        Check(recipe.textureLayoutId == (form == A::FormDarkweaver ? 141u : 0u),
            "only the Ethereal body requests the armor atlas; Dreadnaught remains armor-free");
        Check(recipe.geosets.size() == (form == A::FormDreadnaught ? 4u : 12u),
            "defaults retain minimal wrapped anatomy and glow while ornate costumes remain optional");
        for (uint32_t option = 0; option < E::OptionCount(model); ++option) {
            const uint32_t id = E::Option(model, option);
            Check(!E::OptionName(id).empty(), "every Ethereal control has a label");
            for (uint32_t value = 0; value < E::Count(id); ++value) {
                A::ChrChoice choice{};
                Check(E::Choice(id, value, choice) && E::Allowed(model, choice.id), "every source choice is selectable");
                Check(!E::ChoiceName(choice.id).empty() && E::Build(sex, &choice.id, 1, recipe, form),
                    "every individual geometry and material choice resolves");
            }
        }
        uint32_t duplicate[] = {E::FirstChoice(model, 0), E::FirstChoice(model, 0)};
        Check(!E::Build(sex, duplicate, 2, recipe, form), "duplicate option selections are rejected");
        const uint32_t other = E::FirstChoice(form == A::FormDarkweaver ? E::Profile + 2 : E::Profile, 0);
        Check(!E::Build(sex, &other, 1, recipe, form), "form choices cannot overwrite the other form");
    }
    A::Recipe recipe;
    struct ParticleModel {
        std::vector<uint32_t> slots, colors, mids, ends;
        void ReplaceParticleColor(uint32_t slot, CImVector start, CImVector mid, CImVector end) {
            slots.push_back(slot);
            colors.push_back(uint32_t(start.b) | uint32_t(start.g) << 8 | uint32_t(start.r) << 16 | uint32_t(start.a) << 24);
            mids.push_back(uint32_t(mid.b) | uint32_t(mid.g) << 8 | uint32_t(mid.r) << 16 | uint32_t(mid.a) << 24);
            ends.push_back(uint32_t(end.b) | uint32_t(end.g) << 8 | uint32_t(end.r) << 16 | uint32_t(end.a) << 24);
        }
    };
    for (uint32_t palette = 0; palette < E::EffectCount(); ++palette) {
        ParticleModel model;
        const uint32_t choice = E::FirstChoice(E::Profile + 2, 7) + palette;
        E::ApplyEffects(model, &choice, 1);
        Check(model.slots == std::vector<uint32_t>({11,12,13}), "Dreadnaught effect palettes bind the three authored particle channels");
        const auto& effect = E::Effect(palette);
        Check(model.colors == std::vector<uint32_t>(effect.start.begin(), effect.start.end())
            && model.mids == std::vector<uint32_t>(effect.mid.begin(), effect.mid.end())
            && model.ends == std::vector<uint32_t>(effect.end.begin(), effect.end.end()),
            "particle color bytes match the source table without channel swaps");
    }
    Check(!E::Build(2, nullptr, 0, recipe, A::FormDarkweaver)
        && !E::Build(0, nullptr, 1, recipe, A::FormDarkweaver)
        && !E::Build(0, nullptr, 0, recipe, A::FormWolf), "invalid Ethereal inputs fail closed");
    std::vector<uint8_t> skin(48 + 7 * 48);
    Put(skin, 0, 0x4e494b53); Put(skin, 28, 7); Put(skin, 32, 48);
    skin[48 + 6 * 48] = 101;
    const auto before = skin;
    std::string error;
    Check(E::PrepareSkin(skin, false, error), error.c_str());
    Check(E::Motion::Half(skin, 48) == 6001 && E::Motion::Half(skin, 48 + 6 * 48) == 6100,
        "shared geoset-zero layers and costumes receive independent private selectors");
    const auto adapted = skin;
    Check(!E::PrepareSkin(skin, false, error) && skin == adapted, "double adaptation fails without changing the input");
    skin = before; skin.resize(skin.size() - 1);
    Check(!E::PrepareSkin(skin, false, error), "truncated Ethereal section arrays are rejected");
    auto body = Body();
    const Retail::Ethereal::Motion::Reader noFiles = [](uint32_t, auto&, auto&) { return false; };
    Check(E::Motion::Embed(body, {}, noFiles, true, false, error), "resident animation metadata is rebuilt independently");
    Check(E::Motion::Word(body, 0x1c) == 1, "embedding preserves source sequence count");
    {
        auto shared = Body();
        std::vector<uint8_t> sequences(shared.begin() + Get(shared, 0x20),
            shared.begin() + Get(shared, 0x20) + 64);
        const auto run = sequences;
        sequences.insert(sequences.end(), run.begin(), run.end());
        E::Motion::HalfPut(sequences, 64, 5); // Run selects slot one; the source color still has only shared slot zero.
        Put(shared, 0x20, uint32_t(E::Motion::Append(shared, sequences))); Put(shared, 0x1c, 2);
        const size_t color = E::Motion::Append(shared, std::vector<uint8_t>(40));
        Put(shared, 0x48, 1); Put(shared, 0x4c, uint32_t(color));
        E::Motion::HalfPut(shared, color + 2, 65535); E::Motion::HalfPut(shared, color + 22, 65535);
        E::Motion::Keys tint;
        tint.times.resize(4); tint.values.resize(12);
        const float rgb[] = {.5f, .25f, 1.0f}; std::memcpy(tint.values.data(), rgb, 12);
        E::Motion::StoreTrack(shared, color, 12, {tint});
        Check(E::Motion::Embed(shared, {}, noFiles, true, false, error), "embed an idle/run model with one shared tint");
        Check(Get(shared, color + 4) == 1, "shared material tint remains shared when the model gains animation slots");
        E::Motion::Keys running;
        const uint32_t runSlot = 1 < Get(shared, color + 4) ? 1 : 0;
        Check(E::Motion::KeysAt(shared, color, 12, runSlot, running) && running.values == tint.values,
            "the renderer sees the same nonblack tint during running as idle");
    }
}

int main(int argc, char** argv) {
    TestLookWire();
    TestEthereal();
    if (argc == 4 && std::string(argv[1]) == "--ethereal-clothing")
    {
        const auto read = [](std::filesystem::path const& path) {
            std::ifstream input(path,std::ios::binary);
            Check(input.good(),"open staged clothing fixture");
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),{});
        };
        for (uint32_t id : {5926159u,6152557u}) {
            const std::filesystem::path assets(argv[2]), prepared(argv[3]);
            const std::string stem = std::to_string(id);
            const auto clothing = read(assets/(stem+"-clothing.m2"));
            const auto skin = read(assets/(stem+"-clothing00.skin"));
            const auto parent = read(prepared/(stem+".m2"));
            std::string error;
            Check(Retail::ValidateSkin(skin,clothing,error),error.c_str());
            Check(Get(clothing,0x2c) == Get(parent,0x2c),"clothing retains every parent bone for exact pose sharing");
            for (uint32_t i = 0; i < Get(parent,0x2c); ++i)
                Check(!std::memcmp(clothing.data()+Get(clothing,0x30)+size_t(i)*88,
                    parent.data()+Get(parent,0x30)+size_t(i)*88,16)
                    && !std::memcmp(clothing.data()+Get(clothing,0x30)+size_t(i)*88+76,
                    parent.data()+Get(parent,0x30)+size_t(i)*88+76,12),
                    "clothing bone identity/hierarchy/flags/pivots satisfy native exact-rig binding");
            Check(Get(clothing,0x3c) < 65536,"clothing vertices fit the renderer");
            uint32_t uploaded = 0;
            bool cape = false, arms = false, torso = false, legs = false, gloves = false, hips = false;
            for (uint32_t i = 0; i < Get(skin,28); ++i) {
                const size_t at = Get(skin,32)+size_t(i)*48;
                const uint16_t geoset = Retail::Ethereal::Motion::Half(skin,at);
                uploaded += Retail::Ethereal::Motion::Half(skin,at+10);
                Check(Retail::Ethereal::Motion::Half(skin,at+12) <= 75,"each clothing section fits 75 GPU bones");
                cape = cape || geoset == 1501;
                arms = arms || geoset == 6103;
                torso = torso || geoset == 6118;
                legs = legs || geoset == 6122;
                gloves = gloves || geoset == 6301;
                hips = hips || geoset == 6302;
            }
            Check(uploaded == Get(skin,12),"sequential GPU uploads fit the actual index allocation");
            Check(cape && arms && torso && legs,"clothing contains cloak and all default anatomy overlays");
            std::set<std::string> parentPositions;
            for (uint32_t i = 0; i < Get(parent,0x3c); ++i) {
                const size_t at = Get(parent,0x40)+size_t(i)*48;
                parentPositions.emplace(reinterpret_cast<char const*>(parent.data()+at),16);
            }
            for (uint32_t i = 0; i < Get(skin,28); ++i) {
                const size_t at = Get(skin,32)+size_t(i)*48;
                if (Retail::Ethereal::Motion::Half(skin,at)!=6301) continue;
                const uint32_t first = uint32_t(Retail::Ethereal::Motion::Half(skin,at+2))<<16
                    | Retail::Ethereal::Motion::Half(skin,at+8);
                const uint32_t count = Retail::Ethereal::Motion::Half(skin,at+10);
                for (uint32_t n = first; n < first+count; ++n) {
                    const uint16_t lookup = Retail::Ethereal::Motion::Half(skin,Get(skin,16)+size_t(n)*2);
                    const uint16_t vertex = Retail::Ethereal::Motion::Half(skin,Get(skin,8)+size_t(lookup)*2);
                    const size_t position = Get(clothing,0x40)+size_t(vertex)*48;
                    Check(parentPositions.count(std::string(reinterpret_cast<char const*>(clothing.data()+position),16)),
                        "UV gloves must use exact original hand positions/weights rather than an inflated shell");
                }
            }
            if (Get(clothing,0x70) == 2)
            {
                Check(gloves && !hips,"solid fingers are retained without an invented waist surface");
                for (uint32_t batch = 0; batch < Get(skin,36); ++batch) {
                    const size_t at = Get(skin,40)+size_t(batch)*24;
                    const uint16_t section = Retail::Ethereal::Motion::Half(skin,at+4);
                    if (Retail::Ethereal::Motion::Half(skin,Get(skin,32)+size_t(section)*48) == 6301)
                        Check(Retail::Ethereal::Motion::Half(skin,at+10) == 1
                            && Retail::Ethereal::Motion::Half(clothing,Get(clothing,0x74)+6) == 0,
                            "solid finger coverage must not alpha-discard the glove texture's cutout pixels");
                }
            }
        }
        std::puts("Ethereal clothing native skin validation, parent rig identity and upload bounds passed.");
        return 0;
    }
    if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--ethereal-source") {
        namespace E = Retail::Ethereal;
        Retail::CascSource source;
        std::string error;
        const auto require = [&](bool pass) { Check(pass, error.c_str()); };
        require(source.Configure(argv[2], error));
        for (uint32_t id : E::Bodies) {
            std::ifstream fixture(std::filesystem::path(argv[3]) / (std::to_string(id) + ".m2"), std::ios::binary);
            const std::vector<uint8_t> original((std::istreambuf_iterator<char>(fixture)), {});
            std::vector<uint8_t> fetched, player;
            std::printf("Ethereal fixture %u\n", id); std::fflush(stdout);
            require(!original.empty() && source.Inspect(Retail::FileName(id, ".m2").c_str(), fetched, error));
            Check(fetched == original, "native source matches the immutable audited fixture");
            require(source.Read(E::Path(id).c_str(), player, error));
            if (argc == 5) {
                const std::filesystem::path output(argv[4]);
                std::filesystem::create_directories(output);
                std::ofstream out(output / (std::to_string(id) + ".m2"), std::ios::binary);
                Check(bool(out.write(reinterpret_cast<const char*>(player.data()), player.size())), "export prepared Ethereal pose fixture");
                if (id != E::Bodies[2]) {
                    const uint32_t donorId = 1814471, skeletonId = 1845581, parentId = 1839628;
                    const auto originalFile = [&](uint32_t file, const char* extension) {
                        std::ifstream input(std::filesystem::path(argv[3]) / (std::to_string(file) + extension), std::ios::binary);
                        Check(input.good(), "open original Nightborne pose fixture");
                        return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), {});
                    };
                    std::vector<uint8_t> skeleton, donor;
                    Retail::ModelFiles graph;
                    require(Retail::MergeSkeleton(originalFile(skeletonId, ".skel"), originalFile(parentId, ".skel"), skeleton, error));
                    require(Retail::PrepareModel(originalFile(donorId, ".m2"), donor, graph, error, &skeleton, false));
                    const E::Motion::Reader read = [&](uint32_t file, auto& bytes, auto&) {
                        bytes = originalFile(file, ".anim"); return true;
                    };
                    require(E::Motion::Embed(donor, graph, read, false, false, error));
                    const auto findBone = [](const auto& model, uint32_t key) {
                        for (uint32_t i = 0; i < Get(model, 0x2c); ++i)
                            if (Get(model, Get(model, 0x30) + size_t(i)*88) == key) return int32_t(i);
                        return int32_t(-1);
                    };
                    for (uint16_t action : {13,69,84,96,97,98,119,120,209}) {
                        const uint16_t targetSequence = E::Motion::Half(player, Get(player, 0x28) + size_t(action)*2);
                        const uint16_t donorSequence = E::Motion::Half(donor, Get(donor, 0x28) + size_t(action)*2);
                        Check(targetSequence < Get(player, 0x1c) && donorSequence < Get(donor, 0x1c),
                            "each reported broken action resolves a real donor clip");
                        for (uint32_t key = 0; key <= 18; ++key) {
                            const int32_t a = findBone(player,key), b = findBone(donor,key);
                            if (a < 0 || b < 0) continue;
                            E::Motion::Keys actual, expected;
                            Check(E::Motion::KeysAt(player, Get(player,0x30)+size_t(a)*88+36,8,targetSequence,actual)
                                && E::Motion::KeysAt(donor, Get(donor,0x30)+size_t(b)*88+36,8,donorSequence,expected),
                                "reported action joint rotations are readable");
                            Check(actual.times == expected.times && actual.values == expected.values,
                                "reported actions retain the Nightborne bind-axis rotations without caster-idle offsets");
                        }
                    }
                    std::ofstream motion(output / (std::to_string(donorId) + ".m2"), std::ios::binary);
                    Check(bool(motion.write(reinterpret_cast<const char*>(donor.data()), donor.size())), "export resident Nightborne pose fixture");
                }
            }
            size_t body = 0;
            for (size_t at = 0; at + 8 <= original.size(); at += Get(original, at + 4) + 8)
                if (!std::memcmp(original.data() + at, "MD21", 4)) { body = at + 8; break; }
            Check(body && Get(player, 0x128) == Get(original, body + 0x128), "all authored glow particle emitters survive");
            for (uint32_t tag : {0x52485324u,0x4c485324u}) {
                size_t event = 0;
                for (uint32_t e = 0; e < Get(player,0x100); ++e) {
                    const size_t record = Get(player,0x104)+size_t(e)*36;
                    if (Get(player,record) == tag) { event = record; break; }
                }
                Check(event != 0,"both hand-transfer events exist without relying on combat refreshes");
                if (id != E::Bodies[2]) {
                    const uint16_t bone = E::Motion::Half(player,event+8);
                    Check(bone < Get(player,0x2c),"draw/sheath event bone exists");
                    uint32_t parent = E::Motion::Half(player,Get(player,0x30)+size_t(bone)*88+8);
                    bool dispatched = false;
                    for (uint32_t depth = 0; parent != 65535 && depth < Get(player,0x2c); ++depth) {
                        Check(parent < Get(player,0x2c),"draw/sheath event ancestry is bounded");
                        dispatched = dispatched || Get(player,Get(player,0x30)+size_t(parent)*88) ==
                            (tag == 0x52485324 ? 3u : 2u);
                        parent = E::Motion::Half(player,Get(player,0x30)+size_t(parent)*88+8);
                    }
                    Check(dispatched,"the real dispatcher parent walk reaches the animated arm root");
                }
                for (uint16_t action : {89,90}) {
                    const uint16_t clip = E::Motion::Half(player,Get(player,0x28)+size_t(action)*2);
                    E::Motion::Keys clock;
                    Check(clip < Get(player,0x1c) && E::Motion::KeysAt(player,event+24,0,clip,clock)
                        && !clock.times.empty(),"both hand-transfer events are scheduled during normal and back draw/sheath clips");
                    const uint32_t duration = Get(player,Get(player,0x20)+size_t(clip)*64+4);
                    for (size_t time = 0; time < clock.times.size(); time += 4)
                        Check(Get(clock.times,time) <= duration,"hand item transfer occurs before the animation completes");
                }
            }
            Check(Get(player, 0x2c) == Get(original, body + 0x2c), "source rig and extra cloth bones are retained");
            if (id != E::Bodies[2]) {
                const auto boneForKey = [&](uint32_t key) {
                    for (uint32_t i = 0; i < Get(player, 0x2c); ++i)
                        if (Get(player, Get(player, 0x30) + size_t(i) * 88) == key) return i;
                    Check(false, "the Ethereal keeps both authored foot anchors"); return 0u;
                };
                const uint16_t backward = E::Motion::Half(player, Get(player, 0x28) + 13 * 2);
                Check(backward < Get(player, 0x1c) && E::Motion::Half(player, Get(player, 0x20) + backward * 64) == 13,
                    "backward walking resolves its own authored Nightborne action");
                for (uint32_t foot : {59u,60u}) {
                    uint32_t joint = E::Motion::Half(player, Get(player, 0x30) + size_t(boneForKey(foot)) * 88 + 8);
                    for (uint32_t level = 0; level < 2; ++level) {
                        Check(joint < Get(player, 0x2c), "both Ethereal leg chains contain a real thigh and knee");
                        E::Motion::Keys keys;
                        Check(E::Motion::KeysAt(player, Get(player, 0x30) + size_t(joint) * 88 + 36, 8, backward, keys),
                            "backward leg rotation keys are resident and bounded");
                        bool moving = false;
                        for (size_t k = 8; k < keys.values.size(); k += 8)
                            moving = moving || std::memcmp(keys.values.data(), keys.values.data() + k, 8) != 0;
                        Check(moving, "backward walking animates each thigh and knee rather than leaving legs frozen");
                        joint = E::Motion::Half(player, Get(player, 0x30) + size_t(joint) * 88 + 8);
                    }
                }
                std::set<std::pair<uint16_t,uint16_t>> identities;
                for (uint32_t i = 0; i < Get(player, 0x1c); ++i)
                    Check(identities.emplace(E::Motion::Half(player, Get(player, 0x20) + size_t(i)*64),
                        E::Motion::Half(player, Get(player, 0x20) + size_t(i)*64 + 2)).second,
                        "replacing Point clips preserves unique action/variation identities");
            }
            const std::vector<uint8_t> rawBody(original.begin() + body,
                original.begin() + body + Get(original, body - 4));
            std::vector<E::Motion::TrackRef> sourceTracks, playerTracks;
            Check(E::Motion::Tracks(rawBody, true, false, sourceTracks)
                && E::Motion::Tracks(player, false, false, playerTracks), "enumerate source and prepared material/effect tracks");
            for (auto* tracks : {&sourceTracks, &playerTracks})
                tracks->erase(std::remove_if(tracks->begin(), tracks->end(), [](const auto& track) {
                    return track.category != 0 || track.spline || track.event;
                }), tracks->end());
            Check(sourceTracks.size() == playerTracks.size(), "material/effect tracks retain their source identities");
            uint32_t sharedEffects = 0;
            for (size_t i = 0; i < sourceTracks.size(); ++i) {
                const auto& from = sourceTracks[i]; const auto& to = playerTracks[i];
                if (from.category == 1 || Get(rawBody, from.at + 4) != 1) continue;
                E::Motion::Keys expected, actual;
                // Shared values may live in an external animation file; source-resident constants are checked here.
                if (!(Get(rawBody, Get(rawBody, 0x20) + 12) & 0x20)) continue;
                Check(E::Motion::KeysAt(rawBody, from.at, from.key, 0, expected, from.spline),
                    "source shared material/effect keys are readable");
                Check(Get(player, to.at + 4) == 1, "single-span material/effect tracks cannot become sparse animation arrays");
                const size_t particles = Get(rawBody, 0x12c);
                const bool packedGravity = from.at >= particles && (from.at - particles) / 492 < Get(rawBody, 0x128)
                    && (from.at - particles) % 492 == 0x84
                    && (Get(rawBody, particles + ((from.at - particles) / 492) * 492 + 4) & 0x800000);
                if (packedGravity) continue; // The native adapter converts compressed gravity vectors to scalar values.
                for (uint32_t action = 0; action < Get(player, 0x1c); ++action) {
                    const uint32_t slot = action < Get(player, to.at + 4) ? action : 0;
                    Check(E::Motion::KeysAt(player, to.at, to.key, slot, actual, to.spline)
                        && actual.times == expected.times && actual.values == expected.values,
                        "shared tint, opacity, UV and glow settings survive every authored and borrowed animation");
                }
                ++sharedEffects;
            }
            Check(sharedEffects > 10, "real source fixtures exercise shared colors and particle settings");
            std::set<uint16_t> actions;
            for (uint32_t i = 0; i < Get(player, 0x1c); ++i)
                actions.insert(E::Motion::Half(player, Get(player, 0x20) + size_t(i) * 64));
            for (uint16_t action : {0,1,4,5,37,38,39,41,42,43,44,45,50,51,53,54,69,70,119,120,133,134})
                Check(actions.count(action), "both forms retain movement, casting, combat, swim, stealth and fishing actions");
            for (uint16_t action : {42,50,120,133,134}) {
                const uint16_t sequence = E::Motion::Half(player, Get(player, 0x28) + size_t(action) * 2);
                bool moving = false;
                for (uint32_t bone = 0; bone < Get(player, 0x2c); ++bone) {
                    const size_t at = Get(player, 0x30) + size_t(bone) * 88;
                    const int32_t key = int32_t(Get(player, at));
                    if (key < 0 || key > 18) continue;
                    for (size_t offset : {16u,36u,56u}) {
                        E::Motion::Keys keys;
                        const size_t size = offset == 36 ? 8 : 12;
                        Check(E::Motion::KeysAt(player, at + offset, size, sequence, keys), "action keyframes are resident and bounded");
                        for (size_t i = size; i < keys.values.size(); i += size)
                            moving = moving || std::memcmp(keys.values.data(), keys.values.data() + i, size) != 0;
                    }
                }
                Check(moving, "swimming, looting, stealth and fishing contain moving core bones rather than idle aliases");
            }
            const auto sockets = Get(player, 0xfc);
            for (uint16_t socket : {1,2,5,6,11,12})
                Check(socket < Get(player, 0xf8) && E::Motion::Half(player, sockets + socket * 2) < Get(player, 0xf0),
                    "weapon, shoulder, helmet and back sockets resolve in each form");
            Retail::ModelFiles graph;
            require(Retail::InspectModel(original, graph, error));
            for (size_t lod = 0; lod < Get(player, 0x44); ++lod) {
                char suffix[16]; std::snprintf(suffix, sizeof(suffix), "%02u.skin", unsigned(lod));
                const auto path = E::Path(id);
                std::vector<uint8_t> skin;
                require(source.Read((path.substr(0, path.size() - 3) + suffix).c_str(), skin, error));
                require(Retail::ValidateSkin(skin, player, error));
                std::vector<uint8_t> originalSkin;
                require(source.Inspect(Retail::FileName(graph.skins[lod], ".skin").c_str(), originalSkin, error));
                for (uint32_t mesh = 0; mesh < Get(skin, 28); ++mesh) {
                    Check(E::Motion::Half(skin, Get(skin, 32) + size_t(mesh) * 48) >= 6000,
                        "all LOD geometry is independently controlled outside equipment groups");
                    const size_t at = Get(skin, 32) + size_t(mesh) * 48;
                    if (E::Motion::Half(skin,at) == E::BareHandGeoset)
                    {
                        const uint32_t first = uint32_t(E::Motion::Half(skin,at+2))<<16 | E::Motion::Half(skin,at+8);
                        const uint32_t count = E::Motion::Half(skin,at+10);
                        Check(count>0,"the isolated original hand surface exists in every advertised LOD");
                        for (uint32_t index = first; index < first+count; ++index) {
                            const uint16_t lookup = E::Motion::Half(skin,Get(skin,16)+size_t(index)*2);
                            const uint16_t vertex = E::Motion::Half(skin,Get(skin,8)+size_t(lookup)*2);
                            const size_t position = Get(player,0x40)+size_t(vertex)*48;
                            float y,z; std::memcpy(&y,player.data()+position+4,4); std::memcpy(&z,player.data()+position+8,4);
                            Check(std::abs(y)>.45f && z>.50f,"hand isolation must never remove feet or other anatomy");
                        }
                        continue;
                    }
                    const size_t old = Get(originalSkin, 32) + size_t(mesh) * 48;
                    Check(E::Motion::Half(skin, at + 12) <= 75, "each selectable mesh fits the renderer's bone constants");
                    const uint32_t first = E::Motion::Half(skin, at + 4), count = E::Motion::Half(skin, at + 6);
                    for (uint32_t vertex = first; vertex < first + count; ++vertex) {
                        const uint16_t index = E::Motion::Half(skin, Get(skin, 8) + size_t(vertex) * 2);
                        for (size_t influence = 0; influence < 4; ++influence) {
                            if (!player[Get(player, 0x40) + size_t(index) * 48 + 12 + influence]) continue;
                            const uint8_t local = skin[Get(skin, 24) + size_t(vertex) * 4 + influence];
                            const uint8_t previous = originalSkin[Get(originalSkin, 24) + size_t(vertex) * 4 + influence];
                            const uint16_t actual = E::Motion::Half(player,
                                Get(player, 0x7c) + 2 * (E::Motion::Half(skin, at + 14) + local));
                            const uint16_t expected = E::Motion::Half(original,
                                body + Get(original, body + 0x7c) + 2 * (E::Motion::Half(originalSkin, old + 14) + previous));
                            Check(actual == expected, "palette compaction preserves every original weighted bone influence");
                        }
                    }
                }
                if (id != E::Bodies[2])
                {
                    std::multiset<std::array<uint16_t,3>> originalTriangles, isolatedTriangles;
                    const auto collect = [&](std::vector<uint8_t> const& bytes, size_t row,
                        std::multiset<std::array<uint16_t,3>>& triangles) {
                        const uint32_t first = uint32_t(E::Motion::Half(bytes,row+2))<<16 | E::Motion::Half(bytes,row+8);
                        const uint32_t count = E::Motion::Half(bytes,row+10);
                        for (uint32_t at = first; at < first+count; at+=3) {
                            std::array<uint16_t,3> tri{};
                            for (size_t corner = 0; corner < 3; ++corner) {
                                const uint16_t lookup = E::Motion::Half(bytes,Get(bytes,16)+size_t(at+corner)*2);
                                tri[corner] = E::Motion::Half(bytes,Get(bytes,8)+size_t(lookup)*2);
                            }
                            triangles.insert(tri);
                        }
                    };
                    collect(originalSkin,Get(originalSkin,32),originalTriangles);
                    for (uint32_t row = 0; row < Get(skin,28); ++row) {
                        const size_t at = Get(skin,32)+size_t(row)*48;
                        const uint16_t geo = E::Motion::Half(skin,at);
                        if (geo == 6001 || geo == E::BareHandGeoset) collect(skin,at,isolatedTriangles);
                    }
                    Check(originalTriangles == isolatedTriangles,
                        "hand isolation partitions the original anatomy without losing or duplicating triangles");
                }
            }
        }
        std::puts("Ethereal immutable source, full motion, glow, sockets and every advertised skin profile passed.");
        return 0;
    }
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--centaur-source") {
        Retail::CascSource source;
        std::string error;
        const auto require = [&](bool ok) { Check(ok, error.c_str()); };
        require(source.Configure(argv[2], error));
        for (const uint32_t id : Retail::Centaur::Bodies) {
            std::vector<uint8_t> original, player;
            const auto ordinary = "retail/" + std::to_string(id) + ".m2";
            std::printf("Checking original %s\n", ordinary.c_str()); std::fflush(stdout);
            source.RegisterCharacterModel(id);
            require(source.Read(ordinary.c_str(), original, error));
            const auto adapted = Retail::Centaur::Path(id);
            std::printf("Checking player %s\n", adapted.c_str()); std::fflush(stdout);
            require(source.Read(adapted.c_str(), player, error));
            Check(Retail::SlimeGiant::Word(player, 0x50) == 7
                && Retail::SlimeGiant::Half(player, Retail::SlimeGiant::Word(player, 0x6c) + 15 * 2) == 6,
                "playable Centaur reserves an independent belt texture without replacing body/cloak textures");
            for (uint32_t i = 0; i < Retail::SlimeGiant::Word(player, 0x100); ++i) {
                const size_t at = Retail::SlimeGiant::Word(player, 0x104) + size_t(i) * sizeof(M2Event);
                auto& event = *reinterpret_cast<M2Event*>(player.data() + at);
                event.m_enabled.m_sequenceTimes.Relocate(player.data());
                for (uint32_t sequence = 0; sequence < Retail::SlimeGiant::Word(player, 0x1c); ++sequence) {
                    auto* times = event.TimesForSequence(sequence);
                    const uint32_t index = event.m_enabled.m_loopIndex == 0xffff ? sequence : 0;
                    Check((times != nullptr) == (index < event.m_enabled.m_sequenceTimes.m_count),
                        "all inherited Centaur sequences safely handle absent model event tracks");
                }
            }
            for (const auto array : {std::pair<size_t, size_t>{0x1c, 64}, {0x2c, 88}, {0x3c, 48}}) {
                const uint32_t count = Retail::SlimeGiant::Word(original, array.first);
                const uint32_t offset = Retail::SlimeGiant::Word(original, array.first + 4);
                const uint32_t playerOffset = Retail::SlimeGiant::Word(player, array.first + 4);
                Check(count == Retail::SlimeGiant::Word(player, array.first), "Centaur source array counts are preserved");
                for (uint32_t i = 0; i < count; ++i)
                    Check(!std::memcmp(original.data() + offset + i * array.second,
                        player.data() + playerOffset + i * array.second, array.first == 0x1c ? 62 : array.second),
                        "Centaur preserves motion metadata, bones and vertices; only alias-ring links change");
            }
            for (const auto ids : {std::pair<uint16_t, uint16_t>{50, 128}, {188, 123}, {189, 129}}) {
                Check(Retail::Centaur::PickupSequence(adapted.c_str(), ids.first) == ids.second,
                    "playable Centaur pickup uses existing motion without adding sequence aliases");
                const auto index = Retail::SlimeGiant::Half(player,
                    Retail::SlimeGiant::Word(player, 0x28) + ids.second * 2);
                Check(index < Retail::SlimeGiant::Word(player, 0x1c), "pickup source exists in the original rig");
                uint32_t next = index;
                const auto count = Retail::SlimeGiant::Word(player, 0x1c);
                for (uint32_t step = 0; step <= count; ++step) {
                    Check(next < count && step < count, "pickup external-animation alias ring terminates");
                    next = Retail::SlimeGiant::Half(player, Retail::SlimeGiant::Word(player, 0x20) + next * 64 + 62);
                    if (next == index) break;
                }
            }
            for (const uint32_t socket : {1u, 2u, 5u, 6u, 11u, 12u}) {
                const uint32_t count = Retail::SlimeGiant::Word(player, 0xf8);
                const uint32_t offset = Retail::SlimeGiant::Word(player, 0xfc);
                Check(socket < count && Retail::SlimeGiant::Half(player, offset + socket * 2)
                    < Retail::SlimeGiant::Word(player, 0xf0), "all required Centaur equipment sockets resolve");
            }
            for (uint32_t lod = 0; lod < Retail::SlimeGiant::Word(player, 0x44); ++lod) {
                char suffix[16]; std::snprintf(suffix, sizeof(suffix), "%02u.skin", lod);
                const auto name = adapted.substr(0, adapted.size() - 3) + suffix;
                std::printf("Checking skin %s\n", name.c_str()); std::fflush(stdout);
                std::vector<uint8_t> skin;
                require(source.Read(name.c_str(), skin, error));
                const auto ordinarySkinName = ordinary.substr(0, ordinary.size() - 3) + suffix;
                std::vector<uint8_t> ordinarySkin, expectedBatches;
                require(source.Read(ordinarySkinName.c_str(), ordinarySkin, error));
                for (uint32_t i = 0; i < Retail::SlimeGiant::Word(ordinarySkin, 0x24); ++i) {
                    const size_t at = Retail::SlimeGiant::Word(ordinarySkin, 0x28) + size_t(i) * 24;
                    const uint16_t section = Retail::SlimeGiant::Half(ordinarySkin, at + 4);
                    const uint16_t geoset = Retail::SlimeGiant::Half(ordinarySkin,
                        Retail::SlimeGiant::Word(ordinarySkin, 0x20) + size_t(section) * 48);
                    if (id == Retail::Centaur::Bodies[1] && (geoset == 601 || geoset == 602 || geoset == 902))
                        continue;
                    expectedBatches.insert(expectedBatches.end(), ordinarySkin.begin() + at, ordinarySkin.begin() + at + 24);
                }
                Check(Retail::SlimeGiant::Word(skin, 0x24) * 24 == expectedBatches.size()
                    && !std::memcmp(skin.data() + Retail::SlimeGiant::Word(skin, 0x28),
                        expectedBatches.data(), expectedBatches.size()),
                    "every unrelated source render batch is byte-identical and in its original order");
                bool body = false, arms = false, chest = false, tail = false;
                uint32_t wrapSections = 0;
                for (uint32_t i = 0; i < Retail::SlimeGiant::Word(skin, 0x1c); ++i) {
                    const uint32_t geoset = Retail::SlimeGiant::Half(skin,
                        Retail::SlimeGiant::Word(skin, 0x20) + i * 48);
                    body = body || geoset == 0; arms = arms || geoset == 6101;
                    chest = chest || geoset == 6201; tail = tail || geoset == 6701;
                    wrapSections += geoset == Retail::Centaur::FemaleChestWrap;
                    Check(!geoset || geoset >= 6000, "Centaur cosmetics cannot overlap player item selectors");
                }
                Check(body && tail && (id != Retail::Centaur::Bodies[1] || (arms && chest)),
                    "all Centaur LODs retain complete sex-specific anatomy");
                Check(wrapSections == (id == Retail::Centaur::Bodies[1] ? 1u : 0u),
                    "female chest wrap has one dedicated section, separate from source outfit 202");
                if (id == Retail::Centaur::Bodies[1])
                    for (uint32_t i = 0; i < Retail::SlimeGiant::Word(skin, 0x24); ++i) {
                        const size_t at = Retail::SlimeGiant::Word(skin, 0x28) + size_t(i) * 24;
                        const uint16_t section = Retail::SlimeGiant::Half(skin, at + 4);
                        const uint16_t geoset = Retail::SlimeGiant::Half(skin,
                            Retail::SlimeGiant::Word(skin, 0x20) + size_t(section) * 48);
                        Check(geoset != 6601 && geoset != 6602 && geoset != 6902,
                            "female built-in belts/quiver have no render batches in any LOD");
                    }
            }
            if (argc == 4) {
                const auto read = [&](const std::filesystem::path& path) {
                    std::ifstream input(path, std::ios::binary);
                    Check(input.good(), "open staged Centaur clothing fixture");
                    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
                };
                const auto root = std::filesystem::path(argv[3]) / "stage/assets/custom/centaur";
                const auto clothing = read(root / (std::to_string(id) + "-clothing.m2"));
                const auto skin = read(root / (std::to_string(id) + "-clothing00.skin"));
                require(Retail::ValidateSkin(skin, clothing, error));
                Check(Retail::SlimeGiant::Word(clothing, 0x2c) == Retail::SlimeGiant::Word(player, 0x2c),
                    "Centaur clothing and native parent have equal bone counts");
                for (uint32_t i = 0; i < Retail::SlimeGiant::Word(player, 0x2c); ++i) {
                    const size_t a = Retail::SlimeGiant::Word(clothing, 0x30) + size_t(i) * 88;
                    const size_t b = Retail::SlimeGiant::Word(player, 0x30) + size_t(i) * 88;
                    Check(!std::memcmp(clothing.data() + a, player.data() + b, 16)
                        && !std::memcmp(clothing.data() + a + 76, player.data() + b + 76, 12),
                        "Centaur clothing can share the exact original animated parent rig");
                }
            }
        }
        std::puts("Both original Centaur bodies, inherited skeletons, advertised render profiles and player adapters passed.");
        return 0;
    }
    {
        struct {
            M2Event event{};
            M2Array<uint32_t> times[5]{};
            uint32_t timestamp = 100;
        } fixture;
        fixture.event.m_enabled.m_loopIndex = 0xffff;
        fixture.event.m_enabled.m_sequenceTimes.m_count = 5;
        fixture.event.m_enabled.m_sequenceTimes.Set(fixture.times);
        fixture.times[1].m_count = 1;
        fixture.times[1].Set(&fixture.timestamp);
        Check(fixture.event.TimesForSequence(1)->Data()[0] == 100,
            "authored event timestamps remain readable");
        Check(fixture.event.TimesForSequence(4) == &fixture.times[4]
            && !fixture.event.TimesForSequence(5) && !fixture.event.TimesForSequence(71)
            && !fixture.event.TimesForSequence(UINT32_MAX),
            "five-entry female event table rejects the captured sequence 71 and other missing tracks");
        fixture.event.m_enabled.m_loopIndex = 0;
        Check(fixture.event.TimesForSequence(71) == &fixture.times[0],
            "global event tracks retain sequence-zero lookup for inherited animations");
        fixture.event.m_enabled.m_sequenceTimes.m_count = 0;
        Check(!fixture.event.TimesForSequence(71), "empty global event tracks are skipped");
    }
    {
        namespace centaur = Retail::Centaur;
        Retail::Db2::appearance::Recipe recipe;
        Check(centaur::Race(73) && centaur::Race(74) && !centaur::Race(72), "Centaur faction boundaries");
        Check(centaur::BeltLayer(55, 55) && !centaur::BeltLayer(0, 0) && !centaur::BeltLayer(33, 55),
            "belt composition excludes pants and anonymous tabard layers");
        Retail::Image belt{2, 4, std::vector<uint8_t>(32, 0)};
        belt.rgba[11] = 255; belt.rgba[23] = 128;
        const auto beltRows = std::vector<uint8_t>(belt.rgba.begin() + 8, belt.rgba.begin() + 24);
        Check(centaur::CropBeltRows(belt) && belt.width == 2 && belt.height == 2 && belt.rgba == beltRows,
            "belt strip crops only transparent rows and preserves the complete horizontal buckle design");
        Retail::Image blankBelt{2, 4, std::vector<uint8_t>(32, 0)}, malformedBelt{2, 4, {0}};
        Check(centaur::CropBeltRows(blankBelt) && blankBelt.height == 4 && !centaur::CropBeltRows(malformedBelt),
            "transparent belts remain transparent and malformed image bounds are rejected");
        Check(centaur::EquipmentSection(6, 55, 55, false) == 7
            && centaur::EquipmentSection(6, 55, 55, true) == -1
            && centaur::EquipmentSection(5, 55, 55, false) == -1
            && centaur::EquipmentSection(6, 33, 55, false) == 6,
            "Centaur boots paint only hooves while trouser textures retain their leg regions");
        for (uint32_t sex = 0; sex < 2; ++sex) for (uint32_t skin = 0; skin < 16; ++skin) {
            const uint32_t choice = centaur::FirstSkin + skin;
            Check(centaur::Build(sex, &choice, 1, recipe), "both Centaur bodies expose all sixteen skins");
            Check(recipe.modelFileDataId == centaur::Bodies[sex] && recipe.textureLayoutId == 141,
                "Centaur model IDs and clothing layout match the requested source");
            Check(recipe.layers[0].fileDataId == centaur::Skins[sex][skin], "Centaur sex-specific skin mapping");
            Check((std::find(recipe.geosets.begin(), recipe.geosets.end(), 6101) != recipe.geosets.end()) == (sex == 1),
                "female arms remain visible independently of equipment");
            Check((std::find(recipe.geosets.begin(), recipe.geosets.end(), centaur::FemaleChestWrap)
                != recipe.geosets.end()) == (sex == 1)
                && std::find(recipe.geosets.begin(), recipe.geosets.end(), 6202) == recipe.geosets.end(),
                "female chest wrap never selects the armored chest and waist outfit");
        }
        const uint32_t invalid = centaur::FirstSkin + 16;
        Check(!centaur::Build(2, nullptr, 0, recipe) && !centaur::Build(0, nullptr, 1, recipe)
            && !centaur::Build(0, &invalid, 1, recipe), "Centaur rejects malformed sex and skin selections");
        for (uint32_t sex = 0; sex < 2; ++sex)
        for (uint32_t head = 0; head < (sex ? 14u : 9u); ++head)
        for (uint32_t beard = 0; beard < (sex ? 1u : 8u); ++beard) {
            const uint32_t choices[] = {centaur::FirstSkin + 15,
                (sex ? centaur::FemaleHead : centaur::MaleHead) + head, centaur::Beard + beard};
            Check(centaur::Build(sex, choices, sex ? 2 : 3, recipe), "every authored Centaur head/beard combination resolves");
            const bool helmet = head >= centaur::VisibleHeads(sex);
            Check(centaur::CreationAllowed(centaur::Profile + sex, choices[1]) == !helmet,
                "helmet head options are unavailable without reordering the retained source IDs");
            if (head) Check((std::find(recipe.geosets.begin(), recipe.geosets.end(), 7000 + head)
                != recipe.geosets.end()) == !helmet, "saved removed helmets render as None");
            if (beard) Check(std::find(recipe.geosets.begin(), recipe.geosets.end(), 6100 + beard)
                != recipe.geosets.end(), "selected Centaur facial hair geometry is retained");
        }
        const uint32_t wrongSex = centaur::MaleHead + 1;
        Check(!centaur::Build(1, &wrongSex, 1, recipe), "female cannot select male-only head choices");
        Check(centaur::PathModel(centaur::Path(centaur::Bodies[0])) == centaur::Bodies[0]
            && !centaur::PathModel("retail/4036647.m2"), "Centaur adapter cannot change ordinary NPC consumers");
        std::vector<uint8_t> skin(48 + 2 * 48);
        std::memcpy(skin.data(), "SKIN", 4); Put(skin, 0x1c, 2); Put(skin, 0x20, 48);
        skin[96] = 101;
        std::string error;
        Check(centaur::PrepareSkin(skin, error) && Retail::SlimeGiant::Half(skin, 48) == 0
            && Retail::SlimeGiant::Half(skin, 96) == 6101, "Centaur anatomy and item selector namespaces stay separate");
        Check(!centaur::PrepareSkin(skin, error), "Centaur skin adaptation rejects accidental double processing");
        std::vector<uint8_t> chestSkin(48 + 4 * 48 + 4 * 24);
        std::memcpy(chestSkin.data(), "SKIN", 4);
        Put(chestSkin, 0x1c, 4); Put(chestSkin, 0x20, 48);
        Put(chestSkin, 0x24, 4); Put(chestSkin, 0x28, 48 + 4 * 48);
        for (uint16_t i = 0; i < 4; ++i) {
            const uint16_t source = i < 2 ? 201 : 202;
            const uint16_t material = i % 2 ? 0 : 3;
            std::memcpy(chestSkin.data() + 48 + size_t(i) * 48, &source, 2);
            const size_t at = 48 + 4 * 48 + size_t(i) * 24;
            std::memcpy(chestSkin.data() + at + 4, &i, 2);
            std::memcpy(chestSkin.data() + at + 10, &material, 2);
        }
        Check(centaur::PrepareSkin(chestSkin, error, true), "female chest selector fixture adapts");
        const uint16_t chestIds[] = {6201, centaur::FemaleChestWrap, 6202, 6202};
        for (uint32_t i = 0; i < 4; ++i)
            Check(Retail::SlimeGiant::Half(chestSkin, 48 + i * 48) == chestIds[i],
                "native chest wrap cannot collide with either section of source chest outfit 202");
        const auto accessorySkin = [] {
            std::vector<uint8_t> bytes(48 + 7 * 48 + 7 * 24);
            std::memcpy(bytes.data(), "SKIN", 4);
            Put(bytes, 0x1c, 7); Put(bytes, 0x20, 48);
            Put(bytes, 0x24, 7); Put(bytes, 0x28, 48 + 7 * 48);
            const uint16_t geosets[] = {0, 101, 201, 601, 602, 701, 902};
            for (uint16_t i = 0; i < 7; ++i) {
                std::memcpy(bytes.data() + 48 + size_t(i) * 48, &geosets[i], 2);
                std::memcpy(bytes.data() + 48 + 7 * 48 + size_t(i) * 24 + 4, &i, 2);
            }
            return bytes;
        };
        auto femaleSkin = accessorySkin();
        const auto originalFemale = femaleSkin;
        Check(centaur::PrepareSkin(femaleSkin, error, true), "female accessory filtering succeeds");
        Check(Retail::SlimeGiant::Word(femaleSkin, 0x24) == 4,
            "all female source belt and quiver batches are removed");
        const uint16_t retained[] = {0, 1, 2, 5};
        for (uint32_t i = 0; i < 4; ++i)
            Check(!std::memcmp(femaleSkin.data() + Retail::SlimeGiant::Word(femaleSkin, 0x28) + i * 24,
                originalFemale.data() + 48 + 7 * 48 + size_t(retained[i]) * 24, 24),
                "body, arms, chest and tail batches retain their original bytes and indices");
        auto maleSkin = accessorySkin();
        Check(centaur::PrepareSkin(maleSkin, error) && Retail::SlimeGiant::Word(maleSkin, 0x24) == 7,
            "female accessory policy cannot remove male source geometry");
        auto badSkin = accessorySkin();
        badSkin[48 + 7 * 48 + 4] = 7;
        const auto originalBad = badSkin;
        Check(!centaur::PrepareSkin(badSkin, error, true) && badSkin == originalBad,
            "invalid accessory batch indices fail before mutating the source");
    }
    {
        namespace sg = Retail::SlimeGiant;
        Retail::Db2::appearance::Recipe recipe;
        Check(sg::Count(sg::ColorOption) == 35 && sg::Count(sg::DripOption) == 2,
            "thirty new colors retain the original five and independent drip control");
        for (uint32_t index = 0; index < sg::Colors; ++index)
        for (uint32_t drip : {sg::Off, sg::On}) {
            const uint32_t color = sg::ColorId(index);
            const uint32_t choices[] = {color, drip};
            Check(sg::Build(0, choices, 2, recipe), "all seventy Slime Giant appearances resolve");
            Check(recipe.modelFileDataId == sg::Body && recipe.textureLayoutId == 141,
                "all palettes retain the correct body and Kul Tiran armor layout");
            const uint32_t texture = index < sg::OriginalColors ? 3491663 + index * 2
                : sg::LocalTexture + (index - sg::OriginalColors) * 2;
            Check(recipe.layers.size() == 2 && recipe.layers[0].fileDataId == texture
                && recipe.layers[1].fileDataId == texture + 1, "each skin resolves its matching slime pair");
            Check(sg::TexturePath(texture).empty() == (index < sg::OriginalColors),
                "original CASC textures and new local textures have separate namespaces");
            Retail::Db2::appearance::ChrChoice choice{};
            Check(sg::Choice(sg::ColorOption, index, choice) && choice.id == color
                && !sg::ChoiceName(color).empty(), "saved color index maps to its named stable choice");
            Check(recipe.geosets.empty() == (drip == sg::Off), "drip is independent of color");
        }
        Check(sg::ColorId(0) == sg::Green && sg::ColorId(4) == sg::Gold, "existing saved skins keep their IDs");
        uint32_t ignored = 0;
        Check(!sg::ColorIndex(sg::Off, ignored) && !sg::ColorIndex(sg::On, ignored)
            && !sg::ColorIndex(sg::ExtraColor + sg::ExtraColors, ignored), "color IDs cannot overlap drip or overflow");
        Check(sg::TexturePath(sg::LocalTexture - 1).empty()
            && sg::TexturePath(sg::LocalTexture + sg::ExtraColors * 2).empty(), "local texture range is bounded");
        Check(!sg::Build(1, nullptr, 0, recipe), "reject unprovided Slime Giant female body");
        const uint32_t duplicate[] = {sg::Green, sg::Amber};
        Check(!sg::Build(0, duplicate, 2, recipe), "reject contradictory colors");
        Check(!sg::Build(0, nullptr, 1, recipe), "reject missing choice payload");
        Check(sg::PathModel(sg::Path(sg::Body)) == sg::Body && !sg::PathModel("retail/3490792.m2"),
            "player adaptation cannot affect the ordinary creature path");
        std::vector<uint8_t> skin(48 + 4 * 48);
        std::memcpy(skin.data(), "SKIN", 4); Put(skin, 0x1c, 4); Put(skin, 0x20, 48);
        const auto original = skin;
        std::string error;
        Check(sg::PrepareSkin(skin, error), "prepare isolated player mesh selector");
        Check(sg::Half(skin, 48 + 3 * 48) == sg::DripGeoset, "viewer section three gets its own selector");
        skin[48 + 3 * 48] = skin[48 + 3 * 48 + 1] = 0;
        Check(skin == original, "all other original skin bytes are preserved");
        skin.resize(47);
        const auto truncated = skin;
        Check(!sg::PrepareSkin(skin, error) && skin == truncated, "reject truncated skin without mutation");
        std::vector<uint8_t> model(0x130 + 208 * 88 + 3 * 16);
        Put(model, 0x2c, 208); Put(model, 0x30, 0x130);
        Put(model, 0x50, 3); Put(model, 0x54, 0x130 + 208 * 88);
        Put(model, 0x130 + 208 * 88, 11); Put(model, 0x130 + 208 * 88 + 32, 12);
        const auto source = model;
        Check(sg::PrepareModel(model, sg::Body, error), "prepare independent armor and cloak slots");
        Check(sg::Word(model, 0x50) == 5 && sg::Word(model, 0xf0) == sg::Word(source, 0xf0),
            "append only material slots; retain original equipment sockets");
        Check(std::equal(source.begin() + 0x130, source.begin() + 0x130 + 208 * 88, model.begin() + 0x130),
            "player preparation preserves every authored bone and track");
        model = source; Put(model, 0x2c, 207);
        const auto changedRig = model;
        Check(!sg::PrepareModel(model, sg::Body, error) && model == changedRig, "changed source rig fails before mutation");
    }
    {
        using Retail::AllowCreationChoice;
        using Retail::SourceKind;
        Check(!AllowCreationChoice(SourceKind::Retail, 1, 10, "Transmog") &&
            !AllowCreationChoice(SourceKind::Forever, 218, 10, ""), "exclude named and unnamed internal Transmog creator entries");
        Check(!AllowCreationChoice(SourceKind::Forever, 218, 12, "") &&
            !AllowCreationChoice(SourceKind::Forever, 219, 12, ""), "Skyborne creator excludes inherited Blood Elf entries for both bodies");
        Check(AllowCreationChoice(SourceKind::Retail, 218, 12, "") &&
            AllowCreationChoice(SourceKind::Forever, 19, 12, ""), "Skyborne exclusion cannot affect other products or models");
        for (uint32_t requirement : {0u, 141u, 144u, 145u, 4896u, 4898u})
            Check(AllowCreationChoice(SourceKind::Forever, 218, requirement, ""), "retain playable Skyborne choices and authored accessory prerequisites");
    }
    {
        Retail::Image skin{3, 1, {100, 120, 140, 255, 100, 120, 140, 255, 100, 120, 140, 255}};
        const Retail::Image tattoo{3, 1, {255, 255, 255, 255, 128, 128, 128, 255, 0, 0, 0, 0}};
        Retail::PaintImage(skin, tattoo, 0, 0, 3, 1, 4); // Orc layout 105, target 16: multiply.
        Check(skin.rgba == std::vector<uint8_t>{100, 120, 140, 255, 50, 60, 70, 255, 100, 120, 140, 255},
            "Orc tattoo white background and transparent texels preserve skin; only dark marks shade it");

        auto painted = [](std::vector<uint8_t> base, std::vector<uint8_t> source, uint32_t mode) {
            Retail::Image dst{1, 1, std::move(base)}, src{1, 1, std::move(source)};
            Retail::PaintImage(dst, src, 0, 0, 1, 1, mode);
            return dst.rgba;
        };
        // Golden pixels from the pinned character shader and its framebuffer blend factors,
        // including translucent destination alpha rather than only opaque body sheets.
        Check(painted({80, 120, 160, 128}, {128, 64, 255, 128}, 4) == std::vector<uint8_t>{70, 97, 160, 112},
            "DB2 multiply uses shader-result alpha before framebuffer blending");
        Check(painted({64, 128, 192, 255}, {64, 192, 128, 255}, 6) == std::vector<uint8_t>{32, 192, 192, 255},
            "DB2 overlay uses source thresholds instead of Mod2x");
        Check(painted({64, 128, 192, 255}, {64, 192, 128, 255}, 7) == std::vector<uint8_t>{112, 224, 224, 255},
            "DB2 screen preserves the authored highlight operation");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 9) == std::vector<uint8_t>{150, 125, 125, 160},
            "DB2 straight alpha blends RGB and unions destination alpha");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 15) == std::vector<uint8_t>{150, 125, 125, 96},
            "DB2 inferred alpha uses source-alpha factors for alpha too");
        for (uint32_t mode : {0u, 1u})
            Check(painted({100, 150, 200, 255}, {4, 5, 6, 0}, mode) == std::vector<uint8_t>{4, 5, 6, 0},
                "DB2 base blit retains authored RGB beneath a transparent mask");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 2) == std::vector<uint8_t>{180, 110, 80, 160},
            "legacy equipment mode 2 retains straight alpha-over on translucent skin");
        Retail::Image armor{2, 1, {100, 120, 140, 255, 100, 120, 140, 255}};
        const Retail::Image item{1, 1, {200, 40, 60, 128}};
        Retail::PaintImage(armor, item, 1, 0, 1, 1, 2);
        Check(armor.rgba == std::vector<uint8_t>{100, 120, 140, 255, 150, 80, 100, 255},
            "legacy armor still paints only its equipment rectangle over the Human body");
    }
    {
        using namespace Retail::LegacyAppearance;
        for (const auto& option : Options) {
            std::array<uint8_t, 13> fields{};
            // Populate neighboring digits too: modifying one retail option must preserve every
            // other saved choice sharing its original byte, including extra fields above five.
            for (const auto& neighbor : Options) if (neighbor.race == option.race && neighbor.sex == option.sex)
                Check(Encode(neighbor.race, neighbor.sex, neighbor.id, Choices[neighbor.offset + neighbor.count - 1], fields),
                    "installed packed codec fits all authored choices");
            for (uint32_t i = 0; i < option.count; ++i) {
                const auto before = fields;
                const uint32_t selected = Choices[option.offset + i];
                uint32_t decoded = 0;
                Check(Encode(option.race, option.sex, option.id, selected, fields)
                    && Decode(option.race, option.sex, option.id, fields, decoded) && decoded == selected,
                    "frozen source choice IDs survive Retail option reordering");
                for (const auto& neighbor : Options) if (neighbor.race == option.race && neighbor.sex == option.sex && neighbor.id != option.id) {
                    uint32_t oldChoice = 0, newChoice = 0;
                    Check(Decode(neighbor.race, neighbor.sex, neighbor.id, before, oldChoice)
                        && Decode(neighbor.race, neighbor.sex, neighbor.id, fields, newChoice) && oldChoice == newChoice,
                        "changing a packed selector preserves all neighboring saved choices");
                }
            }
            const auto before = fields;
            Check(!Encode(option.race, option.sex, option.id, UINT32_MAX, fields) && fields == before,
                "new unencodable Retail choices cannot corrupt installed appearance bytes");
        }
        Check(Retail::HelmetMaskRace(85) == 3 && Retail::HelmetMaskRace(91) == 4,
            "Horde Earthen/Haranir helmets use visual anatomy rather than faction eligibility");
        Check(!Retail::UsesLegacyClosedFeet(6) && !Retail::UsesLegacyClosedFeet(11)
            && !Retail::UsesLegacyClosedFeet(86) && Retail::UsesLegacyClosedFeet(3),
            "legacy boots preserve hooves and authored Feet choices");
    }
    {
        struct FootModel {
            bool visible[100];
            FootModel() { std::fill(std::begin(visible), std::end(visible), true); }
            void SetGeometryVisible(uint32_t first, uint32_t last, int visibleValue) {
                for (uint32_t id = first; id <= last; ++id) visible[id - 2000] = visibleValue != 0;
            }
            bool Only(uint16_t id) const {
                for (uint16_t i = 1; i < 100; ++i) if (visible[i] != (i + 2000 == id)) return false;
                return true;
            }
        } model;
        Retail::ApplyLegacyFootGeometry(model, 10141, true); // Actual style-zero starter boots.
        Check(model.Only(2002), "style-zero starter boots hide bare toes and all alternate foot meshes");
        Retail::ApplyLegacyFootGeometry(model, 64789, true);
        Check(model.Only(2002), "swapping to styled boots keeps the closed shoe independently of shin style");
        Retail::ApplyLegacyFootGeometry(model, 64789, true); // A robe refresh retains equipped boots.
        Check(model.Only(2002), "a geometry refresh does not restore bare toes under equipped boots");
        Retail::ApplyLegacyFootGeometry(model, 0, true);
        Check(model.Only(2001), "removing boots hides the shoe and restores the original bare foot");
        Retail::ApplyLegacyFootGeometry(model, 10141, true);
        Check(model.Only(2002), "re-equipping boots restores the closed shoe without leftover bare feet");
        Retail::ApplyLegacyFootGeometry(model, 10141, false);
        Check(model.Only(2001), "models lacking a closed-foot mesh retain a visible foot fallback");
    }
    {
        // Real pinned Dwarf recipes omit group 32; the shared visibility reset used to hide both heads.
        const std::vector<uint16_t> dwarfMale{0, 300, 102, 3500, 1600, 3401};
        const std::vector<uint16_t> dwarfFemale{2, 3500, 1600};
        auto has = [](const std::vector<uint16_t>& ids, uint16_t id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        };
        for (const auto& recipe : {dwarfMale, dwarfFemale}) {
            const auto defaults = Retail::DefaultRaceGeosets(recipe);
            Check(has(defaults, 3202) && !has(defaults, 3201),
                "Dwarf recipe restores the full face/head instead of the neck-only variant");
            Check(has(defaults, 2001) && has(defaults, 2201) && has(defaults, 3301) && has(defaults, 5101),
                "head fallback preserves the existing feet, torso and eyes");
        }
        for (const uint16_t shape : {3200, 3202, 3203, 3204}) {
            const auto defaults = Retail::DefaultRaceGeosets({0, 300, 100, 200, shape, 3400, 702});
            Check(!has(defaults, 3202), "explicit Human face shapes and hidden-head choices suppress the fallback");
        }
        Check(!has(Retail::DefaultRaceGeosets({2004, 2202, 3302, 5103}), 2001),
            "anatomy defaults do not override selected equipment/body variants");
        // Pinned source skins 974391/921897, 1100099/1100298 and 1734086/1733813
        // have palms/fingers in 2301, separate from every 401..404 glove cuff. Their
        // default customization recipes omit group 23 for both sexes.
        const std::vector<std::vector<uint16_t>> elfRecipes{
            {2, 200, 300, 100, 3400, 701, 3900, 5100},
            {2, 3900, 3401, 701, 3700, 3500, 1600, 3600, 5100},
            {2, 100, 702, 5100}, {2, 702, 3500, 3600, 3900, 5100},
            {2, 100, 702, 200, 5100}, {2, 3500, 702, 200, 5100}
        };
        for (const auto& recipe : elfRecipes) {
            for (const uint16_t glove : {401, 402, 403, 404}) {
                auto equipped = recipe;
                equipped.push_back(glove);
                Check(has(Retail::DefaultRaceGeosets(equipped), 2301),
                    "both sexes of Night/Blood/Void Elves retain hands with every legacy glove style");
            }
        }
        for (const uint16_t selectedHand : {2300, 2301, 2302})
            Check(!has(Retail::DefaultRaceGeosets({selectedHand}), 2301),
                "explicit hidden or alternate hand recipes suppress the hand fallback");
    }
    {
        struct Model {
            std::set<uint16_t> visible;
            void SetGeometryVisible(uint32_t first, uint32_t last, int show) {
                for (uint32_t id = first; id <= last; ++id) {
                    if (show) visible.insert(uint16_t(id));
                    else visible.erase(uint16_t(id));
                }
            }
        };
        const std::set<uint16_t> inherited{0, 2, 1701, 1702, 1703, 1704, 1705, 2001, 2301, 3301, 5101, 5102, 5103};
        const std::set<uint16_t> authored{0, 2, 2001, 2301, 3301, 5101, 5102, 5103};
        for (uint32_t race : {4u, 95u, 96u}) {
            Model model{inherited};
            Retail::ApplyRaceEyeGeometry(model);
            Check(model.visible == authored, "Skyborne retain anatomy, eyes and eyesight overlays without inherited lightning");
            // A later customization or class refresh restores stock eye-glow meshes first.
            model.SetGeometryVisible(1701, 1701, 1);
            model.SetGeometryVisible(1703, 1703, 1);
            Retail::ApplyRaceEyeGeometry(model);
            Check(model.visible == authored, "Skyborne eye effects stay off after stock/DK and appearance refreshes");
            Retail::ApplyRaceEyeGeometry(model, true);
            auto enabled = authored; enabled.insert(1701);
            Check(model.visible == enabled, "explicit lightning enables exactly one effect while retaining normal eyes");
            Retail::ApplyRaceEyeGeometry(model, false);
            Check(model.visible == authored, "Off reverses an explicit On without altering other appearance");
        }
    }
    {
        using namespace CharacterEyeEffects;
        for (uint64_t payload : {uint64_t{0}, uint64_t{255}, uint64_t{0x123456789abc}, uint64_t{0x7fffffffffffffff}}) {
            const auto enabled = WithLightning(payload, true);
            Check(Lightning(enabled) && Appearance(enabled) == payload, "eye flag preserves every existing extension bit");
            Check(WithLightning(enabled, false) == payload, "clearing eye lightning restores the original packed appearance");
            Check(((uint32_t(enabled >> 32) & LightningWordMask) != 0), "public object high word carries the saved toggle");
        }
        for (bool enabled : {false, true}) {
            const auto bytes = Trailer(enabled); bool read = !enabled;
            Check(ReadTrailer(bytes.data(), bytes.size(), read) && read == enabled, "EYE1 footer round trips Off and On");
            for (size_t size = 0; size < bytes.size(); ++size)
                Check(!ReadTrailer(bytes.data(), size, read), "truncated cosmetic footer cannot become an enabled toggle");
        }
        auto invalid = Trailer(true); bool enabled = false;
        invalid[0] = 2;
        Check(!ReadTrailer(invalid.data(), invalid.size(), enabled), "reject non-boolean eye selection");
        invalid = Trailer(true); invalid[4] = 0;
        Check(!ReadTrailer(invalid.data(), invalid.size(), enabled), "reject unknown cosmetic footer versions");
        invalid = Trailer(true);
        Check(!ReadTrailer(invalid.data(), invalid.size() + 1, enabled), "reject oversized cosmetic trailers");
        struct Packet {
            std::vector<uint8_t> bytes;
            size_t read = 10;
            size_t rpos() const { return read; }
            void rpos(size_t value) { read = value; }
            size_t size() const { return bytes.size(); }
            const uint8_t* contents() const { return bytes.data(); }
            void resize(size_t size) { bytes.resize(size); read = 0; } // Real ByteBuffer behavior.
        };
        for (size_t profile : {size_t{0}, size_t{12}}) for (bool value : {false, true}) {
            Packet packet; packet.bytes.resize(10 + profile, 42);
            const auto original = packet.bytes;
            const auto footer = Trailer(value);
            packet.bytes.insert(packet.bytes.end(), footer.begin(), footer.end());
            bool present = false, selected = !value;
            Check(ReadTail(packet, profile, present, selected) && present && selected == value
                && packet.rpos() == 10 && packet.bytes == original, "optional footer preserves the profile payload and parser cursor");
            present = false;
            Check(ReadTail(packet, profile, present, selected) && !present,
                "legacy clients with no footer retain their existing packet shape");
            packet.bytes.insert(packet.bytes.end(), footer.begin(), footer.end());
            packet.bytes.insert(packet.bytes.end(), footer.begin(), footer.end());
            Check(!ReadTail(packet, profile, present, selected), "duplicate eye footers fail instead of shifting appearance parsing");
        }
    }
    // Reproduce world rendering: item identity survives temporary texture cleanup, and an update
    // of one section must retain the other equipped sections in the next complete retail sheet.
    struct Section {
        void* m_layerTex[7]{};
        uint32_t m_layerItemDisplayId[7]{};
        uint32_t m_layerMask = 0;
    } equipment[8];
    int loadedTexture = 0;
    equipment[3].m_layerItemDisplayId[1] = 100; // chest
    equipment[3].m_layerMask = 2;
    equipment[3].m_layerTex[1] = &loadedTexture;
    equipment[7].m_layerItemDisplayId[0] = 200; // boots
    equipment[7].m_layerMask = 1;
    equipment[7].m_layerTex[0] = &loadedTexture;
    auto beforeCleanup = Retail::CollectEquipmentLayers(equipment, false);
    equipment[3].m_layerTex[1] = equipment[7].m_layerTex[0] = nullptr;
    auto afterCleanup = Retail::CollectEquipmentLayers(equipment, false);
    Check(beforeCleanup.size() == 2 && afterCleanup.size() == 2
        && afterCleanup[0].displayId == 100 && afterCleanup[1].displayId == 200,
        "temporary texture cleanup preserves the equipped body layers");
    equipment[7].m_layerItemDisplayId[0] = 201;
    equipment[7].m_layerTex[0] = &loadedTexture;
    auto partialUpdate = Retail::CollectEquipmentLayers(equipment, false);
    Check(partialUpdate.size() == 2 && partialUpdate[0].displayId == 100
        && !partialUpdate[0].texture && partialUpdate[1].displayId == 201,
        "changing boots retains the chest even when its texture handle was released");
    equipment[7] = {};
    auto removedBoots = Retail::CollectEquipmentLayers(equipment, false);
    Check(removedBoots.size() == 1 && removedBoots[0].displayId == 100,
        "unequipping boots removes only their body layer");
    equipment[5].m_layerItemDisplayId[0] = 300; // pants hidden below robe
    equipment[5].m_layerMask = 1;
    auto robeLayers = Retail::CollectEquipmentLayers(equipment, true);
    auto pantsLayers = Retail::CollectEquipmentLayers(equipment, false);
    Check(robeLayers.size() == 1 && pantsLayers.size() == 2,
        "robe removal restores trousers without changing saved equipment");
    {
        // A legacy helmet is authored relative to the crown socket, while the retail socket is
        // at the head pivot. Test a different head position on all axes rather than a fixed lift.
        const uint16_t dwarfSocket[] = {32767, 32767, 32767, 65535};
        const uint16_t oppositeIdentity[] = {32768, 32768, 32768, 0};
        const uint16_t rotatedSocket[] = {32767, 55938, 32767, 55938};
        const uint16_t invalidSocket[] = {32767, 32767, 32767, 32767};
        Check(Retail::IsIdentityHelmetRotation(dwarfSocket) && Retail::IsIdentityHelmetRotation(oppositeIdentity),
            "accept quantized global identity socket rotations on Dwarf helmets");
        Check(!Retail::IsIdentityHelmetRotation(rotatedSocket) && !Retail::IsIdentityHelmetRotation(invalidSocket),
            "do not apply a translation-only helmet fit to a rotated or invalid socket");
        auto donor = Body();
        Put(donor, 4, 264);
        auto array = [&](size_t field, uint32_t count, size_t stride) {
            const size_t offset = donor.size();
            Put(donor, field, count); Put(donor, field + 4, uint32_t(offset));
            donor.resize(offset + count * stride);
            return offset;
        };
        auto point = [&](size_t offset, float x, float y, float z) {
            const float xyz[] = {x, y, z};
            for (size_t i = 0; i < 3; ++i) {
                uint32_t bits;
                std::memcpy(&bits, &xyz[i], 4);
                Put(donor, offset + i * 4, bits);
            }
        };
        const size_t keys = array(0x34, 7, 2);
        const size_t attachments = array(0xf0, 1, 40);
        const size_t lookup = array(0xf8, 12, 2);
        point(Get(donor, 0x30) + 76, 1, 2, 3);
        Put(donor, attachments, 11);
        point(attachments + 8, 2, 4, 6);
        Retail::HelmetAnchor anchor;
        std::string anchorError;
        Check(Retail::ReadHelmetAnchor(donor, anchor, anchorError), "read installed legacy helmet anchor");
        const auto original = donor;
        const Retail::HelmetAnchor target{{10, 20, 30}, {9, 18, 27}};
        const auto correction = Retail::HelmetTranslation(anchor, target);
        Check(correction == std::array<float, 3>{2, 4, 6}, "retarget crown origin to a different retail head/socket");
        Check(donor == original, "helmet fitting never changes source model bytes");
        Check(Retail::HelmetTranslation(anchor, anchor) == std::array<float, 3>{0, 0, 0},
            "matching skeletons retain the original helmet placement");
        donor[lookup + 22] = donor[lookup + 23] = 0xff;
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a missing helmet socket");
        donor = original; donor[keys + 12] = donor[keys + 13] = 0xff;
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a missing head key bone");
        donor = original; Put(donor, attachments + 8, 0x7fc00000);
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a nonfinite helmet origin");
    }
    std::string error;
    Retail::ModelFiles files;
    std::vector<uint8_t> prepared;
    {
        auto largeRig = Body();
        const size_t originalBone = Get(largeRig, 0x30), bones = largeRig.size();
        const std::vector<uint8_t> bind(largeRig.begin() + originalBone, largeRig.begin() + originalBone + 88);
        for (uint32_t i = 0; i < 300; ++i) largeRig.insert(largeRig.end(), bind.begin(), bind.end());
        Put(largeRig, 0x2c, 300); Put(largeRig, 0x30, uint32_t(bones));
        const size_t palette = Get(largeRig, 0x7c);
        largeRig[palette] = uint8_t(299); largeRig[palette + 1] = uint8_t(299 >> 8);
        Check(Retail::PrepareModel(Model(largeRig), prepared, files, error),
            "large character rigs retain uint16 global bone indices beyond 255");
        Check(Retail::ValidateSkin(Skin(), prepared, error),
            "byte section-local palette can reference global character bone 299");
        Put(largeRig, 0x2c, 65536);
        Check(!Retail::PrepareModel(Model(largeRig), prepared, files, error),
            "reject rigs exceeding the global uint16 bone domain");
    }
    {
        auto skeleton = [](bool parent, bool conflict = false) {
            std::vector<uint8_t> sks(24, 0), skb(16 + 88, 0), result;
            const uint32_t count = parent ? (conflict ? 3 : 2) : 1;
            const size_t sequences = sks.size(); sks.resize(sequences + count * 64);
            Put(sks, 8, count); Put(sks, 12, uint32_t(sequences));
            for (uint32_t i = 0; i < count; ++i) {
                const size_t p = sequences + size_t(i) * 64;
                Put(sks, p, parent ? (i == 2 ? 69 : i ? 60 : 0) : conflict ? 60 : 5);
                Put(sks, p + 4, 1000); Put(sks, p + 12, (parent && i) || (!parent && conflict) ? 0 : 0x20);
                Put(sks, p + 60, 0xffffffff);
                if (parent && i == 2) { Put(sks, p + 12, 0x40); sks[p + 62] = 1; sks[p + 63] = 0; }
            }
            const uint32_t lookupCount = parent ? (conflict ? 70 : 61) : conflict ? 61 : 6;
            const size_t lookup = sks.size(); sks.resize(lookup + lookupCount * 2, 0xff);
            Put(sks, 16, lookupCount); Put(sks, 20, uint32_t(lookup));
            const size_t primary = lookup + (parent ? 0 : conflict ? 120 : 10);
            sks[primary] = sks[primary + 1] = 0;
            if (parent) { sks[lookup + 120] = 1; sks[lookup + 121] = 0; }
            Put(skb, 0, 1); Put(skb, 4, 16); Put(skb, 16 + 8, 0xffff);
            for (const size_t track : {size_t(32), size_t(52), size_t(72)}) skb[track + 2] = skb[track + 3] = 0xff;
            if (parent) {
                const size_t times = skb.size(); skb.resize(times + 16);
                const size_t values = skb.size(); skb.resize(values + 16);
                const size_t residentTime = skb.size(); skb.resize(residentTime + 4);
                const size_t residentValue = skb.size(); skb.resize(residentValue + 12);
                Put(skb, 36, 2); Put(skb, 40, uint32_t(times));
                Put(skb, 44, 2); Put(skb, 48, uint32_t(values));
                Put(skb, times, 1); Put(skb, times + 4, uint32_t(residentTime));
                Put(skb, values, 1); Put(skb, values + 4, uint32_t(residentValue));
                Put(skb, times + 8, 2); Put(skb, times + 12, 0);
                Put(skb, values + 8, 2); Put(skb, values + 12, 8);
            }
            Chunk(result, "SKS1", sks); Chunk(result, "SKB1", skb);
            if (parent || conflict) { std::vector<uint8_t> afid(8); Put(afid, 0, 60); Put(afid, 4, parent ? 777 : 888); Chunk(result, "AFID", afid); }
            return result;
        };
        const auto child = skeleton(false), parent = skeleton(true);
        std::vector<uint8_t> merged;
        Check(Retail::MergeSkeleton(child, parent, merged, error), error.c_str());
        auto rawBody = Body(); Put(rawBody, 0x1c, 0); Put(rawBody, 0x2c, 0);
        auto model = Model(rawBody); Chunk(model, "SKID", Word(111));
        Check(Retail::PrepareModel(model, prepared, files, error, &merged), error.c_str());
        Check(Get(prepared, 0x1c) == 3 && (Get(prepared, Get(prepared, 0x20)) & 65535) == 5,
            "parent skeleton adds missing actions without replacing the child's authored sequence");
        std::vector<uint8_t> payload(32, 0), animation;
        Put(payload, 4, 100); Put(payload, 20, 0x3f800000); Chunk(animation, "AFSB", payload);
        Check(Retail::ValidateAnimation(animation, prepared, 60, 0, error), error.c_str());
        Check(files.animations.size() == 1 && files.animations[0].fileId == 777,
            "inherited external motion retains the parent's original AFID payload owner");
        Check(child == skeleton(false) && parent == skeleton(true), "flattening skeleton inheritance preserves source bytes");
        Check(Retail::MergeSkeleton(skeleton(false, true), skeleton(true, true), merged, error), error.c_str());
        Check(Retail::PrepareModel(model, prepared, files, error, &merged), error.c_str());
        Check(Retail::ValidateAnimation(animation, prepared, 69, 0, error),
            "inherited aliases use their parent's motion when the child overrides the aliased action");
        Check(std::any_of(files.animations.begin(), files.animations.end(), [](const Retail::AnimationFile& a) { return a.id == 69 && a.fileId == 777; })
            && std::any_of(files.animations.begin(), files.animations.end(), [](const Retail::AnimationFile& a) { return a.id == 60 && a.fileId == 888; }),
            "parent aliases and overridden child actions retain separate animation payload owners");
        auto corruptParent = parent;
        const size_t boneChunk = 8 + Get(parent, 4) + 8;
        Put(corruptParent, boneChunk + 16 + 12, 123);
        Check(!Retail::MergeSkeleton(child, corruptParent, merged, error), "reject incompatible inherited bone identities");
    }
    auto table = [](uint32_t fields, uint32_t id) {
        std::vector<uint8_t> data(20 + fields * 4 + 1);
        data[0] = 'W'; data[1] = 'D'; data[2] = 'B'; data[3] = 'C';
        Put(data, 4, 1); Put(data, 8, fields); Put(data, 12, fields * 4); Put(data, 16, 1); Put(data, 20, id);
        return data;
    };
    const auto baseItem = table(8, 10), newItem = table(8, 20);
    Check(Retail::MergeClientTable("item.dbc", baseItem, newItem, prepared, error) && Get(prepared, 4) == 2, "append new item records");
    Check(!Retail::MergeClientTable("item.dbc", baseItem, baseItem, prepared, error), "never overwrite existing client IDs");
    auto baseDisplay = table(25, 10), newDisplay = table(25, 20);
    baseDisplay.push_back('A'); baseDisplay.push_back(0); Put(baseDisplay, 16, 3); Put(baseDisplay, 24, 1);
    newDisplay.push_back('B'); newDisplay.push_back(0); Put(newDisplay, 16, 3); Put(newDisplay, 24, 1);
    Check(Retail::MergeClientTable("itemdisplayinfo.dbc", baseDisplay, newDisplay, prepared, error), "append display metadata with private strings");
    Check(Get(prepared, 24) == 1 && Get(prepared, 124) == 4 && prepared[224] == 'B', "preserve original string offsets and relocate additions");
    auto historicalDisplay = baseDisplay; Put(historicalDisplay, 32, 0xffffffff);
    Check(Retail::MergeClientTable("itemdisplayinfo.dbc", historicalDisplay, newDisplay, prepared, error) &&
        Get(prepared, 32) == 0xffffffff, "preserve opaque historical metadata without modifying unrelated rows");
    auto invalidTable = newDisplay; Put(invalidTable, 24, 999);
    Check(!Retail::MergeClientTable("itemdisplayinfo.dbc", baseDisplay, invalidTable, prepared, error), "reject invalid delta strings");
    invalidTable = newItem; Put(invalidTable, 4, 0xffffffff);
    Check(!Retail::MergeClientTable("item.dbc", baseItem, invalidTable, prepared, error), "reject overflowing table counts");
    for (const auto& schema : {std::pair<const char*, unsigned>{"creaturemodeldata.dbc", 28},
        {"creaturedisplayinfo.dbc", 16}, {"spell.dbc", 234}, {"spellicon.dbc", 2}, {"skilllineability.dbc", 14},
        {"spellvisual.dbc", 32}, {"spellvisualkit.dbc", 38}, {"spellvisualkitmodelattach.dbc", 10},
        {"spellcategory.dbc", 2}, {"spellduration.dbc", 4}, {"spellradius.dbc", 4}, {"spellrange.dbc", 40}, {"itemset.dbc", 53},
        {"itemlimitcategory.dbc", 20}, {"itemextendedcost.dbc", 16}, {"spellitemenchantment.dbc", 38},
        {"scalingstatdistribution.dbc", 22}, {"gameobjectdisplayinfo.dbc", 19}}) {
        Check(Retail::MergeClientTable(schema.first, table(schema.second, 1), table(schema.second, 2), prepared, error), "mount table additions");
        Check(!Retail::MergeClientTable(schema.first, table(schema.second, 1), table(schema.second, 1), prepared, error), "mount ID collision rejection");
    }
    auto baseEffect = table(7, 10), newEffect = table(7, 20);
    newEffect.push_back('E'); newEffect.push_back(0); Put(newEffect, 16, 3); Put(newEffect, 28, 1);
    Check(Retail::MergeClientTable("spellvisualeffectname.dbc", baseEffect, newEffect, prepared, error) &&
        Get(prepared, 4) == 2 && Get(prepared, 56) == 2, "append effect names and relocate their file strings");
    Put(newEffect, 28, 99);
    Check(!Retail::MergeClientTable("spellvisualeffectname.dbc", baseEffect, newEffect, prepared, error), "reject invalid effect name strings");
    auto newSet = table(53, 20);
    newSet.push_back('S'); newSet.push_back(0); Put(newSet, 16, 3); Put(newSet, 24, 1);
    Check(Retail::MergeClientTable("itemset.dbc", table(53, 10), newSet, prepared, error) &&
        Get(prepared, 20 + 53 * 4 + 4) == 2, "relocate an added item set's localized name");
    Put(newSet, 24, 99);
    Check(!Retail::MergeClientTable("itemset.dbc", table(53, 10), newSet, prepared, error), "reject invalid localized strings");
    Check(Retail::MergeClientTable("spell.dbc", table(234, 13990005), table(234, 300000), prepared, error) &&
        Get(prepared, 20) == 13990005, "preserve Esteria's existing high spell IDs verbatim");
    Check(!Retail::MergeClientTable("spell.dbc", table(234, 1), table(234, 13990005), prepared, error), "new imports retain bounded allocation policy");
    const auto body = Body(), valid = Model(body);
    Check(Retail::PrepareModel(valid, prepared, files, error), error.c_str());
    Check(Get(prepared, 4) == 264 && files.version == 274, "validated in-memory layout");
    Check(Get(prepared, Get(prepared, 0x20) + 28) == 150, "Retail split blend durations adapted");
    size_t texture = Get(prepared, 0x54);
    const auto path = reinterpret_cast<const char*>(prepared.data() + Get(prepared, texture + 12));
    Check(std::string(path) == "retail/456.blp", "TXID virtual filename");
    Check(files.skins.size() == 1 && files.skins[0] == 123, "SFID resolution");
    Check(Get(prepared, 0x58) == 1 && prepared[Get(prepared, 0x94)] == 0, "constant-one opacity channel replaces Retail sentinel");
    Check(Get(valid, 12) == 274, "original bytes unchanged");
    auto chunked = body; Put(chunked, 16, 0x200000);
    Check(Retail::PrepareModel(Model(chunked), prepared, files, error), "embedded animation with chunked-file capability flag");
    Put(chunked, Get(chunked, 0x20) + 12, 0);
    Check(!Retail::PrepareModel(Model(chunked), prepared, files, error), "do not accept unsupported chunked external animations");
    Check(Retail::PrepareModel(valid, prepared, files, error), "reset after animation checks");
    const auto skin = Skin();
    Check(Retail::ValidateSkin(skin, prepared, error), error.c_str());
    auto zeroMax = skin; Put(zeroMax, 44, 0);
    Check(Retail::ValidateSkin(zeroMax, prepared, error), "Retail skin with unspecified instance palette maximum");
    auto unusedInfluences = zeroMax; unusedInfluences[Get(zeroMax, 32) + 16] = 0;
    Check(Retail::PrepareSkin(unusedInfluences, prepared, error), "derive unused Retail skin influence count");
    Check(unusedInfluences[Get(zeroMax, 32) + 16] == 1, "reconstructed skin influence count");
    {
        auto twoWeights = body; twoWeights[Get(body, 0x40) + 12] = 200; twoWeights[Get(body, 0x40) + 13] = 55;
        std::vector<uint8_t> weighted;
        Check(Retail::PrepareModel(Model(twoWeights), weighted, files, error), error.c_str());
        auto understated = zeroMax; // stores 1 influence; the vertex carries 2 weights, both on bone 0
        Check(Retail::PrepareSkin(understated, weighted, error) && understated[Get(zeroMax, 32) + 16] == 2,
            "an understated retail influence count is raised to the vertices' weights");
        std::vector<uint8_t> empty(48); Put(empty, 0, 0x4e494b53); Put(empty, 44, 0xcdcdcdcd);
        Check(Retail::PrepareSkin(empty, weighted, error) && Get(empty, 44) == 0, error.c_str());
        auto pastMaterials = zeroMax; pastMaterials[Get(zeroMax, 40) + 10] = 1; // the model has one material
        Check(Retail::PrepareSkin(pastMaterials, weighted, error) && Get(pastMaterials, 36) == 0,
            "a batch naming a material past the model's table is dropped");
    }
    {
        auto twoLookups = body; // both texture lookups name texture 0
        Put(twoLookups, 0x80, 2); Put(twoLookups, 0x84, uint32_t(twoLookups.size())); twoLookups.resize(twoLookups.size() + 4);
        std::vector<uint8_t> twoModel;
        Check(Retail::PrepareModel(Model(twoLookups), twoModel, files, error), error.c_str());
        for (const auto& [shader, expected] : {std::pair<uint16_t, uint16_t>{0x13, 0x10}, {0x8017, 0x10}}) {
            auto pair = skin; const size_t batch = Get(pair, 40);
            pair[batch + 2] = uint8_t(shader); pair[batch + 3] = uint8_t(shader >> 8); pair[batch + 14] = 2;
            Check(Retail::PrepareSkin(pair, twoModel, error) && (pair[batch + 2] | pair[batch + 3] << 8) == expected
                && pair[batch + 14] == 1, "same-texture runtime pair and Opaque_Alpha draw one texture with its alpha");
        }
        auto threeLookups = body;
        Put(threeLookups, 0x80, 3); Put(threeLookups, 0x84, uint32_t(threeLookups.size())); threeLookups.resize(threeLookups.size() + 6);
        std::vector<uint8_t> threeModel;
        Check(Retail::PrepareModel(Model(threeLookups), threeModel, files, error), error.c_str());
        for (uint16_t shader : {uint16_t(0x8011), uint16_t(0x8009), uint16_t(0x4011)}) {
            auto triple = skin; const size_t batch = Get(triple, 40);
            triple[batch + 2] = uint8_t(shader); triple[batch + 3] = uint8_t(shader >> 8); triple[batch + 14] = 3;
            Check(Retail::PrepareSkin(triple, threeModel, error) && (triple[batch + 2] | triple[batch + 3] << 8) == 0x10
                && triple[batch + 14] == 1, "unmapped three-texture combiners draw their base texture");
        }
    }
    {
        auto missing = body; // sequence 1 is external (no 0x20) and no AFID ships it
        const size_t first = Get(body, 0x20), second = missing.size();
        missing.insert(missing.end(), body.begin() + first, body.begin() + first + 64);
        missing.insert(missing.end(), body.begin() + first, body.begin() + first + 64);
        missing[second + 64] = 75; Put(missing, second + 64 + 12, 0);
        Put(missing, 0x1c, 2); Put(missing, 0x20, uint32_t(second));
        std::vector<uint8_t> aliased;
        Check(Retail::PrepareModel(Model(missing), aliased, files, error), error.c_str());
        const size_t seq1 = Get(aliased, 0x20) + 64;
        Check((Get(aliased, seq1 + 12) & 0x40) && aliased[seq1 + 62] == 0 && aliased[seq1 + 63] == 0,
            "an external sequence without its .anim aliases the model's Stand");
    }
    for (size_t n = 0; n < valid.size(); ++n) {
        std::vector<uint8_t> truncated(valid.begin(), valid.begin() + n);
        Check(!Retail::PrepareModel(truncated, prepared, files, error), "reject truncated model at every byte boundary");
    }
    auto bad = valid;
    Put(bad, 4, 0xffffffff); Check(!Retail::PrepareModel(bad, prepared, files, error), "reject oversized chunk");
    bad = valid; Chunk(bad, "MD21", body); Check(!Retail::PrepareModel(bad, prepared, files, error), "reject duplicate model chunk");
    bad = valid; Chunk(bad, "SKID", Word(111));
    Check(!Retail::PrepareModel(bad, prepared, files, error) && error.find("SKEL") != std::string::npos, "explicit shared skeleton rejection");
    auto corruptBody = body;
    Put(corruptBody, 0x3c, 0xffffffff);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject overflowing array count");
    corruptBody = body; Put(corruptBody, 0x128, 1);
    Check(Retail::PrepareModel(Model(corruptBody), prepared, files, error) && Get(prepared, 0x128) == 0, "omit unsupported particles");
    {
        std::vector<uint8_t> chunked = {'A', 'F', 'M', '2', 3, 0, 0, 0, 7, 8, 9}, legacy = {7, 8, 9};
        std::string chunkError;
        Check(Retail::StripAnimationChunk(chunked, chunkError) && chunked == legacy, "strip AFM2 animation chunk header");
        Check(Retail::StripAnimationChunk(legacy, chunkError) && legacy.size() == 3, "leave legacy animation files alone");
        std::vector<uint8_t> truncated = {'A', 'F', 'M', '2', 9, 0, 0, 0, 7};
        Check(!Retail::StripAnimationChunk(truncated, chunkError), "reject truncated AFM2 chunk");
    }
    {
        // Same external offsets intentionally address different values in each payload. The old
        // adapter either discarded AFSB after AFM2 or read an AFSB chunk header as timestamps.
        auto owner = Body();
        Put(owner, Get(owner, 0x20) + 12, 0); // sequence zero is external
        auto array = [&](size_t field, uint32_t count, size_t stride) {
            const size_t offset = owner.size();
            Put(owner, field, count); Put(owner, field + 4, uint32_t(offset));
            owner.resize(offset + count * stride);
            return offset;
        };
        auto track = [&](size_t at) {
            Put(owner, at, 0xffff0001); // linear, no global sequence
            size_t times = array(at + 4, 1, 8);
            Put(owner, times, 2); Put(owner, times + 4, 0);
            size_t keys = array(at + 12, 1, 8);
            Put(owner, keys, 2); Put(owner, keys + 4, 8);
        };
        track(Get(owner, 0x30) + 16);
        size_t weight = array(0x58, 1, 20);
        track(weight);
        size_t attachment = array(0xf0, 1, 40);
        Put(owner, attachment, 11);
        track(attachment + 20);
        std::vector<uint8_t> model(12), bones(32), attachments(10), split;
        for (auto* payload : {&model, &bones, &attachments}) Put(*payload, 4, 100);
        Put(model, 8, 32767);
        Put(bones, 20, 0x3f800000); // translation x changes from zero to one
        attachments[8] = 1;
        Chunk(split, "AFM2", model); Chunk(split, "AFSB", bones); Chunk(split, "AFSA", attachments);
        const auto original = split;
        Check(Retail::StripAnimationChunk(split, error) && split == original,
            "retain every payload in a shared-skeleton animation");
        Retail::AnimationLayout layout;
        Check(Retail::InspectAnimation(split.data(), split.size(), layout, error)
            && layout.chunked && layout.model.size == 12 && layout.bones.size == 32
            && layout.attachments.size == 10 && layout.model.offset != layout.bones.offset,
            "separate model, skeleton-bone and skeleton-attachment address spaces");
        Check(Retail::ValidateAnimation(split, owner, 0, 0, error), error.c_str());
        auto missingBones = std::vector<uint8_t>{};
        Chunk(missingBones, "AFM2", model);
        Check(!Retail::ValidateAnimation(missingBones, owner, 0, 0, error),
            "do not validate skeletal keyframes against the short model payload");
        auto corrupt = split; Put(corrupt, layout.bones.offset, 101);
        Check(!Retail::ValidateAnimation(corrupt, owner, 0, 0, error), "check skeletal timestamp ordering inside AFSB");
        corrupt = split; corrupt.pop_back();
        Check(!Retail::InspectAnimation(corrupt.data(), corrupt.size(), layout, error), "reject truncated skeleton attachment chunks");
        corrupt = split; Chunk(corrupt, "AFSB", bones);
        Check(!Retail::InspectAnimation(corrupt.data(), corrupt.size(), layout, error), "reject ambiguous duplicate skeleton payloads");
        Put(owner, 0x58, 0); Put(owner, 0xf0, 0); // body needs only skeletal data
        std::vector<uint8_t> boneOnly; Chunk(boneOnly, "AFSB", bones);
        Check(Retail::ValidateAnimation(boneOnly, owner, 0, 0, error), "support AFSB-only talk/laugh files");
    }
    corruptBody = body; Put(corruptBody, Get(body, 0x30) + 8, 0);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject bone parent cycle");
    corruptBody = body;
    size_t bone = Get(body, 0x30);
    Put(corruptBody, bone + 20, 1); Put(corruptBody, bone + 24, 0xfffffff0);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject malformed track array");
    Check(Retail::PrepareModel(valid, prepared, files, error), "reset prepared model");
    bad = skin; Put(bad, 8, 0xfffffff0);
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject bad skin offset");
    bad = skin; bad[Get(skin, 40) + 14] = 3;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject unsupported multitexture batch");
    bad = skin; bad[Get(skin, 40) + 2] = 20; bad[Get(skin, 40) + 3] = 0x80;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject unsupported shader");
    bad = skin; bad[Get(skin, 24)] = 5;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject invalid vertex bone palette index");

    // Exercise config and routing without a network request or a WoW installation.
    {
        std::map<uint32_t, Retail::MountOverride> overrides;
        const std::string row = "20;wow;0123456789abcdef0123456789abcdef;2;42;43;0;44;1.25\n";
        Check(Retail::ParseMountOverrides(row, overrides) && overrides.size() == 1
            && overrides.at(20).model == "retail\\42.m2" && overrides.at(20).textures[2] == "44.blp"
            && overrides.at(20).scale == 1.25f, "packed mount identity, palette and scale");
        Check(!Retail::ParseMountOverrides(row + row, overrides) && overrides.size() == 1,
            "duplicate mount display rejected without replacing previous parsed output");
        Check(!Retail::ParseMountOverrides("20;wow;bad;2;42;43;0;44;1\n", overrides),
            "mount mapping requires a real build pin");
        Check(!Retail::ParseMountOverrides("20;wow;0123456789abcdef0123456789abcdef;2;42;0;0;0;nan\n", overrides),
            "mount mapping rejects non-finite scale");
        std::map<uint32_t, Retail::MountSkin> skins;
        Check(Retail::ParseMountSkins("# c\n200;t;1;4547582\n200;g;39;2\n200;g;0;3\n", skins) && skins.at(200).textures[0].second == 4547582
            && skins.at(200).geosets[0].first == 39 && skins.at(200).geosets[1].first == 0, "mount skin textures and geosets");
        Check(!Retail::ParseMountSkins("200;x;1;2\n", skins) && !Retail::ParseMountSkins("200;t;1;0\n", skins)
            && !Retail::ParseMountSkins("200;g;100;1\n", skins) && skins.size() == 1, "bad mount skin rows rejected");
        Check(Retail::ParseMountSkins("201;r;1490;91\n", skins) && skins.at(201).riderAnim == 1490
            && skins.at(201).riderFallback == 91 && !Retail::ParseMountSkins("201;r;120;600\n", skins)
            && !Retail::ParseMountSkins("201;r;120;0\n201;r;97;0\n", skins), "rider pose rows");
        Check(Retail::ParseMountSkins("202;h;0;0\n", skins) && skins.at(202).hideRider && skins.at(202).riderAnim < 0
            && !Retail::ParseMountSkins("202;h;1;0\n", skins), "hidden rider rows");
        std::map<uint32_t, Retail::MountKit> kits;
        Check(Retail::ParseMountKits("# c\r\n300;m;-1;7507893;0.5;-1;0;0\r\n300;m;17;7507894;0.12;0.03;-0.17;0.43\n300;e;0.273;0.133;0.231\n", kits)
            && kits.at(300).models.size() == 2 && kits.at(300).models[0].attachment == -1 && kits.at(300).models[1].offset[1] < 0
            && kits.at(300).tinted && kits.at(300).emissive[2] > 0.23f, "mount kit models and tint");
        Check(!Retail::ParseMountKits("300;m;17;0;1;0;0;0\n", kits) && !Retail::ParseMountKits("300;e;2;0;0\n", kits)
            && !Retail::ParseMountKits("300;m;17;5;1;0;0\n", kits) && !Retail::ParseMountKits("300;m;-2;5;1;0;0;0\n", kits)
            && kits.size() == 1, "bad mount kit rows rejected");
        Check(Retail::ParseMountKits("301;m;-2;7507893;0.5;-1;0;0\n301;e;0.1;0.2;0.3;0.5\n", kits) && kits.at(301).models[0].attachment == -2
            && kits.at(301).alpha == 0.5f && !Retail::ParseMountKits("301;e;0.1;0.2;0.3;0\n", kits)
            && !Retail::ParseMountKits("301;m;-3;5;1;0;0;0\n", kits), "kit translucency and body-centred effects");
    }
    auto temp = std::filesystem::temp_directory_path() / ("wow-retail-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    std::ofstream(temp / "aliases.csv") << "42;Creature/Test/Test.m2\n";
    std::ofstream(temp / "listfile.csv") << "43;Interface/Icons/Test.blp\n";
    std::ofstream(temp / "equipment.csv") << "# hand appearance\n123;916541;916543\n";
    std::ofstream(temp / "RetailCasc.ini") << "mode=online\npath=cache\nbuild=0123456789abcdef0123456789abcdef\naliases=aliases.csv\nlistfile=listfile.csv\nequipment=equipment.csv\n";
    Retail::CascSource source;
    Check(source.Configure((temp / "RetailCasc.ini").string().c_str(), error), error.c_str());
    Check(source.Handles("RETAIL\\43.blp"), "case/slash independent namespace");
    Check(source.Handles("creature\\test\\test.m2"), "explicit replacement alias");
    Check(source.Handles("retail/interface/icons/test.blp"), "listfile stays in Retail namespace");
    Check(!source.Handles("interface/icons/test.blp"), "no implicit stock overrides");
    Retail::HandItemAppearance appearance;
    Check(source.HandItem(123, appearance) && appearance.modelId == 916541 && appearance.textureId == 916543, "equipment uses Wrath display ID and Retail file IDs");
    Check(!source.HandItem(124, appearance), "unmapped equipment unchanged");
    Check(!source.SetHandItem(0, {1, 2}) && !source.SetHandItem(124, {1, 0}), "reject incomplete equipment override");
    Check(source.SetHandItem(123, {456, 789}) && source.HandItem(123, appearance) && appearance.modelId == 456, "session equipment override");
    Check(source.SetMainHandPreview({916541, 916543}) && source.HandItem(124, appearance, true) && appearance.modelId == 916541, "smoke test follows every equipped mainhand");
    Check(!source.HandItem(124, appearance, false), "mainhand smoke test does not replace offhand");
    source.ReleaseModel("retail/unknown.m2"); // A failed/non-Retail model has no retained owner.
    std::ofstream(temp / "bad.ini") << "mode=online\npath=cache\n";
    Retail::CascSource invalid;
    Check(!invalid.Configure((temp / "bad.ini").string().c_str(), error), "require pinned online build");
    std::ofstream(temp / "equipment.csv") << "123;916541;916543\n123;1;2\n";
    Retail::CascSource duplicateEquipment;
    Check(!duplicateEquipment.Configure((temp / "RetailCasc.ini").string().c_str(), error), "reject ambiguous equipment mappings");
    std::ofstream(temp / "equipment.csv") << "123;916541;0\n";
    Retail::CascSource invalidEquipment;
    Check(!invalidEquipment.Configure((temp / "RetailCasc.ini").string().c_str(), error), "reject missing equipment texture");
    std::ofstream(temp / "offline.ini") << "mode=files\npath=.\nbuild=0123456789abcdef0123456789abcdef\n";
    std::ofstream(temp / "42.bin", std::ios::binary).write(reinterpret_cast<const char*>(valid.data()), valid.size());
    Retail::CascSource offline;
    Check(offline.Configure((temp / "offline.ini").string().c_str(), error), "configure offline originals without network");
    Check(offline.Inspect("retail/42.m2", prepared, error) && prepared == valid, "offline source preserves original bytes");
    Check(!offline.Inspect("retail/43.m2", prepared, error), "offline source fails on missing dependency");
    {
        // A 12 x 8 DXT5 BLP (3 x 2 blocks, with a mip) reads as 16 x 8 (4 x 2 blocks), no mips, blocks top-left.
        std::vector<uint8_t> blp(1172 + 6 * 16 + 16, 0);
        std::memcpy(blp.data(), "BLP2\1\0\0\0", 8);
        blp[8] = 2; blp[9] = 8; blp[10] = 7; blp[11] = 1;
        const uint32_t head[] = { 12, 8, 1172, 1172 + 96 };
        std::memcpy(blp.data() + 12, head, 8); std::memcpy(blp.data() + 20, head + 2, 8);
        const uint32_t sizes[] = { 96, 16 };
        std::memcpy(blp.data() + 84, sizes, 8);
        for (int i = 0; i < 96; ++i) blp[1172 + i] = uint8_t(i + 1);
        std::ofstream(temp / "44.bin", std::ios::binary).write(reinterpret_cast<const char*>(blp.data()), blp.size());
        uint32_t w = 0, h = 0, size = 0;
        const bool read = offline.Read("retail/44.blp", prepared, error) && prepared.size() == 1172 + 128;
        if (read) { std::memcpy(&w, prepared.data() + 12, 4); std::memcpy(&h, prepared.data() + 16, 4); std::memcpy(&size, prepared.data() + 84, 4); }
        Check(read && w == 16 && h == 8 && size == 128 && prepared[11] == 0 && prepared[1172] == 1 && prepared[1172 + 47] == 48
            && prepared[1172 + 48] == 0 && prepared[1172 + 64] == 49 && prepared[1172 + 127] == 0,
            "non-power-of-two DXT texture is padded to a power of two");
    }
    {
        // A test reader stands in for SFile's mounted MPQ callback; the CASC/loose file remains absent.
        Retail::SetPackedAssetReader([](const char* name, std::vector<uint8_t>& out,
            std::string& error, uint32_t* size) -> int
        {
            const std::string key(name);
            if (key == "EsteriaRetail\\wow-0123456789abcdef0123456789abcdef\\2\\99.bin")
            {
                if (size)
                    *size = 3;
                else
                    out = {7, 8, 9};
                return 1;
            }
            if (key == "EsteriaRetail\\wow-0123456789abcdef0123456789abcdef\\2\\100.bin")
            {
                error = "Damaged MPQ";
                return -1;
            }
            return 0;
        });
        Check(offline.Inspect("retail/99.blp", prepared, error) && prepared == std::vector<uint8_t>({7, 8, 9}),
            "packed original bypasses missing loose/CASC storage");
        uint32_t size = 0;
        Check(offline.CachedOriginalSize("retail/99.blp", size) && size == 3
            && offline.Exists("retail/99.blp", error), "packed originals are local before preparation");
        Check(!offline.Inspect("retail/100.blp", prepared, error) && error == "Damaged MPQ",
            "damaged packed original fails without silently falling back to network");
        Check(offline.MatchesBuild("wow", "0123456789abcdef0123456789abcdef", 2)
            && !offline.MatchesBuild("wow", "1123456789abcdef0123456789abcdef", 2),
            "mount mapping cannot bind an original from another build");
        Retail::SetPackedAssetReader(nullptr);
    }
    {
        // A real source restart with an absent CASC installation: only the durable cache can satisfy it.
        const std::string build = "0123456789abcdef0123456789abcdef";
        const auto root = temp / "decoded" / ("wow-" + build) / "2" / "strict";
        const auto foreverRoot = temp / "decoded" / ("wow_classic_beta-" + build) / "2" / "strict";
        std::vector<std::filesystem::path> created;
        auto persist = [&](const std::filesystem::path& file, const std::vector<uint8_t>& data) {
            Check(Retail::WriteDecodedCache(file, data, error), error.c_str());
            created.push_back(file);
        };
        persist(root / "id-42.bin", valid);
        persist(root / "id-123.bin", skin);
        persist(foreverRoot / "id-42.bin", {1, 2, 3});
        const auto config = temp / "cached.ini";
        std::ofstream(config) << "mode=local\npath=absent-storage\ncache=decoded\nbuild=" << build << "\n";
        created.push_back(config);
        {
            Retail::CascSource first;
            Check(first.Configure(config.string().c_str(), error) && first.Inspect("retail/42.m2", prepared, error)
                && prepared == valid, "cache supplies immutable original without opening CASC");
        }
        Retail::CascSource restarted;
        Check(restarted.Configure(config.string().c_str(), error), error.c_str());
        uint32_t localBytes = 0;
        Check(restarted.CachedOriginalSize("retail/42.m2", localBytes) && localBytes == valid.size(),
            "durable originals report local before any model is prepared");
        Check(restarted.Exists("RETAIL\\42.m2", error) && restarted.Read("retail/42.m2", prepared, error),
            "restart loads and prepares cached model and skin with storage absent");
        Check(prepared != valid && Get(prepared, 0) == 0x3032444d,
            "prepared bytes are derived from the original on load");
        Check(restarted.Stats().reused == 2 && restarted.Stats().acquired == 0,
            "warm model plus dependency accurately report reuse with zero acquisition");
        std::vector<uint8_t> original;
        Check(Retail::ReadDecodedCache(root / "id-42.bin", original) && original == valid,
            "preparation never changes cached originals");
        restarted.ReleaseModel("retail/42.m2");
        std::ofstream(config) << "mode=local\nproduct=wow_classic_beta\npath=absent-storage\ncache=decoded\nbuild=" << build << "\n";
        Retail::CascSource forever(Retail::SourceKind::Forever);
        Check(forever.Configure(config.string().c_str(), error), error.c_str());
        Check(forever.Handles("FOREVER\\42.m2") && !forever.Handles("retail/42.m2")
            && !restarted.Handles("forever/42.m2"), "source namespaces cannot cross-route colliding FileDataIDs");
        Check(forever.Inspect("forever/42.m2", original, error) && original == std::vector<uint8_t>({1, 2, 3}),
            "Forever FileDataID collision uses its own product/build cache");
        {
            Retail::SourceScope scope(Retail::SourceKind::Forever);
            Check(Retail::FileName(42, ".m2") == "forever/42.m2", "scoped dependency names retain Forever provenance");
        }
        Check(Retail::FileName(42, ".m2") == "retail/42.m2", "source scope restores the caller after nested work");
        // Lenient table data must not satisfy a strict model request.
        persist(root.parent_path() / "tables" / "id-77.bin", {0, 0, 0});
        Check(!restarted.Inspect("retail/77.m2", original, error), "zero-filled table cache cannot satisfy a strict read");
        std::ofstream(config) << "mode=local\npath=absent-storage\ncache=decoded\nbuild=" << build << "\nlocale=4\n";
        Retail::CascSource otherLocale;
        Check(otherLocale.Configure(config.string().c_str(), error) && !otherLocale.Inspect("retail/42.m2", original, error),
            "cache is isolated by locale");
        std::ofstream(config) << "mode=local\npath=absent-storage\ncache=decoded\nbuild=1123456789abcdef0123456789abcdef\n";
        Retail::CascSource otherBuild;
        Check(otherBuild.Configure(config.string().c_str(), error) && !otherBuild.Inspect("retail/42.m2", original, error),
            "cache is isolated by pinned build");
        const auto damaged = root / "id-42.bin";
        {
            std::fstream output(damaged, std::ios::binary | std::ios::in | std::ios::out);
            output.seekp(16); output.put('x');
        }
        Check(!Retail::ReadDecodedCache(damaged, original) && original.empty(), "reject corrupt cached payload by checksum");
        Check(Retail::WriteDecodedCache(damaged, valid, error), error.c_str());
        std::filesystem::resize_file(damaged, 17);
        Check(!Retail::ReadDecodedCache(damaged, original), "reject interrupted or truncated cached payload");
        Check(Retail::WriteDecodedCache(damaged, valid, error)
            && Retail::ReadDecodedCache(damaged, original) && original == valid, "atomically replace corrupt entries");
        persist(root / "empty.bin", {});
        Check(Retail::ReadDecodedCache(root / "empty.bin", original) && original.empty(), "cache complete empty assets without repeated acquisition");
        // Only remove the files and empty directories created by this test.
        std::set<std::filesystem::path> directories;
        for (const auto& file : created) {
            std::filesystem::remove(file);
            for (auto dir = file.parent_path(); dir != temp && dir != dir.parent_path(); dir = dir.parent_path()) directories.insert(dir);
        }
        for (auto dir = directories.rbegin(); dir != directories.rend(); ++dir) std::filesystem::remove(*dir);
    }
    {
        std::vector<uint8_t> emptyTable(212);
        Put(emptyTable, 0, 0x35434457); Put(emptyTable, 4, 5);
        Put(emptyTable, 140, 2); // physical fields, no records/sections/column storage
        Put(emptyTable, 176, 2);
        Retail::Db2::wdc5::Table table;
        std::vector<Retail::Db2::wdc5::FieldShape> fields(2);
        Check(table.Load(emptyTable.data(), emptyTable.size(), fields, 0, &error) && table.Rows().empty(),
            "Forever empty WDC5 tables have no column storage");
        emptyTable.pop_back();
        Check(!table.Load(emptyTable.data(), emptyTable.size(), fields, 0, &error), "reject truncated empty WDC5 descriptors");
        emptyTable.resize(212); Put(emptyTable, 136, 1);
        Check(!table.Load(emptyTable.data(), emptyTable.size(), fields, 0, &error), "empty-table exception cannot accept nonempty data");
    }
    // Remove only the individual files created here; no recursive cleanup of computed paths.
    for (const char* file : {"aliases.csv", "listfile.csv", "equipment.csv", "RetailCasc.ini", "bad.ini", "offline.ini", "42.bin"}) std::filesystem::remove(temp / file);
    std::filesystem::remove(temp);
    if (argc == 3 && std::string(argv[1]) == "--slime-source") {
        const std::filesystem::path root(argv[2]);
        const auto read = [](const std::filesystem::path& path) {
            std::ifstream stream(path, std::ios::binary);
            Check(stream.good(), "open SlimeSkeleton source fixture");
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)), {});
        };
        const auto original = read(root / "correct-source/3490792.m2");
        Retail::ModelFiles graph;
        std::vector<uint8_t> before, player;
        Check(Retail::PrepareModel(original, before, graph, error), error.c_str());
        player = before;
        Check(Retail::SlimeGiant::PrepareModel(player, Retail::SlimeGiant::Body, error), error.c_str());
        for (const size_t field : {size_t(0x1c), size_t(0x2c), size_t(0x34), size_t(0x3c), size_t(0xf0), size_t(0xf8)})
            Check(Get(before, field) == Get(player, field) && Get(before, field + 4) == Get(player, field + 4),
                "retain original animations, bones, vertices and sockets");
        Check(std::equal(before.begin() + 0x130, before.end(), player.begin() + 0x130),
            "retain all original source payload bytes");
        for (size_t i = 0; i < std::min<size_t>(graph.skins.size(), Get(player, 0x44)); ++i) {
            const uint32_t skinId = graph.skins[i];
            auto skin = read(root / "correct-source" / (std::to_string(skinId) + ".skin"));
            Check(Retail::SlimeGiant::PrepareSkin(skin, error), error.c_str());
            Check(Retail::PrepareSkin(skin, player, error, true), error.c_str());
        }
        const auto clothing = read(root / "stage/assets/custom/slimegiant/3490792-clothing.m2");
        auto skin = read(root / "stage/assets/custom/slimegiant/3490792-clothing00.skin");
        Check(Retail::PrepareSkin(skin, clothing, error), error.c_str());
        Check(Get(clothing, 0x2c) == Get(player, 0x2c), "collection uses the full source rig");
        for (uint32_t i = 0; i < Get(clothing, 0x2c); ++i) {
            const size_t a = Get(clothing, 0x30) + size_t(i) * 88, b = Get(player, 0x30) + size_t(i) * 88;
            Check(!std::memcmp(clothing.data() + a, player.data() + b, 16) &&
                !std::memcmp(clothing.data() + a + 76, player.data() + b + 76, 12),
                "collection bind identity permits native parent-pose skinning");
        }
        const auto fetch = [](uint32_t id, std::vector<uint8_t>& bytes, void* context) {
            const auto& fixture = *static_cast<const std::filesystem::path*>(context);
            const auto local = Retail::SlimeGiant::TexturePath(id);
            const auto path = local.empty() ? fixture / "correct-source" / (std::to_string(id) + ".blp")
                : fixture / "color-expansion-30/assets" / local;
            std::ifstream input(path, std::ios::binary);
            if (!input.good()) return false;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            return true;
        };
        for (uint32_t color = 0; color < Retail::SlimeGiant::Colors; ++color)
        for (uint32_t drip : {Retail::SlimeGiant::Off, Retail::SlimeGiant::On}) {
            const uint32_t choices[] = {Retail::SlimeGiant::ColorId(color), drip};
            Retail::Db2::appearance::Recipe recipe;
            Check(Retail::SlimeGiant::Build(0, choices, 2, recipe), "build fixture appearance");
            std::vector<Retail::ComposedTexture> textures;
            auto context = root;
            Check(Retail::ComposeTextures(recipe, fetch, &context, textures, error), error.c_str());
            Check(textures.size() == 2, "both skin and animated slime compose");
            for (size_t part = 0; part < textures.size(); ++part) {
                std::vector<uint8_t> bytes;
                Retail::Image expected;
                Check(fetch(recipe.layers[part].fileDataId, bytes, &context)
                    && Retail::DecodeBlp(bytes, expected, error), "decode original palette dimensions");
                Check(textures[part].image.width == expected.width && textures[part].image.height == expected.height,
                    "all seventy appearances retain each source texture's native dimensions");
            }
        }
        std::puts("All 35 Slime Giant colors and both drip states composed from their real texture pairs.");
        std::puts("SlimeSkeleton original payload, engine skin profiles and clothing rig validated.");
    } else if (argc == 3 && std::string(argv[1]) == "--shader-bls") {
        std::ifstream input(argv[2], std::ios::binary);
        std::vector<uint8_t> bls((std::istreambuf_iterator<char>(input)), {});
        Check(bls.size() >= 12 && Get(bls, 0) == 0x47585348 && Get(bls, 8) == 16, "stock shader fixture header");
        size_t p = 12;
        for (unsigned i = 0; i < 16; ++i) {
            Check(p + 16 <= bls.size(), "shader record bounds");
            const size_t count = Get(bls, p + 12); p += 16;
            Check(count <= bls.size() - p, "shader bytecode bounds");
            const std::vector<uint8_t> original(bls.begin() + p, bls.begin() + p + count);
            Check(Retail::AddGlowLayer(original, prepared, error), error.c_str());
            Check(prepared.size() > original.size() && Get(prepared, 0) == 0xffff0300, "third texture layer added to every permutation");
            auto corrupt = original; corrupt.pop_back();
            Check(!Retail::AddGlowLayer(corrupt, prepared, error), "reject malformed shader bytecode");
            p = (p + count + 3) & ~size_t(3);
        }
        Check(p == bls.size(), "complete shader fixture consumed");
    } else Check(argc == 1, "unknown test arguments");
    std::puts("Retail asset validation and routing passed.");
    return 0;
}
