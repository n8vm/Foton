#include "Pluto.hxx"

bool Initialized = false;
#include <thread>
#include <iostream>
#include <map>
#include <string>

#include "Tools/Options.hxx"

#include "Libraries/GLFW/GLFW.hxx"
#include "Libraries/Vulkan/Vulkan.hxx"
#if BUILD_OPENVR
#include "Libraries/OpenVR/OpenVR.hxx"
#endif

#include "Systems/EventSystem/EventSystem.hxx"
#include "Systems/PythonSystem/PythonSystem.hxx"
#include "Systems/RenderSystem/RenderSystem.hxx"

#include "Pluto/Camera/Camera.hxx"
#include "Pluto/Texture/Texture.hxx"
#include "Pluto/Transform/Transform.hxx"
#include "Pluto/Material/Material.hxx"
#include "Pluto/Mesh/Mesh.hxx"
#include "Pluto/Light/Light.hxx"
#include "Pluto/Entity/Entity.hxx"

#include <tiny_obj_loader.h>
#include <glm/glm.hpp>

namespace Pluto {

    void Initialize(
        bool useGLFW,
        bool useOpenVR,
        std::set<std::string> validation_layers,
        std::set<std::string> instance_extensions,
        std::set<std::string> device_extensions,
        std::set<std::string> device_features
    ) {
        if (Initialized) 
            throw std::runtime_error("Error: Pluto already initialized");
        Initialized = true;

        auto glfw = Libraries::GLFW::Get();
        auto vulkan = Libraries::Vulkan::Get();
        
        auto event_system = Systems::EventSystem::Get();
        auto render_system = Systems::RenderSystem::Get();

        event_system->use_openvr(useOpenVR);
        render_system->use_openvr(useOpenVR);

        if (useGLFW) event_system->create_window("TEMP", 1, 1, false, false, false, false);
        vulkan->create_instance(validation_layers.size() > 0, validation_layers, instance_extensions, useOpenVR);
        
        auto surface = (useGLFW) ? glfw->create_vulkan_surface(vulkan, "TEMP") : vk::SurfaceKHR();
        vulkan->create_device(device_extensions, device_features, 8, surface, useOpenVR);
        
        /* Initialize Component Factories. Order is important. */
        Transform::Initialize();
        Light::Initialize();
        Camera::Initialize();
        Entity::Initialize();
        Texture::Initialize();
        Mesh::Initialize();
        Material::Initialize();

        Light::CreateShadowCameras();

        auto skybox = Entity::Create("Skybox");
        auto sphere = Mesh::CreateSphere("SkyboxSphere");
        auto transform = Transform::Create("SkyboxTransform");
        auto material = Material::Create("SkyboxMaterial");
        material->show_environment();
        transform->set_scale(100000, 100000, 100000);
        skybox->set_mesh(sphere);
        skybox->set_material(material);
        skybox->set_transform(transform);

    #if BUILD_OPENVR
        if (useOpenVR) {
            auto ovr = Libraries::OpenVR::Get();
            ovr->create_eye_textures();

            Transform::Create("VRLeftHand");
            Transform::Create("VRRightHand");
        }
    #endif
        if (useGLFW) event_system->destroy_window("TEMP");
    }

    void CleanUp()
    {
        Initialized = false;

        auto glfw = Libraries::GLFW::Get();
        auto vulkan = Libraries::Vulkan::Get();
        auto openvr = Libraries::Vulkan::Get();

        if (vulkan->is_initialized())
        {
            Material::CleanUp();
            Transform::CleanUp();
            Light::CleanUp();
            Camera::CleanUp();
            Entity::CleanUp();
        }
    }

