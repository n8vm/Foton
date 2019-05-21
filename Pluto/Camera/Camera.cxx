// #pragma optimize("", off)

#include "./Camera.hxx"
#include "Pluto/Libraries/Vulkan/Vulkan.hxx"
#include "Pluto/Texture/Texture.hxx"
#include "Pluto/Material/Material.hxx"
#include "Pluto/Mesh/Mesh.hxx"
#include "Pluto/Entity/Entity.hxx"
#include "Pluto/Transform/Transform.hxx"

#include <algorithm>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_DEPTH_ZERO_TO_ONE

Camera Camera::cameras[MAX_CAMERAS];
CameraStruct* Camera::pinnedMemory;
std::map<std::string, uint32_t> Camera::lookupTable;
vk::Buffer Camera::SSBO;
vk::DeviceMemory Camera::SSBOMemory;
vk::Buffer Camera::stagingSSBO;
vk::DeviceMemory Camera::stagingSSBOMemory;
std::mutex Camera::creation_mutex;
bool Camera::Initialized = false;
int32_t Camera::minRenderOrder = 0;
int32_t Camera::maxRenderOrder = 0;

using namespace Libraries;

void Camera::setup(uint32_t tex_width, uint32_t tex_height, uint32_t msaa_samples, uint32_t max_views, bool use_depth_prepass, bool use_multiview)
{
	if ((tex_width == 0) || (tex_height == 0)) {
		throw std::runtime_error("Error: width and height of camera " + name + " texture must be greater than zero!");
	}

	#if DISABLE_MULTIVIEW
	this->use_multiview = false;
	#else
	this->use_multiview = use_multiview;
	#endif

	this->use_depth_prepass = use_depth_prepass;
	if (max_views > MAX_MULTIVIEW)
    {
		throw std::runtime_error( std::string("Error: Camera component cannot render to more than " + std::to_string(MAX_MULTIVIEW) + " layers simultaneously."));
    }

	this->maxViews = max_views;
	set_view(glm::mat4(1.0), 0);
	// set_orthographic_projection(-1, 1, -1, 1, -1, 1);
	this->msaa_samples = msaa_samples;
	
	if (max_views == 6) {
		renderTexture = Texture::CreateCubemap(name, tex_width, tex_height, true, true);
		// TODO: Enable MSAA for cubemaps
	}
	else {
		renderTexture = Texture::Create2D(name, tex_width, tex_height, true, true, msaa_samples, max_views);
		if (msaa_samples != 1)
			resolveTexture = Texture::Create2D(name + "_resolve", tex_width, tex_height, true, true, 1, max_views);
	}

	for (uint32_t i = 0; i < max_views; ++i) {
		camera_struct.multiviews[i].tex_id = (msaa_samples != 1) ? resolveTexture->get_id() : renderTexture->get_id();
	}

	/* Since the main renderloop needs to use these command buffers, the main renderloop must create them */
	// create_command_buffers();
	// create_semaphores();
	create_fences();
	create_render_passes(max_views, msaa_samples);
	create_frame_buffers(max_views);
	create_query_pool();
	for(auto renderpass : renderpasses) {
		Material::SetupGraphicsPipelines(renderpass, msaa_samples, use_depth_prepass);
	}
	if (use_depth_prepass)
	{
		for(auto renderpass : depthPrepasses) {
			Material::SetupGraphicsPipelines(renderpass, msaa_samples, use_depth_prepass);
		}
	}
}

void Camera::create_query_pool()
{
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

	vk::QueryPoolCreateInfo queryPoolInfo;
	queryPoolInfo.queryType = vk::QueryType::eOcclusion;
	queryPoolInfo.queryCount = MAX_ENTITIES;
	queryPool = device.createQueryPool(queryPoolInfo);

	queryResults.resize(MAX_ENTITIES + 1, 1);
}

bool Camera::needs_command_buffers()
{
	return (command_buffer == vk::CommandBuffer());
}

void Camera::create_command_buffers()
{
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();
	vk::CommandBufferAllocateInfo cmdAllocInfo;
    cmdAllocInfo.commandPool = vulkan->get_command_pool();
    cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandBufferCount = 1;
    command_buffer = device.allocateCommandBuffers(cmdAllocInfo)[0];
}

// void Camera::create_semaphores()
// {
// 	auto vulkan = Libraries::Vulkan::Get();
// 	auto device = vulkan->get_device();

// 	uint32_t max_frames_in_flight = 2;
// 	vk::SemaphoreCreateInfo semaphoreInfo;
	
// 	semaphores.resize(max_frames_in_flight);
// 	for (uint32_t frame = 0; frame < max_frames_in_flight; ++frame) {
// 		semaphores[frame] = device.createSemaphore(semaphoreInfo);
// 	}
// }

void Camera::create_fences()
{
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

	uint32_t max_frames_in_flight = 2;
	vk::FenceCreateInfo fenceInfo;

	fences.resize(max_frames_in_flight);
	for (uint32_t frame = 0; frame < max_frames_in_flight; ++frame) {
		fences[frame] = device.createFence(fenceInfo);
	}
}

