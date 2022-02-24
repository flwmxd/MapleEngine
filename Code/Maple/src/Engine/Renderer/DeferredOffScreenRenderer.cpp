///////////////////////////////////////////////////////////////////////////
// This file is part of the Maple Engine                              		//
//////////////////////////////////////////////////////////////////////////////

#include "DeferredOffScreenRenderer.h"

#include "RHI/GPUProfile.h"
#include "RHI/Pipeline.h"
#include "RHI/Shader.h"
#include "RHI/Texture.h"
#include "RHI/CommandBuffer.h"

#include "Engine/Camera.h"
#include "Engine/GBuffer.h"
#include "Engine/Mesh.h"
#include "Engine/Profiler.h"
#include "Engine/Mesh.h"

#include "Scene/Component/MeshRenderer.h"
#include "Scene/Component/Light.h"
#include "Scene/Component/Transform.h"
#include "Scene/Component/Component.h"
#include "Scene/Scene.h"

#include "PostProcessRenderer.h"

#include "Engine/Vientiane/ReflectiveShadowMap.h"

#include "Application.h"
#include "ImGui/ImGuiHelpers.h"
#include "Others/Randomizer.h"
#include "RendererData.h"
#include <ecs/ecs.h>
#include <glm/gtc/type_ptr.hpp>

namespace maple
{
	namespace component
	{
		DeferredData::DeferredData()
		{
			deferredColorShader = Shader::create("shaders/DeferredColor.shader");
			deferredLightShader = Shader::create("shaders/DeferredLight.shader");
			stencilShader = Shader::create("shaders/Outline.shader");
			commandQueue.reserve(1000);

			MaterialProperties properties;
			properties.albedoColor       = glm::vec4(1.f, 1.f, 1.f, 1.f);
			properties.roughnessColor    = glm::vec4(0);
			properties.metallicColor     = glm::vec4(0);
			properties.usingAlbedoMap    = 0.0f;
			properties.usingRoughnessMap = 0.0f;
			properties.usingNormalMap    = 0.0f;
			properties.usingMetallicMap  = 0.0f;

			defaultMaterial = std::make_shared<Material>(deferredColorShader, properties);
			defaultMaterial->createDescriptorSet();

			DescriptorInfo info{};
			descriptorColorSet.resize(3);
			descriptorLightSet.resize(1);

			info.shader           = deferredColorShader.get();
			info.layoutIndex      = 0;
			descriptorColorSet[0] = DescriptorSet::create(info);

			info.layoutIndex      = 2;
			descriptorColorSet[2] = DescriptorSet::create(info);

			info.shader           = deferredLightShader.get();
			info.layoutIndex      = 0;
			descriptorLightSet[0] = DescriptorSet::create(info);
			screenQuad            = Mesh::createQuad(true);


			preintegratedFG = Texture2D::create("preintegrated", "textures/ibl_brdf_lut.png", { TextureFormat::RGBA8, TextureFilter::Linear, TextureFilter::Linear, TextureWrap::ClampToEdge });

			stencilDescriptorSet = DescriptorSet::create({0,stencilShader.get()});
		}
	}        // namespace component

	namespace deferred_offscreen
	{
		using Entity = ecs::Chain
			::Write<component::DeferredData>
			::Read<component::ShadowMapData>
			::Read<component::CameraView>
			::Read<component::RendererData>
			::Read<component::SSAOData>
			::To<ecs::Entity>;

		using LightDefine = ecs::Chain
			::Write<Light>
			::Read<Transform>;

		using Query = LightDefine
			::To<ecs::Query>;

		using EnvQuery = ecs::Chain
			::Read<Environment>
			::To<ecs::Query>;

		using MeshQuery = ecs::Chain
			::Write<MeshRenderer>
			::Write<Transform>
			::ReadIfExist<StencilComponent>
			::To<ecs::Query>;

		using LightEntity = LightDefine::To<ecs::Entity>;

