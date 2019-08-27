#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#define GLM_FORCE_RIGHT_HANDED

#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <iostream>
#include <map>
#include <unordered_map>
#include <mutex>
#include <string>

#include "Pluto/Libraries/Vulkan/Vulkan.hxx"
#include "Pluto/Tools/StaticFactory.hxx"

#include "./MaterialStruct.hxx"
#include "Pluto/Systems/RenderSystem/RenderSystem.hxx"

class Entity;
class Texture;
class Camera;

enum MaterialFlags : uint32_t { 
    MATERIAL_FLAGS_HIDDEN = 1, 
    MATERIAL_FLAGS_SHOW_SKYBOX = 2
};

class Material : public StaticFactory
{
    friend class StaticFactory;
    // friend class Systems::RenderSystem; // temporary. Required so rendersystem can read rendermode parameter
    public:
        /* Creates a new material component */
        static Material* Create(std::string name);

        /* Retrieves a material component by name*/
        static Material* Get(std::string name);

        /* Retrieves a material component by id */
        static Material* Get(uint32_t id);

        /* Returns a pointer to the list of material components */
        static Material* GetFront();

        /* Returns the total number of reserved materials */
	    static uint32_t GetCount();

        /* Deallocates a material with the given name */
        static void Delete(std::string name);

        /* Deallocates a material with the given id */
        static void Delete(uint32_t id);

        /* Initializes all vulkan descriptor resources, as well as the Mesh factory. */
        static void Initialize();

        /* TODO: Explain this */
        static bool IsInitialized();

        /* Transfers all material components to an SSBO */
        static void UploadSSBO(vk::CommandBuffer command_buffer);

        /* Returns the SSBO vulkan buffer handle */
        static vk::Buffer GetSSBO();

        /* Returns the size in bytes of the current material SSBO */
        static uint32_t GetSSBOSize();

        /* Releases vulkan resources */
        static void CleanUp();

        /* Returns a json string summarizing the material */
        std::string to_string();

        /* These methods choose which pipeline a material should use when drawing an entity */
        // void show_pbr();
        // void show_normals();
        // void show_base_color();
        // void show_texcoords();
        // void show_blinn();
        // void show_fragment_depth();
        // void show_vr_mask();
        // void show_depth();
        // void show_position();
        // void show_volume();

        /* This method makes an entity render as if it were the sky */
        void show_environment(bool show);

        /* This method prevents an entity from rendering. */
        void hidden(bool hide);

        /* Accessors / Mutators */
        void set_base_color(glm::vec3 color);
        void set_base_color(float r, float g, float b);
        void set_subsurface_color(glm::vec3 color);
        void set_subsurface_color(float r, float g, float b);
        void set_subsurface_radius(glm::vec3 subsurface_radius);
        void set_subsurface_radius(float x, float y, float z);
        void set_alpha(float a);
        void set_subsurface(float subsurface);
        void set_metallic(float metallic);
        void set_specular(float specular);
        void set_specular_tint(float specular_tint);
        void set_roughness(float roughness);
        void set_anisotropic(float anisotropic);
        void set_anisotropic_rotation(float anisotropic_rotation);
        void set_sheen(float sheen);
        void set_sheen_tint(float sheen_tint);
        void set_clearcoat(float clearcoat);
        void set_clearcoat_roughness(float clearcoat_roughness);
        void set_ior(float ior);
        void set_transmission(float transmission);
        void set_transmission_roughness(float transmission_roughness);
        
        glm::vec3 get_base_color();
        glm::vec3 get_subsurface_color();
        glm::vec3 get_subsurface_radius();
        float get_alpha();
        float get_subsurface();
        float get_metallic();
        float get_specular();
        float get_specular_tint();
        float get_roughness();
        float get_anisotropic();
        float get_anisotropic_rotation();
        float get_sheen();
        float get_sheen_tint();
        float get_clearcoat();
        float get_clearcoat_roughness();
        float get_ior();
        float get_transmission();
        float get_transmission_roughness();

        /* Certain constant material properties can be replaced with texture lookups. */
        void set_base_color_texture(uint32_t texture_id);
        void set_base_color_texture(Texture *texture);
        void clear_base_color_texture();

        void set_roughness_texture(uint32_t texture_id);
        void set_roughness_texture(Texture *texture);
        void clear_roughness_texture();

        void set_bump_texture(uint32_t texture_id);
        void set_bump_texture(Texture *texture);
        void clear_bump_texture();

        void set_alpha_texture(uint32_t texture_id);
        void set_alpha_texture(Texture *texture);
        void clear_alpha_texture();

        /* A uniform base color can be replaced with per-vertex colors as well. */
        void use_vertex_colors(bool use);

        /* The volume texture to be used by volume type materials */
        void set_volume_texture(uint32_t texture_id);
        void set_volume_texture(Texture *texture);

        void set_transfer_function_texture(uint32_t texture_id);
        void set_transfer_function_texture(Texture *texture);
        void clear_transfer_function_texture();

        bool contains_transparency();
        bool should_show_skybox();
        bool is_hidden();

    private:
        /* Creates an uninitialized material. Useful for preallocation. */
        Material();

        /* Creates a material with the given name and id. */
        Material(std::string name, uint32_t id);

        /* TODO */
		static std::shared_ptr<std::mutex> creation_mutex;

        /* TODO */
	    static bool Initialized;
    
        /*  A list of the material components, allocated statically */
        static Material materials[MAX_MATERIALS];

        /* A lookup table of name to material id */
        static std::map<std::string, uint32_t> lookupTable;
        
        /* A pointer to the mapped material SSBO. This memory is shared between the GPU and CPU. */
        static MaterialStruct* pinnedMemory;

        /* A vulkan buffer handle corresponding to the material SSBO  */
        static vk::Buffer SSBO;

        /* The corresponding material SSBO memory */
        static vk::DeviceMemory SSBOMemory;

        /* TODO  */
        static vk::Buffer stagingSSBO;

        /* TODO */
        static vk::DeviceMemory stagingSSBOMemory;

        /* Creates the SSBO which will contain all material components, and maps that SSBO to a pinned memory pointer */
        static void CreateSSBO();
        
        /* The structure containing all shader material properties. This is what's coppied into the SSBO per instance */
        MaterialStruct material_struct;
};