void Camera::create_render_passes(uint32_t layers, uint32_t sample_count)
{
    renderpasses.clear();
    depthPrepasses.clear();
    
    int iterations = (use_multiview) ? 1 : layers;
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

	auto sampleFlag = vulkan->highest(vulkan->min(vulkan->get_closest_sample_count_flag(sample_count), vulkan->get_msaa_sample_flags()));

    for(int i = 0; i < iterations; i++) {
        #pragma region ColorAttachment
        // Color attachment
        vk::AttachmentDescription colorAttachment;
        colorAttachment.format = renderTexture->get_color_format(); // TODO
        colorAttachment.samples = sampleFlag;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear; // clears image to black
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference colorAttachmentRef;
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
        #pragma endregion

        #pragma region CreateDepthAttachment
        vk::AttachmentDescription depthAttachment;
        depthAttachment.format = renderTexture->get_depth_format();
        depthAttachment.samples = sampleFlag;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore; 
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef;
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        #pragma endregion

        #pragma region ColorAttachmentResolve
        // Color attachment
        vk::AttachmentDescription colorAttachmentResolve;
        colorAttachmentResolve.format = renderTexture->get_color_format(); // TODO
        colorAttachmentResolve.samples = vk::SampleCountFlagBits::e1;
        colorAttachmentResolve.loadOp = vk::AttachmentLoadOp::eDontCare; // dont clear
        colorAttachmentResolve.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachmentResolve.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachmentResolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachmentResolve.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachmentResolve.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference colorAttachmentResolveRef;
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
        #pragma endregion

        #pragma region CreateSubpass
        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        if (msaa_samples != 1)
            subpass.pResolveAttachments = &colorAttachmentResolveRef;

        // Use subpass dependencies for layout transitions
        std::array<vk::SubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;
        #pragma endregion

        #pragma region CreateRenderPass

        uint32_t mask = 0;
        for (uint32_t i = 0; i < layers; ++i)
            mask |= 1 << i;

        // Support for multiview
        const uint32_t viewMasks[] = {mask};
        const uint32_t correlationMasks[] = {mask};

        vk::RenderPassMultiviewCreateInfo renderPassMultiviewInfo;
        renderPassMultiviewInfo.subpassCount = 1;
        renderPassMultiviewInfo.pViewMasks = viewMasks;
        renderPassMultiviewInfo.dependencyCount = 0;
        renderPassMultiviewInfo.pViewOffsets = NULL;
        renderPassMultiviewInfo.correlationMaskCount = 1;
        renderPassMultiviewInfo.pCorrelationMasks = correlationMasks;

        /* Create the render pass */
        std::vector<vk::AttachmentDescription> attachments = {colorAttachment, depthAttachment};
        if (msaa_samples != 1) attachments.push_back(colorAttachmentResolve);
        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = (uint32_t) attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = (uint32_t) dependencies.size();
        renderPassInfo.pDependencies = dependencies.data();
        renderPassInfo.pNext = (use_multiview) ? (&renderPassMultiviewInfo) : nullptr;
		if (use_depth_prepass)
		{
			/* Depth prepass will transition from undefined to optimal now. */
			attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachments[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			attachments[1].loadOp = vk::AttachmentLoadOp::eDontCare;
			attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
			renderpasses.push_back(device.createRenderPass(renderPassInfo));
			
			/* Transition from undefined to attachment optimal in depth prepass. */
			attachments[0].loadOp = vk::AttachmentLoadOp::eDontCare; // dont clear
			attachments[0].storeOp = vk::AttachmentStoreOp::eDontCare;
			attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
			attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

			attachments[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
			attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
        	depthPrepasses.push_back(device.createRenderPass(renderPassInfo));
		} else {
			renderpasses.push_back(device.createRenderPass(renderPassInfo));
		}
    }
	#pragma endregion
}

void Camera::create_frame_buffers(uint32_t layers) {
    framebuffers.clear();
    
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

	if (use_multiview) {
		vk::ImageView attachments[3];
		attachments[0] = renderTexture->get_color_image_view();
		attachments[1] = renderTexture->get_depth_image_view();
		if (msaa_samples != 1)
			attachments[2] = resolveTexture->get_color_image_view();

		vk::FramebufferCreateInfo fbufCreateInfo;
		fbufCreateInfo.renderPass = renderpasses[0];
		fbufCreateInfo.attachmentCount = (msaa_samples == 1) ? 2 : 3;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = renderTexture->get_width();
		fbufCreateInfo.height = renderTexture->get_height();
		fbufCreateInfo.layers = renderTexture->get_total_layers();

		framebuffers.push_back(device.createFramebuffer(fbufCreateInfo));

		if (use_depth_prepass)
		{
			fbufCreateInfo.renderPass = depthPrepasses[0];
			depthPrepassFramebuffers.push_back(device.createFramebuffer(fbufCreateInfo));
		}
	}
	else {
		for(uint32_t i = 0; i < layers; i++) {
			vk::ImageView attachments[3];
			attachments[0] = renderTexture->get_color_image_view_layers()[i];
			attachments[1] = renderTexture->get_depth_image_view_layers()[i];
			if (msaa_samples != 1)
				attachments[2] = resolveTexture->get_color_image_view_layers()[i];
			
			vk::FramebufferCreateInfo fbufCreateInfo;
			fbufCreateInfo.renderPass = renderpasses[i];
			fbufCreateInfo.attachmentCount = (msaa_samples == 1) ? 2 : 3;
			fbufCreateInfo.pAttachments = attachments;
			fbufCreateInfo.width = renderTexture->get_width();
			fbufCreateInfo.height = renderTexture->get_height();
			fbufCreateInfo.layers = 1;
			
			framebuffers.push_back(device.createFramebuffer(fbufCreateInfo));

			if (use_depth_prepass)
			{
				fbufCreateInfo.renderPass = depthPrepasses[i];
				depthPrepassFramebuffers.push_back(device.createFramebuffer(fbufCreateInfo));
			}
		}
	}
}

void Camera::update_used_views(uint32_t multiview) {
	usedViews = (usedViews >= multiview) ? usedViews : multiview;
}

void Camera::check_multiview_index(uint32_t multiview) {
	if (multiview >= maxViews)
    {
		throw std::runtime_error( std::string("Error: multiview index is larger than " + std::to_string(maxViews) ));
    }
}

Camera::Camera() {
	this->initialized = false;
}

Camera::Camera(std::string name, uint32_t id) {
	this->initialized = true;
	this->name = name;
	this->id = id;
	this->renderModeOverride = RenderMode::RENDER_MODE_NONE;

	this->render_complete_mutex = std::make_shared<std::mutex>();
	this->cv = std::make_shared<std::condition_variable>();
}

// glm::mat4 MakeInfReversedZOrthoRH(float left, float right, float bottom, float top, float zNear)
// {
// 	return glm::mat4 (
// 		2.0f / (right - left), 0.0f,  0.0f,  0.0f,
// 		0.0f,    2.0f / (top - bottom),  0.0f,  0.0f,
// 		0.0f, 0.0f,  0.0, -1.0f,
// 		0.0, 0.0, zNear,  0.0f
// 	);
// }

// bool Camera::set_orthographic_projection(float left, float right, float bottom, float top, float near_pos, uint32_t multiview)
// {
// 	check_multiview_index(multiview);
// 	camera_struct.multiviews[multiview].near_pos = near_pos;
// 	camera_struct.multiviews[multiview].proj = MakeInfReversedZOrthoRH(left, right, bottom, top, near_pos);
// 	camera_struct.multiviews[multiview].projinv = glm::inverse(camera_struct.multiviews[multiview].proj);
// 	return true;
// };

glm::mat4 MakeInfReversedZProjRH(float fovY_radians, float aspectWbyH, float zNear)
{
    float f = 1.0f / tan(fovY_radians / 2.0f);
    return glm::mat4(
        f / aspectWbyH, 0.0f,  0.0f,  0.0f,
                  0.0f,    f,  0.0f,  0.0f,
                  0.0f, 0.0f,  0.0f, -1.0f,
                  0.0f, 0.0f, zNear,  0.0f);
}

glm::mat4 MakeProjRH(float fovY_radians, float aspectWbyH, float zNear)
{
	auto proj = glm::perspectiveFov(fovY_radians, aspectWbyH, 1.0f, zNear, 1000.0f);
	return proj;
}

void Camera::set_perspective_projection(float fov_in_radians, float width, float height, float near_pos, uint32_t multiview)
{
	check_multiview_index(multiview);
	camera_struct.multiviews[multiview].near_pos = near_pos;
	#ifndef DISABLE_REVERSE_Z
	camera_struct.multiviews[multiview].proj = MakeInfReversedZProjRH(fov_in_radians, width / height, near_pos);
	set_clear_depth(0.0);
	#else
	camera_struct.multiviews[multiview].proj = MakeProjRH(fov_in_radians, width / height, near_pos);
	set_clear_depth(1.0);
	#endif
	camera_struct.multiviews[multiview].projinv = glm::inverse(camera_struct.multiviews[multiview].proj);
};

void Camera::set_custom_projection(glm::mat4 custom_projection, float near_pos, uint32_t multiview)
{
	check_multiview_index(multiview);
	camera_struct.multiviews[multiview].near_pos = near_pos;
	camera_struct.multiviews[multiview].proj = custom_projection;
	camera_struct.multiviews[multiview].projinv = glm::inverse(custom_projection);
}

float Camera::get_near_pos(uint32_t multiview) { 
	check_multiview_index(multiview);
	return camera_struct.multiviews[multiview].near_pos; 
}

glm::mat4 Camera::get_view(uint32_t multiview) { 
	check_multiview_index(multiview);
	return camera_struct.multiviews[multiview].view; 
};

void Camera::set_view(glm::mat4 view, uint32_t multiview)
{
	check_multiview_index(multiview);
	update_used_views(multiview + 1);
	camera_struct.multiviews[multiview].view = view;
	camera_struct.multiviews[multiview].viewinv = glm::inverse(view);
};

void Camera::set_render_order(int32_t order) {
	renderOrder = order;
	
	/* Update min/max render orders */
	for (auto &camera : cameras) {
		if (!camera.is_initialized()) continue;
		minRenderOrder = std::min(minRenderOrder, camera.get_render_order());
		maxRenderOrder = std::max(maxRenderOrder, camera.get_render_order());
	}
}

int32_t Camera::get_render_order()
{
	return renderOrder;
}

int32_t Camera::GetMinRenderOrder() {
	return minRenderOrder;
}

int32_t Camera::GetMaxRenderOrder() {
	return maxRenderOrder;
}

glm::mat4 Camera::get_projection(uint32_t multiview) { 
	check_multiview_index(multiview);
	return camera_struct.multiviews[multiview].proj; 
};


Texture* Camera::get_texture()
{
	if (msaa_samples == 1) return renderTexture;
	else return resolveTexture;
}

/* TODO: Explain this */
uint32_t Camera::get_max_views()
{
	return maxViews;
}

std::string Camera::to_string()
{
	std::string output;
	output += "{\n";
	output += "\ttype: \"Camera\",\n";
	output += "\tname: \"" + name + "\",\n";
	output += "\tused_views: " + std::to_string(usedViews) + ",\n";
	output += "\tprojections: [\n";
	for (uint32_t i = 0; i < usedViews; ++i)
	{
		output += "\t\t";
		output += glm::to_string(camera_struct.multiviews[i].proj);
		output += (i == (usedViews - 1)) ? "\n" : ",\n";
	}
	output += "\t],\n";
	output += "\tviews: [\n";
	for (uint32_t i = 0; i < usedViews; ++i)
	{
		output += "\t\t";
		output += glm::to_string(camera_struct.multiviews[i].view);
		output += (i == (usedViews - 1)) ? "\n" : ",\n";
		output += "\t\tnear_pos: " + std::to_string(camera_struct.multiviews[i].near_pos) + ",\n";
		output += "\t\tfov: " + std::to_string(camera_struct.multiviews[i].fov) + "\n";
	}
	output += "\t],\n";
	output += "}";
	return output;
}

// this should be in the render system...
void Camera::begin_renderpass(vk::CommandBuffer command_buffer, uint32_t index)
{
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));

	if (!use_depth_prepass) {
		auto m = render_complete_mutex.get();
		std::lock_guard<std::mutex> lk(*m);
		render_ready = false;
	}

	renderTexture->make_renderable(command_buffer);
	
	vk::RenderPassBeginInfo rpInfo;
	rpInfo.renderPass = renderpasses[index];
	rpInfo.framebuffer = framebuffers[index];
    rpInfo.renderArea.offset = vk::Offset2D{0, 0};
	rpInfo.renderArea.extent = vk::Extent2D{renderTexture->get_width(), renderTexture->get_height()};

	std::vector<vk::ClearValue> clearValues;

	clearValues.push_back(vk::ClearValue());
	clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
	
	if (!use_depth_prepass){
		clearValues.push_back(vk::ClearValue());
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(clearDepth, clearStencil);
	}

	rpInfo.clearValueCount = (uint32_t)clearValues.size();
	rpInfo.pClearValues = clearValues.data();

	/* Start the render pass */
	command_buffer.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

	/* Set viewport*/
	vk::Viewport viewport;
	viewport.width = (float)renderTexture->get_width();
	viewport.height = -(float)renderTexture->get_height();
	viewport.y = (float)renderTexture->get_height();
	viewport.x = 0;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	command_buffer.setViewport(0, {viewport});

	/* Set Scissors */
	vk::Rect2D rect2D;
	rect2D.extent.width = renderTexture->get_width();
	rect2D.extent.height = renderTexture->get_height();
	rect2D.offset.x = 0;
	rect2D.offset.y = 0;

	command_buffer.setScissor(0, {rect2D});
}

