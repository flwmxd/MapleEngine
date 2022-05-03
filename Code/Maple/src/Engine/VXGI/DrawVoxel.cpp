//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#include "DrawVoxel.h"
#include "Voxelization.h"

#include "Scene/System/ExecutePoint.h"
#include "Scene/Component/BoundingBox.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/RendererData.h"
#include "Engine/GBuffer.h"
#include "RHI/Shader.h"
#include "RHI/Pipeline.h"

#include "Application.h"

namespace maple
{
	namespace vxgi_debug
	{
		namespace global::component
		{
			struct DrawVoxelPipeline
			{
				std::shared_ptr<Shader> shader;
				std::vector<std::shared_ptr<DescriptorSet>> descriptors;
				struct 
				{
					glm::mat4 mvp;
					glm::vec4 frustumPlanes[6];
					glm::vec3 worldMinPoint;
					float voxelSize;
				}ubo;
			};
		}

		namespace draw_voxel 
		{
			using Entity = ecs::Chain
				::Write<component::Voxelization>
				::To<ecs::Entity>;

			inline auto system(Entity entity,
				component::RendererData & rendererData,
				global::component::DrawVoxelPipeline& pipline,
				const component::CameraView & cameraView,
				const global::component::DrawVoxelRender & render,
				const maple::global::component::VoxelBuffer& voxelBuffer,
				const component::BoundingBoxComponent & box)
			{
				auto [voxel] = entity;

				if (render.enable && box.box)
				{
					auto drawMipLevel = 0;

					auto vDimension = static_cast<uint32_t>(
						component::Voxelization::voxelDimension / std::pow(2.0f, drawMipLevel)
					);

					auto vSize = voxel.volumeGridSize / vDimension;
					auto model = glm::translate(glm::mat4(1.f), box.box->min) * glm::scale(glm::mat4(1.f), glm::vec3(vSize));
					pipline.ubo.mvp = cameraView.projView * model;
					pipline.ubo.voxelSize = voxel.voxelSize;
					pipline.ubo.worldMinPoint = box.box->min;

					for (auto i = 0;i<6;i++)
					{
						pipline.ubo.frustumPlanes[i] = cameraView.frustum.getPlane(i);
					}

					pipline.descriptors[0]->setTexture("uVoxelBuffer", voxelBuffer.voxelVolume[render.id]);
					pipline.descriptors[0]->setUniform("UniformBufferObjectVert", "volumeDimension", &vDimension);
					pipline.descriptors[0]->update();
					pipline.descriptors[1]->setUniformBufferData("UniformBufferObjectGemo", &pipline.ubo);
					pipline.descriptors[1]->update();

					Application::getRenderDevice()->clearRenderTarget(rendererData.gbuffer->getDepthBuffer(), rendererData.commandBuffer);

					PipelineInfo pipelineInfo;
					pipelineInfo.shader = pipline.shader;
					pipelineInfo.polygonMode = PolygonMode::Fill;
					pipelineInfo.cullMode = CullMode::Back;
					pipelineInfo.blendMode = BlendMode::SrcAlphaOneMinusSrcAlpha;
					pipelineInfo.clearTargets = true;
					pipelineInfo.colorTargets[0] = rendererData.gbuffer->getBuffer(GBufferTextures::SCREEN);
					pipelineInfo.depthTarget = rendererData.gbuffer->getDepthBuffer();
					pipelineInfo.clearColor = {	0,0,0,1.f };
					pipelineInfo.depthTest = true;

					auto pipeline = Pipeline::get(pipelineInfo);
					pipeline->bind(rendererData.commandBuffer);
					Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, pipline.descriptors);
					Renderer::drawArrays(rendererData.commandBuffer, DrawType::Point, component::Voxelization::voxelVolume);
					pipeline->end(rendererData.commandBuffer);

				}
			}
		}

		auto registerVXGIVisualization(ExecuteQueue& begin, ExecuteQueue& renderer, std::shared_ptr<ExecutePoint> point) -> void
		{
			point->registerGlobalComponent<global::component::DrawVoxelRender>();
			point->registerGlobalComponent<global::component::DrawVoxelPipeline>([](auto & pipline) {
				pipline.shader = Shader::create("shaders/VXGI/DrawVoxels.shader");
				pipline.descriptors.emplace_back(
					DescriptorSet::create({ 0, pipline.shader.get() })
				);
				pipline.descriptors.emplace_back(
					DescriptorSet::create({ 1, pipline.shader.get() })
				);
			});
			point->registerWithinQueue<draw_voxel::system>(renderer);
		}
	}
};