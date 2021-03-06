/* File shared by both GLSL and C++ */

#ifdef GLSL
#define int32_t int
#endif

#ifndef MAX_ENTITIES
#define MAX_ENTITIES 256
#endif

struct EntityStruct {
	int32_t initialized;
	int32_t transform_id;
	int32_t camera_id;
	int32_t material_id;
	int32_t light_id;
	int32_t mesh_id;
};