void Camera::end_renderpass(vk::CommandBuffer command_buffer, uint32_t index) {
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
		
	command_buffer.endRenderPass();

	renderTexture->overrideColorImageLayout(vk::ImageLayout::eColorAttachmentOptimal);	
	renderTexture->overrideDepthImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);	

	// /* hack for now*/ 
	if (renderOrder < 0) {
		renderTexture->make_samplable(command_buffer);
	}
}

void Camera::begin_depth_prepass(vk::CommandBuffer command_buffer, uint32_t index)
{
	if (!use_depth_prepass)
		throw std::runtime_error( std::string("Error: depth prepass not enabled on this camera"));

    if(index >= depthPrepasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));

	auto m = render_complete_mutex.get();
	std::lock_guard<std::mutex> lk(*m);
	render_ready = false;
	
	renderTexture->make_renderable(command_buffer);

	vk::RenderPassBeginInfo rpInfo;
	rpInfo.renderPass = depthPrepasses[index];
	rpInfo.framebuffer = depthPrepassFramebuffers[index];
    rpInfo.renderArea.offset = vk::Offset2D{0, 0};
	rpInfo.renderArea.extent = vk::Extent2D{renderTexture->get_width(), renderTexture->get_height()};

	std::array<vk::ClearValue, 2> clearValues = {};
	clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
	clearValues[1].depthStencil = vk::ClearDepthStencilValue(clearDepth, clearStencil);

	rpInfo.clearValueCount = (uint32_t)clearValues.size();
	rpInfo.pClearValues = clearValues.data();

	/* Start the render pass */
	command_buffer.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

	/* Set viewport*/
	vk::Viewport viewport;
	viewport.width = (float)renderTexture->get_width();
	viewport.height = -(float)renderTexture->get_height();
	viewport.y = (float)renderTexture->get_height();
	viewport.x = 0;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	command_buffer.setViewport(0, {viewport});

	/* Set Scissors */
	vk::Rect2D rect2D;
	rect2D.extent.width = renderTexture->get_width();
	rect2D.extent.height = renderTexture->get_height();
	rect2D.offset.x = 0;
	rect2D.offset.y = 0;

	command_buffer.setScissor(0, {rect2D});
}

