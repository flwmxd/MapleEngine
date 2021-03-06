//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////
#include "ShadowRenderer.h"
#include "RHI/CommandBuffer.h"
#include "RHI/DescriptorSet.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

#include "Scene/Component/BoundingBox.h"
#include "Scene/Component/Light.h"
#include "Scene/Component/MeshRenderer.h"
#include "Scene/Scene.h"

#include "Engine/Camera.h"
#include "Engine/CaptureGraph.h"
#include "Engine/Core.h"
#include "Engine/Material.h"
#include "Engine/Mesh.h"
#include "Engine/PathTracer/PathIntegrator.h"
#include "Engine/Profiler.h"
#include "Engine/Renderer/GeometryRenderer.h"
#include "Engine/Renderer/RendererData.h"

#include "Engine/LPVGI/ReflectiveShadowMap.h"

#include <ecs/ecs.h>

namespace maple
{
	namespace        //private block
	{
		inline auto updateCascades(const component::CameraView &camera, component::ShadowMapData &shadowData, component::Light *light)
		{
			PROFILE_FUNCTION();

			float cascadeSplits[SHADOWMAP_MAX];

			const float nearClip  = camera.nearPlane;
			const float farClip   = camera.farPlane;
			const float clipRange = farClip - nearClip;
			const float minZ      = nearClip;
			const float maxZ      = nearClip + clipRange;
			const float range     = maxZ - minZ;
			const float ratio     = maxZ / minZ;
			// Calculate split depths based on view camera frustum
			// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
			for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
			{
				float p          = static_cast<float>(i + 1) / static_cast<float>(shadowData.shadowMapNum);
				float log        = minZ * std::pow(ratio, p);
				float uniform    = minZ + range * p;
				float d          = shadowData.cascadeSplitLambda * (log - uniform) + uniform;
				cascadeSplits[i] = (d - nearClip) / clipRange;
			}

			float lastSplitDist = 0.0;
			for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
			{
				PROFILE_SCOPE("Create Cascade");
				float splitDist = cascadeSplits[i];

				auto frum = camera.frustum;

				glm::vec3 *frustumCorners = frum.getVertices();

				for (uint32_t i = 0; i < 4; i++)
				{
					glm::vec3 dist        = frustumCorners[i + 4] - frustumCorners[i];
					frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
					frustumCorners[i]     = frustumCorners[i] + (dist * lastSplitDist);
				}

				glm::vec3 frustumCenter = glm::vec3(0.0f);
				for (uint32_t i = 0; i < 8; i++)
				{
					frustumCenter += frustumCorners[i];
				}
				frustumCenter /= 8.0f;

				float radius = 0.0f;
				for (uint32_t i = 0; i < 8; i++)
				{
					float distance = glm::length(frustumCorners[i] - frustumCenter);
					radius         = glm::max(radius, distance);
				}
				radius = std::ceil(radius * 16.0f) / 16.0f;

				glm::vec3 maxExtents = glm::vec3(radius);
				glm::vec3 minExtents = -maxExtents;

				glm::vec3 lightDir         = glm::normalize(glm::vec3(light->lightData.direction));
				glm::mat4 lightViewMatrix  = glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, maple::UP);
				glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, -(maxExtents.z - minExtents.z), maxExtents.z - minExtents.z);

				shadowData.splitDepth[i]     = glm::vec4(camera.nearPlane + splitDist * clipRange) * -1.f;
				shadowData.shadowProjView[i] = lightOrthoMatrix * lightViewMatrix;
				if (i == 0)
				{
					shadowData.lightMatrix = lightViewMatrix;
					shadowData.lightDir    = lightDir;
				}

