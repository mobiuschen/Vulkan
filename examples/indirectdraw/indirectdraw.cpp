/*
* Vulkan Example - Indirect drawing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Use a device local buffer that stores draw commands for instanced rendering of different meshes stored
* in the same buffer.
*
* Indirect drawing offloads draw command generation and offers the ability to update them on the GPU
* without the CPU having to touch the buffer again, also reducing the number of drawcalls.
*
* The example shows how to setup and fill such a buffer on the CPU side, stages it to the device and
* shows how to render it using only one draw command.
*
* See readme.md for details
*
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false

// Number of instances per object
//#if defined(__ANDROID__)
//#define OBJECT_INSTANCE_COUNT 1024
//// Circular range of plant distribution
//#define PLANT_RADIUS 2.0f
//#define PRIMITIVE_COUNT 256
//#else
//#define OBJECT_INSTANCE_COUNT 2048
//#define PRIMITIVE_COUNT 256
//// Circular range of plant distribution
//#define PLANT_RADIUS 2.0f
//#endif

// Circular range of plant distribution
#define PLANT_RADIUS 2.0f

static const uint32_t INSTANCE_PER_PRIM_PER_MESH = 32;
static const uint32_t PRIMITIVE_COUNT = 32;
static const uint32_t PRIMITIVE_COUNT_BORDER = 4;
static const uint32_t OBJECT_INSTANCE_COUNT = INSTANCE_PER_PRIM_PER_MESH * PRIMITIVE_COUNT;
static const float PRIM_GAP = 5.0f;
static const float CULL_DISTANCE = 100.0f;

enum EAttrLocation : uint32_t
{
	Pos,
	Normal,
	UV,
	Color,
	instanceTransformRow0,
	instanceTransformRow1,
	instanceTransformRow2,
	instanceTransformRow3,
	instanceTexIndex,
	primitiveIndex
};

enum ERegister: uint32_t
{
	Scene,
	PlantTextureArray,
	Texture,
	Primitives,
	Materials
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		vks::Texture2DArray plants;
		vks::Texture2D ground;
	} textures;

	struct {
		vkglTF::Model plants;
		vkglTF::Model ground;
		vkglTF::Model skysphere;
	} models;

	// Per-instance data block
	struct InstanceData {
		glm::vec4 transRow0;
		glm::vec4 transRow1;
		glm::vec4 transRow2;
		glm::vec4 transRow3;
		uint32_t texIndex;
		uint32_t primIndex;
	};

	struct Material {
		glm::vec4 tint;
		uint32_t textureIndex;
	};

	// Contains the instanced data
	vks::Buffer instanceBuffer;
	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	uint32_t indirectDrawCount;
	uint32_t plantTypeCount;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
	} uboVS;

	struct PrimitiveData {
		glm::mat4 transform;
		glm::vec3 pos;
		float cullDistance;
	};

	struct {
		vks::Buffer scene;
		vks::Buffer primitives;
		vks::Buffer materials;
	} uniformData;

	struct {
		VkPipeline plants;
		VkPipeline ground;
		VkPipeline skysphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkSampler samplerRepeat;

	uint32_t objectCount = 0;

	// Store the indirect draw commands containing index offsets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

	std::vector<Material> materials;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Indirect rendering";
		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-12.0f, 159.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.4f, 1.25f, 0.0f));
		camera.movementSpeed = 5.0f;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.plants, nullptr);
		vkDestroyPipeline(device, pipelines.ground, nullptr);
		vkDestroyPipeline(device, pipelines.skysphere, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		textures.plants.destroy();
		textures.ground.destroy();
		instanceBuffer.destroy();
		indirectCommandsBuffer.destroy();
		uniformData.scene.destroy();
		uniformData.primitives.destroy();
		uniformData.materials.destroy();
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Example uses multi draw indirect if available
		if (deviceFeatures.multiDrawIndirect) {
			enabledFeatures.multiDrawIndirect = VK_TRUE;
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.18f, 0.27f, 0.5f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			// Skysphere
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skysphere);
			models.skysphere.draw(drawCmdBuffers[i]);
			// Ground
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ground);
			models.ground.draw(drawCmdBuffers[i]);

			// [POI] Instanced multi draw rendering of the plants
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.plants);
			// Binding point 0 : Mesh vertex buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.plants.vertices.buffer, offsets);
			// Binding point 1 : Instance data buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.plants.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// If the multi draw feature is supported:
			// One draw call for an arbitrary number of objects
			// Index offsets and instance count are taken from the indirect buffer
			if (vulkanDevice->features.multiDrawIndirect)
			{
				vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, 0, indirectDrawCount, sizeof(VkDrawIndexedIndirectCommand));
			}
			else
			{
				// If multi draw is not available, we must issue separate draw commands
				for (auto j = 0; j < indirectCommands.size(); j++)
				{
					vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, j * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
				}
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.plants.loadFromFile(getAssetPath() + "models/plants.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ground.loadFromFile(getAssetPath() + "models/plane_circle.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skysphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.plants.loadFromFile(getAssetPath() + "textures/texturearray_plants_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.ground.loadFromFile(getAssetPath() + "textures/ground_dry_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, ERegister::Scene),
			// Binding 1: Fragment shader combined sampler (plants texture array)
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, ERegister::PlantTextureArray),
            // Binding 2: Fragment shader combined sampler (ground texture)
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, ERegister::Texture),
            // Binding 3: vertex shader uniform buffer primitive data
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, ERegister::Primitives),
			// Binding 4
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, ERegister::Materials),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ERegister::Scene, &uniformData.scene.descriptor),
			// Binding 1: Plants texture array combined
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ERegister::PlantTextureArray, &textures.plants.descriptor),
			// Binding 2: Ground texture combined
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ERegister::Texture, &textures.ground.descriptor),
			// Binding 3: Primitive Data uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ERegister::Primitives, &uniformData.primitives.descriptor),
			// Binding 4
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ERegister::Materials, &uniformData.materials.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// This example uses two different input states, one for the instanced part and one for non-instanced rendering
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

		// Vertex input bindings
		// The instancing pipeline uses a vertex input state with two bindings
		bindingDescriptions = {
		    // Binding point 0: Mesh vertex layout description at per-vertex rate
		    vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(vkglTF::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
		    // Binding point 1: Instanced data at per-instance rate
		    vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
		};

		// Vertex attribute bindings
		// Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
		// instanced.vert:
		//	layout (location = 0) in vec3 inPos;		Per-Vertex
		//	...
		//	layout (location = 4) in vec3 instancePos;	Per-Instance
		attributeDescriptions = {
		    // Per-vertex attributes
		    // These are advanced for each vertex fetched by the vertex shader
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Pos, VK_FORMAT_R32G32B32_SFLOAT, 0),								// Location 0: Position
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Normal, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),				// Location 1: Normal
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::UV, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),					// Location 2: Texture coordinates
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,   EAttrLocation::Color, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),				// Location 3: Color
            // Per-Instance attributes
            // These are fetched for each instance rendered
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, transRow0)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, transRow1)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow2, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, transRow2)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTransformRow3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, transRow3)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::instanceTexIndex, VK_FORMAT_R32_SINT, offsetof(InstanceData, texIndex)),		// Location 7: Texture array layer index
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, EAttrLocation::primitiveIndex, VK_FORMAT_R32_SINT, offsetof(InstanceData, primIndex)),		// Location 8: Primitive index
		};
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
		inputState.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());

		pipelineCreateInfo.pVertexInputState = &inputState;

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/indirectdraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.plants));

		// Only use non-instanced vertex attributes for models rendered without instancing
		inputState.vertexBindingDescriptionCount = 1;
		inputState.vertexAttributeDescriptionCount = 4;

		// Ground
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/ground.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/ground.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ground));

		// Skysphere
		shaderStages[0] = loadShader(getShadersPath() + "indirectdraw/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "indirectdraw/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skysphere));
	}

	// Prepare (and stage) a buffer containing the indirect draw commands
	void prepareIndirectData()
	{
		indirectCommands.clear();

		// Create on indirect command for node in the scene with a mesh attached to it
		uint32_t m = 0;
		for (auto &node : models.plants.nodes)
		{
			if (node->mesh)
			{
				VkDrawIndexedIndirectCommand indirectCmd{};
				indirectCmd.instanceCount = OBJECT_INSTANCE_COUNT;
				indirectCmd.firstInstance = m * OBJECT_INSTANCE_COUNT;
				// @todo: Multiple primitives
				// A glTF node may consist of multiple primitives, so we may have to do multiple commands per mesh
				indirectCmd.firstIndex = node->mesh->primitives[0]->firstIndex;
				indirectCmd.indexCount = node->mesh->primitives[0]->indexCount;

				indirectCommands.push_back(indirectCmd);

				m++;
			}
		}

		plantTypeCount = m;

		indirectDrawCount = static_cast<uint32_t>(indirectCommands.size());

		objectCount = 0;
		for (auto& indirectCmd : indirectCommands)
		{
			objectCount += indirectCmd.instanceCount;
		}

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand),
			indirectCommands.data()));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indirectCommandsBuffer,
			stagingBuffer.size));

		vulkanDevice->copyBuffer(&stagingBuffer, &indirectCommandsBuffer, queue);

		stagingBuffer.destroy();
	}

	void prepareMaterials()
	{
		std::vector<Material> materials;
		for ( uint32_t i = 0; i < textures.plants.layerCount; i++ )
        {
            Material mat;
            mat.tint = glm::vec4(1.0, 1.0, 1.0, 1.0);
            mat.textureIndex = i;
            materials.push_back(mat);
        }

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
			materials.size() * sizeof(Material),
			materials.data()
        ));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&uniformData.materials,
			stagingBuffer.size
		));
		
		vulkanDevice->copyBuffer(&stagingBuffer, &uniformData.materials, queue);
		stagingBuffer.destroy();
	}

	void preparePrimitiveData()
    {
		// setup primitive data
        std::vector<PrimitiveData> primitives;
		primitives.resize(PRIMITIVE_COUNT);

        // setup instance data
        std::vector<InstanceData> instanceData;
        instanceData.resize(objectCount);

		uint32_t totalInstance = 0;
		for ( uint32_t primIdx = 0; primIdx < PRIMITIVE_COUNT; primIdx++ )
		{
            primitives[primIdx].cullDistance = CULL_DISTANCE;
            primitives[primIdx].transform = 
            	glm::translate(glm::mat4(1.0f), 
            				   glm::vec3((float)(primIdx % PRIMITIVE_COUNT_BORDER) * PRIM_GAP, 0.0f, (float)(primIdx / PRIMITIVE_COUNT_BORDER) * PRIM_GAP));
            primitives[primIdx].pos = glm::vec3(primIdx*2, 0, 0);

            std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
            std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

			for ( uint32_t plantIdx = 0; plantIdx < plantTypeCount; plantIdx++ )
            {
                for ( uint32_t ins = 0; ins < INSTANCE_PER_PRIM_PER_MESH; ins++ )
                {
                    const uint32_t insIndex = primIdx * plantIdx * INSTANCE_PER_PRIM_PER_MESH + ins;
                    float theta = 2 * float(M_PI) * uniformDist(rndEngine);
                    float phi = acos(1 - 2 * uniformDist(rndEngine));

					const float scale = 2.0f;
					const glm::vec3 pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
                    glm::mat4 insTransform = glm::translate(glm::mat4(1.0f), pos);
					insTransform = glm::rotate(insTransform, glm::radians(0.0f), { 0.0f, 1.0f, 0.0f });
					insTransform = glm::scale(insTransform, { scale, scale, scale });
					instanceData[insIndex].transRow0 = { insTransform[0][0], insTransform[1][0], insTransform[2][0], insTransform[3][0] };
					instanceData[insIndex].transRow1 = { insTransform[0][1], insTransform[1][1], insTransform[2][1], insTransform[3][1] };
                    instanceData[insIndex].transRow2 = { insTransform[0][2], insTransform[1][2], insTransform[2][2], insTransform[3][2] };
                    instanceData[insIndex].transRow3 = { insTransform[0][3], insTransform[1][3], insTransform[2][3], insTransform[3][3] };
                    instanceData[insIndex].texIndex = plantIdx;
                    instanceData[insIndex].primIndex = primIdx;
					totalInstance++;
                }
			}
		}

		assert(totalInstance == objectCount);

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			primitives.size() * sizeof(PrimitiveData),
			primitives.data()
		));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&uniformData.primitives,
			stagingBuffer.size
		));

		vulkanDevice->copyBuffer(&stagingBuffer, &uniformData.primitives, queue);
		stagingBuffer.destroy();

        // setup instance data
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            instanceData.size() * sizeof(InstanceData),
            instanceData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &instanceBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &instanceBuffer, queue);

        stagingBuffer.destroy();
	}

	//// Prepare (and stage) a buffer containing instanced data for the mesh draws
	//void prepareInstanceData()
	//{
	//	std::vector<InstanceData> instanceData;
	//	instanceData.resize(objectCount);

	//	std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
	//	std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

	//	for (uint32_t i = 0; i < objectCount; i++) {
	//		float theta = 2 * float(M_PI) * uniformDist(rndEngine);
	//		float phi = acos(1 - 2 * uniformDist(rndEngine));
	//		instanceData[i].rot = glm::vec3(0.0f, float(M_PI) * uniformDist(rndEngine), 0.0f);
	//		instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
	//		instanceData[i].scale = 1.0f + uniformDist(rndEngine) * 2.0f;
	//		instanceData[i].texIndex = i / OBJECT_INSTANCE_COUNT;
	//	}

	//	vks::Buffer stagingBuffer;
	//	VK_CHECK_RESULT(vulkanDevice->createBuffer(
	//		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	//		&stagingBuffer,
	//		instanceData.size() * sizeof(InstanceData),
	//		instanceData.data()));

	//	VK_CHECK_RESULT(vulkanDevice->createBuffer(
	//		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	//		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	//		&instanceBuffer,
	//		stagingBuffer.size));

	//	vulkanDevice->copyBuffer(&stagingBuffer, &instanceBuffer, queue);

	//	stagingBuffer.destroy();
	//}

	void prepareSceneData()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformData.scene,
			sizeof(uboVS)));

		VK_CHECK_RESULT(uniformData.scene.map());

		updateSceneData(true);
	}

	void updateSceneData(bool viewChanged)
	{
		if (viewChanged)
		{
			uboVS.projection = camera.matrices.perspective;
			uboVS.view = camera.matrices.view;
		}

		memcpy(uniformData.scene.mapped, &uboVS, sizeof(uboVS));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Command buffer to be submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareMaterials();
		prepareIndirectData();
		preparePrimitiveData();
		//prepareInstanceData();
		prepareSceneData();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (camera.updated)
		{
			updateSceneData(true);
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (!vulkanDevice->features.multiDrawIndirect) {
			if (overlay->header("Info")) {
				overlay->text("multiDrawIndirect not supported");
			}
		}
		if (overlay->header("Statistics")) {
			overlay->text("Objects: %d", objectCount);
		}
	}
};

VULKAN_EXAMPLE_MAIN()