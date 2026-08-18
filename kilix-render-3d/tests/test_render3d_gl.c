#include "kr3d_gl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); goto done; } } while (0)
int main(void)
{
    EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;kr3d_gl_device*device=NULL;kr3d_mesh_handle mesh=0;kr3d_texture_handle texture=0;kr3d_material_handle material=0;int result=EXIT_FAILURE;
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if(get_platform)display=get_platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    if(display==EGL_NO_DISPLAY)display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major=0,minor=0;CHECK(display!=EGL_NO_DISPLAY&&eglInitialize(display,&major,&minor));CHECK(eglBindAPI(EGL_OPENGL_API));
    static const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};EGLConfig config;EGLint count=0;CHECK(eglChooseConfig(display,config_attrs,&config,1,&count)&&count==1);
    static const EGLint pbuffer_attrs[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};surface=eglCreatePbufferSurface(display,config,pbuffer_attrs);CHECK(surface!=EGL_NO_SURFACE);
    static const EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);CHECK(context!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(display,surface,surface,context));
    kr3d_error error={0};kr3d_device_desc dd={sizeof dd,4,4,4,64,64};kr3d_gl_options options={sizeof options,true,true,KR3D_GL_FAULT_NONE};
    {kr3d_gl_device*faulted=(kr3d_gl_device*)(uintptr_t)1;kr3d_gl_options fault_options=options;
     fault_options.fault=KR3D_GL_FAULT_SHADER_COMPILE;CHECK(!kr3d_gl_device_create(&dd,&fault_options,&faulted,&error));CHECK(faulted==NULL&&error.code==KR3D_ERROR_UNSUPPORTED&&strstr(error.message,"shader failed")&&strlen(error.message)<sizeof error.message);
     faulted=(kr3d_gl_device*)(uintptr_t)1;fault_options.fault=KR3D_GL_FAULT_PROGRAM_LINK;CHECK(!kr3d_gl_device_create(&dd,&fault_options,&faulted,&error));CHECK(faulted==NULL&&error.code==KR3D_ERROR_UNSUPPORTED&&strcmp(error.message,"GL shader program link failed")==0);
     fault_options.fault=KR3D_GL_FAULT_FRAME_BEGIN_CONTEXT_LOST;CHECK(kr3d_gl_device_create(&dd,&fault_options,&faulted,&error));kr3d_frame_desc injected={sizeof injected,64,64,NULL,NULL,0,kr3d_mat4_identity(),kr3d_mat4_identity(),{0,0,-1},.2f,.8f};CHECK(!kr3d_gl_frame_begin(faulted,&injected,&error)&&error.code==KR3D_ERROR_DEVICE_LOST&&strstr(error.message,"injected"));CHECK(!kr3d_gl_frame_begin(faulted,&injected,&error)&&error.code==KR3D_ERROR_DEVICE_LOST&&strcmp(error.message,"GL context is lost")==0);kr3d_gl_device_destroy(faulted);}
    CHECK(kr3d_gl_device_create(&dd,&options,&device,&error));CHECK(kr3d_gl_renderer(device)!=NULL);uint32_t texel=UINT32_C(0xff00ff00);kr3d_texture_desc td={sizeof td,1,1,&texel,KR3D_FILTER_NEAREST,KR3D_ADDRESS_CLAMP,KR3D_ADDRESS_CLAMP};CHECK(kr3d_gl_texture_create(device,&td,&texture,&error));texel=UINT32_C(0xff0000ff);kr3d_texture_update_desc tu={sizeof tu,0,0,1,1,&texel};CHECK(kr3d_gl_texture_update(device,texture,&tu,&error));tu.x=1;CHECK(!kr3d_gl_texture_update(device,texture,&tu,&error)&&error.code==KR3D_ERROR_ARGUMENT);tu.x=0;
    const kr3d_vertex vertices[]={{{-0.8f,-0.8f,0.0f},{0,0,1},{0,0},UINT32_C(0xff0000ff)},{{0.8f,-0.8f,0.0f},{0,0,1},{1,0},UINT32_C(0xff0000ff)},{{0.0f,0.8f,0.0f},{0,0,1},{0,1},UINT32_C(0xff0000ff)}};const uint32_t indices[]={0,1,2};kr3d_mesh_desc md={sizeof md,vertices,3,indices,3};CHECK(kr3d_gl_mesh_create(device,&md,&mesh,&error));
    kr3d_material_desc mat={sizeof mat,texture,UINT32_C(0xffffffff),KR3D_MATERIAL_UNLIT,KR3D_ALPHA_OPAQUE,0};CHECK(kr3d_gl_material_create(device,&mat,&material,&error));kr3d_frame_desc frame={sizeof frame,64,64,NULL,NULL,UINT32_C(0xff0d0b07),kr3d_mat4_identity(),kr3d_mat4_identity(),{0,0,-1},0.2f,0.8f};CHECK(kr3d_gl_frame_begin(device,&frame,&error));kr3d_draw_desc draw={sizeof draw,mesh,material,kr3d_mat4_identity()};CHECK(kr3d_gl_draw(device,&draw,&error));kr3d_frame_result output={.struct_size=sizeof output};CHECK(kr3d_gl_frame_end(device,&output,&error));
    CHECK(output.color&&output.depth&&output.width==64&&output.height==64);CHECK(output.submitted_triangles==1&&output.rasterized_triangles==1);uint32_t center=output.color[32u*64u+32u];CHECK((center&UINT32_C(0x00ffffff))==UINT32_C(0x000000ff));CHECK(output.depth[32u*64u+32u]>0.49f&&output.depth[32u*64u+32u]<0.51f);{kr3d_material_handle lit=0;mat.flags=0;CHECK(kr3d_gl_material_create(device,&mat,&lit,&error));CHECK(kr3d_gl_frame_begin(device,&frame,&error));draw.material=lit;draw.model=kr3d_mat4_scale((kr3d_vec3){1,1,0});CHECK(!kr3d_gl_draw(device,&draw,&error)&&error.code==KR3D_ERROR_ARGUMENT);CHECK(kr3d_gl_frame_end(device,&output,&error));kr3d_gl_material_destroy(device,lit);}result=EXIT_SUCCESS;
done:
    if(result!=EXIT_SUCCESS&&device)fprintf(stderr,"GL backend: %s\n",kr3d_gl_renderer(device));
    kr3d_gl_material_destroy(device,material);kr3d_gl_texture_destroy(device,texture);kr3d_gl_mesh_destroy(device,mesh);kr3d_gl_device_destroy(device);
    if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);eglTerminate(display);}
    if(result==EXIT_SUCCESS)puts("kilix-render-3d GL tests passed");
    return result;
}
