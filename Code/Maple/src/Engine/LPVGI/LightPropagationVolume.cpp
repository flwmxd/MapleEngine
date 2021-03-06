//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "LightPropagationVolume.h"
#include "Math/BoundingBox.h"
#include "ReflectiveShadowMap.h"
#include "Scene/Component/BoundingBox.h"
#include "Scene/Component/Transform.h"

#include "Engine/GBuffer.h"
#include "Engine/Mesh.h"
#include "Engine/Renderer/RendererData.h"
#include "Engine/Renderer/ShadowRenderer.h"

#include "Math/BoundingBox.h"
#include "RHI/CommandBuffer.h"
#include "RHI/DescriptorSet.h"
#include "RHI/IndexBuffer.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "RHI/VertexBuffer.h"
#include <ecs/ecs.h>

namespace maple
{
	namespace
	{
		inline auto updateGrid(component::LPVGrid &grid, maple::BoundingBox *box)
		{
			TextureParameters paramemters(TextureFormat::R32UI, TextureFilter::Nearest, TextureWrap::ClampToEdge);
			if (grid.lpvGridB == nullptr)
			{
				grid.lpvGridR = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvGridB = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvGridG = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);

				grid.lpvGeometryVolumeR = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvGeometryVolumeG = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvGeometryVolumeB = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);

