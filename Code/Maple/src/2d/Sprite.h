//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Engine/Quad2D.h"
#include "Scene/Component/Component.h"
#include <cereal/cereal.hpp>
#include <glm/glm.hpp>

namespace maple
{
	class Texture2D;
	namespace component
	{
		class MAPLE_EXPORT Sprite
		{
		  public:
			Sprite();
			Sprite(const std::string &uniqueName, const std::vector<uint8_t> &data, uint32_t width, uint32_t height);
			virtual ~Sprite();

			auto setTextureFromFile(const std::string &filePath) -> void;
			auto loadQuad(const std::string &path) -> void;

			virtual auto getQuad() -> const Quad2D &
			{
				return quad;
			}

			template <typename Archive>
			auto save(Archive &archive) const -> void
			{
				std::string newPath = "";
				archive(
				    cereal::make_nvp("TexturePath", getTexturePath()), );
			}

			template <typename Archive>
			auto load(Archive &archive) -> void
			{
				std::string textureFilePath;
				archive(
				    cereal::make_nvp("TexturePath", textureFilePath));

				if (!textureFilePath.empty())
					loadQuad(textureFilePath);
			}

			inline auto getWidth() const
			{
				return quad.getWidth();
			}
			inline auto getHeight() const
			{
				return quad.getHeight();
			}

			auto getTexturePath() const -> const std::string &;

		  protected:
			Quad2D quad;
		};

		class MAPLE_EXPORT AnimatedSprite : public Sprite
		{
		  public:
			struct AnimationFrame
			{
				uint32_t    width;
				uint32_t    height;
				float       delay;
				std::string uniqueKey;
				Quad2D      quad;
			};

			AnimatedSprite();
			virtual ~AnimatedSprite() = default;

			auto addFrame(const std::vector<uint8_t> &data, uint32_t width, uint32_t height, float delay, const std::string &uniqueKey, float xOffset, float yOffset, bool flipY = false, uint32_t color = UINT32_MAX) -> void;
			auto onUpdate(float dt) -> void;
			auto getAnimatedUVs() -> const std::array<glm::vec2, 4> &;
			auto getQuad() -> const Quad2D & override;

			inline auto getCurrentFrame() const -> const AnimationFrame *
			{
				if (currentFrame < animationFrames.size())
				{
					return &animationFrames[currentFrame];
				}
				return nullptr;
			};

			inline auto getDelay() const
			{
				auto frame = getCurrentFrame();
				return frame ? frame->delay : 0;
			};

			inline auto getFrames() const -> int32_t
			{
				return animationFrames.size();
			};

			inline auto getWidth() const
			{
				auto frame = getCurrentFrame();
				return frame ? frame->width : 0;
			}

			inline auto getHeight() const
			{
				auto frame = getCurrentFrame();
				return frame ? frame->height : 0;
			}

			float                       frameTimer   = 0.0f;
			uint32_t                    currentFrame = 0;
			bool                        loop         = true;
			std::vector<AnimationFrame> animationFrames;
		};
	};        // namespace component
}        // namespace maple