void Camera::end_depth_prepass(vk::CommandBuffer command_buffer, uint32_t index) {
	if (!use_depth_prepass)
		throw std::runtime_error( std::string("Error: depth prepass not enabled on this camera"));

    if(index >= depthPrepasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
		
	command_buffer.endRenderPass();

	renderTexture->overrideColorImageLayout(vk::ImageLayout::eColorAttachmentOptimal);	
	renderTexture->overrideDepthImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);	
}

vk::RenderPass Camera::get_renderpass(uint32_t index)
{
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
	return renderpasses[index];
}

vk::RenderPass Camera::get_depth_prepass(uint32_t index)
{
    if(index >= depthPrepasses.size())
        throw std::runtime_error( std::string("Error: depthPrepasses index out of bounds"));
	return depthPrepasses[index];
}

uint32_t Camera::get_num_renderpasses() {
    return (uint32_t) renderpasses.size();
}

vk::CommandBuffer Camera::get_command_buffer() {
	return command_buffer;
}

vk::QueryPool Camera::get_query_pool()
{
	return queryPool;
}

void Camera::reset_query_pool(vk::CommandBuffer command_buffer)
{
	// This might need to be changed, depending on if MAX_ENTITIES is the "number of queries in the query pool".
	// The spec might be referring to visable_entities.size() instead...
	// if (queryDownloaded) {
		command_buffer.resetQueryPool(queryPool, 0, MAX_ENTITIES); 
		max_queried = 0;
	// }

	queryRecorded = false;

}