		inline auto beginScene(Entity entity, Query lightQuery, EnvQuery env, MeshQuery meshQuery,ecs::World world)
		{
			auto [data, shadowData, cameraView,renderData,ssao] = entity;
			data.commandQueue.clear();
			auto descriptorSet = data.descriptorColorSet[0];

			if (cameraView.cameraTransform == nullptr)
				return;

			data.stencilDescriptorSet->setUniform("UniformBufferObject", "projView", &cameraView.projView);

			data.descriptorColorSet[0]->setUniform("UniformBufferObject", "projView", &cameraView.projView);
			data.descriptorColorSet[0]->setUniform("UniformBufferObject", "view", &cameraView.view);
			data.descriptorColorSet[0]->setUniform("UniformBufferObject", "projViewOld", &cameraView.projViewOld);

			data.descriptorColorSet[2]->setUniform("UBO", "view", &cameraView.view);
			data.descriptorColorSet[2]->setUniform("UBO", "nearPlane", &cameraView.nearPlane);
			data.descriptorColorSet[2]->setUniform("UBO", "farPlane", &cameraView.farPlane);

			Light *directionaLight = nullptr;

			LightData lights[32] = {};
			uint32_t  numLights  = 0;

			{
				PROFILE_SCOPE("Get Light");
				lightQuery.forEach([&](LightEntity entity) {
					auto [light, transform] = entity;

					light.lightData.position  = {transform.getWorldPosition(), 1.f};
					light.lightData.direction = {glm::normalize(transform.getWorldOrientation() * maple::FORWARD), 1.f};

					if (static_cast<LightType>(light.lightData.type) == LightType::DirectionalLight)
						directionaLight = &light;

					lights[numLights] = light.lightData;
					numLights++;
				});
			}

			const glm::mat4 *shadowTransforms = shadowData.shadowProjView;
			const glm::vec4 *splitDepth       = shadowData.splitDepth;
			const glm::mat4  lightView        = shadowData.lightMatrix;
			const auto       numShadows       = shadowData.shadowMapNum;
			//auto cubeMapMipLevels = envData->environmentMap ? envData->environmentMap->getMipMapLevels() - 1 : 0;
			int32_t renderMode = 0;
			auto cameraPos = glm::vec4{cameraView.cameraTransform->getWorldPosition(), 1.f};
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "lights", lights, sizeof(LightData) * numLights, false);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "cameraPosition", &cameraPos);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "viewMatrix", &cameraView.view);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "lightView", &lightView);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "shadowTransform", shadowTransforms);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "splitDepths", splitDepth);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "biasMat", &BIAS_MATRIX);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "shadowMapSize", &shadowData.shadowMapSize);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "shadowFade", &shadowData.shadowFade);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "cascadeTransitionFade", &shadowData.cascadeTransitionFade);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "maxShadowDistance", &shadowData.maxShadowDistance);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "initialBias", &shadowData.initialBias);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "lightCount", &numLights);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "shadowCount", &numShadows);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "mode", &renderMode);

	
			data.descriptorLightSet[0]->setTexture("uPreintegratedFG", data.preintegratedFG);
			data.descriptorLightSet[0]->setTexture("uShadowMap", shadowData.shadowTexture);

			if (!env.empty())
			{
				auto [evnData] = env.convert(*env.begin());
				data.descriptorLightSet[0]->setTexture("uPrefilterMap", evnData.getEnvironment());
				data.descriptorLightSet[0]->setTexture("uIrradianceMap", evnData.getIrradianceMap());
			}

			int32_t ssaoEnable = ssao.enable ? 1 : 0;
			int32_t cubeMapMipLevels = 0;
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "ssaoEnable", &ssaoEnable);
			data.descriptorLightSet[0]->setUniform("UniformBufferLight", "cubeMapMipLevels", &cubeMapMipLevels);


			PipelineInfo pipelineInfo{};
			pipelineInfo.shader = data.deferredColorShader;
			pipelineInfo.polygonMode = PolygonMode::Fill;
			pipelineInfo.blendMode = BlendMode::SrcAlphaOneMinusSrcAlpha;
			pipelineInfo.clearTargets = false;
			pipelineInfo.swapChainTarget = false;

			for (auto entityHandle : meshQuery)
			{
				auto [mesh, trans] = meshQuery.convert(entityHandle);

				//if (mesh.isActive())
				{
					const auto& worldTransform = trans.getWorldMatrix();

					auto bb = mesh.getMesh()->getBoundingBox();
			
					{
						//auto inside = forwardData->frustum.isInsideFast(bbCopy);

						//if (inside != Intersection::Outside)
						{
							auto& cmd = data.commandQueue.emplace_back();
							cmd.mesh = mesh.getMesh().get();
							cmd.transform = worldTransform;
							cmd.material = mesh.getMesh()->getMaterial() ? mesh.getMesh()->getMaterial().get() : data.defaultMaterial.get();
							cmd.material->bind();

							auto depthTest = data.depthTest;
							pipelineInfo.colorTargets[0] = renderData.gbuffer->getBuffer(GBufferTextures::COLOR);
							pipelineInfo.colorTargets[1] = renderData.gbuffer->getBuffer(GBufferTextures::POSITION);
							pipelineInfo.colorTargets[2] = renderData.gbuffer->getBuffer(GBufferTextures::NORMALS);
							pipelineInfo.colorTargets[3] = renderData.gbuffer->getBuffer(GBufferTextures::PBR);
							pipelineInfo.colorTargets[4] = renderData.gbuffer->getBuffer(GBufferTextures::VIEW_POSITION);
							pipelineInfo.colorTargets[5] = renderData.gbuffer->getBuffer(GBufferTextures::VIEW_NORMALS);
							pipelineInfo.colorTargets[6] = renderData.gbuffer->getBuffer(GBufferTextures::VELOCITY);
			
							pipelineInfo.cullMode = cmd.material->isFlagOf(Material::RenderFlags::TwoSided) ? CullMode::None : CullMode::Back;
							pipelineInfo.transparencyEnabled = cmd.material->isFlagOf(Material::RenderFlags::AlphaBlend);

							if (depthTest && cmd.material->isFlagOf(Material::RenderFlags::DepthTest))
							{
								pipelineInfo.depthTarget = renderData.gbuffer->getDepthBuffer();
							}

							if (meshQuery.hasComponent<StencilComponent>(entityHandle))
							{
								pipelineInfo.shader = data.stencilShader;
								pipelineInfo.stencilTest = true;
								pipelineInfo.stencilMask = 0x00;
								pipelineInfo.stencilFunc = StencilType::Notequal;
								pipelineInfo.stencilFail = StencilType::Keep;
								pipelineInfo.stencilDepthFail = StencilType::Keep;
								pipelineInfo.stencilDepthPass = StencilType::Replace;
								pipelineInfo.depthTest = true;
								cmd.stencilPipelineInfo = pipelineInfo;
								cmd.stencilPipelineInfo.colorTargets[0] = renderData.gbuffer->getBuffer(GBufferTextures::SCREEN);
								cmd.stencilPipelineInfo.colorTargets[1] = nullptr;
								cmd.stencilPipelineInfo.colorTargets[2] = nullptr;
								cmd.stencilPipelineInfo.colorTargets[3] = nullptr;

								pipelineInfo.shader = data.deferredColorShader;
								pipelineInfo.stencilMask = 0xFF;
								pipelineInfo.stencilFunc = StencilType::Always;
								pipelineInfo.stencilFail = StencilType::Keep;
								pipelineInfo.stencilDepthFail = StencilType::Keep;
								pipelineInfo.stencilDepthPass = StencilType::Replace;
								pipelineInfo.depthTest = true;
							}
							cmd.pipelineInfo = pipelineInfo;
						}
					}
				}
			}
		}

		inline auto onRender(Entity entity, ecs::World world)
		{
			auto [data, shadowData, cameraView, renderData,ssao] = entity;

			data.descriptorColorSet[0]->update();
			data.descriptorColorSet[2]->update();
			data.stencilDescriptorSet->update();

			std::shared_ptr<Pipeline> pipeline;

			for (auto& command : data.commandQueue)
			{
				pipeline = Pipeline::get(command.pipelineInfo);

				if (renderData.commandBuffer)
					renderData.commandBuffer->bindPipeline(pipeline.get());
				else
					pipeline->bind(renderData.commandBuffer);

				data.descriptorColorSet[1] = command.material->getDescriptorSet();
				auto& pushConstants = data.deferredColorShader->getPushConstants()[0];
				pushConstants.setValue("transform", &command.transform);
				data.deferredColorShader->bindPushConstants(renderData.commandBuffer, pipeline.get());
				Renderer::bindDescriptorSets(pipeline.get(), renderData.commandBuffer, 0, data.descriptorColorSet);
				Renderer::drawMesh(renderData.commandBuffer, pipeline.get(), command.mesh);

				if (command.stencilPipelineInfo.stencilTest)
				{
					auto stencilPipeline = Pipeline::get(command.stencilPipelineInfo);

					if (renderData.commandBuffer)
						renderData.commandBuffer->bindPipeline(stencilPipeline.get());
					else
						stencilPipeline->bind(renderData.commandBuffer);

					auto& pushConstants = data.stencilShader->getPushConstants()[0];
					pushConstants.setValue("transform", &command.transform);
					data.stencilShader->bindPushConstants(renderData.commandBuffer, stencilPipeline.get());

					Renderer::bindDescriptorSets(stencilPipeline.get(), renderData.commandBuffer, 0, { data.stencilDescriptorSet });
					Renderer::drawMesh(renderData.commandBuffer, stencilPipeline.get(), command.mesh);

					if (renderData.commandBuffer)
						renderData.commandBuffer->unbindPipeline();
					else
						stencilPipeline->end(renderData.commandBuffer);
				}
			}

			if (renderData.commandBuffer)
				renderData.commandBuffer->unbindPipeline();
			else if (pipeline)
				pipeline->end(renderData.commandBuffer);
		}

		auto registerDeferredOffScreenRenderer(ExecuteQueue &begin, ExecuteQueue &renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerGlobalComponent<component::DeferredData>();
			executePoint->registerWithinQueue<deferred_offscreen::beginScene>(begin);
			executePoint->registerWithinQueue<deferred_offscreen::onRender>(renderer);
		}
	}        // namespace deferred_offscreen

	namespace deferred_lighting
	{
		using Entity = ecs::Chain
			::Write<component::DeferredData>
			::Read<component::ShadowMapData>
			::Read<component::CameraView>
			::Read<component::RendererData>
			::To<ecs::Entity>;
		
		using EnvQuery = ecs::Chain
			::Read<Environment>
			::To<ecs::Query>;

		inline auto onRender(Entity entity, EnvQuery envQuery, ecs::World world)
		{
			auto [data, shadow, cameraView, rendererData] = entity;

			auto descriptorSet = data.descriptorLightSet[0];
			descriptorSet->setTexture("uColorSampler", rendererData.gbuffer->getBuffer(GBufferTextures::COLOR));
			descriptorSet->setTexture("uPositionSampler", rendererData.gbuffer->getBuffer(GBufferTextures::POSITION));
			descriptorSet->setTexture("uNormalSampler", rendererData.gbuffer->getBuffer(GBufferTextures::NORMALS));

			//descriptorSet->setTexture("uViewPositionSampler", gBuffer->getBuffer(GBufferTextures::VIEW_POSITION));
			//descriptorSet->setTexture("uViewNormalSampler", gBuffer->getBuffer(GBufferTextures::VIEW_NORMALS));
			descriptorSet->setTexture("uPBRSampler", rendererData.gbuffer->getBuffer(GBufferTextures::PBR));
			descriptorSet->setTexture("uSSAOSampler", rendererData.gbuffer->getBuffer(GBufferTextures::SSAO_BLUR));
			descriptorSet->setTexture("uDepthSampler", rendererData.gbuffer->getDepthBuffer());
			descriptorSet->setTexture("uShadowMap", shadow.shadowTexture);

			if (!envQuery.empty())
			{
				auto [envData] = envQuery.convert(*envQuery.begin());
				descriptorSet->setTexture("uIrradianceMap", envData.getIrradianceMap());
				descriptorSet->setTexture("uPrefilterMap", envData.getEnvironment());
				descriptorSet->setTexture("uPreintegratedFG", data.preintegratedFG);
			}
			descriptorSet->update();

			PipelineInfo pipeInfo;
			pipeInfo.shader = data.deferredLightShader;
			pipeInfo.polygonMode = PolygonMode::Fill;
			pipeInfo.cullMode = CullMode::None;
			pipeInfo.transparencyEnabled = false;
			pipeInfo.depthBiasEnabled = false;
			pipeInfo.clearTargets = false;
			pipeInfo.colorTargets[0] = rendererData.gbuffer->getBuffer(GBufferTextures::SCREEN);
			auto deferredLightPipeline = Pipeline::get(pipeInfo);
			deferredLightPipeline->bind(rendererData.commandBuffer);

			Renderer::bindDescriptorSets(deferredLightPipeline.get(), rendererData.commandBuffer, 0, data.descriptorLightSet);
			Renderer::drawMesh(rendererData.commandBuffer, deferredLightPipeline.get(), data.screenQuad.get());
			deferredLightPipeline->end(rendererData.commandBuffer);
		}

		auto registerDeferredLighting(ExecuteQueue& begin, ExecuteQueue& renderer, std::shared_ptr<ExecutePoint> executePoint) -> void
		{
			executePoint->registerGlobalComponent<component::DeferredData>();
			executePoint->registerWithinQueue<deferred_lighting::onRender>(renderer);
		}
	};
};           // namespace maple