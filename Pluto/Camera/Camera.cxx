#include "./Camera.hxx"
#include "Pluto/Libraries/Vulkan/Vulkan.hxx"
#include "Pluto/Texture/Texture.hxx"
#include "Pluto/Material/Material.hxx"

Camera Camera::cameras[MAX_CAMERAS];
CameraStruct* Camera::pinnedMemory;
std::map<std::string, uint32_t> Camera::lookupTable;
vk::Buffer Camera::ssbo;
vk::DeviceMemory Camera::ssboMemory;

using namespace Libraries;

void Camera::setup(bool allow_recording, bool cubemap, uint32_t tex_width, uint32_t tex_height, uint32_t msaa_samples, uint32_t layers)
{
	if (layers > MAX_MULTIVIEW)
    {
		throw std::runtime_error( std::string("Error: Camera component cannot render to more than " + std::to_string(MAX_MULTIVIEW) + " layers simultaneously."));
    }

	maxMultiview = layers;
	set_view(glm::mat4(1.0), 0);
	// set_orthographic_projection(-1, 1, -1, 1, -1, 1);
	if (allow_recording)
	{
		this->allow_recording = true;
		this->msaa_samples = msaa_samples;
		
		if (cubemap)
		{
			renderTexture = Texture::CreateCubemap(name, tex_width, tex_height, true, true);
		}
		else
		{
			renderTexture = Texture::Create2D(name, tex_width, tex_height, true, true, msaa_samples, layers);
			if (msaa_samples != 1)
				resolveTexture = Texture::Create2D(name + "_resolve", tex_width, tex_height, true, true, 1, layers);
		}
		create_command_buffer();
		create_render_passes(tex_width, tex_height, (cubemap) ? 6 : layers, msaa_samples);
		create_frame_buffers(layers);
        for(auto renderpass : renderpasses) {
            Material::SetupGraphicsPipelines(renderpass, msaa_samples);
        }
	}
}

void Camera::create_command_buffer()
{
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();
	vk::CommandBufferAllocateInfo cmdAllocInfo;
    cmdAllocInfo.commandPool = vulkan->get_command_pool(1);
    cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    cmdAllocInfo.commandBufferCount = 1;
    command_buffer = device.allocateCommandBuffers(cmdAllocInfo)[0];
}

void Camera::create_render_passes(uint32_t framebufferWidth, uint32_t framebufferHeight, uint32_t layers, uint32_t sample_count)
{
    renderpasses.clear();
    
#ifdef DISABLE_MULTIVIEW
    int iterations = layers;
#else
    int iterations = 1;
#endif
    
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
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = renderTexture->get_color_image_layout();

        vk::AttachmentReference colorAttachmentRef;
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = renderTexture->get_color_image_layout();
        #pragma endregion

        #pragma region CreateDepthAttachment
        vk::AttachmentDescription depthAttachment;
        depthAttachment.format = renderTexture->get_depth_format();
        depthAttachment.samples = sampleFlag;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
        depthAttachment.finalLayout = renderTexture->get_depth_image_layout();

        vk::AttachmentReference depthAttachmentRef;
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = renderTexture->get_depth_image_layout();
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
        colorAttachmentResolve.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachmentResolve.finalLayout = renderTexture->get_color_image_layout();

        vk::AttachmentReference colorAttachmentResolveRef;
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout = renderTexture->get_color_image_layout();
        #pragma endregion

        // #pragma region CreateDepthAttachmentResolve
        // vk::AttachmentDescription depthAttachmentResolve;
        // depthAttachmentResolve.format = renderTexture->get_depth_format();
        // depthAttachmentResolve.samples = vk::SampleCountFlagBits::e1;
        // depthAttachmentResolve.loadOp = vk::AttachmentLoadOp::eDontCare;
        // depthAttachmentResolve.storeOp = vk::AttachmentStoreOp::eStore;
        // depthAttachmentResolve.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        // depthAttachmentResolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        // depthAttachmentResolve.initialLayout = vk::ImageLayout::eUndefined;
        // depthAttachmentResolve.finalLayout = renderTexture->get_depth_image_layout();

        // vk::AttachmentReference depthAttachmentResolveRef;
        // depthAttachmentResolveRef.attachment = 3;
        // depthAttachmentResolveRef.layout = renderTexture->get_depth_image_layout();
        // #pragma endregion



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
#ifdef DISABLE_MULTIVIEW
        renderPassInfo.pNext = nullptr;
#else
        renderPassInfo.pNext = &renderPassMultiviewInfo;
#endif

        renderpasses.push_back(device.createRenderPass(renderPassInfo));
    }
	#pragma endregion
}

void Camera::create_frame_buffers(uint32_t layers) {
    framebuffers.clear();
    
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();

    
#ifdef DISABLE_MULTIVIEW
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
    }
#else
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
#endif
    
}

void Camera::update_used_views(uint32_t multiview) {
	usedViews = (usedViews >= multiview) ? usedViews : multiview;
}

void Camera::check_multiview_index(uint32_t multiview) {
	if (multiview >= maxMultiview)
    {
		throw std::runtime_error( std::string("Error: multiview index is larger than " + std::to_string(maxMultiview) ));
    }
}