    std::vector<Entity*> ImportOBJ(std::string filepath, std::string mtl_base_dir, glm::vec3 position, glm::vec3 scale, glm::quat rotation)
    {
        struct stat st;
        if (stat(filepath.c_str(), &st) != 0)
            throw std::runtime_error( std::string(filepath + " does not exist!"));

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::map<std::string, int> material_map;
        std::string err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filepath.c_str(), mtl_base_dir.c_str()))
            throw std::runtime_error( std::string("Error: Unable to load " + filepath));

        if (err.size() > 0)
            std::cout<< err << std::endl;

        std::vector<Material*> materialComponents;
        std::vector<Transform*> transformComponents;
        std::vector<Entity*> entities;

        std::set<std::string> texture_paths;
        std::map<std::string, Texture*> texture_map;

        for (uint32_t i = 0; i < materials.size(); ++i) {
            materialComponents.push_back(Material::Create(materials[i].name));

            if (materials[i].alpha_texname.length() > 0)
                texture_paths.insert(materials[i].alpha_texname);

            if (materials[i].diffuse_texname.length() > 0)
                texture_paths.insert(materials[i].diffuse_texname);

            if (materials[i].bump_texname.length() > 0)
                texture_paths.insert(materials[i].bump_texname);
        }

        for (auto &path : texture_paths)
        {
            texture_map[path] = Texture::CreateFromPNG(mtl_base_dir + path, mtl_base_dir + path);
            // Maybe think of a better name here? Could accidentally conflict...
        }

        for (uint32_t i = 0; i < materials.size(); ++i) {

            // if (materials[i].alpha_texname.length() > 0)
                // texture_paths.insert(materials[i].alpha_texname);

            if (materials[i].diffuse_texname.length() > 0) {
                materialComponents[i]->set_base_color_texture(texture_map[materials[i].diffuse_texname]);
            }
            
            // if (materials[i].bump_texname.length() > 0)
                // texture_paths.insert(materials[i].bump_texname);
        }

        for (uint32_t i = 0; i < shapes.size(); ++i) {

            /* Determine how many materials are in this shape... */
            std::set<uint32_t> material_ids;
            for (uint32_t j = 0; j < shapes[i].mesh.material_ids.size(); ++j) {
                material_ids.insert(shapes[i].mesh.material_ids[j]);
            }

            uint32_t mat_offset = 0;

            /* Create a model for each found material id for the given shape. */
            for (auto material_id : material_ids) 
            {
                mat_offset++;

                std::vector<glm::vec3> positions; 
                std::vector<glm::vec4> colors; 
                std::vector<glm::vec3> normals; 
                std::vector<glm::vec2> texcoords; 

                /* For each face */
                size_t index_offset = 0;
                for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
                    int fv = shapes[i].mesh.num_face_vertices[f];

                    /* Skip any faces which don't use the current material */
                    if (shapes[i].mesh.material_ids[f] != material_id) {
                        index_offset += fv;
                        continue;
                    }

                    // Loop over vertices in the face.
                    for (size_t v = 0; v < fv; v++) {
                        auto index = shapes[i].mesh.indices[index_offset + v];
                        positions.push_back(glm::vec3(
                            attrib.vertices[3 * index.vertex_index + 0],
                            attrib.vertices[3 * index.vertex_index + 1],
                            attrib.vertices[3 * index.vertex_index + 2]
                        ));

                        if (attrib.colors.size() != 0) {
                            colors.push_back(glm::vec4(
                                attrib.colors[3 * index.vertex_index + 0],
                                attrib.colors[3 * index.vertex_index + 1],
                                attrib.colors[3 * index.vertex_index + 2],
                                1.0f
                            ));
                        }

                        if (attrib.normals.size() != 0) {
                            normals.push_back(glm::vec3(
                                attrib.normals[3 * index.normal_index + 0],
                                attrib.normals[3 * index.normal_index + 1],
                                attrib.normals[3 * index.normal_index + 2]
                            ));
                        }

                        if (attrib.texcoords.size() != 0) {
                            texcoords.push_back(glm::vec2(
                                attrib.texcoords[2 * index.texcoord_index + 0],
                                attrib.texcoords[2 * index.texcoord_index + 1]
                            ));
                        }
                    }
                    index_offset += fv;
                }

                /* Some shapes with multiple materials report sizes which aren't a multiple of 3... This is a kludge... */
                if (positions.size() % 3 != 0) positions.resize(positions.size() - (positions.size() % 3));
                if (colors.size() % 3 != 0) colors.resize(colors.size() - (colors.size() % 3));
                if (normals.size() % 3 != 0) normals.resize(normals.size() - (normals.size() % 3));
                if (texcoords.size() % 3 != 0) texcoords.resize(texcoords.size() - (texcoords.size() % 3));

                /* We need at least one point to render... */
                if (positions.size() < 3) continue;

                auto entity = Entity::Create(shapes[i].name + "_" + std::to_string(mat_offset));
                auto transform = Transform::Create(shapes[i].name + "_" + std::to_string(mat_offset));
                transform->set_position(position);
                transform->set_scale(scale);
                transform->set_rotation(rotation);
                entities.push_back(entity);
                transformComponents.push_back(transform);
                entity->set_transform(transform);

                // Since there can be multiple material ids per shape, we have to separate these shapes into
                // separate entities...
                entity->set_material(materialComponents[material_id]);

                auto mesh = Mesh::CreateFromRaw(shapes[i].name + "_" + std::to_string(mat_offset), positions, normals, colors, texcoords);
                entity->set_mesh(mesh);
            }
        }

        return entities;
    }

}
