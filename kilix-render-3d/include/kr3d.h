#ifndef KR3D_H
#define KR3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KR3D_API_VERSION 1u

typedef struct kr3d_device kr3d_device;
typedef uint32_t kr3d_mesh_handle;
typedef uint32_t kr3d_texture_handle;
typedef uint32_t kr3d_material_handle;

typedef struct { float x, y, z; } kr3d_vec3;
typedef struct { float x, y, z, w; } kr3d_quat;
/* Column-major; column vectors; right-handed world and view; OpenGL [-1,1] Z. */
typedef struct { float m[16]; } kr3d_mat4;
typedef struct { kr3d_vec3 min, max; } kr3d_aabb;
typedef struct { kr3d_vec3 normal; float d; } kr3d_plane;
typedef struct { kr3d_plane planes[6]; } kr3d_frustum;

kr3d_vec3 kr3d_vec3_add(kr3d_vec3 a, kr3d_vec3 b);
kr3d_vec3 kr3d_vec3_sub(kr3d_vec3 a, kr3d_vec3 b);
kr3d_vec3 kr3d_vec3_scale(kr3d_vec3 v, float s);
float kr3d_vec3_dot(kr3d_vec3 a, kr3d_vec3 b);
kr3d_vec3 kr3d_vec3_cross(kr3d_vec3 a, kr3d_vec3 b);
bool kr3d_vec3_normalize(kr3d_vec3 v, kr3d_vec3 *out);
kr3d_quat kr3d_quat_identity(void);
bool kr3d_quat_axis_angle(kr3d_vec3 axis, float radians, kr3d_quat *out);
bool kr3d_quat_normalize(kr3d_quat q, kr3d_quat *out);
kr3d_quat kr3d_quat_mul(kr3d_quat a, kr3d_quat b);
kr3d_quat kr3d_quat_nlerp(kr3d_quat a, kr3d_quat b, float t);
kr3d_mat4 kr3d_mat4_identity(void);
kr3d_mat4 kr3d_mat4_mul(kr3d_mat4 a, kr3d_mat4 b);
kr3d_mat4 kr3d_mat4_translate(kr3d_vec3 v);
kr3d_mat4 kr3d_mat4_scale(kr3d_vec3 v);
kr3d_mat4 kr3d_mat4_from_quat(kr3d_quat q);
bool kr3d_mat4_look_at(kr3d_vec3 eye, kr3d_vec3 center, kr3d_vec3 up,
                       kr3d_mat4 *out);
bool kr3d_mat4_perspective(float fovy_radians, float aspect, float near_z,
                           float far_z, kr3d_mat4 *out);
bool kr3d_mat4_transform_point(kr3d_mat4 m, kr3d_vec3 p, kr3d_vec3 *out);
kr3d_vec3 kr3d_mat4_transform_vector(kr3d_mat4 m, kr3d_vec3 v);
bool kr3d_frustum_from_matrix(kr3d_mat4 clip, kr3d_frustum *out);
bool kr3d_frustum_contains_sphere(const kr3d_frustum *f, kr3d_vec3 c, float r);
bool kr3d_frustum_intersects_aabb(const kr3d_frustum *f, kr3d_aabb box);

typedef enum { KR3D_OK = 0, KR3D_ERROR_ARGUMENT, KR3D_ERROR_LIMIT,
    KR3D_ERROR_MEMORY, KR3D_ERROR_STATE, KR3D_ERROR_UNSUPPORTED,
    KR3D_ERROR_DEVICE_LOST } kr3d_error_code;
typedef struct { kr3d_error_code code; char message[96]; } kr3d_error;
typedef enum { KR3D_FILTER_NEAREST = 0, KR3D_FILTER_BILINEAR } kr3d_filter;
typedef enum { KR3D_ADDRESS_CLAMP = 0, KR3D_ADDRESS_REPEAT } kr3d_address;
typedef enum { KR3D_ALPHA_OPAQUE = 0, KR3D_ALPHA_TEST, KR3D_ALPHA_BLEND } kr3d_alpha_mode;

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t color_rgba; /* R in least-significant byte. */
} kr3d_vertex;
typedef struct { size_t struct_size; const kr3d_vertex *vertices; size_t vertex_count;
    const uint32_t *indices; size_t index_count; } kr3d_mesh_desc;