void Camera::download_query_pool_results()
{
	if (!visibilityTestingPaused)
	{
		if (queryPool == vk::QueryPool()) return;

		if (!queryRecorded) return;
		
		if (max_queried == 0) return;

		auto vulkan = Libraries::Vulkan::Get();
		auto device = vulkan->get_device();
		// vulkan->flush_queues();


		uint32_t num_queries = max_queried;
		uint32_t data_size = sizeof(uint64_t) * (MAX_ENTITIES); // Why do I need an extra number here?
		uint32_t first_query = 0;
		uint32_t stride = sizeof(uint64_t);

		std::vector<uint64_t> temp(MAX_ENTITIES, 0);
		auto result = device.getQueryPoolResults(queryPool, first_query, num_queries, 
				data_size, temp.data(), stride, // wait locks up... with availability requires an extra integer?
				vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::ePartial | vk::QueryResultFlagBits::eWithAvailability); //  vk::QueryResultFlagBits::ePartial | //| vk::QueryResultFlagBits::eWait

		if (result == vk::Result::eSuccess) {
			for (uint32_t i = 0; i < max_queried; ++i) {
				if (temp[i] > 0) {
					queryResults[i] = temp[i] - 1;
				}
			}
			previousEntityToDrawIdx = entityToDrawIdx;
			queryDownloaded = true;
		}
		else {
			// std::cout<<"camera " << id << " " << vk::to_string(result)<<std::endl;
		}
	}
}

std::vector<uint64_t> & Camera::get_query_pool_results()
{
		// /* For some reason, multiview doesn't seem to work with this? */
	// if (usedViews > 0) {
	// 	queryResults = std::vector<uint64_t>(MAX_ENTITIES, 2);
	// 	return queryResults;
	// }


	return queryResults;
}

// TODO: Improve performance of this. We really shouldn't be using a map like this...
bool Camera::is_entity_visible(uint32_t entity_id)
{
	/* Assume true if not queried. */
	if (previousEntityToDrawIdx.find( entity_id ) == previousEntityToDrawIdx.end()) return true;
	
	/* This should never happen, but if it does, assume the object is visible */
	if (previousEntityToDrawIdx[entity_id] >= queryResults.size()) return true;
	
	return (queryResults[previousEntityToDrawIdx[entity_id]]) > 0;
}

void Camera::begin_visibility_query(vk::CommandBuffer command_buffer, uint32_t entity_id, uint32_t draw_idx)
{
	if (!visibilityTestingPaused)
	{
		queryRecorded = true;
		queryDownloaded = false;
		entityToDrawIdx[entity_id] = draw_idx;
		command_buffer.beginQuery(queryPool, draw_idx, vk::QueryControlFlags());
	}
}

void Camera::pause_visibility_testing()
{
	visibilityTestingPaused = true;
}

void Camera::resume_visibility_testing()
{
	visibilityTestingPaused = false;
}

