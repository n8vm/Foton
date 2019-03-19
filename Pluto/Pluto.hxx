#pragma once

#include <set>
#include <string>
#include <vector>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

extern bool Initialized;

class Entity;

namespace Pluto {

    void Initialize(
        bool useGLFW = true, 
        bool useOpenVR = false,
        std::set<std::string> validation_layers = {}, 
        std::set<std::string> instance_extensions = {}, 
        std::set<std::string> device_extensions = {}, 
        std::set<std::string> device_features = {
            "shaderUniformBufferArrayDynamicIndexing",
            "shaderSampledImageArrayDynamicIndexing",
            "shaderStorageBufferArrayDynamicIndexing",
            "shaderStorageImageArrayDynamicIndexing",
            "vertexPipelineStoresAndAtomics", 
            "fragmentStoresAndAtomics"}
    );

    void CleanUp();

    std::vector<Entity*> ImportOBJ(std::string filepath, std::string mtl_base_dir, 
        glm::vec3 position = glm::vec3(0.0f), 
        glm::vec3 scale = glm::vec3(1.0f),
        glm::quat rotation = glm::angleAxis(0.0f, glm::vec3(1.0f, 0.0f, 0.0f)));

}
