//////////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "ReflectiveShadowMap.h"

#include "Engine/Camera.h"
#include "Engine/Core.h"
#include "Engine/Material.h"
#include "Engine/Profiler.h"
#include "Engine/Mesh.h"

#include "ImGui/ImGuiHelpers.h"
#include "Math/Frustum.h"
#include "Math/MathUtils.h"

#include "Scene/Component/Light.h"
#include "Scene/Scene.h"
#include "Scene/Component/MeshRenderer.h"

#include "RHI/CommandBuffer.h"
#include "RHI/DescriptorSet.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"

#include "Engine/Renderer/RendererData.h"

#include <ecs/ecs.h>

namespace maple
{
	namespace        //private block
	{
		inline auto updateCascades(const component::CameraView & camera, component::ShadowMapData& shadowData, Light *light)
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

			for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
			{
				PROFILE_SCOPE("Create Cascade");
				float splitDist     = cascadeSplits[i];
				float lastSplitDist = cascadeSplits[i];

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

				glm::vec3 lightDir         = glm::normalize(glm::vec3(light->lightData.direction) * -1.f);
				glm::mat4 lightViewMatrix  = glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, maple::UP);
				glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

				shadowData.splitDepth[i]     = glm::vec4(camera.nearPlane + splitDist * clipRange) * -1.f;
				shadowData.shadowProjView[i] = lightOrthoMatrix * lightViewMatrix;