void Camera::end_visibility_query(vk::CommandBuffer command_buffer, uint32_t entity_id, uint32_t draw_idx)
{
	if (!visibilityTestingPaused)
	{
		max_queried = std::max((uint64_t)draw_idx + 1, max_queried);
		command_buffer.endQuery(queryPool, draw_idx);
	}
}

// vk::Semaphore Camera::get_semaphore(uint32_t frame_idx) {
// 	return semaphores[frame_idx];
// }

vk::Fence Camera::get_fence(uint32_t frame_idx) {
	return fences[frame_idx];
}

void Camera::set_clear_color(float r, float g, float b, float a) {
	clearColor = glm::vec4(r, g, b, a);
}

void Camera::set_clear_stencil(uint32_t stencil) {
	clearStencil = stencil;
}

void Camera::set_clear_depth(float depth) {
	clearDepth = depth;
}

/* SSBO Logic */
void Camera::Initialize()
{
	if (IsInitialized()) return;

	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();
	auto physical_device = vulkan->get_physical_device();

	{
		vk::BufferCreateInfo bufferInfo = {};
		bufferInfo.size = MAX_CAMERAS * sizeof(CameraStruct);
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		stagingSSBO = device.createBuffer(bufferInfo);

		vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(stagingSSBO);
		vk::MemoryAllocateInfo allocInfo = {};
		allocInfo.allocationSize = memReqs.size;

		vk::PhysicalDeviceMemoryProperties memProperties = physical_device.getMemoryProperties();
		vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
		allocInfo.memoryTypeIndex = vulkan->find_memory_type(memReqs.memoryTypeBits, properties);

		stagingSSBOMemory = device.allocateMemory(allocInfo);
		device.bindBufferMemory(stagingSSBO, stagingSSBOMemory, 0);
	}

	{
		vk::BufferCreateInfo bufferInfo = {};
		bufferInfo.size = MAX_CAMERAS * sizeof(CameraStruct);
		bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
		SSBO = device.createBuffer(bufferInfo);

		vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(SSBO);
		vk::MemoryAllocateInfo allocInfo = {};
		allocInfo.allocationSize = memReqs.size;

		vk::PhysicalDeviceMemoryProperties memProperties = physical_device.getMemoryProperties();
		vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
		allocInfo.memoryTypeIndex = vulkan->find_memory_type(memReqs.memoryTypeBits, properties);

		SSBOMemory = device.allocateMemory(allocInfo);
		device.bindBufferMemory(SSBO, SSBOMemory, 0);
	}

	Initialized = true;
}

bool Camera::IsInitialized()
{
    return Initialized;
}

void Camera::UploadSSBO(vk::CommandBuffer command_buffer)
{
	auto vulkan = Libraries::Vulkan::Get();
    auto device = vulkan->get_device();

    if (SSBOMemory == vk::DeviceMemory()) return;
    if (stagingSSBOMemory == vk::DeviceMemory()) return;
    
    auto bufferSize = MAX_CAMERAS * sizeof(CameraStruct);

	/* Pin the buffer */
	pinnedMemory = (CameraStruct*) device.mapMemory(stagingSSBOMemory, 0, bufferSize);
	if (pinnedMemory == nullptr) return;
	
	/* TODO: remove this for loop */
	for (uint32_t i = 0; i < MAX_CAMERAS; ++i) {
		if (!cameras[i].is_initialized()) continue;
		pinnedMemory[i] = cameras[i].camera_struct;

		for (uint32_t j = 0; j < cameras[i].maxViews; ++j) {
			pinnedMemory[i].multiviews[j].viewinv = glm::inverse(pinnedMemory[i].multiviews[j].view);
			pinnedMemory[i].multiviews[j].projinv = glm::inverse(pinnedMemory[i].multiviews[j].proj);
			pinnedMemory[i].multiviews[j].viewproj = pinnedMemory[i].multiviews[j].proj * pinnedMemory[i].multiviews[j].view;
		}
	};

	device.unmapMemory(stagingSSBOMemory);

	vk::BufferCopy copyRegion;
	copyRegion.size = bufferSize;
    command_buffer.copyBuffer(stagingSSBO, SSBO, copyRegion);
}

vk::Buffer Camera::GetSSBO()
{
	return SSBO;
}

uint32_t Camera::GetSSBOSize()
{
	return MAX_CAMERAS * sizeof(CameraStruct);
}

void Camera::CleanUp()
{
	if (!IsInitialized()) return;

	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

	if (SSBO != vk::Buffer()) device.destroyBuffer(SSBO);
    if (SSBOMemory != vk::DeviceMemory()) device.freeMemory(SSBOMemory);

	if (stagingSSBO != vk::Buffer()) device.destroyBuffer(stagingSSBO);
    if (stagingSSBOMemory != vk::DeviceMemory()) device.freeMemory(stagingSSBOMemory);

	for (auto &camera : cameras) {
		if (camera.initialized) {
			camera.cleanup();
			Camera::Delete(camera.id);
		}
	}

	SSBO = vk::Buffer();
    SSBOMemory = vk::DeviceMemory();
    stagingSSBO = vk::Buffer();
    stagingSSBOMemory = vk::DeviceMemory();

	Initialized = false;
}	


