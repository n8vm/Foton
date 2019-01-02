#include "./Texture.hxx"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include <stb_image.h>
#include <stb_image_write.h>

Texture Texture::textures[MAX_TEXTURES];
std::map<std::string, uint32_t> Texture::lookupTable;

// TODO
void Texture::Initialize()
{
    auto vulkan = Libraries::Vulkan::Get();
    auto device = vulkan->get_device();
    auto physical_device = vulkan->get_physical_device();

    // Create the default texture here
    std::string resource_path = Options::GetResourcePath();
    CreateFromKTX("BRDF", resource_path + "/Defaults/brdf-lut.ktx");
    CreateFromKTX("DiffuseEnvironment", resource_path + "/Defaults/default-diffuse-cubemap.ktx");
    CreateFromKTX("DefaultTex2D", resource_path + "/Defaults/missing-texture.ktx");
    CreateFromKTX("DefaultTexCube", resource_path + "/Defaults/missing-texcube.ktx");
    CreateFromKTX("DefaultTex3D", resource_path + "/Defaults/missing-volume.ktx");
    
    // fatal error here if result is nullptr...
}

void Texture::CleanUp()
{
    for (int i = 0; i < MAX_TEXTURES; ++i)
        textures[i].cleanup();
}

std::vector<vk::ImageView> Texture::GetImageViews(vk::ImageViewType view_type) 
{
    // Get the default texture
    Texture *DefaultTex;
    if (view_type == vk::ImageViewType::e2D) DefaultTex = Get("DefaultTex2D");
    else if (view_type == vk::ImageViewType::e3D) DefaultTex = Get("DefaultTex3D");
    else if (view_type == vk::ImageViewType::eCube) DefaultTex = Get("DefaultTexCube");
    else return {};

    std::vector<vk::ImageView> image_views(MAX_TEXTURES);

    // For each texture
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        if (textures[i].initialized 
            && (textures[i].data.colorImageView != vk::ImageView())
            && (textures[i].data.colorImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) 
            && (textures[i].data.viewType == view_type)) {
            // then add it's image view to the vector
            image_views[i] = textures[i].data.colorImageView;
        }
        // otherwise, add the default texture image view
        else {
            image_views[i] = DefaultTex->data.colorImageView;
        }
    }
    
    // finally, return the image view vector
    return image_views;
}

std::vector<vk::Sampler> Texture::GetSamplers() 
{
    // Get the default texture (for now, just use the default 2D texture)
    auto DefaultTex = Get("DefaultTex2D");
    
    std::vector<vk::Sampler> samplers(MAX_TEXTURES);

    // For each texture
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        if (textures[i].initialized 
            && (textures[i].data.colorImageView != vk::ImageView())
            && (textures[i].data.colorImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)) {
            // then add it's sampler to the vector
            samplers[i] = textures[i].data.colorSampler;
        }
        // otherwise, add the default texture sampler
        else {
            samplers[i] = DefaultTex->data.colorSampler;
        }
    }
    
    // finally, return the sampler vector
    return samplers;
}

std::vector<vk::ImageLayout> Texture::GetLayouts(vk::ImageViewType view_type) 
{
    // Get the default texture
    Texture *DefaultTex;
    if (view_type == vk::ImageViewType::e2D) DefaultTex = Get("DefaultTex2D");
    else if (view_type == vk::ImageViewType::e3D) DefaultTex = Get("DefaultTex3D");
    else if (view_type == vk::ImageViewType::eCube) DefaultTex = Get("DefaultTexCube");
    else return {};

    std::vector<vk::ImageLayout> layouts(MAX_TEXTURES);

    // For each texture
    for (int i = 0; i < MAX_TEXTURES; ++i) {
        if (textures[i].initialized 
            && (textures[i].data.colorImageView != vk::ImageView())
            && (textures[i].data.colorImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) 
            && (textures[i].data.viewType == view_type)) {
            // then add it's layout to the vector
            layouts[i] = textures[i].data.colorImageLayout;
        }
        // otherwise, add the default 2D texture layout
        else {
            layouts[i] = DefaultTex->data.colorImageLayout;
        }
    }
    
    // finally, return the layout vector
    return layouts;
}

/* Static Factory Implementations */
Texture *Texture::CreateFromKTX(std::string name, std::string filepath)
{
    auto tex = StaticFactory::Create(name, "Texture", lookupTable, textures, MAX_TEXTURES);
    if (!tex) return nullptr;
    if (!tex->loadKTX(filepath)) return nullptr;
    return tex;
}

Texture* Texture::CreateCubemap(
    std::string name, uint32_t width, uint32_t height, bool hasColor, bool hasDepth) 
{
    auto tex = StaticFactory::Create(name, "Texture", lookupTable, textures, MAX_TEXTURES);
    if (!tex) return nullptr;

    tex->data.width = width;
    tex->data.height = height;
    tex->data.layers = 6;
    tex->data.imageType = vk::ImageType::e2D;
    tex->data.viewType  = vk::ImageViewType::eCube;
    if (hasColor) tex->create_color_image_resources();
    if (hasDepth) tex->create_depth_stencil_resources();
    return tex;
}

Texture* Texture::Create2D(
    std::string name, uint32_t width, uint32_t height, bool hasColor, bool hasDepth)
{
    auto tex = StaticFactory::Create(name, "Texture", lookupTable, textures, MAX_TEXTURES);
    if (!tex) return nullptr;


    tex->data.width = width;
    tex->data.height = height;
    tex->data.layers = 1;
    tex->data.viewType  = vk::ImageViewType::e2D;
    tex->data.imageType = vk::ImageType::e2D;
    if (hasColor) tex->create_color_image_resources();
    if (hasDepth) tex->create_depth_stencil_resources();
    return tex;
}

Texture* Texture::Create2DFromColorData (
    std::string name, uint32_t width, uint32_t height, std::vector<float> data)
{
    auto tex = StaticFactory::Create(name, "Texture", lookupTable, textures, MAX_TEXTURES);
    if (!tex) return nullptr;
    tex->data.width = width;
    tex->data.height = height;
    tex->data.layers = 1;
    tex->data.viewType  = vk::ImageViewType::e2D;
    tex->data.imageType = vk::ImageType::e2D;
    tex->create_color_image_resources();
    tex->upload_color_data(width, height, 1, data);
    return tex;
}

Texture* Texture::CreateFromExternalData(std::string name, Data data)
{
    auto tex = StaticFactory::Create(name, "Texture", lookupTable, textures, MAX_TEXTURES);
    if (!tex) return nullptr;
    tex->setData(data);
    return tex;
}

Texture* Texture::Get(std::string name) {
    return StaticFactory::Get(name, "Texture", lookupTable, textures, MAX_TEXTURES);
}

Texture* Texture::Get(uint32_t id) {
    return StaticFactory::Get(id, "Texture", lookupTable, textures, MAX_TEXTURES);
}

bool Texture::Delete(std::string name) {
    return StaticFactory::Delete(name, "Texture", lookupTable, textures, MAX_TEXTURES);
}

bool Texture::Delete(uint32_t id) {
    return StaticFactory::Delete(id, "Texture", lookupTable, textures, MAX_TEXTURES);
}

Texture* Texture::GetFront() {
    return textures;
}

uint32_t Texture::GetCount() {
    return MAX_TEXTURES;
}
