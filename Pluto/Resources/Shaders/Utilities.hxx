#ifndef UTILITIES_HXX
#define UTILITIES_HXX

/* MATH UTILITIES */
float saturate(float v)
{
    return clamp(v, 0.0, 1.0);
}

float pow2(float x) { 
    return x*x;
}

float luminance(const in vec3 c) {
	return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}


/* TEXTURE UTILITIES */
// Texture Lookups --------------------------------------
// --- unfiltered checkerboard ---
float checkersTexture( in vec3 p )
{
    vec3 q = floor(p);
    return mod( q.x+q.y+q.z, 2.0 );            // xor pattern
}

// From GPU gems
float filterwidth(in vec3 v)
{
    // #ifndef RAYTRACING
    // vec3 fw = max(abs(dFdxFine(v)), abs(dFdyFine(v)));
    // return max(max(fw.x, fw.y), fw.z);
    // #else
    // #endif
    vec3 q = floor(v);
    return mod( q.x+q.y+q.z, 2.0 );            // xor pattern
}

float checker(in vec3 uvw, float scale)
{
    float width = filterwidth(uvw*scale);
    vec3 p0 = (uvw * scale) - .5 * width;
    vec3 p1 = (uvw * scale) + .5 * width;
    #define BUMPINT(x) (floor((x)/2.0f) + 2.f * max( ((x)/2.0f ) - floor((x)/2.0f ) - .5f, 0.f))
    vec3 i = (BUMPINT(p1) - BUMPINT(p0)) / width;
    return step(.5, i.x * i.y * i.z + (1.0f - i.x) * (1.0f - i.y) * (1.0f - i.z));
}

vec4 sample_texture_2D(vec4 default_color, int texture_id, vec2 uv, vec3 m_position)
{
    vec4 color = default_color; 
    if ((texture_id < 0) || (texture_id >= MAX_TEXTURES)) return default_color;

    TextureStruct tex = txbo.textures[texture_id];
    
    if ((tex.sampler_id < 0) || (tex.sampler_id >= MAX_SAMPLERS))  return default_color;

    /* Raster texture */
    if (tex.type == 0) {
        color = texture(sampler2D(texture_2Ds[texture_id], samplers[tex.sampler_id]), uv);
    }

    /* Procedural checker texture */
    else if (tex.type == 1)
    {
        float mate = checker(m_position, tex.scale);
        // If I do gamma correction here on linux, I get weird nans...
        color = mix(tex.color1, tex.color2, mate);
    }

    return color;
}

vec4 sample_texture_3D(vec4 default_color, int texture_id, vec3 uvw, float lod)
{
    if ((texture_id < 0) || (texture_id >= MAX_TEXTURES)) return default_color;
    vec4 color = default_color;

    TextureStruct tex = txbo.textures[texture_id];
    
    if ((tex.sampler_id < 0) || (tex.sampler_id >= MAX_SAMPLERS))  return default_color;

    if (tex.type == 0)
    {
        color = texture(sampler3D(texture_3Ds[texture_id], samplers[tex.sampler_id]), uvw);
    }
    else if (tex.type == 1)
    {
        float mate = checker(uvw, tex.scale);//texture(sampler3D(texture_3Ds[material.volume_texture_id], samplers[material.volume_texture_id]), normalize(position) );
        color = mix(tex.color1, tex.color2, mate);
    }

    //textureLod(volumeTexture, pos, mbo.lod);
    return color;//vec4(color.rgb, 0.0);
}

// Material Properties --------------------------------------
vec4 getAlbedo(inout MaterialStruct material, vec4 vert_color, vec2 uv, vec3 m_position)
{
	vec4 albedo = material.base_color; 

	/* If the use vertex colors flag is set, use the vertex color as the base color instead. */
	if ((material.flags & (1 << 0)) != 0)
		albedo = vert_color;

    albedo = sample_texture_2D(albedo, material.base_color_texture_id, uv, m_position);
	return vec4(pow(albedo.rgb, vec3(push.consts.gamma)), albedo.a);
}