/* Static Factory Implementations */
Camera* Camera::Create(std::string name,uint32_t tex_width, uint32_t tex_height, uint32_t msaa_samples, uint32_t max_views, bool use_depth_prepass, bool use_multiview)
{
	std::lock_guard<std::mutex> lock(creation_mutex);
	auto camera = StaticFactory::Create(name, "Camera", lookupTable, cameras, MAX_CAMERAS);
	try {
		camera->setup(tex_width, tex_height, msaa_samples, max_views, use_depth_prepass, use_multiview);
		return camera;
	} catch (...) {
		StaticFactory::DeleteIfExists(name, "Camera", lookupTable, cameras, MAX_CAMERAS);
		throw;
	}
}

Camera* Camera::Get(std::string name) {
	return StaticFactory::Get(name, "Camera", lookupTable, cameras, MAX_CAMERAS);
}

Camera* Camera::Get(uint32_t id) {
	return StaticFactory::Get(id, "Camera", lookupTable, cameras, MAX_CAMERAS);
}

void Camera::Delete(std::string name) {
	StaticFactory::Delete(name, "Camera", lookupTable, cameras, MAX_CAMERAS);
}

void Camera::Delete(uint32_t id) {
	StaticFactory::Delete(id, "Camera", lookupTable, cameras, MAX_CAMERAS);
}

Camera* Camera::GetFront() {
	return cameras;
}

uint32_t Camera::GetCount() {
	return MAX_CAMERAS;
}

std::vector<Camera *> Camera::GetCamerasByOrder(uint32_t order)
{
	/* Todo: improve the performance of this. */
	std::vector<Camera *> selected_cameras;
	for (uint32_t i = 0; i < MAX_CAMERAS; ++i) {
		if (!cameras[i].is_initialized()) continue;

		if (cameras[i].renderOrder == order) {
			selected_cameras.push_back(&cameras[i]);
		}
	}
	return selected_cameras;
}

void Camera::cleanup()
{
	if (!initialized) return;

	auto vulkan = Vulkan::Get();
	auto device = vulkan->get_device();

	if (command_buffer)
	{
		device.freeCommandBuffers(vulkan->get_command_pool(), {command_buffer});
		command_buffer = vk::CommandBuffer();
	}
    if (renderpasses.size() > 0) {
        for(auto renderpass : renderpasses) {
            device.destroyRenderPass(renderpass);
        }
		renderpasses.clear();
    }
	if (depthPrepasses.size() > 0) {
        for(auto renderpass : depthPrepasses) {
            device.destroyRenderPass(renderpass);
        }
		depthPrepasses.clear();
    }

	uint32_t max_frames_in_flight = 2;
	// for (uint32_t frame = 0; frame < max_frames_in_flight; ++frame) {
	// 	device.destroySemaphore(semaphores[frame]);
	// }

	if (queryPool != vk::QueryPool()) {
		device.destroyQueryPool(queryPool);
		queryPool = vk::QueryPool();
	}
}

void Camera::force_render_mode(RenderMode rendermode)
{
	renderModeOverride = rendermode;
}

RenderMode Camera::get_rendermode_override() {
	return renderModeOverride;
}

bool Camera::should_record_depth_prepass()
{
	return use_depth_prepass;
}

bool Camera::should_use_multiview()
{
	return use_multiview;
}

enum side { LEFT = 0, RIGHT = 1, TOP = 3, BOTTOM = 2, BACK = 4, FRONT = 5 };

bool checkSphere(std::array<glm::vec4, 6> &planes, glm::vec3 pos, float radius)
{
	// The point is the center of
	// the radius.  So, the point might be outside of the frustum, but it doesn't
	// mean that the rest of the sphere is.  It could be half and half.  So instead of
	// checking if it's less than 0, we need to add on the radius to that.  Say the
	// equation produced -2, which means the center of the sphere is the distance of
	// 2 behind the plane.  Well, what if the radius was 5?  The sphere is still inside,
	// so we would say, if(-2 < -5) then we are outside.  In that case it's false,
	// so we are inside of the frustum, but a distance of 3.  This is reflected below.

	for (auto i = 0; i < planes.size(); i++)
	{
		if ((planes[i].x * pos.x) + (planes[i].y * pos.y) + (planes[i].z * pos.z) + planes[i].w < -radius)
		{
			return false;
		}
	}
	return true;
}

