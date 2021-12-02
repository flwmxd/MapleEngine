//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "Engine/Core.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace maple
{
	class Shader;
	class RenderPass;
	class CommandBuffer;
	class DescriptorSet;
	class Pipeline;
	struct VertexInputDescription;
	struct DescriptorLayoutInfo;
	struct DescriptorPoolInfo;

	class Texture;
	class Texture2D;
	class TextureCube;
	class TextureDepth;
	class TextureDepthArray;
	class FrameBuffer;
	class Mesh;
	class Material;

	enum class TextureType : int32_t;
	enum class TextureFormat;
	enum class TextureType;
	class RenderPass;

	constexpr uint16_t SHADOWMAP_SiZE_MAX = 4096;
	constexpr uint8_t  MAX_RENDER_TARGETS = 8;
	constexpr uint8_t  SHADOWMAP_MAX      = 16;
	constexpr uint8_t  MAX_MIPS           = 32;
	constexpr uint8_t  MAX_TEXTURES       = 16;

	enum class CullMode : int32_t
	{
		Front,
		Back,
		FrontAndBack,
		None
	};

	enum class PolygonMode : int32_t
	{
		Fill,
		Line,
		Point
	};

	enum class BlendMode : int32_t
	{
		None = 0,
		OneZero,
		ZeroSrcColor,
		SrcAlphaOneMinusSrcAlpha,
	};

	enum class TextureWrap : int32_t
	{
		None,
		Repeat,
		Clamp,
		MirroredRepeat,
		ClampToEdge,
		ClampToBorder
	};

	enum class TextureFilter : int32_t
	{
		None,
		Linear,
		Nearest
	};

	enum class TextureFormat : int32_t
	{
		NONE,
		R8,
		RG8,
		RGB8,
		RGBA8,
		RGB16,
		RGBA16,
		RGB32,
		RGBA32,
		RGB,
		RGBA,
		DEPTH,
		STENCIL,
		DEPTH_STENCIL,
		SCREEN
	};

	enum class TextureType : int32_t
	{
		Color,
		Depth,
		DepthArray,
		Cube,
		Other
	};

	struct RenderPassInfo
	{
		std::vector<std::shared_ptr<Texture>> attachments;

		bool clear = true;
	};

	enum class SubPassContents
	{
		Inline,
		Secondary
	};

	struct TextureParameters
	{
		TextureFormat format;
		TextureFilter minFilter;
		TextureFilter magFilter;
		TextureWrap   wrap;
		bool          srgb      = false;
		uint16_t      msaaLevel = 1;

		constexpr TextureParameters() :
		    format(TextureFormat::RGBA8), minFilter(TextureFilter::Nearest), magFilter(TextureFilter::Nearest), wrap(TextureWrap::Repeat)
		{
		}

		constexpr TextureParameters(TextureFormat format, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrap) :
		    format(format), minFilter(minFilter), magFilter(magFilter), wrap(wrap)
		{
		}

		constexpr TextureParameters(TextureFilter minFilter, TextureFilter magFilter) :
		    format(TextureFormat::RGBA8), minFilter(minFilter), magFilter(magFilter), wrap(TextureWrap::Clamp)
		{
		}

		constexpr TextureParameters(TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrap) :
		    format(TextureFormat::RGBA8), minFilter(minFilter), magFilter(magFilter), wrap(wrap)
		{
		}

		constexpr TextureParameters(TextureWrap wrap) :
		    format(TextureFormat::RGBA8), minFilter(TextureFilter::Linear), magFilter(TextureFilter::Linear), wrap(wrap)
		{
		}

		constexpr TextureParameters(TextureFormat format) :
		    format(format), minFilter(TextureFilter::Linear), magFilter(TextureFilter::Linear), wrap(TextureWrap::Clamp)
		{
		}

		constexpr TextureParameters(TextureFormat format, TextureWrap wrap) :
		    format(format), minFilter(TextureFilter::Linear), magFilter(TextureFilter::Linear), wrap(wrap)
		{
		}
	};

	struct TextureLoadOptions
	{
		bool flipX;
		bool flipY;
		bool generateMipMaps;

		constexpr TextureLoadOptions() :
		    TextureLoadOptions(false, false, true)
		{
		}

		constexpr TextureLoadOptions(bool flipX, bool flipY, bool genMips = true) :
		    flipX(flipX), flipY(flipY), generateMipMaps(genMips)
		{
		}
	};

	enum TextureFlags : uint16_t
	{
		Texture_Sampled              = BIT(0),
		Texture_Storage              = BIT(1),
		Texture_RenderTarget         = BIT(2),
		Texture_DepthStencil         = BIT(3),
		Texture_DepthStencilReadOnly = BIT(4)
	};

	enum class ImageLayout
	{
		Undefined,
		General,
		Color_Attachment_Optimal,
		Depth_Stencil_Attachment_Optimal,
		Depth_Stencil_Read_Only_Optimal,
		Shader_Read_Only_Optimal,
		Transfer_Dst_Optimal,
		Present_Src
	};

	enum RendererBufferType
	{
		RendererBufferNone    = 0,
		RendererBufferColor   = BIT(0),
		RendererBufferDepth   = BIT(1),
		RendererBufferStencil = BIT(2)
	};

	enum class DrawType : int32_t
	{
		Point,
		Triangle,
		Lines
	};

	enum class StencilType : int32_t
	{
		Equal,
		Notequal,
		Keep,
		Replace,
		Zero,
		Always
	};

	enum class PixelPackType : int32_t
	{
		Pack,
		UnPack
	};

	enum class RendererBlendFunction : int32_t
	{
		None,
		Zero,
		One,
		SourceAlpha,
		DestinationAlpha,
		OneMinusSourceAlpha
	};

	enum class RendererBlendEquation : int32_t
	{
		None,
		Add,
		Subtract
	};

	enum class RenderMode : int32_t
	{
		Fill,
		Wireframe
	};

	enum class DataType : int32_t
	{
		Float,
		UnsignedInt,
		UnsignedByte
	};

	struct RenderCommand
	{
		Mesh *                    mesh     = nullptr;
		Material *                material = nullptr;
		std::shared_ptr<Pipeline> pipeline;
		glm::mat4                 transform;
	};
}        // namespace maple