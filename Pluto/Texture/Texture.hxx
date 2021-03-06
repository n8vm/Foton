#pragma once

#include <iostream>
#include <map>
#include <vector>

#include "Pluto/Libraries/Vulkan/Vulkan.hxx"
#include "Pluto/Tools/StaticFactory.hxx"
#include "Pluto/Texture/TextureStruct.hxx"

class Texture : public StaticFactory
{
	public:
		/* This is public so that external libraries can easily create 
			textures while still conforming to the component interface */
		struct Data
		{
			vk::Image colorImage, depthImage;
			vk::Format colorFormat, depthFormat;
			vk::DeviceMemory colorImageMemory, depthImageMemory;
            vk::ImageView colorImageView, depthImageView;
            std::vector<vk::ImageView> colorImageViewLayers, depthImageViewLayers;
			vk::ImageLayout colorImageLayout, depthImageLayout;
			uint32_t width = 1, height = 1, depth = 1, colorMipLevels = 1, layers = 1;
			vk::ImageViewType viewType;
			vk::ImageType imageType;
			uint32_t colorSamplerId = 0; uint32_t depthSamplerId = 0;
			vk::SampleCountFlagBits sampleCount;
			std::vector<vk::Image> additionalColorImages;
		};

		/* Creates a texture from a khronos texture file (.ktx) */
		static Texture *CreateFromKTX(std::string name, std::string filepath, bool submit_immediately = false);

		/* Creates a texture from data allocated outside this class. Helpful for swapchains, external libraries, etc */
		static Texture *CreateFromExternalData(std::string name, Data data);

		/* Creates a texture from a flattened sequence of RGBA floats, whose shape was originally (width, height, 4) */
		static Texture *Create2DFromColorData(std::string name, uint32_t width, uint32_t height, std::vector<float> data, bool submit_immediately = false);

		/* Creates a cubemap texture of a given width and height, and with color and/or depth resources. */
		static Texture *CreateCubemap(std::string name, uint32_t width, uint32_t height, bool hasColor, bool hasDepth, bool submit_immediately = false);

		/* Creates a 2d texture of a given width and height, consisting of possibly several layers, and with color and/or depth resources. */
		static Texture *Create2D(std::string name, uint32_t width, uint32_t height, bool hasColor, bool hasDepth, uint32_t sampleCount, uint32_t layers, bool submit_immediately = false);

		/* Creates a procedural checker texture. */
		static Texture* CreateChecker(std::string name, bool submit_immediately = false);

		/* Retrieves a texture component by name */
		static Texture *Get(std::string name);

		/* Retrieves a texture component by id */
		static Texture *Get(uint32_t id);

		/* Returns a pointer to the list of texture components */
		static Texture *GetFront();

		/* Returns the total number of reserved textures */
		static uint32_t GetCount();

		/* Deallocates a texture with the given name */
		static void Delete(std::string name);

		/* Deallocates a texture with the given id */
		static void Delete(uint32_t id);

		/* Initializes the Mesh factory. Loads default meshes. */
		static void Initialize();

		/* Transfers all texture components to an SSBO */
        static void UploadSSBO();

		/* Returns the SSBO vulkan buffer handle */
        static vk::Buffer GetSSBO();

        /* Returns the size in bytes of the current texture SSBO */
        static uint32_t GetSSBOSize();

		/* Returns a list of samplers corresponding to the texture list, or defaults if the texture isn't usable. 
			Useful for updating descriptor sets. */
		static std::vector<vk::Sampler> GetSamplers();

		/* Returns a list of samplers corresponding to the texture list, or defaults if the texture isn't usable. 
			Useful for updating descriptor sets. */
		static std::vector<vk::ImageView> GetImageViews(vk::ImageViewType view_type);

		/* Returns a list of samplers corresponding to the texture list, or defaults if the texture isn't usable. 
			Useful for updating descriptor sets. */
		static std::vector<vk::ImageLayout> GetLayouts(vk::ImageViewType view_type);

		/* Releases vulkan resources */
		static void CleanUp();

		/* Creates an uninitialized texture. Useful for preallocation. */
		Texture();

		/* Creates a texture with the given name and id. */
		Texture(std::string name, uint32_t id);

