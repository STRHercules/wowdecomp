// GxEffects over Vulkan: the post passes Lumenveil draws between the world's draws (see
// Engine/Source/Gx/GxEffects.h for the contract). Not in the original binary.
//
// Everything here records into the frame's main command buffer OUTSIDE the engine's render pass
// (frame::Outside(); the engine's next draw resumes its pass with the contents kept), in render
// passes of its own: one colour attachment, no depth, LOADed from or CLEARed to its resting
// SHADER_READ_ONLY_OPTIMAL layout -- the layout every colour target of the backend rests in, so the
// world's scene and render-target textures can be drawn onto as they are.
//
// Per frame slot: a descriptor pool and a ring of uniform memory, reset the first time the slot's
// frame is touched. Pipelines are cached by (program, colour format, blend, write mask) for the
// device's life. Objects that go away are retired with the frame that last named them.
//
// A compute records outside any render pass too. Its storage textures rest in SHADER_READ_ONLY_OPTIMAL
// like every other target and go to GENERAL for the dispatch only; the barriers around it order it
// after everything recorded before and before everything after.
#include "Engine/Source/Gx/GxEffects.h"

#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Effects.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Log.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Memory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"

#include "Engine/Source/Gx/CGxDeviceVulkan/Shaders/PresentSpirv.inc"

#include <cstring>
#include <vector>

using namespace gxvk::log;
using namespace gxvk::vk;

struct GxEffects::Texture
{
    GxEffects::Format format = GxEffects::Format::BGRA8;
    VkFormat          vk     = VK_FORMAT_UNDEFINED;
    VkExtent3D        extent = { 0, 0, 1 };
    uint32_t          levels = 1;   ///< a raw image's mip levels
    bool              target = false;
    bool              storage = false;   ///< a target with STORAGE usage, for Compute::images
    /// A colour texture: the texture module's image. A volume or a depth texture: made here.
    texture::Image    image;
    VkImage           raw     = VK_NULL_HANDLE;
    VkImageView       rawView = VK_NULL_HANDLE;
    memory::Block     rawBlock;
    VkFramebuffer     fb      = VK_NULL_HANDLE;
    uint64_t          lastUsed = 0;

    VkImageView View() const { return raw != VK_NULL_HANDLE ? rawView : image.view; }
};

struct GxEffects::Readback
{
    frame::AsyncRead read;
};

struct GxEffects::Program
{
    VkShaderModule module = VK_NULL_HANDLE;
    mutable VkPipeline compute = VK_NULL_HANDLE;   ///< made the first time it is dispatched
    char           name[48] = {};
};

namespace
{
    constexpr uint32_t kUniformBinding   = GxEffects::kMaxTextures;
    constexpr uint32_t kImageBinding     = kUniformBinding + 1;
    constexpr uint32_t kBindings         = kImageBinding + GxEffects::kMaxImages;
    constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    constexpr uint32_t kSetsPerFrame     = 512;
    constexpr VkDeviceSize kUniformRing  = 1u << 20;
    constexpr uint32_t kMaxWorldTargets  = 8;
    /// Frames a framebuffer over one of the engine's targets is kept without being drawn on.
    constexpr uint64_t kWorldTargetFrames = 120;

    struct PassPair
    {
        VkFormat     format = VK_FORMAT_UNDEFINED;
        VkRenderPass load   = VK_NULL_HANDLE;
        VkRenderPass clear  = VK_NULL_HANDLE;
    };

    struct PipelineEntry
    {
        const GxEffects::Program* program = nullptr;
        const GxEffects::Program* vertex  = nullptr;   ///< null: the full-screen triangle
        VkFormat           format    = VK_FORMAT_UNDEFINED;
        GxEffects::Blend   blend     = GxEffects::Blend::Opaque;
        uint32_t           writeMask = 0;
        VkPipeline         pipeline  = VK_NULL_HANDLE;
    };

    /// A framebuffer over one of the engine's own colour targets: the scene (id 0, for one
    /// swapchain generation) or a render-target texture (by the texture module's id, never reused).
    struct WorldTarget
    {
        VkImage       image    = VK_NULL_HANDLE;
        uint32_t      id       = 0;
        uint32_t      swapGen  = 0;
        VkImageView   view     = VK_NULL_HANDLE;
        VkFramebuffer fb       = VK_NULL_HANDLE;
        uint64_t      lastUsed = 0;
    };

    struct SlotState
    {
        VkDescriptorPool pool     = VK_NULL_HANDLE;
        memory::Buffer   uniforms;
        VkDeviceSize     cursor   = 0;
        uint64_t         serial   = 0;
    };

    struct State
    {
        bool                  ready  = false;
        bool                  failed = false;
        VkDevice              device = VK_NULL_HANDLE;
        const loader::Device* dapi   = nullptr;
        VkDeviceSize          uniformAlign = 256;
        VkShaderModule        vert   = VK_NULL_HANDLE;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout      layout = VK_NULL_HANDLE;
        VkSampler             samplers[8] = {};   ///< bit 0 linear, bit 1 wrap, bit 2 mips
        SlotState             slots[frame::kFramesInFlight];
        std::vector<PassPair>      passes;
        std::vector<PipelineEntry> pipelines;
        std::vector<GxEffects::Program*> programs;
        std::vector<GxEffects::Texture*> textures;   ///< alive: Shutdown() frees what the effects still hold
        WorldTarget           world[kMaxWorldTargets];
        GxEffects::Texture*   blank = nullptr;    ///< what an empty slot binds
        bool                  warnedSets = false;
        bool                  warnedUniforms = false;
        bool                  warnedDepth = false;
        bool                  compute = false;   ///< the graphics queue runs computes too
    };

    State s;

    bool Check(VkResult r, const char* what)
    {
        if (r == VK_SUCCESS) return true;
        Write(Error, "vulkan: effects: %s failed: %s", what, ResultName(r));
        return false;
    }

