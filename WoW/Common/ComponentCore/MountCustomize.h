#ifndef WOW_COMMON_COMPONENTCORE_MOUNTCUSTOMIZE_H
#define WOW_COMMON_COMPONENTCORE_MOUNTCUSTOMIZE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CM2Model;

// Retail-style mount customization: Dragonflight drakes and other ChrModel mounts (composed by the retail appearance
// resolver) and client-defined option sets such as the D.R.I.V.E.'s paint (texture swaps), from
// EsteriaMounts\custom.csv (Tools/RetailAssets/mount_custom.py). Players' choices come from SMSG_ESTERIA_MOUNT_LOOKS.
namespace MountCustomize
{
    struct Choice { uint32_t id; std::string name; uint32_t swatch; }; // swatch: ARGB, 0 when the choice has none
    struct Option { uint32_t id; std::string name; std::vector<Choice> choices; };

    // The options a mount display offers, in display order; false when it is not customizable.
    bool Options(uint32_t display, std::vector<Option>& out);
    // The display's own look (retail's chosen variant), one choice per option.
    std::vector<uint32_t> Defaults(uint32_t display);

    // Looks by player guid, replaced wholesale by each SMSG_ESTERIA_MOUNT_LOOKS.
    void SetLooks(uint64_t guid, std::map<uint32_t, std::vector<uint32_t>> looks);
    const std::vector<uint32_t>* Look(uint64_t guid, uint32_t display);
    void ClearLooks();

    // Draws `model` (a mount of `display`) with `choices`; no-op for a display that is not customizable.
    void Apply(CM2Model* model, uint32_t display, const std::vector<uint32_t>& choices);
    // The owner's saved look, if any (else the model keeps the display's own look).
    void ApplyOwner(CM2Model* model, uint32_t display, uint64_t ownerGuid);
}

#endif
