#pragma once

#include <set>
#include <string>

extern bool Initialized;

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