				lastSplitDist = cascadeSplits[i];
			}
		}
	}        // namespace

	namespace shadow_map_pass
	{
		using Entity = ecs::Registry ::Modify<component::ShadowMapData>::Fetch<component::CameraView>::OptinalModify<component::ReflectiveShadowData>::To<ecs::Entity>;

		using LightQuery = ecs::Registry ::Modify<component::Light>::To<ecs::Group>;

		using MeshQuery = ecs::Registry ::Modify<component::MeshRenderer>::Modify<component::Transform>::To<ecs::Group>;

		using MeshEntity = ecs::Registry ::Modify<component::MeshRenderer>::Modify<component::Transform>::To<ecs::Entity>;

		using BoneMeshQuery = ecs::Registry ::Modify<component::BoneComponent>::Modify<component::Transform>::To<ecs::Group>;

		using SkinnedMeshQuery = ecs::Registry ::Modify<component::SkinnedMeshRenderer>::Modify<component::Transform>::To<ecs::Group>;

		using PathTraceGroup = ecs::Registry::Fetch<component::PathIntegrator>::To<ecs::Group>;

		auto beginScene(Entity entity, LightQuery lightQuery, MeshQuery meshQuery, SkinnedMeshQuery skinnedQuery, BoneMeshQuery boneQuery,
		                const global::component::SceneTransformChanged &sceneChanged,
		                PathTraceGroup                                  pathGroup,
		                ecs::World                                      world)
		{
			auto [shadowData, cameraView] = entity;

			for (auto ent : pathGroup)
			{
				auto [path] = pathGroup.convert(ent);
				if (path.enable)
					return;
			}

			if (sceneChanged.dirty || shadowData.dirty)
			{
				shadowData.dirty = false;
				for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
				{
					shadowData.cascadeCommandQueue[i].clear();
				}

				shadowData.animationQueue.clear();

				if (!lightQuery.empty())
				{
					component::Light *directionaLight = nullptr;

					for (auto entity : lightQuery)
					{
						auto [light] = lightQuery.convert(entity);
						if (static_cast<component::LightType>(light.lightData.type) == component::LightType::DirectionalLight)
						{
							directionaLight = &light;
							break;
						}
					}

					if (directionaLight && directionaLight->castShadow)
					{
						if (entity.hasComponent<component::ReflectiveShadowData>())
						{
							auto &rsm = entity.getComponent<component::ReflectiveShadowData>();
							rsm.descriptorSets[2]->setUniform("LightUBO", "light", &directionaLight->lightData);
						}

						if (directionaLight)
						{
							updateCascades(cameraView, shadowData, directionaLight);

							for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
							{
								shadowData.cascadeFrustums[i].from(shadowData.shadowProjView[i]);
							}
						}
#pragma omp parallel for num_threads(4)
						for (int32_t i = 0; i < shadowData.shadowMapNum; i++)
						{
							meshQuery.forEach([&, i](MeshEntity meshEntity) {
								auto [mesh, trans] = meshEntity;
								if (mesh.castShadow && mesh.active && mesh.mesh != nullptr)
								{
									auto bb     = mesh.mesh->getBoundingBox()->transform(trans.getWorldMatrix());
									auto inside = shadowData.cascadeFrustums[i].isInside(bb);
									if (inside)
									{
										auto &cmd     = shadowData.cascadeCommandQueue[i].emplace_back();
										cmd.mesh      = mesh.mesh.get();
										cmd.transform = trans.getWorldMatrix();
									}
								}
							});
						}

						for (auto skinEntity : skinnedQuery)
						{
							auto [mesh, trans] = skinnedQuery.convert(skinEntity);

							if (mesh.castShadow && mesh.mesh != nullptr)
							{
								auto bb     = mesh.mesh->getBoundingBox()->transform(trans.getWorldMatrix());
								auto inside = shadowData.cascadeFrustums[0].isInside(bb);
								if (inside)
								{
									auto &cmd          = shadowData.animationQueue.emplace_back();
									cmd.mesh           = mesh.mesh.get();
									cmd.transform      = trans.getWorldMatrix();
									cmd.boneTransforms = mesh.boneTransforms;
								}
							}
						}

						shadowData.descriptorSet[0]->setUniform("UniformBufferObject", "projView", shadowData.shadowProjView);
						shadowData.animDescriptorSet[0]->setUniform("UniformBufferObject", "projView", shadowData.shadowProjView);
					}
				}
			}
		}

		using RenderEntity = ecs::Registry ::Modify<component::ShadowMapData>::Fetch<component::RendererData>::Modify<capture_graph::component::RenderGraph>::To<ecs::Entity>;

		inline auto onRender(RenderEntity                                    entity,
		                     PathTraceGroup                                  pathGroup,
		                     const global::component::SceneTransformChanged &sceneChanged,
		                     ecs::World                                      world)
		{
			auto [shadowData, rendererData, renderGraph] = entity;

			for (auto ent : pathGroup)
			{
				auto [path] = pathGroup.convert(ent);
				if (path.enable)
					return;
			}

			if (sceneChanged.dirty)
			{
				shadowData.descriptorSet[0]->update(rendererData.commandBuffer);

				PipelineInfo pipelineInfo;
				pipelineInfo.shader = shadowData.shader;

				pipelineInfo.cullMode            = CullMode::Back;
				pipelineInfo.transparencyEnabled = false;
				pipelineInfo.depthBiasEnabled    = false;
				pipelineInfo.depthArrayTarget    = shadowData.shadowTexture;
				pipelineInfo.clearTargets        = true;
				pipelineInfo.depthTest           = true;
				pipelineInfo.clearColor          = {0, 0, 0, 0};
				pipelineInfo.pipelineName        = "ShadowMapping";

				auto pipeline = Pipeline::get(pipelineInfo, shadowData.descriptorSet, renderGraph);

				for (uint32_t i = 0; i < shadowData.shadowMapNum; ++i)
				{
					//GPUProfile("Shadow Layer Pass");
					pipeline->bind(rendererData.commandBuffer, i);

					for (auto &command : shadowData.cascadeCommandQueue[i])
					{
						Mesh *      mesh          = command.mesh;
						const auto &trans         = command.transform;
						auto &      pushConstants = shadowData.shader->getPushConstants()[0];

						pushConstants.setValue("transform", (void *) &trans);
						pushConstants.setValue("cascadeIndex", (void *) &i);

						shadowData.shader->bindPushConstants(rendererData.commandBuffer, pipeline.get());

						Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, shadowData.descriptorSet);
						Renderer::drawMesh(rendererData.commandBuffer, pipeline.get(), mesh);
					}
					pipeline->end(rendererData.commandBuffer);
				}
			}
		}

		inline auto onRenderAnim(RenderEntity                                    entity,
		                         PathTraceGroup                                  pathGroup,
		                         const global::component::SceneTransformChanged &sceneChanged,
		                         ecs::World                                      world)
		{
			auto [shadowData, rendererData, renderGraph] = entity;

			for (auto ent : pathGroup)
			{
				auto [path] = pathGroup.convert(ent);
				if (path.enable)
					return;
			}

			if (sceneChanged.dirty)
			{
				shadowData.animDescriptorSet[0]->update(rendererData.commandBuffer);
				PipelineInfo pipelineInfo;
				pipelineInfo.shader              = shadowData.animShader;
				pipelineInfo.cullMode            = CullMode::Back;
				pipelineInfo.transparencyEnabled = false;
				pipelineInfo.depthBiasEnabled    = false;
				pipelineInfo.depthArrayTarget    = shadowData.shadowTexture;
				pipelineInfo.clearTargets        = false;
				pipelineInfo.pipelineName        = "ShadowMapping";

				auto pipeline = Pipeline::get(pipelineInfo, shadowData.animDescriptorSet, renderGraph);

				pipeline->bind(rendererData.commandBuffer);

				for (auto &command : shadowData.animationQueue)
				{
					Mesh *mesh = command.mesh;

					if (command.boneTransforms != nullptr)
					{
						shadowData.animDescriptorSet[0]->setUniform("UniformBufferObject", "boneTransforms", command.boneTransforms.get());
						shadowData.animDescriptorSet[0]->update(rendererData.commandBuffer);

						const auto &trans         = command.transform;
						auto &      pushConstants = shadowData.animShader->getPushConstants()[0];

						pushConstants.setValue("transform", (void *) &trans);

						shadowData.animShader->bindPushConstants(rendererData.commandBuffer, pipeline.get());

						Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, shadowData.animDescriptorSet);
						Renderer::drawMesh(rendererData.commandBuffer, pipeline.get(), mesh);
					}
				}
				pipeline->end(rendererData.commandBuffer);
			}
		}
	}        // namespace shadow_map_pass

	namespace shadow_map
	{
		auto registerShadowMap(ExecuteQueue &begin, ExecuteQueue &renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerGlobalComponent<component::ShadowMapData, component::RendererData>([](component::ShadowMapData &data,
			                                                                                            component::RendererData & renderData) {
				data.shadowTexture = TextureDepthArray::create(SHADOWMAP_SiZE_MAX, SHADOWMAP_SiZE_MAX, data.shadowMapNum, renderData.commandBuffer);
				data.shader        = Shader::create("shaders/Shadow.shader");
				data.animShader    = Shader::create("shaders/ShadowAnim.shader");

				data.descriptorSet.resize(1);
				data.animDescriptorSet.resize(1);

				data.descriptorSet[0]     = DescriptorSet::create({0, data.shader.get()});
				data.animDescriptorSet[0] = DescriptorSet::create({0, data.animShader.get()});

				data.animationQueue.reserve(50);

				data.cascadeCommandQueue[0].reserve(500);
				data.cascadeCommandQueue[1].reserve(500);
				data.cascadeCommandQueue[2].reserve(500);
				data.cascadeCommandQueue[3].reserve(500);

				data.shadowTexture->setName("uShaderMapSampler");
			});

			executePoint->registerWithinQueue<shadow_map_pass::beginScene>(begin);
			executePoint->registerWithinQueue<shadow_map_pass::onRender>(renderer);
			executePoint->registerWithinQueue<shadow_map_pass::onRenderAnim>(renderer);
		}
	};        // namespace shadow_map
}        // namespace maple
