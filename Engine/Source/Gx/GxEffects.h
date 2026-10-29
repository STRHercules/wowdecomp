#ifndef ENGINE_SOURCE_GX_GXEFFECTS_H
#define ENGINE_SOURCE_GX_GXEFFECTS_H

#include <cstddef>
#include <cstdint>

class CGxTex;

// Not in the original 3.3.5a binary. Full-screen post passes over the world for effects the engine's own Gx
// pipeline cannot express (float targets, many samplers, a constant file, the world's depth as a texture):
// Lumenveil's fog, lights and water. A backend that has them returns a device from GxEffects::Get(); the others
// return null and the effects stay off. Vulkan is the only one so far.
//
// The passes run between the world's draws, on the render thread, while the world's own target is bound: a pass
// with no target draws onto that target, CopyColor and CopyDepth copy it.
//
// A program is a SPIR-V fragment module drawn over a full-screen triangle, with this interface:
//
//   layout(location = 0) in vec2 uv;                          // 0,0 the top left of the viewport, 1,1 the bottom right
//   layout(set = 0, binding = 0..15) uniform sampler2D tN;    // or sampler3D: the pass's textures by slot
//   layout(set = 0, binding = 16) uniform Constants { vec4 c[kMaxConstantRows]; };
//   layout(location = 0) out vec4 colour;
//
// gl_FragCoord is in target pixels, its centre at +0.5 (D3D9's VPOS is at the integer; subtract 0.5).
//
// A pass may instead draw points: a vertex program run `points` times (its index in gl_VertexIndex, no vertex
// buffer) that writes gl_Position and gl_PointSize, with the same textures and constants as the fragment program.
//
// A Compute is a SPIR-V compute module run over `groups` work groups, with the same textures and constants plus:
//
//   layout(set = 0, binding = 17..24, rgba16f) uniform image2D iN;   // the compute's storage textures by slot
//   layout(push_constant) uniform Push { ... };                      // up to kMaxPushBytes
//
// Each storage texture is read and written in place (imageLoad / imageStore); the compute must not use an empty
// image slot. Every write of an earlier pass or compute is visible to it, and its writes to the passes after it.
namespace GxEffects {

constexpr uint32_t kMaxTextures = 16;
constexpr uint32_t kMaxConstantRows = 256;
constexpr uint32_t kMaxImages = 8;
constexpr uint32_t kMaxPushBytes = 128;

enum class Format : uint32_t {
    BGRA8,      // D3DFMT_A8R8G8B8
    RGBA16F,    // D3DFMT_A16B16G16R16F
    RGBA32F,    // D3DFMT_A32B32G32R32F
    R32F,       // D3DFMT_R32F
    RG16F,      // D3DFMT_G16R16F
    RG32F,      // D3DFMT_G32R32F
    R8,         // D3DFMT_L8 (sampled as .r)
    Depth,      // the world's depth buffer format, for CopyDepth; sampled as .r in 0..1
};

enum class Blend : uint32_t {
    Opaque,     // the result replaces the target
    Alpha,      // src * a + dst * (1 - a)
    Premultiplied, // src + dst * (1 - a)
    Add,        // src + dst
    Multiply,   // src * dst
    Max,        // max(src, dst)
};

struct Texture;
struct Program;
struct Readback;   // a read of a target in flight

// An empty slot reads (0, 0, 0, 1), as D3D9 reads a stage with no texture.
struct Binding {
    Texture* texture = nullptr;
    // Instead of texture: one of the engine's own 2D textures (a render target such as a sun shadow map), sampled as
    // the engine left it. An engine texture the backend has no image for, or a cube map, reads as an empty slot.
    CGxTex* engine = nullptr;
    bool linear = false;
    bool wrap = false;
    bool mips = false;      // filter between mip levels (linear); off, level 0 only
};

struct Pass {
    Texture* target = nullptr;      // null: the world's current target
    // The viewport in target pixels; a zero width draws the whole target.
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    const Program* program = nullptr;
    const Program* vertexProgram = nullptr;   // with points: a point list; without: the full-screen triangle
    uint32_t points = 0;
    Binding textures[kMaxTextures];
    const float (*constants)[4] = nullptr;
    uint32_t constantRows = 0;
    Blend blend = Blend::Opaque;
    uint32_t writeMask = 0xF;       // bit 0 red .. bit 3 alpha
    bool clear = false;             // clear the whole target to clearColour first
    float clearColour[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct Compute {
    const Program* program = nullptr;
    uint32_t groupsX = 1;
    uint32_t groupsY = 1;
    uint32_t groupsZ = 1;
    Binding textures[kMaxTextures];
    Texture* images[kMaxImages] = {};   // from CreateStorage; none may also be in textures
    const float (*constants)[4] = nullptr;
    uint32_t constantRows = 0;
    const void* push = nullptr;
    uint32_t pushBytes = 0;
};

class Device {
    public:
    virtual ~Device() = default;

    // A texture of one level. A target can be drawn into and copied into; the others are filled by Upload.
    // Its contents start as zero. Null when it cannot be made (logged).
    virtual Texture* CreateTexture(uint32_t width, uint32_t height, Format format, bool target) = 0;
    // A target a Compute can also write as a storage image. Null when the format cannot be (see CanStore).
    virtual Texture* CreateStorage(uint32_t width, uint32_t height, Format format) = 0;
    // Textures filled once, with `levels` mip levels: levelTexels[i] is level i, tightly packed (a volume's slice
    // after slice), each level half the one before.
    virtual Texture* CreateFilled(uint32_t width, uint32_t height, uint32_t levels, Format format, const void* const* levelTexels) = 0;
    virtual Texture* CreateVolume(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, Format format, const void* const* levelTexels) = 0;
    // Destroys it once the GPU is done with it. Null is ignored.
    virtual void Destroy(Texture* texture) = 0;
    virtual void Size(const Texture* texture, uint32_t& width, uint32_t& height) = 0;
    // Replaces the whole level from tightly packed texels.
    virtual bool Upload(Texture* texture, const void* texels) = 0;

    // A fragment, vertex or compute module; which one is up to the pass or compute that uses it.
    virtual Program* CreateProgram(const uint32_t* spirv, size_t bytes, const char* name) = 0;

    // Whether a target of this format can be drawn into with a blend other than Opaque.
    virtual bool CanBlend(Format format) = 0;
    // Whether CreateStorage can make a texture of this format.
    virtual bool CanStore(Format format) = 0;

    virtual bool Draw(const Pass& pass) = 0;
    virtual bool Dispatch(const Compute& compute) = 0;

    // The world's current target: its size, and copies of its colour and depth. CopyColor copies a rectangle of
    // it (a zero width: all of it) onto the whole destination, scaling with a linear or point filter (D3D9's
    // StretchRect); CopyDepth needs a Format::Depth texture of exactly the target's size.
    virtual bool TargetSize(uint32_t& width, uint32_t& height) = 0;
    virtual bool CopyColor(Texture* destination, int32_t x = 0, int32_t y = 0, uint32_t width = 0, uint32_t height = 0, bool linear = true) = 0;
    virtual bool CopyDepth(Texture* destination) = 0;

    // Blocks until the frame so far has run, then reads a rectangle of a BGRA8 or float target, as its texels
    // lie. For tests and rare probes only.
    virtual bool Read(Texture* texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* out) = 0;

    // The same without the wait, for a target of any format: BeginRead records the copy and returns a handle (null
    // when it cannot); PollRead gives the texels (width * height * the format's bytes) and frees the handle once the
    // GPU has run that frame, a frame or two later, and is false until then; CancelRead drops a handle unread.
    virtual Readback* BeginRead(Texture* texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual bool PollRead(Readback* read, void* out) = 0;
    virtual void CancelRead(Readback* read) = 0;
};

// While on, the engine's draws write depth whatever their own state says (Lumenveil turns it on through the liquid
// pass, which the client draws without depth writes, so the water surface is in the depth the passes read).
void ForceDepthWrite(bool on);
bool DepthWriteForced();

// The active backend's device, or null when it has none (or it is not up yet).
Device* Get();

}

#endif