std::vector<std::vector<std::pair<float, Entity*>>> Camera::get_visible_entities(uint32_t camera_entity_id)
{
	if (!visibilityTestingPaused) {
		std::vector<std::vector<Entity*>> visible_entities;

		Entity* entities = Entity::GetFront();
		auto camera_entity = Entity::Get(camera_entity_id);
		if (!camera_entity) return {};
		if (camera_entity->get_transform() == nullptr) return {};
		auto cam_transform = camera_entity->get_transform();
		if (!cam_transform) return {};
		glm::vec3 cam_pos = cam_transform->get_position();

		for (uint32_t i = 0; i < get_max_views(); ++i)
			visible_entities.push_back(std::vector<Entity*>());
	
		/* Test each entities bounding sphere against the frustum */
		for (uint32_t view_idx = 0; view_idx < usedViews; ++view_idx) {
			for (uint32_t i = 0; i < Entity::GetCount(); ++i) 
			{
				if (i == camera_entity_id) continue;
				
				if (!entities[i].is_initialized()) continue;
				auto mesh = entities[i].get_mesh();
				auto transform = entities[i].get_transform();
				if ((mesh == nullptr) || (transform == nullptr)) continue;

				std::array<glm::vec4, 6> planes;

				/* Get projection planes for frustum/sphere intersection */
				auto matrix = camera_struct.multiviews[view_idx].proj * camera_struct.multiviews[view_idx].view * 
					cam_transform->get_parent_to_local_rotation_matrix() * cam_transform->get_parent_to_local_translation_matrix() * transform->get_local_to_world_matrix();

				planes[LEFT].x = matrix[0].w + matrix[0].x;
				planes[LEFT].y = matrix[1].w + matrix[1].x;
				planes[LEFT].z = matrix[2].w + matrix[2].x;
				planes[LEFT].w = matrix[3].w + matrix[3].x;

				planes[RIGHT].x = matrix[0].w - matrix[0].x;
				planes[RIGHT].y = matrix[1].w - matrix[1].x;
				planes[RIGHT].z = matrix[2].w - matrix[2].x;
				planes[RIGHT].w = matrix[3].w - matrix[3].x;

				planes[TOP].x = matrix[0].w - matrix[0].y;
				planes[TOP].y = matrix[1].w - matrix[1].y;
				planes[TOP].z = matrix[2].w - matrix[2].y;
				planes[TOP].w = matrix[3].w - matrix[3].y;

				planes[BOTTOM].x = matrix[0].w + matrix[0].y;
				planes[BOTTOM].y = matrix[1].w + matrix[1].y;
				planes[BOTTOM].z = matrix[2].w + matrix[2].y;
				planes[BOTTOM].w = matrix[3].w + matrix[3].y;

				planes[BACK].x = matrix[0].w + matrix[0].z;
				planes[BACK].y = matrix[1].w + matrix[1].z;
				planes[BACK].z = matrix[2].w + matrix[2].z;
				planes[BACK].w = matrix[3].w + matrix[3].z;

				planes[FRONT].x = matrix[0].w - matrix[0].z;
				planes[FRONT].y = matrix[1].w - matrix[1].z;
				planes[FRONT].z = matrix[2].w - matrix[2].z;
				planes[FRONT].w = matrix[3].w - matrix[3].z;

				for (auto i = 0; i < planes.size(); i++)
				{
					float length = sqrtf(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
					planes[i] /= length;
				}

				auto centroid = mesh->get_centroid();
				auto radius = mesh->get_bounding_sphere_radius();

				bool entity_seen = false;
				
				entity_seen |= checkSphere(planes, centroid, radius);

				if (entity_seen) visible_entities[view_idx].push_back(&entities[i]);
			}
		}

		/* If we're using multiview, there's only one renderpass, so we merge all frustum test results together. */
		if (use_multiview) {
			std::set<Entity*> merged_entities;
			for (uint32_t idx = 0; idx < visible_entities.size(); ++idx) {
				for (uint32_t e_id = 0; e_id < visible_entities[idx].size(); ++e_id) {
					merged_entities.insert(visible_entities[idx][e_id]);
				}
			}
			visible_entities.clear();
			visible_entities.push_back(std::vector<Entity*>());
			visible_entities[0].insert(visible_entities[0].end(), merged_entities.begin(), merged_entities.end());
		}

		/* Sort by depth */
		std::vector<std::vector<std::pair<float, Entity*>>> sorted_visible_entities;
		for (uint32_t idx = 0; idx < visible_entities.size(); ++idx) {
			sorted_visible_entities.push_back(std::vector<std::pair<float, Entity*>>());
			for (uint32_t i = 0; i < visible_entities[idx].size(); ++i) 
			{
				auto transform = visible_entities[idx][i]->get_transform();
				auto mesh = visible_entities[idx][i]->get_mesh();

				auto centroid = mesh->get_centroid();
				auto w_centroid =  glm::vec3(transform->get_local_to_world_matrix() * glm::vec4(centroid.x, centroid.y, centroid.z, 1.0));

				sorted_visible_entities[idx].push_back(std::pair<float, Entity*>(glm::distance(cam_pos, w_centroid), visible_entities[idx][i]));
			}

			std::sort(sorted_visible_entities[idx].begin(), sorted_visible_entities[idx].end(), std::less<std::pair<float, Entity*>>());	
		}
		frustum_culling_results = sorted_visible_entities;
		return sorted_visible_entities;
	}
	else {
		return frustum_culling_results;
	}
	
}

void Camera::mark_render_as_complete()
{
	{
		auto m = render_complete_mutex.get();
		std::lock_guard<std::mutex> lk(*m);
		render_ready = true;
	}
	cv->notify_one();
}

void Camera::wait_for_render_complete()
{
	// Wait until main() sends data
	auto m = render_complete_mutex.get();
    std::unique_lock<std::mutex> lk(*m);
	render_ready = false;
    cv->wait(lk, [this]{return render_ready;});
	render_ready = false;
	lk.unlock();
}