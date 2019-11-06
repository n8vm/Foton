// ┌──────────────────────────────────────────────────────────────────┐
// │  Camera                                                          │
// └──────────────────────────────────────────────────────────────────┘

#pragma once

#include <vulkan/vulkan.hpp>
#include <mutex>
#include <condition_variable>

#include "Foton/Tools/StaticFactory.hxx"
#include "Foton/Camera/CameraStruct.hxx"

#include "Foton/Systems/RenderSystem/RenderSystem.hxx"


class Texture;
class Entity;

enum RenderMode : uint32_t;

struct VisibleEntityInfo {
	float distance;
	bool visible;
	uint32_t entity_index;
	Entity* entity;

	bool operator < (const VisibleEntityInfo& other) const
	{
		return (distance < other.distance);
	};
};

class Camera : public StaticFactory
{
	friend class StaticFactory;
    friend class Systems::RenderSystem;
  public:
	/* Creates a camera, which can be used to record the scene. Can be used to render to several texture layers for use in cubemap rendering/VR renderpasses. 
	Note, for VR, max_views must be 2, and for a cubemap, max_views must be 6. */
	static Camera *Create(std::string name, uint32_t width = 512, uint32_t height = 512, uint32_t msaa_samples = 1, uint32_t max_views = 1, bool use_depth_prepass = true, bool use_multiview = false);

	/* Retrieves a camera component by name. */
	static Camera *Get(std::string name);

	/* Retrieves a camera component by id. */
	static Camera *Get(uint32_t id);

	/* Returns a pointer to the list of camera components. */
	static Camera *GetFront();

	/* Returns the total number of reserved cameras. */
	static uint32_t GetCount();

	/* Returns a vector containing a list of camera components with the given order. */
	static std::vector<Camera *> GetCamerasByOrder(uint32_t order);

	/* Deallocates a camera with the given name. */
	static void Delete(std::string name);

	/* Deallocates a camera with the given id. */
	static void Delete(uint32_t id);

	/* Initializes the Camera factory. Loads any default components. */
	static void Initialize();

	/* TODO: Explain this */
	static bool IsInitialized();

	/* Transfers all camera components to an SSBO */
	static void UploadSSBO(vk::CommandBuffer command_buffer);

	/* Returns the SSBO vulkan buffer handle */
	static vk::Buffer GetSSBO();

	/* Returns the size in bytes of the current camera SSBO. */
	static uint32_t GetSSBOSize();

	/* Releases vulkan resources */
	static void CleanUp();

	/* Constructs an orthographic projection for the given multiview. */
	// bool set_orthographic_projection(float left, float right, float bottom, float top, float near_pos, uint32_t multiview = 0);

	/* Constructs a reverse Z perspective projection for the given multiview. */
	void set_perspective_projection(float fov_in_radians, float width, float height, float near_pos, uint32_t multiview = 0);

	/* Uses an external projection matrix for the given multiview. 
		Note, the projection must be a reversed Z projection. */
	void set_custom_projection(glm::mat4 custom_projection, float near_pos, uint32_t multiview = 0);

	/* Returns the near position of the given multiview */
	float get_near_pos(uint32_t multiview = 0);

	/* Returns the entity transform to camera matrix for the given multiview. 
		This additional transform is applied on top of an entity transform during a renderpass
		to see a particular "view". */
	glm::mat4 get_view(uint32_t multiview = 0);

	/* Sets the entity transform to camera matrix for the given multiview. 
		This additional transform is applied on top of an entity transform during a renderpass
		to see a particular "view". */
	void set_view(glm::mat4 view, uint32_t multiview = 0);

	/* Returns the camera to projection matrix for the given multiview.
		This transform can be used to achieve perspective (eg a vanishing point), or for scaling
		an orthographic view. */
	glm::mat4 get_projection(uint32_t multiview = 0);

	/* Returns the texture component being rendered to. 
		Otherwise, returns None/nullptr. */
	Texture *get_texture();

	/* TODO: Explain this */
	uint32_t get_max_views();

	/* Returns a json string summarizing the camera. */
	std::string to_string();

	/* Sets the clear color to be used to reset the color image of this camera's
		texture component when beginning a renderpass. */
	void set_clear_color(float r, float g, float b, float a);

	/* Sets the clear stencil to be used to reset the depth/stencil image of this 
		camera's texture component when beginning a renderpass. */
	void set_clear_stencil(uint32_t stencil);

	/* Sets the clear depth to be used to reset the depth/stencil image of this 
		camera's texture component when beginning a renderpass. Note: If reverse Z projections are used, 
		this should always be 1.0 */
	void set_clear_depth(float depth);

	/* Sets the renderpass order of the current camera. This is used to handle dependencies between 
		renderpasses. Eg, rendering shadow maps or reflections. */
	void set_render_order(int32_t order);

	void set_max_visible_distance(float max_distance);

	float get_max_visible_distance();
	