				grid.lpvAccumulatorB = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvAccumulatorG = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);
				grid.lpvAccumulatorR = Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters);

				grid.lpvRs.emplace_back(grid.lpvGridR);
				grid.lpvGs.emplace_back(grid.lpvGridG);
				grid.lpvBs.emplace_back(grid.lpvGridB);

				for (auto i = 0; i < grid.propagateCount; i++)
				{
					grid.lpvBs.emplace_back(Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters));
					grid.lpvRs.emplace_back(Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters));
					grid.lpvGs.emplace_back(Texture3D::create(grid.gridDimension.x * 4, grid.gridDimension.y, grid.gridDimension.z, paramemters));
				}
			}
		}
	}        // namespace

	namespace component
	{
		struct InjectLightData
		{
			std::shared_ptr<Shader>                     shader;
			std::vector<std::shared_ptr<DescriptorSet>> descriptors;
			BoundingBox                                 boundingBox;
			InjectLightData()
			{
				shader = Shader::create("shaders/LPV/LightInjection.shader");
				descriptors.emplace_back(DescriptorSet::create({0, shader.get()}));
			}
		};

		struct InjectGeometryVolume
		{
			std::shared_ptr<Shader>                     shader;
			std::vector<std::shared_ptr<DescriptorSet>> descriptors;
			InjectGeometryVolume()
			{
				shader = Shader::create("shaders/LPV/GeometryInjection.shader");
				descriptors.emplace_back(DescriptorSet::create({0, shader.get()}));
			}
		};

		struct PropagationData
		{
			std::shared_ptr<Shader>                     shader;
			std::vector<std::shared_ptr<DescriptorSet>> descriptors;
			PropagationData()
			{
				shader = Shader::create("shaders/LPV/LightPropagation.shader");
				for (auto i = 0; i < LPVGrid::PROPAGATE_COUNT; i++)
				{
					descriptors.emplace_back(DescriptorSet::create({0, shader.get()}));
				}
			}
		};

		struct DebugAABBData
		{
			std::shared_ptr<Shader>                     shader;
			std::vector<std::shared_ptr<DescriptorSet>> descriptors;
			std::shared_ptr<Mesh>                       sphere;
			DebugAABBData()
			{
				shader = Shader::create("shaders/LPV/AABBDebug.shader");
				descriptors.emplace_back(DescriptorSet::create({0, shader.get()}));
				descriptors.emplace_back(DescriptorSet::create({1, shader.get()}));
				sphere = Mesh::createSphere();
			}
		};

	};        // namespace component

	namespace light_propagation_volume
	{
		namespace inject_light_pass
		{
			using Entity = ecs::Registry ::Modify<component::LPVGrid>::To<ecs::Entity>;

			inline auto beginScene(Entity                                          entity,
			                       component::ReflectiveShadowData &               rsm,
			                       component::RendererData &                       render,
			                       component::InjectLightData &                    injectLight,
			                       component::BoundingBoxComponent &               scenAABB,
			                       const global::component::SceneTransformChanged &sceneChanged,
			                       ecs::World                                      world)
			{
				auto [lpv] = entity;

				if (scenAABB.box == nullptr)
					return;

				if (injectLight.boundingBox != *scenAABB.box || sceneChanged.dirty)
				{
					auto size         = scenAABB.box->size();
					auto maxValue     = std::max(size.x, std::max(size.y, size.z));
					lpv.cellSize      = maxValue / lpv.gridSize;
					lpv.gridDimension = size / lpv.cellSize;
					glm::ceil(lpv.gridDimension);
					lpv.gridDimension = glm::min(lpv.gridDimension, {lpv.gridSize, lpv.gridSize, lpv.gridSize});

					updateGrid(lpv, scenAABB.box);

					injectLight.boundingBox.min = scenAABB.box->min;
					injectLight.boundingBox.max = scenAABB.box->max;

					injectLight.descriptors[0]->setUniform("UniformBufferObject", "gridSize", &lpv.gridSize);
					injectLight.descriptors[0]->setUniform("UniformBufferObject", "minAABB", glm::value_ptr(injectLight.boundingBox.min));
					injectLight.descriptors[0]->setUniform("UniformBufferObject", "cellSize", &lpv.cellSize);
				}
			}

			inline auto render(Entity                                          entity,
			                   component::ReflectiveShadowData &               rsm,
			                   component::RendererData &                       rendererData,
			                   component::InjectLightData &                    injectLight,
			                   const global::component::SceneTransformChanged &sceneChanged,
			                   ecs::World                                      world)
			{
				auto [lpv] = entity;

				if (lpv.lpvGridR == nullptr)
					return;

				if (sceneChanged.dirty)
				{
					lpv.lpvGridR->clear(rendererData.commandBuffer);
					lpv.lpvGridG->clear(rendererData.commandBuffer);
					lpv.lpvGridB->clear(rendererData.commandBuffer);

					injectLight.descriptors[0]->setTexture("LPVGridR", lpv.lpvGridR);
					injectLight.descriptors[0]->setTexture("LPVGridG", lpv.lpvGridG);
					injectLight.descriptors[0]->setTexture("LPVGridB", lpv.lpvGridB);
					injectLight.descriptors[0]->setTexture("uFluxSampler", rsm.fluxTexture);
					injectLight.descriptors[0]->setTexture("uRSMWorldSampler", rsm.worldTexture);
					injectLight.descriptors[0]->update(rendererData.commandBuffer);

					PipelineInfo pipelineInfo;
					pipelineInfo.shader      = injectLight.shader;
					pipelineInfo.groupCountX = rsm.normalTexture->getWidth() / injectLight.shader->getLocalSizeX();
					pipelineInfo.groupCountY = rsm.normalTexture->getHeight() / injectLight.shader->getLocalSizeY();
					auto pipeline            = Pipeline::get(pipelineInfo);
					pipeline->bind(rendererData.commandBuffer);
					Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, injectLight.descriptors);
					Renderer::dispatch(rendererData.commandBuffer, pipelineInfo.groupCountX, pipelineInfo.groupCountY, 1);

					lpv.lpvGridR->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);
					lpv.lpvGridG->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);
					lpv.lpvGridB->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);

					pipeline->end(rendererData.commandBuffer);
				}
			}
		};        // namespace inject_light_pass

		namespace inject_geometry_pass
		{
			using Entity = ecs::Registry ::Fetch<component::LPVGrid>::Modify<component::InjectGeometryVolume>::Fetch<component::BoundingBoxComponent>::Fetch<component::ShadowMapData>::Fetch<component::ReflectiveShadowData>::Fetch<component::RendererData>::To<ecs::Entity>;

			inline auto beginScene(Entity                                          entity,
			                       const global::component::SceneTransformChanged &sceneChanged,
			                       ecs::World                                      world)
			{
				auto [lpv, geometry, aabb, shadowData, rsm, rendererData] = entity;
				if (lpv.lpvGridR == nullptr || !sceneChanged.dirty)
					return;
				geometry.descriptors[0]->setUniform("UniformBufferObject", "lightViewMat", glm::value_ptr(rsm.lightMatrix));
				geometry.descriptors[0]->setUniform("UniformBufferObject", "minAABB", glm::value_ptr(aabb.box->min));
				geometry.descriptors[0]->setUniform("UniformBufferObject", "cellSize", &lpv.cellSize);
				geometry.descriptors[0]->setUniform("UniformBufferObject", "lightDir", glm::value_ptr(shadowData.lightDir));
				geometry.descriptors[0]->setUniform("UniformBufferObject", "rsmArea", &rsm.lightArea);
			}

			inline auto render(Entity                                          entity,
			                   const global::component::SceneTransformChanged &sceneChanged,
			                   ecs::World                                      world)
			{
				auto [lpv, geometry, aabb, shadowData, rsm, rendererData] = entity;
				if (lpv.lpvGridR == nullptr || !sceneChanged.dirty)
					return;

				auto cmdBuffer = rendererData.commandBuffer;

				lpv.lpvGeometryVolumeR->clear(rendererData.commandBuffer);
				lpv.lpvGeometryVolumeG->clear(rendererData.commandBuffer);
				lpv.lpvGeometryVolumeB->clear(rendererData.commandBuffer);

				geometry.descriptors[0]->setTexture("uGeometryVolumeR", lpv.lpvGeometryVolumeR);
				geometry.descriptors[0]->setTexture("uGeometryVolumeG", lpv.lpvGeometryVolumeG);
				geometry.descriptors[0]->setTexture("uGeometryVolumeB", lpv.lpvGeometryVolumeB);
				geometry.descriptors[0]->setTexture("uRSMNormalSampler", rsm.normalTexture);
				geometry.descriptors[0]->setTexture("uRSMWorldSampler", rsm.worldTexture);
				geometry.descriptors[0]->setTexture("uFluxSampler", rsm.fluxTexture);

				geometry.descriptors[0]->update(cmdBuffer);

				PipelineInfo pipelineInfo;
				pipelineInfo.shader      = geometry.shader;
				pipelineInfo.groupCountX = rsm.normalTexture->getWidth() / geometry.shader->getLocalSizeX();
				pipelineInfo.groupCountY = rsm.normalTexture->getHeight() / geometry.shader->getLocalSizeY();
				auto pipeline            = Pipeline::get(pipelineInfo);
				pipeline->bind(cmdBuffer);
				Renderer::bindDescriptorSets(pipeline.get(), cmdBuffer, 0, geometry.descriptors);
				Renderer::dispatch(cmdBuffer, pipelineInfo.groupCountX, pipelineInfo.groupCountY, 1);

				lpv.lpvGeometryVolumeR->memoryBarrier(cmdBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);
				lpv.lpvGeometryVolumeG->memoryBarrier(cmdBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);
				lpv.lpvGeometryVolumeB->memoryBarrier(cmdBuffer, MemoryBarrierFlags::Shader_Storage_Barrier);

				pipeline->end(cmdBuffer);
			}
		};        // namespace inject_geometry_pass

		namespace propagation_pass
		{
			using Entity = ecs::Registry ::Fetch<component::LPVGrid>::Modify<component::PropagationData>::Fetch<component::BoundingBoxComponent>::Modify<component::RendererData>::To<ecs::Entity>;

			inline auto beginScene(Entity                                          entity,
			                       const global::component::SceneTransformChanged &sceneChanged,
			                       ecs::World                                      world)
			{
				auto [lpv, data, aabb, renderData] = entity;
				if (lpv.lpvGridR == nullptr || !sceneChanged.dirty)
					return;
				data.descriptors[0]->setUniform("UniformObject", "gridDim", glm::value_ptr(aabb.box->size()));
				data.descriptors[0]->setUniform("UniformObject", "occlusionAmplifier", &lpv.occlusionAmplifier);
			}

			inline auto render(Entity                                          entity,
			                   const global::component::SceneTransformChanged &sceneChanged,
			                   ecs::World                                      world)
			{
				auto [lpv, data, aabb, rendererData] = entity;
				if (lpv.lpvGridR == nullptr || !sceneChanged.dirty)
					return;

				lpv.lpvAccumulatorR->clear(rendererData.commandBuffer);
				lpv.lpvAccumulatorG->clear(rendererData.commandBuffer);
				lpv.lpvAccumulatorB->clear(rendererData.commandBuffer);

				for (auto i = 1; i <= lpv.propagateCount; i++)
				{
					lpv.lpvBs[i]->clear(rendererData.commandBuffer);
					lpv.lpvRs[i]->clear(rendererData.commandBuffer);
					lpv.lpvGs[i]->clear(rendererData.commandBuffer);
				}

				for (auto i = 1; i <= lpv.propagateCount; i++)
				{
					data.descriptors[i - 1]->setTexture("uGeometryVolumeR", lpv.lpvGeometryVolumeR);
					data.descriptors[i - 1]->setTexture("uGeometryVolumeG", lpv.lpvGeometryVolumeG);
					data.descriptors[i - 1]->setTexture("uGeometryVolumeB", lpv.lpvGeometryVolumeB);
					data.descriptors[i - 1]->setTexture("RAccumulatorLPV_", lpv.lpvAccumulatorR);
					data.descriptors[i - 1]->setTexture("GAccumulatorLPV_", lpv.lpvAccumulatorG);
					data.descriptors[i - 1]->setTexture("BAccumulatorLPV_", lpv.lpvAccumulatorB);

					data.descriptors[i - 1]->setTexture("LPVGridR", lpv.lpvRs[i - 1]);
					data.descriptors[i - 1]->setTexture("LPVGridG", lpv.lpvGs[i - 1]);
					data.descriptors[i - 1]->setTexture("LPVGridB", lpv.lpvBs[i - 1]);
					data.descriptors[i - 1]->setTexture("LPVGridR_", lpv.lpvRs[i]);
					data.descriptors[i - 1]->setTexture("LPVGridG_", lpv.lpvGs[i]);
					data.descriptors[i - 1]->setTexture("LPVGridB_", lpv.lpvBs[i]);
					data.descriptors[i - 1]->setUniform("UniformObject", "step", &i);
					data.descriptors[i - 1]->update(rendererData.commandBuffer);
				}

				PipelineInfo pipelineInfo;
				pipelineInfo.shader      = data.shader;
				pipelineInfo.groupCountX = lpv.gridSize / data.shader->getLocalSizeX();
				pipelineInfo.groupCountY = lpv.gridSize / data.shader->getLocalSizeY();
				pipelineInfo.groupCountZ = lpv.gridSize / data.shader->getLocalSizeZ();
				auto pipeline            = Pipeline::get(pipelineInfo);
				pipeline->bind(rendererData.commandBuffer);
				for (auto i = 1; i <= lpv.propagateCount; i++)
				{
					//wait
					Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, {data.descriptors[i - 1]});
					Renderer::dispatch(rendererData.commandBuffer, pipelineInfo.groupCountX, pipelineInfo.groupCountY, pipelineInfo.groupCountZ);

					lpv.lpvAccumulatorR->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Image_Access_Barrier);
					lpv.lpvAccumulatorG->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Image_Access_Barrier);
					lpv.lpvAccumulatorB->memoryBarrier(rendererData.commandBuffer, MemoryBarrierFlags::Shader_Image_Access_Barrier);
				}
				pipeline->end(rendererData.commandBuffer);
			}
		};        // namespace propagation_pass

		namespace aabb_debug
		{
			using Entity = ecs::Registry ::Fetch<component::LPVGrid>::Modify<component::DebugAABBData>::Fetch<component::BoundingBoxComponent>::Modify<component::RendererData>::Fetch<component::CameraView>::To<ecs::Entity>;

			inline auto beginScene(Entity entity, ecs::World world)
			{
				auto [lpv, data, aabb, renderData, cameraView] = entity;
				if (lpv.lpvGridR == nullptr || !lpv.debugAABB)
					return;

				data.descriptors[0]->setUniform("UniformBufferObjectVert", "projView", glm::value_ptr(cameraView.projView));

				data.descriptors[1]->setUniform("UniformBufferObjectFrag", "minAABB", glm::value_ptr(aabb.box->min));
				data.descriptors[1]->setUniform("UniformBufferObjectFrag", "cellSize", &lpv.cellSize);
			}

			inline auto render(Entity entity, ecs::World world)
			{
				auto [lpv, data, aabb, renderData, cameraView] = entity;
				if (lpv.lpvGridR == nullptr || !lpv.debugAABB)
					return;

				if (lpv.showGeometry)
				{
					data.descriptors[1]->setTexture("uRAccumulatorLPV", lpv.lpvGeometryVolumeR);
					data.descriptors[1]->setTexture("uGAccumulatorLPV", lpv.lpvGeometryVolumeG);
					data.descriptors[1]->setTexture("uBAccumulatorLPV", lpv.lpvGeometryVolumeB);
				}
				else
				{
					data.descriptors[1]->setTexture("uRAccumulatorLPV", lpv.lpvAccumulatorR);
					data.descriptors[1]->setTexture("uGAccumulatorLPV", lpv.lpvAccumulatorG);
					data.descriptors[1]->setTexture("uBAccumulatorLPV", lpv.lpvAccumulatorB);
				}

				for (auto descriptor : data.descriptors)
				{
					descriptor->update(renderData.commandBuffer);
				}

				auto min = aabb.box->min;
				auto max = aabb.box->max;

				PipelineInfo pipelineInfo{};
				pipelineInfo.shader          = data.shader;
				pipelineInfo.polygonMode     = PolygonMode::Fill;
				pipelineInfo.blendMode       = BlendMode::SrcAlphaOneMinusSrcAlpha;
				pipelineInfo.clearTargets    = false;
				pipelineInfo.swapChainTarget = false;
				pipelineInfo.depthTarget     = renderData.gbuffer->getDepthBuffer();
				pipelineInfo.colorTargets[0] = renderData.gbuffer->getBuffer(GBufferTextures::SCREEN);

				auto pipeline = Pipeline::get(pipelineInfo);

				if (renderData.commandBuffer)
					renderData.commandBuffer->bindPipeline(pipeline.get());
				else
					pipeline->bind(renderData.commandBuffer);

				const auto r = 0.1 * lpv.cellSize;

				for (float i = min.x; i < max.x; i += lpv.cellSize)
				{
					for (float j = min.y; j < max.y; j += lpv.cellSize)
					{
						for (float k = min.z; k < max.z; k += lpv.cellSize)
						{
							glm::mat4 model = glm::mat4(1);
							model           = glm::translate(model, glm::vec3(i, j, k));
							model           = glm::scale(model, glm::vec3(r, r, r));

							auto &pushConstants = data.shader->getPushConstants()[0];
							pushConstants.setValue("transform", &model);
							data.shader->bindPushConstants(renderData.commandBuffer, pipeline.get());

							Renderer::bindDescriptorSets(pipeline.get(), renderData.commandBuffer, 0, data.descriptors);
							Renderer::drawMesh(renderData.commandBuffer, pipeline.get(), data.sphere.get());
						}
					}
				}

				if (renderData.commandBuffer)
					renderData.commandBuffer->unbindPipeline();
				else if (pipeline)
					pipeline->end(renderData.commandBuffer);
			}
		}        // namespace aabb_debug

		auto registerGlobalComponent(std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerGlobalComponent<component::LPVGrid>();
			executePoint->registerGlobalComponent<component::InjectLightData>();
			executePoint->registerGlobalComponent<component::InjectGeometryVolume>();
			executePoint->registerGlobalComponent<component::PropagationData>();
			executePoint->registerGlobalComponent<component::DebugAABBData>();
		}

		auto registerLPV(ExecuteQueue &begin, ExecuteQueue &renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerWithinQueue<inject_light_pass::beginScene>(begin);
			executePoint->registerWithinQueue<inject_light_pass::render>(renderer);
			executePoint->registerWithinQueue<inject_geometry_pass::beginScene>(begin);
			executePoint->registerWithinQueue<inject_geometry_pass::render>(renderer);

			executePoint->registerWithinQueue<propagation_pass::beginScene>(begin);
			executePoint->registerWithinQueue<propagation_pass::render>(renderer);
		}

		auto registerLPVDebug(ExecuteQueue &begin, ExecuteQueue &renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerWithinQueue<aabb_debug::beginScene>(begin);
			executePoint->registerWithinQueue<aabb_debug::render>(renderer);
		}
	};        // namespace light_propagation_volume
};            // namespace maple
