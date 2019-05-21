/* File shared by both GLSL and C++ */
#ifndef MAX_MATERIALS
#ifndef LARGE_SCENE_SUPPORT
#define MAX_MATERIALS 256
#else
#define MAX_MATERIALS 1024
#endif
#endif

#ifndef GLSL
#include <glm/glm.hpp>
using namespace glm;
#endif

#ifdef GLSL
#define int32_t int
#endif

/* Not all these properties are mapped to PBR. */
struct MaterialStruct {
    vec4 base_color; // 16
    vec4 subsurface_radius; // 32
    vec4 subsurface_color; //48
    
    float subsurface; // 52
    float metallic; // 56
    float specular; // 60
    float specular_tint; // 64
    float roughness; // 68
    float anisotropic; // 72
    float anisotropic_rotation; // 76
    float sheen; // 80
    float sheen_tint; // 84
    float clearcoat; // 88
    float clearcoat_roughness; // 92
    float ior; // 96
    float transmission; // 100
    float transmission_roughness; // 104
    
    int32_t volume_texture_id; // 108
    int32_t transfer_function_texture_id; // 112
    int32_t flags; // 116
    int32_t base_color_texture_id; // 120
    int32_t roughness_texture_id; // 124
    int32_t occlusion_texture_id; // 128



    int32_t alpha_texture_id; // 132
    int32_t bump_texture_id; // 136
    int32_t ph3_id; // 140
    int32_t ph4_id; // 144
    int32_t ph5_id; // 148
    int32_t ph6_id; // 152
    int32_t ph7_id; // 156
    int32_t ph8_id; // 160

    int32_t ph9_id; // 164
    int32_t ph10_id; // 168
    int32_t ph11_id; // 172
    int32_t ph12_id; // 176
    int32_t ph13_id; // 180
    int32_t ph14_id; // 184
    int32_t ph15_id; // 188
    int32_t ph16_id; // 192
    // int32_t ph8_id; // 196
    // int32_t ph8_id; // 200
    // int32_t ph8_id; // 184
    // int32_t ph8_id; // 184
};
