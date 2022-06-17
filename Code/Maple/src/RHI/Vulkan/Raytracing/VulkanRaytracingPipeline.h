//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#pragma once

#include "RHI/Vulkan/VulkanPipeline.h"
#include "RHI/Vulkan/VulkanHelper.h"
#include "Engine/Core.h"

#include <functional>
#include <memory>

namespace maple
{
	class VulkanRaytracingPipeline : public VulkanPipeline
	{
	public:
		constexpr static uint32_t MAX_DESCRIPTOR_SET = 1500;
		VulkanRaytracingPipeline(const PipelineInfo& info);
		NO_COPYABLE(VulkanRaytracingPipeline);

		auto init(const PipelineInfo& info) -> bool;

		inline auto getWidth()->uint32_t override { return 0; }
		inline auto getHeight()->uint32_t override { return 0; }

		auto bind(const CommandBuffer* commandBuffer, uint32_t layer = 0, int32_t cubeFace = -1, int32_t mipMapLevel = 0)->FrameBuffer* override;
		auto end(const CommandBuffer* commandBuffer) -> void override {};
		auto clearRenderTargets(const CommandBuffer* commandBuffer) -> void override {};

		inline auto getPipelineBindPoint() const -> VkPipelineBindPoint override {	return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;	}
	};
};        // namespace maple