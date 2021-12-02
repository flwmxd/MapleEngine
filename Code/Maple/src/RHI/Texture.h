//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "Definitions.h"
#include "FileSystem/IResource.h"
#include <string>

namespace maple
{
	class MAPLE_EXPORT Texture : public IResource
	{
	  public:
		virtual ~Texture()
		{
		}

		virtual auto bind(uint32_t slot = 0) const -> void      = 0;
		virtual auto unbind(uint32_t slot = 0) const -> void    = 0;
		virtual auto getFilePath() const -> const std::string & = 0;
		virtual auto getHandle() const -> void *                = 0;
		virtual auto getWidth() const -> uint32_t               = 0;
		virtual auto getHeight() const -> uint32_t              = 0;
		virtual auto getType() const -> TextureType             = 0;
		virtual auto getFormat() const -> TextureFormat         = 0;

		virtual auto getSize() const -> uint32_t
		{
			return 0;
		}
		virtual auto getMipMapLevels() const -> uint32_t
		{
			return 0;
		}

		virtual auto getDescriptorInfo() const -> void *
		{
			return getHandle();
		}

		static auto isDepthStencilFormat(TextureFormat format)
		{
			return format == TextureFormat::DEPTH_STENCIL;
		}

		static auto isDepthFormat(TextureFormat format)
		{
			return format == TextureFormat::DEPTH;
		}

		static bool isStencilFormat(TextureFormat format)
		{
			return format == TextureFormat::STENCIL;
		}

		inline auto isSampled() const
		{
			return flags & Texture_Sampled;
		}
		inline auto isStorage() const
		{
			return flags & Texture_Storage;
		}
		inline auto isDepthStencil() const
		{
			return flags & Texture_DepthStencil;
		}
		inline auto isRenderTarget() const
		{
			return flags & Texture_RenderTarget;
		}

		virtual auto setName(const std::string &name) -> void
		{
			this->name = name;
		};
		virtual auto getName() const -> const std::string &
		{
			return name;
		};

		virtual auto getResourceType() const -> FileType override
		{
			return FileType::Texture;
		};

		virtual auto getPath() const -> std::string override
		{
			return "";
		}

	  public:
		static auto getStrideFromFormat(TextureFormat format) -> uint8_t;
		static auto bitsToTextureFormat(uint32_t bits) -> TextureFormat;
		static auto calculateMipMapCount(uint32_t width, uint32_t height) -> uint32_t;

	  protected:
		uint16_t    flags = 0;
		std::string name;
	};

	class MAPLE_EXPORT Texture2D : public Texture
	{
	  public:
		virtual auto setData(const void *data) -> void = 0;

	  public:
		static auto  getDefaultTexture() -> std::shared_ptr<Texture2D>;
		static auto  create() -> std::shared_ptr<Texture2D>;
		static auto  create(uint32_t width, uint32_t height, void *data, TextureParameters parameters = TextureParameters(), TextureLoadOptions loadOptions = TextureLoadOptions()) -> std::shared_ptr<Texture2D>;
		static auto  create(const std::string &name, const std::string &filePath, TextureParameters parameters = TextureParameters(), TextureLoadOptions loadOptions = TextureLoadOptions()) -> std::shared_ptr<Texture2D>;
		virtual auto update(int32_t x, int32_t y, int32_t w, int32_t h, const void *buffer) -> void                                                                                              = 0;
		virtual auto buildTexture(TextureFormat internalformat, uint32_t width, uint32_t height, bool srgb = false, bool depth = false, bool samplerShadow = false, bool mipmap = false) -> void = 0;
	};

	class MAPLE_EXPORT TextureCube : public Texture
	{
	  protected:
		enum class InputFormat
		{
			VERTICAL_CROSS,
			HORIZONTAL_CROSS
		};

	  public:
		static auto create(uint32_t size) -> std::shared_ptr<TextureCube>;
		static auto create(uint32_t size, TextureFormat format, int32_t numMips) -> std::shared_ptr<TextureCube>;
		static auto createFromFile(const std::string &filePath) -> std::shared_ptr<TextureCube>;
		static auto createFromFiles(const std::array<std::string, 6> &files) -> std::shared_ptr<TextureCube>;
		static auto createFromVCross(const std::vector<std::string> &files, uint32_t mips, TextureParameters params, TextureLoadOptions loadOptions, InputFormat = InputFormat::VERTICAL_CROSS) -> std::shared_ptr<TextureCube>;

		virtual auto update(CommandBuffer *commandBuffer, FrameBuffer *framebuffer, int32_t cubeIndex, int32_t mipmapLevel = 0) -> void = 0;

		virtual auto generateMipmap() -> void = 0;
	};

	class MAPLE_EXPORT TextureDepth : public Texture
	{
	  public:
		static auto create(uint32_t width, uint32_t height) -> std::shared_ptr<TextureDepth>;

		virtual auto resize(uint32_t width, uint32_t height) -> void = 0;
	};

	class MAPLE_EXPORT TextureDepthArray : public Texture
	{
	  public:
		static auto create(uint32_t width, uint32_t height, uint32_t count) -> std::shared_ptr<TextureDepthArray>;

		virtual auto init() -> void                                                  = 0;
		virtual auto resize(uint32_t width, uint32_t height, uint32_t count) -> void = 0;
		virtual auto getHandleArray(uint32_t index) -> void *
		{
			return getHandle();
		};
	};
}        // namespace maple