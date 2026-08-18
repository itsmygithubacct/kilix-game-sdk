#include "conformance_scene.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KR3D_CONFORMANCE_GL
#include "kr3d_gl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define DEV kr3d_gl_device
#define device_create(d,o,e) kr3d_gl_device_create((d),(o),&device,(e))
#define device_destroy kr3d_gl_device_destroy
#define mesh_create kr3d_gl_mesh_create
#define texture_create kr3d_gl_texture_create
#define material_create kr3d_gl_material_create
#define frame_begin kr3d_gl_frame_begin
#define draw_submit kr3d_gl_draw
#define frame_end kr3d_gl_frame_end
#else
#define DEV kr3d_device
#define device_create(d,o,e) kr3d_device_create((d),&device,(e))
#define device_destroy kr3d_device_destroy
#define mesh_create kr3d_mesh_create
#define texture_create kr3d_texture_create
#define material_create kr3d_material_create
#define frame_begin kr3d_frame_begin
#define draw_submit kr3d_draw
#define frame_end kr3d_frame_end
#endif

static DEV *device;

static void check_color(uint32_t actual, uint32_t expected, const char *name)
{
    unsigned ar=actual&255u,ag=(actual>>8)&255u,ab=(actual>>16)&255u;
    unsigned er=expected&255u,eg=(expected>>8)&255u,eb=(expected>>16)&255u;
    if (abs((int)ar-(int)er)>3 || abs((int)ag-(int)eg)>3 ||
        abs((int)ab-(int)eb)>3) {
        fprintf(stderr,"probe %s got=%08" PRIx32 " expected=%08" PRIx32 "\n",
                name,actual,expected);
        abort();
    }
}

static uint64_t render(uint32_t width, uint32_t height,
                       kr3d_mesh_handle quad, kr3d_mesh_handle triangle,
                       kr3d_mesh_handle back, kr3d_mesh_handle clipped,
                       kr3d_material_handle texture, kr3d_material_handle red,
                       kr3d_material_handle green, kr3d_material_handle blue)
{
    kr3d_error error={0};
    kr3d_frame_desc frame={sizeof frame,width,height,NULL,NULL,CONF_CLEAR,
        {{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}},
        {{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}, {0,0,-1},1,0};
    assert(frame_begin(device,&frame,&error));
    kr3d_draw_desc draw={sizeof draw,quad,texture,kr3d_mat4_identity()};
    assert(draw_submit(device,&draw,&error));
    draw.mesh=triangle; draw.material=blue;
    draw.model=kr3d_mat4_mul(kr3d_mat4_translate((kr3d_vec3){.55f,-.55f,.5f}),
                             kr3d_mat4_scale((kr3d_vec3){1.4f,1.4f,1}));
    assert(draw_submit(device,&draw,&error));
    draw.material=green; draw.model=kr3d_mat4_mul(
        kr3d_mat4_translate((kr3d_vec3){.55f,-.55f,-.5f}),
        kr3d_mat4_scale((kr3d_vec3){1.0f,1.0f,1}));
    assert(draw_submit(device,&draw,&error));
    draw.material=red; draw.model=kr3d_mat4_translate((kr3d_vec3){.55f,.55f,0});
    assert(draw_submit(device,&draw,&error));
    draw.mesh=clipped; draw.material=blue; draw.model=kr3d_mat4_identity();
    assert(draw_submit(device,&draw,&error));
    draw.mesh=back; draw.material=red;
    assert(draw_submit(device,&draw,&error));
    kr3d_frame_result result={.struct_size=sizeof result};
    assert(frame_end(device,&result,&error));
    assert(result.width==width&&result.height==height&&result.submitted_triangles==7);
    for(size_t i=0;i<sizeof conf_probes/sizeof conf_probes[0];++i){
        size_t x=conf_pixel(conf_probes[i].x,width);
        size_t y=conf_pixel(-conf_probes[i].y,height);
        assert(x<width&&y<height);
        check_color(result.color[y*(size_t)width+x],conf_probes[i].expected,
                    conf_probes[i].name);
    }
    return conf_hash(result.color,(size_t)width*height);
}

int main(void)
{
#ifdef KR3D_CONFORMANCE_GL
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY); EGLint major,minor,count;
    EGLConfig config; EGLSurface surface; EGLContext context;
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
    const EGLint pa[]={EGL_WIDTH,96,EGL_HEIGHT,96,EGL_NONE};
    const EGLint xa[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    assert(display!=EGL_NO_DISPLAY&&eglInitialize(display,&major,&minor));
    assert(eglBindAPI(EGL_OPENGL_API)&&eglChooseConfig(display,ca,&config,1,&count)&&count==1);
    surface=eglCreatePbufferSurface(display,config,pa); assert(surface!=EGL_NO_SURFACE);
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,xa); assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,surface,surface,context));
#endif
    kr3d_error error={0}; kr3d_device_desc dd={sizeof dd,4,2,4,96,96};
#ifdef KR3D_CONFORMANCE_GL
    kr3d_gl_options options={sizeof options,true,true,KR3D_GL_FAULT_NONE}; assert(device_create(&dd,&options,&error));
#else
    assert(device_create(&dd,NULL,&error));
#endif
    kr3d_mesh_handle quad=0,tri=0,back=0,clip=0;
    kr3d_mesh_desc md={sizeof md,conf_quad_vertices,4,conf_quad_indices,6};
    assert(mesh_create(device,&md,&quad,&error));
    md=(kr3d_mesh_desc){sizeof md,conf_triangle_vertices,3,conf_front_indices,3};
    assert(mesh_create(device,&md,&tri,&error));
    md.indices=conf_back_indices; assert(mesh_create(device,&md,&back,&error));
    md=(kr3d_mesh_desc){sizeof md,conf_clip_vertices,3,conf_clip_indices,3};
    assert(mesh_create(device,&md,&clip,&error));
    kr3d_texture_handle tex=0; kr3d_texture_desc td={sizeof td,2,2,conf_texels,
        KR3D_FILTER_NEAREST,KR3D_ADDRESS_CLAMP,KR3D_ADDRESS_CLAMP};
    assert(texture_create(device,&td,&tex,&error));
    kr3d_material_handle textured=0,red=0,green=0,blue=0;
    kr3d_material_desc mat={sizeof mat,tex,UINT32_MAX,KR3D_MATERIAL_UNLIT,
        KR3D_ALPHA_OPAQUE,.5f,1,0}; assert(material_create(device,&mat,&textured,&error));
#define MAKE_MATERIAL(out,color) do { mat.texture=0;mat.rgba=(color); \
    assert(material_create(device,&mat,&(out),&error)); } while(0)
    MAKE_MATERIAL(red,CONF_RED); MAKE_MATERIAL(green,CONF_GREEN); MAKE_MATERIAL(blue,CONF_BLUE);
    uint64_t first=render(64,64,quad,tri,back,clip,textured,red,green,blue);
    (void)render(80,48,quad,tri,back,clip,textured,red,green,blue);
#ifndef KR3D_CONFORMANCE_GL
    uint64_t repeat=render(64,64,quad,tri,back,clip,textured,red,green,blue);
    assert(first==repeat);
    /* Intentional scalar raster changes update this reviewed byte golden. */
    assert(first==UINT64_C(0x0de7af950beb653e));
    printf("scalar conformance hash=%016" PRIx64 "\n",first);
#else
    (void)first;
#endif
    device_destroy(device);
#ifdef KR3D_CONFORMANCE_GL
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
#endif
    puts("renderer conformance tests passed"); return 0;
}