	/* Gets the renderpass order of the current camera. This is used to handle dependencies between 
		renderpasses. Eg, rendering shadow maps or reflections. */
	int32_t get_render_order();

	/* Returns the minimum render order set in the camera list */
	static int32_t GetMinRenderOrder();
	
	/* Returns the maximum render order set in the camera list */
	static int32_t GetMaxRenderOrder();

	/* TODO: Explain this */
	void force_render_mode(RenderMode rendermode);

	/* TODO: Explain this */
	RenderMode get_rendermode_override();

	/* TODO: Explain this */
	bool should_record_depth_prepass();

	/* TODO: Explain this */
	bool should_use_multiview();

	/* TODO: */
	void pause_visibility_testing();
	
	/* TODO: */
	void resume_visibility_testing();

	void mark_render_as_complete();

	void wait_for_render_complete();

  private:
  	/* Creates an uninitialized camera. Useful for preallocation. */
	Camera();

	/* Creates a camera with the given name and id. */
	Camera(std::string name, uint32_t id);

  	/* TODO */
	static std::shared_ptr<std::mutex> creation_mutex;

	/* TODO */
	static bool Initialized;
	
	/* Determines whether this camera should use a depth prepass to reduce fragment complexity at the cost of 
	vertex shader complexity */
	bool use_depth_prepass;

	/* TODO */
	bool use_multiview;

	/* Marks the total number of multiviews being used by the current camera. */
	uint32_t usedViews = 1;

	/* Marks the maximum number of views this camera can support, which can be possibly less than MAX_MULTIVIEW. */
	uint32_t maxViews = MAX_MULTIVIEW;

	/* Marks when this camera should render during a frame. */
	int32_t renderOrder = 0;

	/* Marks the range of render orders, so that the render system can create the right number of semaphores. */
	static int32_t minRenderOrder;
	static int32_t maxRenderOrder;

	/* A struct containing all data to be uploaded to the GPU via an SSBO. */
	CameraStruct camera_struct;

	/* TODO: */
	bool queryRecorded = false;

	bool queryDownloaded = true;

	bool visibilityTestingPaused = false;

	uint64_t max_queried = 0;

	float max_visibility_distance;

	std::vector<std::vector<VisibleEntityInfo>> frustum_culling_results;

	/* The texture component attached to the framebuffer, which will be rendered to. */
	Texture *renderTexture = nullptr;
	
	/* If msaa_samples is more than one, this texture component is used to resolve MSAA samples. */
	Texture *resolveTexture = nullptr;
	
	/* The RGBA color used when clearing the color attachment at the beginning of a renderpass. */
	glm::vec4 clearColor = glm::vec4(0.0);
	
	/* The depth value used when clearing the depth/stencil attachment at the beginning of a renderpass. */
	float clearDepth = 0.0f;
	
	/* The stencil value used when clearing the depth/stencil attachment at the beginning of a renderpass. */
	uint32_t clearStencil = 0;
	
	/* An integer representation of the number of MSAA samples to take when rendering. Must be a power of 2.
		if 1, it's inferred that a resolve texture is not required.  */
	uint32_t msaa_samples = 1;

	bool render_ready = false;
	std::shared_ptr<std::mutex> render_complete_mutex;
	std::shared_ptr<std::condition_variable> cv;

	/* A list of the camera components, allocated statically */
	static Camera cameras[MAX_CAMERAS];
	
	/* A lookup table of name to camera id */
	static std::map<std::string, uint32_t> lookupTable;
	
	/* A pointer to the mapped camera SSBO. This memory is shared between the GPU and CPU. */
	static CameraStruct *pinnedMemory;
	
	/* A vulkan buffer handle corresponding to the material SSBO */
	static vk::Buffer SSBO;
	
	/* The corresponding material SSBO memory. */
	static vk::DeviceMemory SSBOMemory;

	/* TODO */
	static vk::Buffer stagingSSBO;
	
	/* TODO */
	static vk::DeviceMemory stagingSSBOMemory;

	RenderMode renderModeOverride;

	/* Allocates (and possibly frees existing) textures, renderpass, and framebuffer required for rendering. */
	void setup(uint32_t tex_width = 0, uint32_t tex_height = 0, uint32_t msaa_samples = 1, uint32_t max_views = 1, bool use_depth_prepass = true, bool use_multiview = false);

	/* Updates the usedViews field to account for a new multiview. This is fixed to the allocated texture layers 
		when recording is enabled. */
	void update_used_views(uint32_t multiview);

	/* Checks to see if a given multiview index is within the multiview bounds, bounded either by MAX_MULTIVIEW, or the 
		camera's texture layers */
	void check_multiview_index(uint32_t multiview);

	/* Releases any vulkan resources. */
	void cleanup();

	/* Indicates that one of the components has been edited */
	static bool Dirty;

	/* Indicates this component has been edited */
	bool dirty = true;

	void mark_dirty() {
		Dirty = true;
		dirty = true;
	};
};