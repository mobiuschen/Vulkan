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
#if defined(__ANDROID__)
#define OBJECT_INSTANCE_COUNT 1024
// Circular range of plant distribution
#define PLANT_RADIUS 20.0f
#else
#define OBJECT_INSTANCE_COUNT 4096
// Circular range of plant distribution
#define PLANT_RADIUS 25.0f
#endif

static const uint32_t CLUSTER_TRIANGLE_NUM = 64;

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

    struct InstanceData {
        glm::vec3 pos;
		float pad;
        glm::vec3 rot;
        float scale;
    };

    struct InstanceTexIndexData {
        uint32_t texIndex;
    };

	struct VertexData {
		glm::vec4 pos;
		glm::vec3 normal;
		float pad0;
		glm::vec2 uv;
		glm::vec2 pad1;
		glm::vec3 color;
		float pad2;
	};

	struct ClusterDesc
	{
		uint32_t indexOffset;
		uint32_t instanceOffset;
	};

	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	uint32_t indirectDrawCount;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
	} uboVS;

	struct {
		vks::Buffer scene;
	} uniformData;

    vks::Buffer fixedIndexBuffer;
    vks::Buffer clusterBuffer;

    vks::Buffer instanceStorageBuffer;
    vks::Buffer instanceTexIndexStorageBuffer;
	vks::Buffer vertexDataStorageBuffer;
    vks::Buffer indexStorageBuffer;

	struct {
		VkPipeline plants;
		VkPipeline ground;
		VkPipeline skysphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

    VkQueryPool queryPool;

    // Vector for storing pipeline statistics results
    std::vector<uint64_t> pipelineStats;
    std::vector<std::string> pipelineStatNames;

	VkSampler samplerRepeat;

    uint32_t objectCount = 0;
    uint32_t clusterCount = 0;

	// Store the indirect draw commands containing index offsets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Noodle batch rendering";
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
		fixedIndexBuffer.destroy();
		clusterBuffer.destroy();
		instanceStorageBuffer.destroy();
		instanceTexIndexStorageBuffer.destroy();
		vertexDataStorageBuffer.destroy();
		indirectCommandsBuffer.destroy();
		indexStorageBuffer.destroy();
		uniformData.scene.destroy();
        vkDestroyQueryPool(device, queryPool, nullptr);
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
		if ( deviceFeatures.pipelineStatisticsQuery )
		{
			enabledFeatures.pipelineStatisticsQuery = VK_TRUE;
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

            // Reset timestamp query pool
            vkCmdResetQueryPool(drawCmdBuffers[i], queryPool, 0, static_cast<uint32_t>(pipelineStats.size()));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            // Start capture of pipeline statistics
            vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 0, 0);

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
            vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &clusterBuffer.buffer, offsets);

            //vkCmdBindIndexBuffer(drawCmdBuffers[i], models.plants.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], fixedIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

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

            // End capture of pipeline statistics
            vkCmdEndQuery(drawCmdBuffers[i], queryPool, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = 
			vkglTF::FileLoadingFlags::PreTransformVertices | 
			vkglTF::FileLoadingFlags::PreMultiplyVertexColors | 
			vkglTF::FileLoadingFlags::FlipY | 
			vkglTF::FileLoadingFlags::DegenerateTriangles64 |
			vkglTF::FileLoadingFlags::KeepCpuData;
        models.plants.loadFromFile(getAssetPath() + "models/plants.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ground.loadFromFile(getAssetPath() + "models/plane_circle.gltf", vulkanDevice, queue, glTFLoadingFlags);
        models.skysphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.plants.loadFromFile(getAssetPath() + "textures/texturearray_plants_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.ground.loadFromFile(getAssetPath() + "textures/ground_dry_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

    // Setup a query pool for storing pipeline statistics
    void setupQueryPool()
    {
        pipelineStatNames = {
            "Input assembly vertex count        ",
            "Input assembly primitives count    ",
            "Vertex shader invocations          ",
            "Clipping stage primitives processed",
            "Clipping stage primitives output    ",
            "Fragment shader invocations        "
        };
        if ( deviceFeatures.tessellationShader )
        {
            pipelineStatNames.push_back("Tess. control shader patches       ");
            pipelineStatNames.push_back("Tess. eval. shader invocations     ");
        }
        pipelineStats.resize(pipelineStatNames.size());

        VkQueryPoolCreateInfo queryPoolInfo = {};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        // This query pool will store pipeline statistics
        queryPoolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        // Pipeline counters to be returned for this pool
        queryPoolInfo.pipelineStatistics =
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        if ( deviceFeatures.tessellationShader )
        {
            queryPoolInfo.pipelineStatistics |=
                VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
                VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
        }
        queryPoolInfo.queryCount = deviceFeatures.tessellationShader ? 8 : 6;
        VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, NULL, &queryPool));
    }

    // Retrieves the results of the pipeline statistics query submitted to the command buffer
    void getQueryResults()
    {
        uint32_t count = static_cast<uint32_t>(pipelineStats.size());
        vkGetQueryPoolResults(
            device,
            queryPool,
            0,
            1,
            count * sizeof(uint64_t),
            pipelineStats.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
    }

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1: Fragment shader combined sampler (plants texture array)
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2: Fragment shader combined sampler (ground texture)
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
            // Binding 3: 
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 3),
            // Binding 4: 
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 4),
            // Binding 5:
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 5),
            // Binding 6:
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 6),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformData.scene.descriptor),
			// Binding 1: Plants texture array combined
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.plants.descriptor),
            // Binding 2: Ground texture combined
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.ground.descriptor),
            // Binding 3: Instance Data storage buffer
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &instanceStorageBuffer.descriptor),
            // Binding 4: 
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &instanceTexIndexStorageBuffer.descriptor),
            // Binding 5: 
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &vertexDataStorageBuffer.descriptor),
            // Binding 6: 
            vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6, &indexStorageBuffer.descriptor),
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

        bindingDescriptions = {
            // Binding point 0: Mesh vertex layout description at per-vertex rate
            vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(ClusterDesc), VK_VERTEX_INPUT_RATE_INSTANCE),
		};
        attributeDescriptions = {
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 0, VK_FORMAT_R32_UINT, offsetof(ClusterDesc, indexOffset)),
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 1, VK_FORMAT_R32_UINT, offsetof(ClusterDesc, instanceOffset)),
		};
        inputState.pVertexBindingDescriptions = bindingDescriptions.data();
        inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
        inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());

		// we do not need vertex input in plant pipeline.
		pipelineCreateInfo.pVertexInputState = &inputState;

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = loadShader(getShadersPath() + "noodlebatch/noodlebatch.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "noodlebatch/noodlebatch.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.plants));

        // Vertex input bindings
        // The instancing pipeline uses a vertex input state with two bindings
        bindingDescriptions = {
            // Binding point 0: Mesh vertex layout description at per-vertex rate
            vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(vkglTF::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
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
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),								// Location 0: Position
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),				// Location 1: Normal
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),					// Location 2: Texture coordinates
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),				// Location 3: Color
        };
        inputState.pVertexBindingDescriptions = bindingDescriptions.data();
        inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
        inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());

        pipelineCreateInfo.pVertexInputState = &inputState;
		// Only use non-instanced vertex attributes for models rendered without instancing
		inputState.vertexBindingDescriptionCount = 1;
		inputState.vertexAttributeDescriptionCount = 4;

		// Ground
		shaderStages[0] = loadShader(getShadersPath() + "noodlebatch/ground.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "noodlebatch/ground.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ground));

		// Skysphere
		shaderStages[0] = loadShader(getShadersPath() + "noodlebatch/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "noodlebatch/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skysphere));
	}

	// Prepare (and stage) a buffer containing the indirect draw commands
	void prepareIndirectData()
	{
		indirectCommands.clear();

        VkDrawIndexedIndirectCommand indirectCmd{};
        indirectCmd.instanceCount = clusterCount;
        indirectCmd.firstInstance = 0;
        indirectCmd.firstIndex = 0;
        indirectCmd.indexCount = CLUSTER_TRIANGLE_NUM * 3;
        indirectCommands.push_back(indirectCmd);

		indirectDrawCount = static_cast<uint32_t>(indirectCommands.size());

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


	void prepareIndexData()
    {
        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            models.plants.cpuIndices.size() * sizeof(uint32_t),
            models.plants.cpuIndices.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &indexStorageBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &indexStorageBuffer, queue);
        stagingBuffer.destroy();

        // create fixedIndexBuffer
        std::vector<uint32_t> fixedIndices;
        fixedIndices.resize(CLUSTER_TRIANGLE_NUM * 3);
        for ( int i = 0; i < CLUSTER_TRIANGLE_NUM * 3; i++ )
        {
            fixedIndices[i] = i;
        }
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            fixedIndices.size() * sizeof(uint32_t),
            fixedIndices.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &fixedIndexBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &fixedIndexBuffer, queue);
        stagingBuffer.destroy();
	}

    void prepareVertexData()
    {
        std::vector<VertexData> vertices;;
		vertices.resize(models.plants.cpuVertices.size());

        for ( uint32_t i = 0; i < vertices.size(); i++ )
        {
			vertices[i].pos = glm::vec4(models.plants.cpuVertices[i].pos, 0.0f);
			vertices[i].normal = models.plants.cpuVertices[i].normal;
			vertices[i].uv = models.plants.cpuVertices[i].uv;
			vertices[i].color = models.plants.cpuVertices[i].color;
        }

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
			vertices.size() * sizeof(VertexData),
			vertices.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &vertexDataStorageBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &vertexDataStorageBuffer, queue);
        stagingBuffer.destroy();
    }

	void prepareClusterData()
	{
		std::vector<ClusterDesc> descDatas;

		uint32_t descCount = 0;
		const uint32_t triangleIndexNum = 3;
		for ( int i = 0; i < models.plants.nodes.size(); i++ )
        {
			const vkglTF::Node* node = models.plants.nodes[i];
			descCount += node->mesh->primitives[0]->indexCount / triangleIndexNum / CLUSTER_TRIANGLE_NUM * OBJECT_INSTANCE_COUNT;
		}
		descDatas.resize(descCount);
        clusterCount = descCount;

        uint32_t descIndex = 0;
        uint32_t instanceIndex = 0;
        for ( uint32_t i = 0; i < models.plants.nodes.size(); i++ )
        {
            const vkglTF::Node* node = models.plants.nodes[i];
			const uint32_t firstIndex = node->mesh->primitives[0]->firstIndex;
			const uint32_t clusterNum = node->mesh->primitives[0]->indexCount / triangleIndexNum / CLUSTER_TRIANGLE_NUM;
			for ( uint32_t j = 0; j < OBJECT_INSTANCE_COUNT; j++ )
			{
				for ( uint32_t clusterIndex = 0; clusterIndex < clusterNum; clusterIndex++ )
                {
					descDatas[descIndex].indexOffset = firstIndex + clusterIndex * CLUSTER_TRIANGLE_NUM * triangleIndexNum;
					descDatas[descIndex].instanceOffset = instanceIndex;
					descIndex++;
				}// for cluster

				instanceIndex++;
			}// for instace
        }// for meshes

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
			descDatas.size() * sizeof(ClusterDesc),
			descDatas.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &clusterBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &clusterBuffer, queue);
        stagingBuffer.destroy();
	}

    // Prepare (and stage) a buffer containing instanced data for the mesh draws
    void prepareInstanceData()
    {
        objectCount = 0;
        for ( auto& node : models.plants.nodes )
        {
            if ( node->mesh )
            {
                objectCount += OBJECT_INSTANCE_COUNT;
            }
        }

        std::vector<InstanceData> instanceData;
		std::vector<InstanceTexIndexData> instanceTexIndexData;
        instanceData.resize(objectCount);
		instanceTexIndexData.resize(objectCount);

        std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
        std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

        for ( uint32_t i = 0; i < objectCount; i++ )
        {
            float theta = 2 * float(M_PI) * uniformDist(rndEngine);
            float phi = acos(1 - 2 * uniformDist(rndEngine));
            instanceData[i].rot = glm::vec3(0.0f, float(M_PI) * uniformDist(rndEngine), 0.0f);
            instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
            instanceData[i].scale = 1.0f + uniformDist(rndEngine) * 2.0f;

			instanceTexIndexData[i].texIndex = i / OBJECT_INSTANCE_COUNT;
        }

        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            instanceData.size() * sizeof(InstanceData),
            instanceData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &instanceStorageBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &instanceStorageBuffer, queue);
        stagingBuffer.destroy();


        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            instanceTexIndexData.size() * sizeof(InstanceTexIndexData),
            instanceTexIndexData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &instanceTexIndexStorageBuffer,
            stagingBuffer.size));

        vulkanDevice->copyBuffer(&stagingBuffer, &instanceTexIndexStorageBuffer, queue);
        stagingBuffer.destroy();
    }


	void prepareUniformBuffers()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformData.scene,
			sizeof(uboVS)));

		VK_CHECK_RESULT(uniformData.scene.map());

		updateUniformBuffer(true);
	}

	void updateUniformBuffer(bool viewChanged)
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

        // Read query results for displaying in next frame
        getQueryResults();

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
        loadAssets();
        setupQueryPool();
        prepareClusterData();
        prepareIndirectData();
		prepareIndexData();
		prepareVertexData();
		prepareInstanceData();
		prepareUniformBuffers();
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
			updateUniformBuffer(true);
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffer(true);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (!vulkanDevice->features.multiDrawIndirect) {
			if (overlay->header("Info")) {
				overlay->text("multiDrawIndirect not supported");
			}
		}
        if ( overlay->header("Statistics") )
        {
            overlay->text("Clusters: %d", clusterCount);
            overlay->text("Objects: %d", objectCount);
		}

        if ( !pipelineStats.empty() )
        {
            if ( overlay->header("Pipeline statistics") )
            {
                for ( auto i = 0; i < pipelineStats.size(); i++ )
                {
                    std::string caption = pipelineStatNames[i] + ": %d";
                    overlay->text(caption.c_str(), pipelineStats[i]);
                }
            }
        }
	}
};

VULKAN_EXAMPLE_MAIN()
