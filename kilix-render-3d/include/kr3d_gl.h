#ifndef KR3D_GL_H
#define KR3D_GL_H

#include "kr3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional OpenGL 3.3 backend.  The caller owns the EGL/GL context and native
 * surface.  A compatible context must be current for create and every device
 * call; the backend never swaps buffers or changes the current context.
 */
typedef struct kr3d_gl_device kr3d_gl_device;

/* Deterministic verification hooks. Production callers leave fault at NONE. */
typedef enum {
    KR3D_GL_FAULT_NONE = 0,
    KR3D_GL_FAULT_SHADER_COMPILE,
    KR3D_GL_FAULT_PROGRAM_LINK,
    KR3D_GL_FAULT_FRAME_BEGIN_CONTEXT_LOST,
    KR3D_GL_FAULT_DRAW_CONTEXT_LOST,
    KR3D_GL_FAULT_FRAME_END_CONTEXT_LOST
} kr3d_gl_fault;

typedef struct {
    size_t struct_size;
    bool readback_color;
    bool readback_depth;
    kr3d_gl_fault fault;
} kr3d_gl_options;

bool kr3d_gl_device_create(const kr3d_device_desc *desc,
                           const kr3d_gl_options *options,
                           kr3d_gl_device **out, kr3d_error *error);
void kr3d_gl_device_destroy(kr3d_gl_device *device);
bool kr3d_gl_mesh_create(kr3d_gl_device *device,
                         const kr3d_mesh_desc *desc,
                         kr3d_mesh_handle *out, kr3d_error *error);
void kr3d_gl_mesh_destroy(kr3d_gl_device *device, kr3d_mesh_handle handle);
bool kr3d_gl_texture_create(kr3d_gl_device *device,
                            const kr3d_texture_desc *desc,
                            kr3d_texture_handle *out, kr3d_error *error);
bool kr3d_gl_texture_update(kr3d_gl_device *device,
                            kr3d_texture_handle handle,
                            const kr3d_texture_update_desc *desc,
                            kr3d_error *error);
void kr3d_gl_texture_destroy(kr3d_gl_device *device,
                             kr3d_texture_handle handle);
bool kr3d_gl_material_create(kr3d_gl_device *device,
                             const kr3d_material_desc *desc,
                             kr3d_material_handle *out, kr3d_error *error);
void kr3d_gl_material_destroy(kr3d_gl_device *device,
                              kr3d_material_handle handle);
bool kr3d_gl_frame_begin(kr3d_gl_device *device,
                          const kr3d_frame_desc *desc, kr3d_error *error);
bool kr3d_gl_draw(kr3d_gl_device *device, const kr3d_draw_desc *desc,
                   kr3d_error *error);
bool kr3d_gl_frame_end(kr3d_gl_device *device, kr3d_frame_result *result,
                        kr3d_error *error);

/* Returns a stable backend name after successful creation. */
const char *kr3d_gl_renderer(const kr3d_gl_device *device);

#ifdef __cplusplus
}
#endif
#endif
