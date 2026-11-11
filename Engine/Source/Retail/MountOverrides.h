#ifndef ENGINE_SOURCE_RETAIL_MOUNTOVERRIDES_H
#define ENGINE_SOURCE_RETAIL_MOUNTOVERRIDES_H

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Retail
{
struct MountOverride
{
    std::string product, build, model;
    uint32_t locale = 0;
    std::array<std::string, 3> textures;
    float scale = 1.0f;
};

inline bool ParseMountOverrides(const std::string& input, std::map<uint32_t, MountOverride>& output)
{
    std::map<uint32_t, MountOverride> parsed;
    std::istringstream lines(input);
    std::string line;
    const auto number = [](const std::string& value, uint32_t& result, int base = 10)
    {
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), result, base);
        return !value.empty() && conversion.ec == std::errc() && conversion.ptr == value.data() + value.size();
    };
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::array<std::string, 9> fields;
        std::istringstream columns(line);
        for (auto& field : fields)
            if (!std::getline(columns, field, ';'))
                return false;
        if (columns.peek() != std::char_traits<char>::eof())
            return false;
        MountOverride entry;
        uint32_t display = 0, model = 0;
        if (!number(fields[0], display) || !display || fields[1] != "wow" || fields[2].size() != 32
            || fields[2].find_first_not_of("0123456789abcdef") != std::string::npos
            || !number(fields[3], entry.locale, 16) || !entry.locale || !number(fields[4], model) || !model)
            return false;
        entry.product = fields[1];
        entry.build = fields[2];
        entry.model = "retail\\" + std::to_string(model) + ".m2";
        for (size_t i = 0; i < entry.textures.size(); ++i)
        {
            uint32_t texture = 0;
            if (!number(fields[5 + i], texture))
                return false;
            if (texture)
                entry.textures[i] = std::to_string(texture) + ".blp";
        }
        const auto scale = std::from_chars(fields[8].data(), fields[8].data() + fields[8].size(), entry.scale);
        if (scale.ec != std::errc() || scale.ptr != fields[8].data() + fields[8].size()
            || !std::isfinite(entry.scale) || entry.scale <= 0 || entry.scale > 100
            || !parsed.emplace(display, std::move(entry)).second)
            return false;
    }
    output.swap(parsed);
    return true;
}

// A customizable retail mount's look: M2 texture type -> FileDataID, and shown geosets (group, value).
struct MountSkin
{
    std::vector<std::pair<uint32_t, uint32_t>> textures, geosets;
    int32_t riderAnim = -1;       // retail rider animation; may be newer than the client's list
    uint32_t riderFallback = 91;  // client animation used when the rider model lacks riderAnim
};

// Rows: display;t;textureType;fileDataId  or  display;g;geosetGroup;geosetValue (group 0 keeps mesh 0)
//   or  display;r;riderAnim;fallbackAnim
inline bool ParseMountSkins(const std::string& input, std::map<uint32_t, MountSkin>& output)
{
    std::map<uint32_t, MountSkin> parsed;
    std::istringstream lines(input);
    std::string line;
    const auto number = [](const std::string& value, uint32_t& result)
    {
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), result);
        return !value.empty() && conversion.ec == std::errc() && conversion.ptr == value.data() + value.size();
    };
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::array<std::string, 4> fields;
        std::istringstream columns(line);
        for (auto& field : fields)
            if (!std::getline(columns, field, ';'))
                return false;
        uint32_t display = 0, first = 0, second = 0;
        if (columns.peek() != std::char_traits<char>::eof() || !number(fields[0], display) || !display
            || !number(fields[2], first) || !number(fields[3], second))
            return false;
        if (fields[1] == "t" && first < 32 && second)
            parsed[display].textures.emplace_back(first, second);
        else if (fields[1] == "g" && first < 100 && second < 100)
            parsed[display].geosets.emplace_back(first, second);
        else if (fields[1] == "r" && first < 0x10000 && second < 0x1FA && parsed[display].riderAnim < 0)
        {
            parsed[display].riderAnim = static_cast<int32_t>(first);
            parsed[display].riderFallback = second;
        }
        else
            return false;
    }
    output.swap(parsed);
    return true;
}

// A retail display's state visual kit, as far as the client can show it: effect models attached to the mount
// (SpellVisualKitModelAttach) and an emissive tint standing in for the kit's gradient effect.
struct MountKitModel
{
    int32_t attachment = -1;  // -1: the model root; -2: the middle of the model's bounds (retail positioners)
    uint32_t model = 0;       // FileDataID of the effect M2
    float scale = 1.0f;
    std::array<float, 3> offset{};
};

struct MountKit
{
    std::vector<MountKitModel> models;
    bool tinted = false;
    std::array<float, 3> emissive{};
    float alpha = 1.0f;  // the mount's own batches draw at this alpha
};

// Rows: display;m;attachment;modelFileDataId;scale;offsetX;offsetY;offsetZ  or  display;e;r;g;b[;alpha]
inline bool ParseMountKits(const std::string& input, std::map<uint32_t, MountKit>& output)
{
    std::map<uint32_t, MountKit> parsed;
    std::istringstream lines(input);
    std::string line;
    const auto real = [](const std::string& value, float& result)
    {
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), result);
        return !value.empty() && conversion.ec == std::errc() && conversion.ptr == value.data() + value.size()
            && std::isfinite(result) && std::fabs(result) <= 1000.0f;
    };
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> fields;
        std::istringstream columns(line);
        for (std::string field; std::getline(columns, field, ';');)
            fields.push_back(field);
        uint32_t display = 0;
        if (fields.size() < 2 || std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), display).ec != std::errc()
            || !display)
            return false;
        if (fields[1] == "m" && fields.size() == 8)
        {
            MountKitModel entry;
            const auto attachment = std::from_chars(fields[2].data(), fields[2].data() + fields[2].size(), entry.attachment);
            const auto model = std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), entry.model);
            if (attachment.ec != std::errc() || entry.attachment < -2 || entry.attachment > 255 || model.ec != std::errc()
                || !entry.model || !real(fields[4], entry.scale) || entry.scale <= 0
                || !real(fields[5], entry.offset[0]) || !real(fields[6], entry.offset[1]) || !real(fields[7], entry.offset[2]))
                return false;
            parsed[display].models.push_back(entry);
        }
        else if (fields[1] == "e" && (fields.size() == 5 || fields.size() == 6))
        {
            MountKit& kit = parsed[display];
            for (size_t i = 0; i < 3; ++i)
                if (!real(fields[2 + i], kit.emissive[i]) || kit.emissive[i] < 0 || kit.emissive[i] > 1)
                    return false;
            if (fields.size() == 6 && (!real(fields[5], kit.alpha) || kit.alpha <= 0 || kit.alpha > 1))
                return false;
            kit.tinted = true;
        }
        else
            return false;
    }
    output.swap(parsed);
    return true;
}
}
#endif