				if (i == 0)
					shadowData.lightMatrix = lightViewMatrix;
			}
		}
	}        // namespace

	component::ShadowMapData::ShadowMapData()
	{
		shadowTexture = TextureDepthArray::create(SHADOWMAP_SiZE_MAX, SHADOWMAP_SiZE_MAX, shadowMapNum);
		shader        = Shader::create("shaders/Shadow.shader");

		DescriptorInfo createInfo{};
		createInfo.layoutIndex = 0;
		createInfo.shader      = shader.get();

		descriptorSet.resize(1);
		descriptorSet[0] = DescriptorSet::create(createInfo);
		currentDescriptorSets.resize(1);
		cascadeCommandQueue[0].reserve(500);
		cascadeCommandQueue[1].reserve(500);
		cascadeCommandQueue[2].reserve(500);
		cascadeCommandQueue[3].reserve(500);

		shadowTexture->setName("uShaderMapSampler");
	}

	component::ReflectiveShadowData::ReflectiveShadowData()
	{
		{
			shader = Shader::create("shaders/LPV/ReflectiveShadowMap.shader");
			descriptorSets.resize(2);
			descriptorSets[0] = DescriptorSet::create({0, shader.get()});
			descriptorSets[1] = DescriptorSet::create({1, shader.get()});

			for (int32_t i = 0; i < NUM_RSM; i++)
			{
				float *sample     = MathUtils::hammersley(i, 2, NUM_RSM);
				vpl.vplSamples[i] = {
				    sample[0],
				    sample[1],
				    sample[0] * std::sin(M_PI_TWO * sample[1]),
				    sample[0] * std::cos(M_PI_TWO * sample[1])};
				delete[] sample;
			}

			TextureParameters parameters;

			parameters.format = TextureFormat::RGBA32;
			parameters.wrap   = TextureWrap::ClampToBorder;

			fluxTexture = Texture2D::create(SHADOW_SIZE, SHADOW_SIZE, nullptr, parameters);
			fluxTexture->setName("uFluxSampler");

			worldTexture = Texture2D::create(SHADOW_SIZE, SHADOW_SIZE, nullptr, parameters);
			worldTexture->setName("uRSMWorldSampler");

			normalTexture = Texture2D::create(SHADOW_SIZE, SHADOW_SIZE, nullptr, parameters);
			normalTexture->setName("uRSMNormalSampler");

			fluxDepth = TextureDepth::create(SHADOW_SIZE, SHADOW_SIZE);
			descriptorSets[1]->setUniformBufferData("VirtualPointLight", &vpl);
		}
	}

	namespace shadow_map_pass
	{
		using Entity = ecs::Chain
			::Write<component::ShadowMapData>
			::Read<component::CameraView>
			::To<ecs::Entity>;

		using LightQuery = ecs::Chain
			::Write<Light>
			::To<ecs::Query>;

		using MeshQuery = ecs::Chain
			::Write<MeshRenderer>
			::Write<Transform>
			::To<ecs::Query>;

		inline auto beginScene(Entity entity, LightQuery lightQuery, MeshQuery meshQuery, ecs::World world)
		{
			auto [shadowData,cameraView] = entity;

			for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
			{
				shadowData.cascadeCommandQueue[i].clear();
			}

			if (!lightQuery.empty())
			{
				Light* directionaLight = nullptr;

				for (auto entity : lightQuery)
				{
					auto [light] = lightQuery.convert(entity);
					if (static_cast<LightType>(light.lightData.type) == LightType::DirectionalLight)
					{
						directionaLight = &light;
						break;
					}
				}

				if (directionaLight)
				{
					if (directionaLight)
					{
						updateCascades(cameraView, shadowData,directionaLight);

						for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
						{
							shadowData.cascadeFrustums[i].from(shadowData.shadowProjView[i]);
						}
					}

					for (auto entityHandle : meshQuery)
					{
						auto [mesh, trans] = meshQuery.convert(entityHandle);

						for (uint32_t i = 0; i < shadowData.shadowMapNum; i++)
						{
							//auto inside = shadowData->cascadeFrustums[i].isInsideFast(bbCopy);
							//if (inside != Intersection::OUTSIDE)
							{
								auto& cmd = shadowData.cascadeCommandQueue[i].emplace_back();
								cmd.mesh = mesh.getMesh().get();
								cmd.transform = trans.getWorldMatrix();
								cmd.material = mesh.getMesh()->getMaterial().get();
							}
						}
					}
				}
			}
		}

		using RenderEntity = ecs::Chain
			::Write<component::ShadowMapData>
			::Read<component::RendererData>
			::To<ecs::Entity>;

		inline auto onRender(RenderEntity entity, ecs::World world)
		{
			auto [shadowData, rendererData] = entity;

			shadowData.descriptorSet[0]->setUniform("UniformBufferObject", "projView", shadowData.shadowProjView);
			shadowData.descriptorSet[0]->update();

			PipelineInfo pipelineInfo;
			pipelineInfo.shader = shadowData.shader;

			pipelineInfo.cullMode = CullMode::Back;
			pipelineInfo.transparencyEnabled = false;
			pipelineInfo.depthBiasEnabled = false;
			pipelineInfo.depthArrayTarget = shadowData.shadowTexture;
			pipelineInfo.clearTargets = true;

			auto pipeline = Pipeline::get(pipelineInfo);

			for (uint32_t i = 0; i < shadowData.shadowMapNum; ++i)
			{
				//GPUProfile("Shadow Layer Pass");
				pipeline->bind(rendererData.commandBuffer, i);

				for (auto& command : shadowData.cascadeCommandQueue[i])
				{
					Mesh* mesh = command.mesh;
					shadowData.currentDescriptorSets[0] = shadowData.descriptorSet[0];
					auto  trans = command.transform;
					auto& pushConstants = shadowData.shader->getPushConstants()[0];

					pushConstants.setValue("transform", (void*)&trans);
					pushConstants.setValue("cascadeIndex", (void*)&i);

					shadowData.shader->bindPushConstants(rendererData.commandBuffer, pipeline.get());

					Renderer::bindDescriptorSets(pipeline.get(), rendererData.commandBuffer, 0, shadowData.descriptorSet);
					Renderer::drawMesh(rendererData.commandBuffer, pipeline.get(), mesh);
				}
				pipeline->end(rendererData.commandBuffer);
			}
		}
	}

	namespace reflective_shadow_map_pass
	{
		using Entity = ecs::Chain
			::Read<component::ShadowMapData>
			::Read<component::ReflectiveShadowData>
			::To<ecs::Entity>;

		inline auto beginScene(Entity entity, ecs::World world)
		{
			auto [shadow, rsm] = entity;
		}

		inline auto onRender(Entity entity, ecs::World world)
		{
			//auto [data, cameraView] = entity;
		}
	}

	namespace reflective_shadow_map
	{
		auto registerShadowMap(ExecuteQueue& begin, ExecuteQueue& renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerGlobalComponent<component::ShadowMapData>();
			executePoint->registerGlobalComponent<component::ReflectiveShadowData>();

			executePoint->registerWithinQueue<shadow_map_pass::beginScene>(begin);
			executePoint->registerWithinQueue<shadow_map_pass::onRender>(renderer);

			executePoint->registerWithinQueue<reflective_shadow_map_pass::beginScene>(begin);
			executePoint->registerWithinQueue<reflective_shadow_map_pass::onRender>(renderer);
		}
	};

	/*
	auto ReflectiveShadowMap::init(const std::shared_ptr<GBuffer> &buffer) -> void
	{
		gbuffer = buffer;
	}

	auto ReflectiveShadowMap::renderScene(Scene *scene) -> void
	{
		PROFILE_FUNCTION();

		auto descriptorSet = data->descriptorSets[1];

		data->descriptorSets[0]->update();
		data->descriptorSets[1]->update();

		auto commandBuffer = getCommandBuffer();

		PipelineInfo pipeInfo;
		pipeInfo.shader              = data->shader;
		pipeInfo.transparencyEnabled = false;
		pipeInfo.depthBiasEnabled    = false;
		pipeInfo.clearTargets        = true;
		pipeInfo.depthTarget         = data->fluxDepth;
		pipeInfo.colorTargets[0]     = data->fluxTexture;
		pipeInfo.colorTargets[1]     = data->worldTexture;
		pipeInfo.colorTargets[2]     = data->normalTexture;
		pipeInfo.clearColor          = {0, 0, 0, 0};

		auto pipeline = Pipeline::get(pipeInfo);

		if (commandBuffer)
			commandBuffer->bindPipeline(pipeline.get());
		else
			pipeline->bind(getCommandBuffer());

		for (auto &command : shadowData->cascadeCommandQueue[0])
		{
			Mesh *      mesh          = command.mesh;
			const auto &trans         = command.transform;
			auto &      pushConstants = data->shader->getPushConstants()[0];

			pushConstants.setValue("transform", (void *) &trans);

			data->shader->bindPushConstants(getCommandBuffer(), pipeline.get());
			if (command.material != nullptr)
			{
				descriptorSet->setUniform("UBO", "albedoColor", &command.material->getProperties().albedoColor);
				descriptorSet->setUniform("UBO", "usingAlbedoMap", &command.material->getProperties().usingAlbedoMap);
				descriptorSet->setTexture("uDiffuseMap", command.material->getTextures().albedo);
				descriptorSet->update();
			}
			Renderer::bindDescriptorSets(pipeline.get(), getCommandBuffer(), 0, data->descriptorSets);
			Renderer::drawMesh(getCommandBuffer(), pipeline.get(), mesh);
		}

		if (commandBuffer)
			commandBuffer->unbindPipeline();
		else
			pipeline->end(getCommandBuffer());
	}

	auto ReflectiveShadowMap::beginScene(Scene *scene, const glm::mat4 &projView) -> void
	{
		auto &rsmData    = scene->getGlobalComponent<component::ReflectiveShadowData>();
		auto &shadowData = scene->getGlobalComponent<component::ShadowMapData>();

		for (uint32_t i = 0; i < shadowData->shadowMapNum; i++)
		{
			shadowData->cascadeCommandQueue[i].clear();
		}

		if (directionaLight)
		{
			updateCascades(scene, directionaLight);

			for (uint32_t i = 0; i < shadowData->shadowMapNum; i++)
			{
				shadowData->cascadeFrustums[i].from(shadowData->shadowProjView[i]);
			}
		}
	}

	auto ReflectiveShadowMap::onImGui() -> void
	{
		if (ImGui::TreeNode("Reflective ShadowMap"))
		{
			
			ImGui::Columns(2);

			if (ImGuiHelper::property("RsmIntensity", data->vpl.rsmIntensity, 0.f, 100.f, ImGuiHelper::PropertyFlag::InputFloat))
			{
				data->descriptorSets[1]->setUniform("VirtualPointLight", "rsmIntensity", &data->vpl.rsmIntensity);
			}
			if (ImGuiHelper::property("RsmRMax", data->vpl.rsmRMax, 0.f, 1.f, ImGuiHelper::PropertyFlag::InputFloat, "%.3f", 0.001))
			{
				data->descriptorSets[1]->setUniform("VirtualPointLight", "rsmRMax", &data->vpl.rsmRMax);
			}

			if (ImGuiHelper::property("NumberOfSamples", data->vpl.numberOfSamples, 1.f, ReflectiveShadowData::NUM_RSM, ImGuiHelper::PropertyFlag::InputFloat))
			{
				data->descriptorSets[1]->setUniform("VirtualPointLight", "numberOfSamples", &data->vpl.numberOfSamples);
			}
			ImGui::Columns(1);
		}
	}
	*/
};        // namespace maple