Camera::Camera() {
	this->initialized = false;
}

Camera::Camera(std::string name, uint32_t id) {
	this->initialized = true;
	this->name = name;
	this->id = id;
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

void Camera::set_perspective_projection(float fov_in_radians, float width, float height, float near_pos, uint32_t multiview)
{
	check_multiview_index(multiview);
	camera_struct.multiviews[multiview].near_pos = near_pos;
	camera_struct.multiviews[multiview].proj = MakeInfReversedZProjRH(fov_in_radians, width / height, near_pos);
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
	update_used_views(multiview);
	usedViews = (usedViews >= multiview) ? usedViews : multiview;
	camera_struct.multiviews[multiview].view = view;
	camera_struct.multiviews[multiview].viewinv = glm::inverse(view);
};

void Camera::set_render_order(uint32_t order) {
	renderOrder = order;
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

bool Camera::allows_recording()
{
	return allow_recording;
}

// this should be in the render system...
void Camera::begin_renderpass(vk::CommandBuffer command_buffer, uint32_t index)
{
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
	/* Not all cameras allow recording. */
	if (!allow_recording)
		throw std::runtime_error( std::string("Error: this camera does not allow recording"));

	vk::RenderPassBeginInfo rpInfo;
	rpInfo.renderPass = renderpasses[index];
	rpInfo.framebuffer = framebuffers[index];
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

vk::RenderPass Camera::get_renderpass(uint32_t index)
{
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
	return renderpasses[index];
}

uint32_t Camera::get_num_renderpasses() {
    return (uint32_t) renderpasses.size();
}

void Camera::end_renderpass(vk::CommandBuffer command_buffer, uint32_t index) {
    if(index >= renderpasses.size())
        throw std::runtime_error( std::string("Error: renderpass index out of bounds"));
	if (!allow_recording) 
		throw std::runtime_error( std::string("Error: this camera does not allow recording"));
		
	command_buffer.endRenderPass();
}

vk::CommandBuffer Camera::get_command_buffer() {
	return command_buffer;
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
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();
	auto physical_device = vulkan->get_physical_device();

	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = MAX_CAMERAS * sizeof(CameraStruct);
	bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	ssbo = device.createBuffer(bufferInfo);

	vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(ssbo);
	vk::MemoryAllocateInfo allocInfo = {};
	allocInfo.allocationSize = memReqs.size;

	vk::PhysicalDeviceMemoryProperties memProperties = physical_device.getMemoryProperties();
	vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
	allocInfo.memoryTypeIndex = vulkan->find_memory_type(memReqs.memoryTypeBits, properties);

	ssboMemory = device.allocateMemory(allocInfo);
	device.bindBufferMemory(ssbo, ssboMemory, 0);

	/* Pin the buffer */
	pinnedMemory = (CameraStruct*) device.mapMemory(ssboMemory, 0, MAX_CAMERAS * sizeof(CameraStruct));
}

void Camera::UploadSSBO()
{
	if (pinnedMemory == nullptr) return;
	
	/* TODO: remove this for loop */
	for (uint32_t i = 0; i < MAX_CAMERAS; ++i) {
		if (!cameras[i].is_initialized()) continue;
		pinnedMemory[i] = cameras[i].camera_struct;

		for (uint32_t j = 0; j < cameras[i].maxMultiview; ++j) {
			pinnedMemory[i].multiviews[j].viewinv = glm::inverse(pinnedMemory[i].multiviews[j].view);
			pinnedMemory[i].multiviews[j].projinv = glm::inverse(pinnedMemory[i].multiviews[j].proj);
			pinnedMemory[i].multiviews[j].viewproj = pinnedMemory[i].multiviews[j].proj * pinnedMemory[i].multiviews[j].view;
		}
	};
}

vk::Buffer Camera::GetSSBO()
{
	return ssbo;
}

uint32_t Camera::GetSSBOSize()
{
	return MAX_CAMERAS * sizeof(CameraStruct);
}

void Camera::CleanUp()
{
	auto vulkan = Libraries::Vulkan::Get();
	auto device = vulkan->get_device();
	device.destroyBuffer(ssbo);
	device.unmapMemory(ssboMemory);
	device.freeMemory(ssboMemory);

	for (uint32_t i = 0; i < GetCount(); ++i) {
		cameras[i].cleanup();
	}
}	


/* Static Factory Implementations */
Camera* Camera::Create(std::string name, bool allow_recording, bool cubemap, uint32_t tex_width, uint32_t tex_height, uint32_t msaa_samples, uint32_t layers)
{
	auto camera = StaticFactory::Create(name, "Camera", lookupTable, cameras, MAX_CAMERAS);
	camera->setup(allow_recording, cubemap, tex_width, tex_height, msaa_samples, layers);
	return camera;
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
	auto vulkan = Vulkan::Get();
	auto device = vulkan->get_device();

	if (command_buffer)
		device.freeCommandBuffers(vulkan->get_command_pool(1), {command_buffer});
    if (renderpasses.size() > 0) {
        for(auto renderpass : renderpasses) {
            device.destroyRenderPass(renderpass);
        }
    }
}
