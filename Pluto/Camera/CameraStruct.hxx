/* File shared by both GLSL and C++ */

#ifndef MAX_MULTIVIEW
#define MAX_MULTIVIEW 6
#endif

#ifndef MAX_CAMERAS
#define MAX_CAMERAS 256
#endif

#ifndef GLSL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;
#endif

struct CameraObject
{
    mat4 view;
    mat4 proj;
    mat4 viewinv;
    mat4 projinv;
    mat4 viewproj;
    float near_pos;
    // float far_pos;
    float fov;
    float pad1;
    float pad2;
};

struct CameraStruct
{
    CameraObject multiviews[MAX_MULTIVIEW];
};