/* Bounded, checksummed K3DMESH1 transport. The decoded arrays are owned by
   kr3d_mesh_data and remain suitable for kr3d_mesh_create until released. */
#define KR3D_MESH_FILE_MAGIC "K3DMESH1"
#define KR3D_MESH_FILE_HEADER_SIZE 104u
typedef struct { size_t struct_size; size_t max_file_bytes; uint32_t max_vertices;
    uint32_t max_indices, max_material_slots; } kr3d_mesh_file_limits;
typedef struct { kr3d_vertex *vertices; uint32_t vertex_count; uint32_t *indices;
    uint32_t index_count, material_slot_count; kr3d_aabb bounds; } kr3d_mesh_data;
bool kr3d_mesh_file_decode(const void *bytes, size_t size,
                           const kr3d_mesh_file_limits *limits,
                           kr3d_mesh_data *out, kr3d_error *error);
bool kr3d_mesh_file_encode(const kr3d_mesh_desc *mesh, uint32_t material_slot_count,
                           void **out_bytes, size_t *out_size, kr3d_error *error);
void kr3d_mesh_file_bytes_free(void *bytes);
void kr3d_mesh_data_release(kr3d_mesh_data *data);
typedef struct { size_t struct_size; uint32_t width, height; const uint32_t *rgba;
    kr3d_filter filter; kr3d_address address_u, address_v; } kr3d_texture_desc;
/* Replaces a tightly packed rectangular texel region without changing the
   texture handle, dimensions, sampler state, or referencing materials. */
typedef struct { size_t struct_size; uint32_t x, y, width, height;
    const uint32_t *rgba; } kr3d_texture_update_desc;
enum { KR3D_MATERIAL_UNLIT = 1u, KR3D_MATERIAL_TWO_SIDED = 2u,
       KR3D_MATERIAL_EMISSIVE = 4u };
typedef struct { size_t struct_size; kr3d_texture_handle texture; uint32_t rgba;
    uint32_t flags; kr3d_alpha_mode alpha_mode; float alpha_cutoff;
    /* Appended ABI fields. Older descriptors default to rough, non-specular. */
    float roughness, specular; } kr3d_material_desc;
typedef struct { size_t struct_size; uint32_t max_meshes, max_textures, max_materials;
    uint32_t max_width, max_height; } kr3d_device_desc;
typedef struct { size_t struct_size; uint32_t width, height; uint32_t *color;
    float *depth; uint32_t clear_rgba; kr3d_mat4 view, projection;
    kr3d_vec3 light_direction; float ambient, directional; } kr3d_frame_desc;
typedef struct { size_t struct_size; kr3d_mesh_handle mesh; kr3d_material_handle material;
    kr3d_mat4 model; } kr3d_draw_desc;
typedef struct { size_t struct_size; const uint32_t *color; const float *depth;
    uint32_t width, height, submitted_triangles, rasterized_triangles, shaded_pixels; } kr3d_frame_result;

bool kr3d_device_create(const kr3d_device_desc *desc, kr3d_device **out, kr3d_error *error);
void kr3d_device_destroy(kr3d_device *device);
bool kr3d_mesh_create(kr3d_device *device, const kr3d_mesh_desc *desc,
                      kr3d_mesh_handle *out, kr3d_error *error);
void kr3d_mesh_destroy(kr3d_device *device, kr3d_mesh_handle handle);
bool kr3d_texture_create(kr3d_device *device, const kr3d_texture_desc *desc,
                         kr3d_texture_handle *out, kr3d_error *error);
bool kr3d_texture_update(kr3d_device *device, kr3d_texture_handle handle,
                         const kr3d_texture_update_desc *desc,
                         kr3d_error *error);
void kr3d_texture_destroy(kr3d_device *device, kr3d_texture_handle handle);
bool kr3d_material_create(kr3d_device *device, const kr3d_material_desc *desc,
                          kr3d_material_handle *out, kr3d_error *error);
void kr3d_material_destroy(kr3d_device *device, kr3d_material_handle handle);
bool kr3d_frame_begin(kr3d_device *device, const kr3d_frame_desc *desc, kr3d_error *error);
bool kr3d_draw(kr3d_device *device, const kr3d_draw_desc *desc, kr3d_error *error);
bool kr3d_frame_end(kr3d_device *device, kr3d_frame_result *result, kr3d_error *error);

#ifdef __cplusplus
}
#endif
#endif