float getAlphaMask(inout MaterialStruct material, vec2 uv, vec3 m_position) {
    return sample_texture_2D(vec4(material.base_color.a), material.alpha_texture_id, uv, m_position).r;
}

float getRoughness(inout MaterialStruct material, vec2 uv, vec3 m_position)
{
    float roughness = sample_texture_2D(vec4(material.roughness), material.roughness_texture_id, uv, m_position).r;
    return clamp(roughness, 0.0, 1.0);
}

float getTransmissionRoughness(inout MaterialStruct material)
{
    return clamp(material.transmission_roughness, 0.0, 1.0);
}

// Todo: add support for ray tracing (dfd... not supported)
// See http://www.thetenthplanet.de/archives/1180
vec3 perturbNormal(inout MaterialStruct material, vec3 w_position, vec3 w_normal, vec2 uv, vec3 m_position)
{
    #ifndef RAYTRACING
    w_normal = normalize(w_normal);
	vec3 tangentNormal = sample_texture_2D(vec4(w_normal.xyz, 0.0), material.bump_texture_id, uv, m_position).xyz;// texture(normalMap, inUV).xyz * 2.0 - 1.0;
    if (w_normal == tangentNormal) return w_normal;

    // tangentNormal = normalize(  tangentNormalDX + tangentNormalDY + vec3(0.0, 0.0, 1.0) );//vec3(0.0, 0.0, 1.0);

	vec3 q1 = dFdx(w_position);
	vec3 q2 = dFdy(w_position);
	vec2 st1 = dFdx(uv);
	vec2 st2 = dFdy(uv);

    // vec3 T = vec3(1.0, 0.0, 0.0); // Right
    // vec3 B = vec3(0.0, 1.0, 0.0); // Forward
    // vec3 N = vec3(0.0, 0.0, 1.0); // Up
	
    vec3 N = normalize(w_normal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

    EntityStruct target_entity = ebo.entities[push.consts.target_id];
    TransformStruct target_transform = tbo.transforms[target_entity.transform_id];
    // tangentNormal = (transpose(target_transform.worldToLocal) * vec4(w_normal, 0.0)).xyz;

	return normalize(TBN * tangentNormal);
    #else
    return w_normal;
    #endif
	// return tangentNormal;
}




void unpack_entity(
    int entity_id,
    inout EntityStruct entity, inout TransformStruct transform,
    inout MaterialStruct material, inout LightStruct light
) {
    if ((entity_id < 0) || (entity_id >= MAX_ENTITIES)) return;
    entity = ebo.entities[entity_id];
    if (entity.initialized != 1) return;
    if ((entity.transform_id < 0) || (entity.transform_id >= MAX_TRANSFORMS)) return;
    transform = tbo.transforms[entity.transform_id];
    if ((entity.material_id >= 0) && (entity.material_id < MAX_MATERIALS)) 
        material = mbo.materials[entity.material_id];
    if ((entity.light_id >= 0) && (entity.light_id < MAX_LIGHTS))
        light = lbo.lights[entity.light_id];
}



void unpack_entity_struct(
    int entity_id, inout EntityStruct entity
) {
    if ((entity_id < 0) || (entity_id >= MAX_ENTITIES)) return;
    entity = ebo.entities[entity_id];
}

void unpack_material_struct(
    int entity_id, const in EntityStruct entity, vec2 uv, vec3 m_position, inout MaterialStruct material
) {
    if ((entity_id < 0) || (entity_id >= MAX_ENTITIES)) return;
    if (entity.initialized != 1) return;
    material = mbo.materials[entity.material_id];

    material.base_color = getAlbedo(material, vec4(0.0), uv, m_position);
    float mask = getAlphaMask(material, uv, m_position);
    material.base_color.a = (mask < material.base_color.a) ? mask : material.base_color.a;
}

void unpack_light_struct(
    int entity_id, const in EntityStruct entity, inout LightStruct light
) {
    if ((entity_id < 0) || (entity_id >= MAX_ENTITIES)) return;
    if (entity.initialized != 1) return;
    light = lbo.lights[entity.light_id];
}

#endif