		/* Accessors / Mutators */
		vk::Format get_color_format();
		vk::ImageLayout get_color_image_layout();
		vk::ImageView get_color_image_view();
        std::vector<vk::ImageView> get_color_image_view_layers();
		vk::Image get_color_image();
		uint32_t get_color_mip_levels();
		vk::Sampler get_color_sampler();
		vk::Format get_depth_format();
		vk::ImageLayout get_depth_image_layout();
		vk::ImageView get_depth_image_view();
        std::vector<vk::ImageView> get_depth_image_view_layers();
		vk::Image get_depth_image();
		vk::Sampler get_depth_sampler();
		uint32_t get_depth();
		uint32_t get_height();
		uint32_t get_total_layers();
		uint32_t get_width();
		vk::SampleCountFlagBits get_sample_count();		

		/* Blits the texture to an image of the given width, height, and depth, then downloads from the GPU to the CPU. */
		std::vector<float> download_color_data(uint32_t width, uint32_t height, uint32_t depth, bool submit_immediately = false);

		/* Blits the provided image of shape (width, height, depth, 4) to the current texture. */
		void upload_color_data(uint32_t width, uint32_t height, uint32_t depth, std::vector<float> color_data, bool submit_immediately = false);

		/* Records a blit of this texture onto another. */
		void record_blit_to(vk::CommandBuffer command_buffer, Texture *other, uint32_t layer = 0);

		/* Replace the current image resources with external resources. Note, these resources must be freed externally. */
		void setData(Data data);

		/* Sets the first color to be used on a procedural texture type */
		void set_procedural_color_1(float r, float g, float b, float a);
		
		/* Sets the second color to be used on a procedural texture type */
		void set_procedural_color_2(float r, float g, float b, float a);

		/* Sets the scale to be used on a procedural texture type */
		void set_procedural_scale(float scale);

		/* Returns a json string summarizing the texture */
		std::string to_string();

		// Create an image memory barrier for changing the layout of
		// an image and put it into an active command buffer
		void setImageLayout(
			vk::CommandBuffer cmdbuffer,
			vk::Image image,
			vk::ImageLayout oldImageLayout,
			vk::ImageLayout newImageLayout,
			vk::ImageSubresourceRange subresourceRange,
			vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eAllCommands,
			vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eAllCommands);

	private:

		/* The list of texture components, allocated statically */
		static Texture textures[MAX_TEXTURES];
		
		/* The list of texture samplers, which a texture refers to in a shader for sampling. */
		static vk::Sampler samplers[MAX_SAMPLERS];
	
		/* A lookup table of name to texture id */
		static std::map<std::string, uint32_t> lookupTable;

		/* A pointer to the mapped texture SSBO. This memory is shared between the GPU and CPU. */
        static TextureStruct* pinnedMemory;

        /* A vulkan buffer handle corresponding to the texture SSBO  */
        static vk::Buffer ssbo;

        /* The corresponding texture SSBO memory */
        static vk::DeviceMemory ssboMemory;

		/* The struct of texture data, aggregating vulkan resources */
		Data data;

		/* The structure containing all shader texture properties. This is what's coppied into the SSBO per instance */
        TextureStruct texture_struct;

		/* Indicates that the texture was made externally (eg, setData was used), and that the resources should not
			be freed internally. */
		bool madeExternally = false;

		/* Frees the current texture's vulkan resources*/
		void cleanup();

		/* Allocates vulkan resources required for a colored image */
		void create_color_image_resources(bool submit_immediately = false);

		/* Allocates vulkan resources required for a depth/stencil image */
		void create_depth_stencil_resources(bool submit_immediately = false);

		/* Creates a color ImageView, which allows the color image resources to be interpreted for use in shaders, blits, etc */
		void createColorImageView();

		/* Queries the physical device for which depth formats are supported, and returns by reference the optimal depth format. */
		bool get_supported_depth_format(vk::PhysicalDevice physicalDevice, vk::Format *depthFormat);

		/* Creates a texture from a khronos texture file, replacing any vulkan resources with new ones containing the ktx data. */
		void loadKTX(std::string imagePath, bool submit_immediately = false);
};
