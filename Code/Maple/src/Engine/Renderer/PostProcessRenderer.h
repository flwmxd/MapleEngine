//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once
#include <memory>
#include "Scene/System/ExecutePoint.h"

namespace maple
{
	class Shader;
	class DescriptorSet;

	namespace component
	{
		struct SSAOData
		{
			std::shared_ptr<Shader>                     ssaoShader;
			std::shared_ptr<Shader>                     ssaoBlurShader;
			std::vector<std::shared_ptr<DescriptorSet>> ssaoSet;
			std::vector<std::shared_ptr<DescriptorSet>> ssaoBlurSet;
			bool  enable = false;
			float bias = 0.025;
			float ssaoRadius = 0.25f;
		};

		struct SSRData
		{
			bool                           enable = false;
			std::shared_ptr<DescriptorSet> ssrDescriptorSet;
			std::shared_ptr<Shader>        ssrShader;
		};
	};

	namespace post_process
	{
		auto registerSSAOPass(ExecuteQueue& begin, ExecuteQueue& renderer, std::shared_ptr<ExecutePoint> executePoint) -> void;
		auto registerSSR(ExecuteQueue& renderer, std::shared_ptr<ExecutePoint> executePoint) -> void;
	};
}        // namespace maple