    VkFormat VkFormatOf(GxEffects::Format f)
    {
        switch (f)
        {
        case GxEffects::Format::BGRA8:   return VK_FORMAT_B8G8R8A8_UNORM;
        case GxEffects::Format::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GxEffects::Format::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case GxEffects::Format::R32F:    return VK_FORMAT_R32_SFLOAT;
        case GxEffects::Format::RG16F:   return VK_FORMAT_R16G16_SFLOAT;
        case GxEffects::Format::RG32F:   return VK_FORMAT_R32G32_SFLOAT;
        case GxEffects::Format::R8:      return VK_FORMAT_R8_UNORM;
        case GxEffects::Format::Depth:   return frame::DepthFormat();
        }
        return VK_FORMAT_UNDEFINED;
    }

    uint32_t TexelBytes(GxEffects::Format f)
    {
        switch (f)
        {
        case GxEffects::Format::BGRA8:   return 4;
        case GxEffects::Format::RGBA16F: return 8;
        case GxEffects::Format::RGBA32F: return 16;
        case GxEffects::Format::R32F:    return 4;
        case GxEffects::Format::RG16F:   return 4;
        case GxEffects::Format::RG32F:   return 8;
        case GxEffects::Format::R8:      return 1;
        case GxEffects::Format::Depth:   return 4;
        }
        return 0;
    }

    uint32_t Mip(uint32_t size, uint32_t level)
    {
        return (size >> level) ? (size >> level) : 1u;
    }

