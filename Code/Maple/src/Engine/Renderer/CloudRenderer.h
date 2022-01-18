//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Renderer.h"

namespace maple
{
	class CloudRenderer : public Renderer
	{
	  public:
		virtual ~CloudRenderer();
		auto init(const std::shared_ptr<GBuffer> &buffer) -> void override;
		auto renderScene() -> void override;
		auto beginScene(Scene *scene, const glm::mat4 &projView) -> void override;
		auto onResize(uint32_t width, uint32_t height) -> void override;
	  private:
		struct RenderData;
		RenderData *data = nullptr;
	};
}        // namespace maple
