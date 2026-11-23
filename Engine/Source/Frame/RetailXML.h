#ifndef ENGINE_SOURCE_FRAME_RETAILXML_H
#define ENGINE_SOURCE_FRAME_RETAILXML_H

// Not in the original: the parts of retail's FrameXML dialect that Blizzard's modern UI files need, so they can be
// loaded as shipped. Stock 3.3.5a XML never uses these attributes, so it loads exactly as before.
//   mixin / secureMixin, <KeyValues>, parentArray       RetailXML_LoadObject
//   <Anchor relativeKey="$parent.Name">                  RetailXML_LoadRelativeAnchors (resolved late when needed)
//   <OnLoad method="OnLoad" inherit="prepend|append"/>   RetailXML_ScriptRef
//   <Texture atlas="name" useAtlasSize="true"/>, file="<FileDataID>", Texture:SetAtlas/GetAtlas, GetAtlasInfo

#include <cstddef>
#include <cstdint>
#include "Engine/Source/lua-5.1/src/lua.h"

class CLayoutFrame;
class CScriptRegion;
class CSimpleTexture;
class CStatus;
class FrameScript_Object;
class XMLNode;

struct RetailAtlasInfo {
    uint32_t fileDataId;
    float width;
    float height;
    float left;
    float right;
    float top;
    float bottom;
    uint32_t flags; // retail CommittedFlags: 4 tiles horizontally, 2 tiles vertically
};

const RetailAtlasInfo* RetailAtlas_Find(const char* name);
bool RetailAtlas_TilesH(const RetailAtlasInfo* info);
bool RetailAtlas_TilesV(const RetailAtlasInfo* info);
bool RetailXML_SetAtlas(CSimpleTexture* texture, const char* name, bool useAtlasSize, bool resetTexCoords = false);
const char* RetailXML_GetAtlas(CSimpleTexture* texture);
void RetailXML_ForgetTexture(CSimpleTexture* texture);
const char* RetailXML_TexturePath(const char* file, char* buffer, size_t bufferSize);
// A texture path the client's archives lack but retail has (RetailTextures.txt): retail/<FileDataID>.blp, else file.
const char* RetailXML_TextureFallback(const char* file, char* buffer, size_t bufferSize);

void RetailXML_LoadObject(FrameScript_Object* object, FrameScript_Object* parent, XMLNode* node, CStatus* status);
void RetailXML_LoadRelativeAnchors(CScriptRegion* region, XMLNode* node, CStatus* status);
void RetailXML_ResolvePendingAnchors(bool final, CStatus* status);
int32_t RetailXML_ScriptRef(lua_State* L, XMLNode* child, int32_t inheritedRef, int32_t ownRef);

// <Frame name="EventFrame" intrinsic="true"> makes <EventFrame> (and CreateFrame("EventFrame")) a Frame that loads
// that element first.
void RetailXML_RegisterIntrinsic(const char* name, const char* baseTag);
const char* RetailXML_IntrinsicBase(const char* name);

void RetailXML_RegisterScriptFunctions();
int32_t RetailXML_Script_SetAtlas(lua_State* L);
int32_t RetailXML_Script_GetAtlas(lua_State* L);

#endif