    void Barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                 VkAccessFlags srcAccess, VkAccessFlags dstAccess, uint32_t levels = 1)
    {
        VkImageMemoryBarrier b = {};
        b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask               = srcAccess;
        b.dstAccessMask               = dstAccess;
        b.oldLayout                   = from;
        b.newLayout                   = to;
        b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        b.image                       = image;
        b.subresourceRange.aspectMask = aspect;
        b.subresourceRange.levelCount = levels;
        b.subresourceRange.layerCount = 1;
        s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    bool Init()
    {
        if (s.ready) return true;
        if (s.failed) return false;
        const context::Handles* h = context::Get();
        if (!h || frame::DepthFormat() == VK_FORMAT_UNDEFINED) return false;
        s.failed = true;   // until every object below exists
        s.device = h->device;
        s.dapi   = h->dapi;

        VkPhysicalDeviceProperties props = {};
        h->iapi->GetPhysicalDeviceProperties(h->physical, &props);
        s.uniformAlign = props.limits.minUniformBufferOffsetAlignment ? props.limits.minUniformBufferOffsetAlignment : 256;

        VkShaderModuleCreateInfo vi = {};
        vi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vi.codeSize = sizeof kPresentVert;
        vi.pCode    = kPresentVert;
        if (!Check(s.dapi->CreateShaderModule(s.device, &vi, nullptr, &s.vert), "vkCreateShaderModule (full-screen triangle)")) return false;

        uint32_t families = 0;
        h->iapi->GetPhysicalDeviceQueueFamilyProperties(h->physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> family(families);
        h->iapi->GetPhysicalDeviceQueueFamilyProperties(h->physical, &families, family.data());
        s.compute = h->graphicsFamily < families && (family[h->graphicsFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

        // One layout for passes and computes: a binding a module does not use needs no descriptor.
        VkDescriptorSetLayoutBinding bindings[kBindings] = {};
        for (uint32_t i = 0; i < kBindings; ++i)
        {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = i < kUniformBinding ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                        : i == kUniformBinding ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = kStages;
        }
        VkDescriptorSetLayoutCreateInfo li = {};
        li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = kBindings;
        li.pBindings    = bindings;
        if (!Check(s.dapi->CreateDescriptorSetLayout(s.device, &li, nullptr, &s.setLayout), "vkCreateDescriptorSetLayout")) return false;

        const VkPushConstantRange push = { kStages, 0, GxEffects::kMaxPushBytes };
        VkPipelineLayoutCreateInfo pl = {};
        pl.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount         = 1;
        pl.pSetLayouts            = &s.setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges    = &push;
        if (!Check(s.dapi->CreatePipelineLayout(s.device, &pl, nullptr, &s.layout), "vkCreatePipelineLayout")) return false;

        for (uint32_t i = 0; i < 8; ++i)
        {
            VkSamplerCreateInfo si = {};
            si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.magFilter    = (i & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            si.minFilter    = si.magFilter;
            si.mipmapMode   = (i & 4) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU = (i & 2) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.addressModeV = si.addressModeU;
            si.addressModeW = si.addressModeU;
            si.maxLod       = (i & 4) ? VK_LOD_CLAMP_NONE : 0.0f;
            si.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            if (!Check(s.dapi->CreateSampler(s.device, &si, nullptr, &s.samplers[i]), "vkCreateSampler")) return false;
        }

        const VkDescriptorPoolSize sizes[3] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerFrame * GxEffects::kMaxTextures },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerFrame },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetsPerFrame * GxEffects::kMaxImages },
        };
        for (SlotState& slot : s.slots)
        {
            VkDescriptorPoolCreateInfo pi = {};
            pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pi.maxSets       = kSetsPerFrame;
            pi.poolSizeCount = 3;
            pi.pPoolSizes    = sizes;
            if (!Check(s.dapi->CreateDescriptorPool(s.device, &pi, nullptr, &slot.pool), "vkCreateDescriptorPool")) return false;
            if (!memory::CreateBuffer(kUniformRing, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, memory::Where::Stream, slot.uniforms) || !slot.uniforms.block.mapped)
            {
                Write(Error, "vulkan: effects: no host-visible memory for the constants");
                return false;
            }
        }

        s.failed = false;
        s.ready  = true;
        Write(Info, "vulkan: effects: ready (depth copies in %s)", FormatName(frame::DepthFormat()));
        return true;
    }

    /// The slot of the open frame, its pool and ring reset the first time this frame touches it.
    SlotState* OpenSlot()
    {
        SlotState& slot = s.slots[frame::Slot()];
        const uint64_t serial = frame::Serial();
        if (slot.serial != serial)
        {
            s.dapi->ResetDescriptorPool(s.device, slot.pool, 0);
            slot.cursor = 0;
            slot.serial = serial;
        }
        return &slot;
    }

    const PassPair* PassesFor(VkFormat format)
    {
        for (const PassPair& p : s.passes)
            if (p.format == format) return &p;
        PassPair pair;
        pair.format = format;
        for (int clear = 0; clear < 2; ++clear)
        {
            VkAttachmentDescription a = {};
            a.format         = format;
            a.samples        = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp         = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout  = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            a.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
            VkSubpassDescription sub = {};
            sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments    = &ref;
            // As the engine's passes: wait for every earlier read and write of the image, and make
            // the writes visible to the shaders and transfers that use it next.
            VkSubpassDependency dep[2] = {};
            dep[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
            dep[0].dstSubpass    = 0;
            dep[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            dep[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
            dep[1].srcSubpass    = 0;
            dep[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
            dep[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dep[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkRenderPassCreateInfo info = {};
            info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            info.attachmentCount = 1;
            info.pAttachments    = &a;
            info.subpassCount    = 1;
            info.pSubpasses      = &sub;
            info.dependencyCount = 2;
            info.pDependencies   = dep;
            VkRenderPass& out = clear ? pair.clear : pair.load;
            if (!Check(s.dapi->CreateRenderPass(s.device, &info, nullptr, &out), "vkCreateRenderPass"))
            {
                if (pair.load != VK_NULL_HANDLE) s.dapi->DestroyRenderPass(s.device, pair.load, nullptr);
                return nullptr;
            }
        }
        s.passes.push_back(pair);
        return &s.passes.back();
    }

    void BlendState(GxEffects::Blend blend, uint32_t mask, VkPipelineColorBlendAttachmentState& a)
    {
        a = {};
        a.colorWriteMask = mask & 0xFu;
        a.blendEnable    = blend == GxEffects::Blend::Opaque ? VK_FALSE : VK_TRUE;
        a.colorBlendOp   = blend == GxEffects::Blend::Max ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
        a.alphaBlendOp   = a.colorBlendOp;
        switch (blend)
        {
        case GxEffects::Blend::Opaque:
        case GxEffects::Blend::Max:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case GxEffects::Blend::Alpha:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case GxEffects::Blend::Premultiplied:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case GxEffects::Blend::Add:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case GxEffects::Blend::Multiply:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        }
        // D3D9 blends alpha with the colour factors unless SEPARATEALPHABLENDENABLE is set.
        a.srcAlphaBlendFactor = a.srcColorBlendFactor == VK_BLEND_FACTOR_DST_COLOR ? VK_BLEND_FACTOR_DST_ALPHA : a.srcColorBlendFactor;
        a.dstAlphaBlendFactor = a.dstColorBlendFactor;
    }

    VkPipeline PipelineFor(const GxEffects::Program* program, const GxEffects::Program* vertexProgram, VkFormat format, GxEffects::Blend blend, uint32_t mask)
    {
        for (const PipelineEntry& e : s.pipelines)
            if (e.program == program && e.vertex == vertexProgram && e.format == format && e.blend == blend && e.writeMask == mask) return e.pipeline;
        const PassPair* passes = PassesFor(format);
        if (!passes) return VK_NULL_HANDLE;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexProgram ? vertexProgram->module : s.vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = program->module;
        stages[1].pName  = "main";

        VkPipelineVertexInputStateCreateInfo vertex = {};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly = {};
        assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = vertexProgram ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport = {};
        viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount  = 1;
        VkPipelineRasterizationStateCreateInfo raster = {};
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.lineWidth   = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample = {};
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment;
        BlendState(blend, mask, attachment);
        VkPipelineColorBlendStateCreateInfo colour = {};
        colour.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colour.attachmentCount = 1;
        colour.pAttachments    = &attachment;
        const VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = {};
        dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates    = dynamics;

        VkGraphicsPipelineCreateInfo info = {};
        info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount          = 2;
        info.pStages             = stages;
        info.pVertexInputState   = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState      = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState   = &multisample;
        info.pColorBlendState    = &colour;
        info.pDynamicState       = &dynamic;
        info.layout              = s.layout;
        info.renderPass          = passes->load;
        PipelineEntry e;
        e.program   = program;
        e.vertex    = vertexProgram;
        e.format    = format;
        e.blend     = blend;
        e.writeMask = mask;
        if (!Check(s.dapi->CreateGraphicsPipelines(s.device, VK_NULL_HANDLE, 1, &info, nullptr, &e.pipeline), program->name)) return VK_NULL_HANDLE;
        s.pipelines.push_back(e);
        return e.pipeline;
    }

    VkPipeline ComputePipelineFor(const GxEffects::Program* program)
    {
        if (program->compute != VK_NULL_HANDLE) return program->compute;
        VkComputePipelineCreateInfo info = {};
        info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = program->module;
        info.stage.pName  = "main";
        info.layout       = s.layout;
        Check(s.dapi->CreateComputePipelines(s.device, VK_NULL_HANDLE, 1, &info, nullptr, &program->compute), program->name);
        return program->compute;
    }

    VkFramebuffer MakeFramebuffer(VkRenderPass pass, VkImageView view, VkExtent2D extent)
    {
        VkFramebufferCreateInfo info = {};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = pass;
        info.attachmentCount = 1;
        info.pAttachments    = &view;
        info.width           = extent.width;
        info.height          = extent.height;
        info.layers          = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        Check(s.dapi->CreateFramebuffer(s.device, &info, nullptr, &fb), "vkCreateFramebuffer");
        return fb;
    }

    /// The engine's current colour target: its image, and a framebuffer of ours over it.
    bool WorldTargetFor(const texture::Image*& image, VkFramebuffer& fb)
    {
        image = frame::Target() ? frame::Target() : frame::Scene();
        if (!image || image->handle == VK_NULL_HANDLE || image->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
        const uint32_t gen = context::SwapchainGeneration();
        const uint64_t now = frame::Serial();
        WorldTarget* slot  = nullptr;
        for (WorldTarget& w : s.world)
        {
            const bool stale = w.fb != VK_NULL_HANDLE
                && ((w.id == 0 && w.swapGen != gen) || now - w.lastUsed > kWorldTargetFrames);
            if (stale)
            {
                frame::RetireRaw(w.fb, w.view, VK_NULL_HANDLE, memory::Block(), w.lastUsed);
                w = WorldTarget();
            }
            if (w.fb != VK_NULL_HANDLE && w.image == image->handle && w.id == image->id) slot = &w;
        }
        if (!slot)
        {
            for (WorldTarget& w : s.world)
                if (w.fb == VK_NULL_HANDLE) slot = &w;
            if (!slot) return false;
            const PassPair* passes = PassesFor(image->format);
            if (!passes) return false;
            VkImageViewCreateInfo vi = {};
            vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image                       = image->handle;
            vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
            vi.format                      = image->format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            if (!Check(s.dapi->CreateImageView(s.device, &vi, nullptr, &slot->view), "vkCreateImageView (world target)")) return false;
            slot->fb = MakeFramebuffer(passes->load, slot->view, image->extent);
            if (slot->fb == VK_NULL_HANDLE)
            {
                s.dapi->DestroyImageView(s.device, slot->view, nullptr);
                *slot = WorldTarget();
                return false;
            }
            slot->image   = image->handle;
            slot->id      = image->id;
            slot->swapGen = gen;
        }
        slot->lastUsed = now;
        fb = slot->fb;
        return true;
    }

    /// A raw image of ours (a volume or a depth copy), with its sampled view.
    bool CreateRaw(GxEffects::Texture& t, VkImageType type, VkImageViewType viewType, VkImageUsageFlags usage, VkImageAspectFlags aspect)
    {
        VkImageCreateInfo info = {};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = type;
        info.format        = t.vk;
        info.extent        = t.extent;
        info.mipLevels     = t.levels;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = usage;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!Check(s.dapi->CreateImage(s.device, &info, nullptr, &t.raw), "vkCreateImage")) return false;
        VkMemoryRequirements req = {};
        s.dapi->GetImageMemoryRequirements(s.device, t.raw, &req);
        VkResult r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        if (memory::Allocate(req, memory::Where::DeviceLocal, t.rawBlock))
            r = s.dapi->BindImageMemory(s.device, t.raw, t.rawBlock.memory, t.rawBlock.offset);
        if (r == VK_SUCCESS)
        {
            VkImageViewCreateInfo vi = {};
            vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image                       = t.raw;
            vi.viewType                    = viewType;
            vi.format                      = t.vk;
            vi.subresourceRange.aspectMask = aspect;
            vi.subresourceRange.levelCount = t.levels;
            vi.subresourceRange.layerCount = 1;
            r = s.dapi->CreateImageView(s.device, &vi, nullptr, &t.rawView);
        }
        if (!Check(r, "vkBindImageMemory / vkCreateImageView"))
        {
            s.dapi->DestroyImage(s.device, t.raw, nullptr);
            memory::Free(t.rawBlock);
            t.raw = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    class VulkanEffects final : public GxEffects::Device
    {
        public:
        GxEffects::Texture* CreateTexture(uint32_t width, uint32_t height, GxEffects::Format format, bool target) override
        {
            return Create(width, height, format, target, false);
        }

        GxEffects::Texture* CreateStorage(uint32_t width, uint32_t height, GxEffects::Format format) override
        {
            return Init() && CanStore(format) ? Create(width, height, format, true, true) : nullptr;
        }

        GxEffects::Texture* Create(uint32_t width, uint32_t height, GxEffects::Format format, bool target, bool storage)
        {
            if (!Init() || width == 0 || height == 0) return nullptr;
            auto* t    = new GxEffects::Texture();
            t->format  = format;
            t->vk      = VkFormatOf(format);
            t->extent  = VkExtent3D{ width, height, 1 };
            t->target  = target && format != GxEffects::Format::Depth;
            t->storage = storage && t->target;
            VkCommandBuffer cmd = frame::PrePass();
            bool ok = cmd != VK_NULL_HANDLE;
            if (ok && format == GxEffects::Format::Depth)
            {
                VkFormatProperties props = {};
                const context::Handles* h = context::Get();
                h->iapi->GetPhysicalDeviceFormatProperties(h->physical, t->vk, &props);
                ok = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
                if (!ok && !s.warnedDepth)
                {
                    s.warnedDepth = true;
                    Write(Error, "vulkan: effects: the depth format %s cannot be sampled on this adapter", FormatName(t->vk));
                }
                ok = ok && CreateRaw(*t, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
                // Its contents are undefined until the first CopyDepth: a depth image cannot be
                // cleared without vkCmdClearDepthStencilImage, which this backend does not load.
                if (ok)
                {
                    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (t->vk == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
                    Barrier(cmd, t->raw, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT);
                }
            }
            else if (ok)
            {
                ok = t->target ? texture::CreateTarget(width, height, 1, false, t->vk, t->image, t->storage)
                               : texture::CreateMipped(width, height, 1, t->vk, t->image);
                const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                ok = ok && texture::RecordClear(cmd, t->image, zero);
                if (ok && t->target)
                {
                    const PassPair* passes = PassesFor(t->vk);
                    t->fb = passes ? MakeFramebuffer(passes->load, t->image.view, VkExtent2D{ width, height }) : VK_NULL_HANDLE;
                    ok = t->fb != VK_NULL_HANDLE;
                }
            }
            if (!ok)
            {
                Write(Error, "vulkan: effects: no %ux%u texture of %s", width, height, FormatName(t->vk));
                Destroy(t);
                return nullptr;
            }
            s.textures.push_back(t);
            return t;
        }

        GxEffects::Texture* CreateFilled(uint32_t width, uint32_t height, uint32_t levels, GxEffects::Format format, const void* const* levelTexels) override
        {
            if (!Init() || !levelTexels || width == 0 || height == 0 || levels == 0 || format == GxEffects::Format::Depth) return nullptr;
            auto* t   = new GxEffects::Texture();
            t->format = format;
            t->vk     = VkFormatOf(format);
            t->extent = VkExtent3D{ width, height, 1 };
            VkDeviceSize bytes = 0;
            for (uint32_t level = 0; level < levels; ++level)
                bytes += VkDeviceSize(Mip(width, level)) * Mip(height, level) * TexelBytes(format);
            VkCommandBuffer cmd = frame::PrePass();
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceSize offset = 0;
            void* mapped = nullptr;
            std::vector<texture::RegionCopy> regions(levels);
            bool ok = cmd != VK_NULL_HANDLE && texture::CreateMipped(width, height, levels, t->vk, t->image) && frame::Stage(bytes, buffer, offset, mapped);
            for (uint32_t level = 0; ok && level < levels; ++level)
            {
                const VkDeviceSize levelBytes = VkDeviceSize(Mip(width, level)) * Mip(height, level) * TexelBytes(format);
                std::memcpy(mapped, levelTexels[level], static_cast<size_t>(levelBytes));
                regions[level].level     = level;
                regions[level].width     = Mip(width, level);
                regions[level].height    = Mip(height, level);
                regions[level].srcOffset = offset;
                mapped  = static_cast<uint8_t*>(mapped) + levelBytes;
                offset += levelBytes;
            }
            ok = ok && texture::RecordRegions(cmd, t->image, buffer, regions.data(), levels) == levels;
            if (!ok)
            {
                Write(Error, "vulkan: effects: no %ux%u texture of %s with %u levels", width, height, FormatName(t->vk), levels);
                Destroy(t);
                return nullptr;
            }
            s.textures.push_back(t);
            return t;
        }

        GxEffects::Texture* CreateVolume(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, GxEffects::Format format, const void* const* levelTexels) override
        {
            if (!Init() || !levelTexels || width == 0 || height == 0 || depth == 0 || levels == 0 || format == GxEffects::Format::Depth) return nullptr;
            auto* t   = new GxEffects::Texture();
            t->format = format;
            t->vk     = VkFormatOf(format);
            t->extent = VkExtent3D{ width, height, depth };
            t->levels = levels;
            VkDeviceSize bytes = 0;
            for (uint32_t level = 0; level < levels; ++level)
                bytes += VkDeviceSize(Mip(width, level)) * Mip(height, level) * Mip(depth, level) * TexelBytes(format);
            VkCommandBuffer cmd = frame::PrePass();
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceSize offset = 0;
            void* mapped = nullptr;
            bool ok = cmd != VK_NULL_HANDLE
                && CreateRaw(*t, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
                && frame::Stage(bytes, buffer, offset, mapped);
            if (ok)
            {
                std::vector<VkBufferImageCopy> copies(levels);
                for (uint32_t level = 0; level < levels; ++level)
                {
                    const VkExtent3D e = { Mip(width, level), Mip(height, level), Mip(depth, level) };
                    const VkDeviceSize levelBytes = VkDeviceSize(e.width) * e.height * e.depth * TexelBytes(format);
                    std::memcpy(mapped, levelTexels[level], static_cast<size_t>(levelBytes));
                    copies[level] = {};
                    copies[level].bufferOffset                = offset;
                    copies[level].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copies[level].imageSubresource.mipLevel   = level;
                    copies[level].imageSubresource.layerCount = 1;
                    copies[level].imageExtent                 = e;
                    mapped  = static_cast<uint8_t*>(mapped) + levelBytes;
                    offset += levelBytes;
                }
                Barrier(cmd, t->raw, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, levels);
                s.dapi->CmdCopyBufferToImage(cmd, buffer, t->raw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, copies.data());
                Barrier(cmd, t->raw, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, levels);
            }
            if (!ok)
            {
                Write(Error, "vulkan: effects: no %ux%ux%u volume of %s", width, height, depth, FormatName(t->vk));
                Destroy(t);
                return nullptr;
            }
            s.textures.push_back(t);
            return t;
        }

        void Destroy(GxEffects::Texture* t) override
        {
            if (!t) return;
            if (t->fb != VK_NULL_HANDLE) frame::RetireRaw(t->fb, VK_NULL_HANDLE, VK_NULL_HANDLE, memory::Block(), t->lastUsed);
            if (t->raw != VK_NULL_HANDLE) frame::RetireRaw(VK_NULL_HANDLE, t->rawView, t->raw, t->rawBlock, t->lastUsed);
            if (t->image.handle != VK_NULL_HANDLE) frame::RetireImage(t->image, t->lastUsed);
            for (size_t i = 0; i < s.textures.size(); ++i)
                if (s.textures[i] == t)
                {
                    s.textures[i] = s.textures.back();
                    s.textures.pop_back();
                    break;
                }
            delete t;
        }

        void Size(const GxEffects::Texture* t, uint32_t& width, uint32_t& height) override
        {
            width  = t ? t->extent.width : 0;
            height = t ? t->extent.height : 0;
        }

        bool Upload(GxEffects::Texture* t, const void* texels) override
        {
            if (!t || !texels || t->image.handle == VK_NULL_HANDLE || frame::QueryActive()) return false;
            const VkDeviceSize bytes = VkDeviceSize(t->extent.width) * t->extent.height * TexelBytes(t->format);
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceSize offset = 0;
            void* mapped = nullptr;
            VkCommandBuffer cmd = frame::Outside();
            if (!cmd || !frame::Stage(bytes, buffer, offset, mapped)) return false;
            std::memcpy(mapped, texels, static_cast<size_t>(bytes));
            texture::RegionCopy region;
            region.width     = t->extent.width;
            region.height    = t->extent.height;
            region.srcOffset = offset;
            t->lastUsed = frame::Serial();
            return texture::RecordRegions(cmd, t->image, buffer, &region, 1) == 1;
        }

        GxEffects::Program* CreateProgram(const uint32_t* spirv, size_t bytes, const char* name) override
        {
            if (!Init() || !spirv || bytes < 20 || spirv[0] != 0x07230203u) return nullptr;
            auto* p = new GxEffects::Program();
            std::strncpy(p->name, name ? name : "effect", sizeof p->name - 1);
            VkShaderModuleCreateInfo info = {};
            info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = bytes;
            info.pCode    = spirv;
            if (!Check(s.dapi->CreateShaderModule(s.device, &info, nullptr, &p->module), p->name))
            {
                delete p;
                return nullptr;
            }
            s.programs.push_back(p);
            return p;
        }

        /// A set from the open frame's pool with the textures and constants written; null when the frame is out of sets or constants.
        VkDescriptorSet MakeSet(const GxEffects::Binding* textures, const float (*constants)[4], uint32_t rowCount)
        {
            if (!s.blank)
            {
                const uint32_t opaqueBlack = 0xFF000000u;
                const void* level = &opaqueBlack;
                s.blank = CreateFilled(1, 1, 1, GxEffects::Format::BGRA8, &level);
                if (!s.blank) return VK_NULL_HANDLE;
            }

            SlotState* slot = OpenSlot();
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo ai = {};
            ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool     = slot->pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts        = &s.setLayout;
            if (s.dapi->AllocateDescriptorSets(s.device, &ai, &set) != VK_SUCCESS)
            {
                if (!s.warnedSets) Write(Warn, "vulkan: effects: more than %u passes and computes in a frame; the rest are skipped (logged once)", kSetsPerFrame);
                s.warnedSets = true;
                return VK_NULL_HANDLE;
            }
            const VkDeviceSize constantBytes = VkDeviceSize(GxEffects::kMaxConstantRows) * 16;
            const VkDeviceSize at = (slot->cursor + s.uniformAlign - 1) / s.uniformAlign * s.uniformAlign;
            if (at + constantBytes > kUniformRing)
            {
                if (!s.warnedUniforms) Write(Warn, "vulkan: effects: the constant ring is full; the frame's later passes are skipped (logged once)");
                s.warnedUniforms = true;
                return VK_NULL_HANDLE;
            }
            auto* rows = static_cast<uint8_t*>(slot->uniforms.block.mapped) + at;
            if (constants && rowCount) std::memcpy(rows, constants, rowCount * 16u);
            std::memset(rows + rowCount * 16u, 0, static_cast<size_t>(constantBytes - rowCount * 16u));
            memory::FlushHost(slot->uniforms.block);
            slot->cursor = at + constantBytes;

            const uint64_t serial = frame::Serial();
            VkDescriptorImageInfo images[GxEffects::kMaxTextures] = {};
            for (uint32_t i = 0; i < GxEffects::kMaxTextures; ++i)
            {
                GxEffects::Texture* t = textures[i].texture ? textures[i].texture : s.blank;
                t->lastUsed          = serial;
                images[i].sampler     = s.samplers[(textures[i].linear ? 1 : 0) | (textures[i].wrap ? 2 : 0) | (textures[i].mips ? 4 : 0)];
                images[i].imageView   = t->View();
                images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            VkDescriptorBufferInfo buffer = { slot->uniforms.handle, at, constantBytes };
            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = set;
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = GxEffects::kMaxTextures;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo      = images;
            writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = set;
            writes[1].dstBinding      = kUniformBinding;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].pBufferInfo     = &buffer;
            s.dapi->UpdateDescriptorSets(s.device, 2, writes, 0, nullptr);

            return set;
        }

        bool Draw(const GxEffects::Pass& pass) override
        {
            if (!Init() || !pass.program || frame::QueryActive()) return false;
            if (pass.constantRows > GxEffects::kMaxConstantRows || (pass.target && !pass.target->target)) return false;
            VkCommandBuffer cmd = frame::Outside();
            if (!cmd) return false;

            VkFormat format;
            VkExtent2D extent;
            VkFramebuffer fb;
            if (pass.target)
            {
                format = pass.target->vk;
                extent = VkExtent2D{ pass.target->extent.width, pass.target->extent.height };
                fb     = pass.target->fb;
            }
            else
            {
                const texture::Image* image = nullptr;
                if (!WorldTargetFor(image, fb)) return false;
                format = image->format;
                extent = image->extent;
            }
            for (const GxEffects::Binding& b : pass.textures)
                if (b.texture && b.texture == pass.target) return false;

            const PassPair* passes = PassesFor(format);
            const VkPipeline pipeline = passes ? PipelineFor(pass.program, pass.points ? pass.vertexProgram : nullptr, format, pass.blend, pass.writeMask) : VK_NULL_HANDLE;
            if (pipeline == VK_NULL_HANDLE) return false;
            const VkDescriptorSet set = MakeSet(pass.textures, pass.constants, pass.constantRows);
            if (set == VK_NULL_HANDLE) return false;

            VkClearValue clear = {};
            std::memcpy(clear.color.float32, pass.clearColour, sizeof clear.color.float32);
            VkRenderPassBeginInfo begin = {};
            begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            begin.renderPass        = pass.clear ? passes->clear : passes->load;
            begin.framebuffer       = fb;
            begin.renderArea.extent = extent;
            begin.clearValueCount   = pass.clear ? 1 : 0;
            begin.pClearValues      = &clear;
            s.dapi->CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp = {};
            vp.x        = static_cast<float>(pass.x);
            vp.y        = static_cast<float>(pass.y);
            vp.width    = static_cast<float>(pass.width ? pass.width : extent.width);
            vp.height   = static_cast<float>(pass.width ? pass.height : extent.height);
            vp.maxDepth = 1.0f;
            VkRect2D scissor = {};
            scissor.offset = VkOffset2D{ pass.x < 0 ? 0 : pass.x, pass.y < 0 ? 0 : pass.y };
            scissor.extent = VkExtent2D{ static_cast<uint32_t>(vp.width), static_cast<uint32_t>(vp.height) };
            if (scissor.offset.x + scissor.extent.width > extent.width) scissor.extent.width = extent.width - scissor.offset.x;
            if (scissor.offset.y + scissor.extent.height > extent.height) scissor.extent.height = extent.height - scissor.offset.y;
            s.dapi->CmdSetViewport(cmd, 0, 1, &vp);
            s.dapi->CmdSetScissor(cmd, 0, 1, &scissor);
            s.dapi->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            s.dapi->CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.layout, 0, 1, &set, 0, nullptr);
            s.dapi->CmdDraw(cmd, pass.points && pass.vertexProgram ? pass.points : 3, 1, 0, 0);
            s.dapi->CmdEndRenderPass(cmd);
            // The engine's draw code caches what it bound on this command buffer; this pass bound its own.
            gxvk::vk::pipelines::ForgetBound();
            if (pass.target) pass.target->lastUsed = frame::Serial();
            return true;
        }

        bool CanBlend(GxEffects::Format format) override
        {
            const context::Handles* h = context::Get();
            if (!h || format == GxEffects::Format::Depth) return false;
            VkFormatProperties props = {};
            h->iapi->GetPhysicalDeviceFormatProperties(h->physical, VkFormatOf(format), &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
        }

        bool CanStore(GxEffects::Format format) override
        {
            const context::Handles* h = context::Get();
            if (!h) return false;
            // GLSL has no BGRA image format; the others past RGBA16F, RGBA32F and R32F are the extended ones.
            const bool extended = format == GxEffects::Format::RG16F || format == GxEffects::Format::RG32F || format == GxEffects::Format::R8;
            if (format == GxEffects::Format::BGRA8 || format == GxEffects::Format::Depth || (extended && !(context::Caps().enabled & kCapStorageImageExt))) return false;
            const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
            VkFormatProperties props = {};
            h->iapi->GetPhysicalDeviceFormatProperties(h->physical, VkFormatOf(format), &props);
            return (props.optimalTilingFeatures & needed) == needed;
        }

        bool Dispatch(const GxEffects::Compute& c) override
        {
            if (!Init() || !s.compute || !c.program || frame::QueryActive()) return false;
            if (c.constantRows > GxEffects::kMaxConstantRows || c.pushBytes > GxEffects::kMaxPushBytes || (c.pushBytes && !c.push)) return false;
            if (c.groupsX == 0 || c.groupsY == 0 || c.groupsZ == 0) return false;
            for (const GxEffects::Texture* t : c.images)
            {
                if (!t) continue;
                if (!t->storage) return false;
                for (const GxEffects::Binding& b : c.textures)
                    if (b.texture == t) return false;
            }
            VkCommandBuffer cmd = frame::Outside();
            if (!cmd) return false;
            const VkPipeline pipeline = ComputePipelineFor(c.program);
            if (pipeline == VK_NULL_HANDLE) return false;
            const VkDescriptorSet set = MakeSet(c.textures, c.constants, c.constantRows);
            if (set == VK_NULL_HANDLE) return false;

            const uint64_t serial = frame::Serial();
            VkDescriptorImageInfo images[GxEffects::kMaxImages] = {};
            VkWriteDescriptorSet writes[GxEffects::kMaxImages] = {};
            VkImageMemoryBarrier toGeneral[GxEffects::kMaxImages] = {};
            VkImageMemoryBarrier toRead[GxEffects::kMaxImages] = {};
            uint32_t n = 0;
            for (uint32_t i = 0; i < GxEffects::kMaxImages; ++i)
            {
                GxEffects::Texture* t = c.images[i];
                if (!t) continue;
                t->lastUsed = serial;
                images[n].imageView   = t->View();
                images[n].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                writes[n].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[n].dstSet          = set;
                writes[n].dstBinding      = kImageBinding + i;
                writes[n].descriptorCount = 1;
                writes[n].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[n].pImageInfo      = &images[n];
                VkImageMemoryBarrier& b = toGeneral[n];
                b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.srcAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                b.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                b.oldLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.newLayout                   = VK_IMAGE_LAYOUT_GENERAL;
                b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
                b.image                       = t->image.handle;
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                toRead[n] = b;
                toRead[n].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                toRead[n].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT
                                        | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                toRead[n].oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
                toRead[n].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                ++n;
            }
            if (n) s.dapi->UpdateDescriptorSets(s.device, n, writes, 0, nullptr);

            // The engine's passes and ours wait for nothing in the compute stage, so the compute waits
            // for every earlier write itself (what it samples included).
            VkMemoryBarrier before = {};
            before.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            before.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, n, toGeneral);
            s.dapi->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            s.dapi->CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.layout, 0, 1, &set, 0, nullptr);
            if (c.pushBytes) s.dapi->CmdPushConstants(cmd, s.layout, kStages, 0, c.pushBytes, c.push);
            s.dapi->CmdDispatch(cmd, c.groupsX, c.groupsY, c.groupsZ);
            s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, n, toRead);
            // Push constants are shared with the graphics bind point.
            gxvk::vk::pipelines::ForgetBound();
            return true;
        }

        bool TargetSize(uint32_t& width, uint32_t& height) override
        {
            const texture::Image* image = frame::Target() ? frame::Target() : frame::Scene();
            width  = image ? image->extent.width : 0;
            height = image ? image->extent.height : 0;
            return width && height;
        }

        bool CopyColor(GxEffects::Texture* dst, int32_t x, int32_t y, uint32_t width, uint32_t height, bool linear) override
        {
            uint32_t targetW, targetH;
            if (!dst || dst->image.handle == VK_NULL_HANDLE || !TargetSize(targetW, targetH)) return false;
            if (width == 0)
            {
                x = 0;
                y = 0;
                width  = targetW;
                height = targetH;
            }
            if (x < 0 || y < 0 || x + width > targetW || y + height > targetH) return false;
            const VkRect2D src  = { { x, y }, { width, height } };
            const VkRect2D rect = { { 0, 0 }, { dst->extent.width, dst->extent.height } };
            dst->lastUsed = frame::Serial();
            return frame::CopyFromTarget(dst->image, src, rect, linear);
        }

        bool CopyDepth(GxEffects::Texture* dst) override
        {
            const VkExtent2D extent = frame::Extent();
            if (!dst || dst->format != GxEffects::Format::Depth || dst->extent.width != extent.width || dst->extent.height != extent.height) return false;
            dst->lastUsed = frame::Serial();
            return frame::CopyTargetDepth(dst->raw);
        }

        bool Read(GxEffects::Texture* t, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* out) override
        {
            // frame::ReadImage moves four bytes a texel.
            if (!t || t->image.handle == VK_NULL_HANDLE || TexelBytes(t->format) != 4) return false;
            const VkRect2D rect = { { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { width, height } };
            return frame::ReadImage(t->image, 0, rect, static_cast<uint8_t*>(out));
        }

        GxEffects::Readback* BeginRead(GxEffects::Texture* t, uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
        {
            if (!t || t->image.handle == VK_NULL_HANDLE || frame::QueryActive()) return nullptr;
            const VkRect2D rect = { { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { width, height } };
            auto* r = new GxEffects::Readback();
            if (!frame::BeginImageRead(t->image, rect, TexelBytes(t->format), r->read))
            {
                delete r;
                return nullptr;
            }
            t->lastUsed = frame::Serial();
            return r;
        }

        bool PollRead(GxEffects::Readback* r, void* out) override
        {
            if (!r || !frame::PollImageRead(r->read, out)) return false;
            delete r;
            return true;
        }

        void CancelRead(GxEffects::Readback* r) override
        {
            if (!r) return;
            frame::CancelImageRead(r->read);
            delete r;
        }
    };

    VulkanEffects g_device;
}

namespace gxvk::vk::effects
{
    void Shutdown()
    {
        if (!s.dapi)
        {
            s = State();
            return;
        }
        // The device is idle: what the passes made goes now, the textures the effects still hold
        // included (they are not used again).
        for (GxEffects::Texture* t : s.textures)
        {
            if (t->fb != VK_NULL_HANDLE) s.dapi->DestroyFramebuffer(s.device, t->fb, nullptr);
            if (t->rawView != VK_NULL_HANDLE) s.dapi->DestroyImageView(s.device, t->rawView, nullptr);
            if (t->raw != VK_NULL_HANDLE) s.dapi->DestroyImage(s.device, t->raw, nullptr);
            memory::Free(t->rawBlock);
            texture::DestroyIdle(t->image);
            // The struct stays: a holder that has not noticed Get() going null may still name it.
            *t = GxEffects::Texture();
        }
        for (const PipelineEntry& e : s.pipelines) s.dapi->DestroyPipeline(s.device, e.pipeline, nullptr);
        for (const PassPair& p : s.passes)
        {
            s.dapi->DestroyRenderPass(s.device, p.load, nullptr);
            s.dapi->DestroyRenderPass(s.device, p.clear, nullptr);
        }
        for (GxEffects::Program* p : s.programs)
        {
            if (p->compute != VK_NULL_HANDLE) s.dapi->DestroyPipeline(s.device, p->compute, nullptr);
            s.dapi->DestroyShaderModule(s.device, p->module, nullptr);
            delete p;
        }
        for (WorldTarget& w : s.world)
        {
            if (w.fb != VK_NULL_HANDLE) s.dapi->DestroyFramebuffer(s.device, w.fb, nullptr);
            if (w.view != VK_NULL_HANDLE) s.dapi->DestroyImageView(s.device, w.view, nullptr);
        }
        for (SlotState& slot : s.slots)
        {
            if (slot.pool != VK_NULL_HANDLE) s.dapi->DestroyDescriptorPool(s.device, slot.pool, nullptr);
            memory::DestroyBuffer(slot.uniforms);
        }
        for (VkSampler sampler : s.samplers)
            if (sampler != VK_NULL_HANDLE) s.dapi->DestroySampler(s.device, sampler, nullptr);
        if (s.layout != VK_NULL_HANDLE) s.dapi->DestroyPipelineLayout(s.device, s.layout, nullptr);
        if (s.setLayout != VK_NULL_HANDLE) s.dapi->DestroyDescriptorSetLayout(s.device, s.setLayout, nullptr);
        if (s.vert != VK_NULL_HANDLE) s.dapi->DestroyShaderModule(s.device, s.vert, nullptr);
        s = State();
        s.failed = true;   // the process is ending: no device again
    }
}

namespace
{
    bool g_forceDepthWrite = false;
}

void GxEffects::ForceDepthWrite(bool on) { g_forceDepthWrite = on; }
bool GxEffects::DepthWriteForced() { return g_forceDepthWrite; }

GxEffects::Device* GxEffects::Get()
{
    return context::Live() && !s.failed && !frame::Failed() ? &g_device : nullptr;
}
