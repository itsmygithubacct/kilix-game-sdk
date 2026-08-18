#define GL_GLEXT_PROTOTYPES 1
#include "kr3d_gl.h"

#include <GL/gl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { GLuint vao, vbo, ebo; GLsizei count; } gl_mesh;
typedef struct { GLuint name; } gl_texture;
typedef struct {
    kr3d_texture_handle texture;
    uint32_t rgba, flags;
    kr3d_alpha_mode alpha;
    float cutoff;
    bool live;
} gl_material;

struct kr3d_gl_device {
    gl_mesh *meshes;
    gl_texture *textures;
    gl_material *materials;
    uint32_t max_meshes, max_textures, max_materials, max_width, max_height;
    GLuint program;
    GLint u_mvp, u_model, u_light, u_ambient, u_directional;
    GLint u_material, u_texture, u_use_texture, u_unlit, u_alpha_mode;
    GLint u_alpha_cutoff;
    bool frame, read_color, read_depth;
    uint32_t width, height, submitted, rasterized;
    uint32_t *color;
    float *depth;
    char renderer[96];
};

static bool fail(kr3d_error *e, kr3d_error_code c, const char *message)
{
    if (e) {
        e->code = c;
        (void)snprintf(e->message, sizeof e->message, "%s", message);
    }
    return false;
}

static bool finite_matrix(kr3d_mat4 m)
{
    for (size_t i = 0; i < 16u; ++i) if (!isfinite(m.m[i])) return false;
    return true;
}

static bool gl_ok(kr3d_error *e, const char *message)
{
    if (glGetError() == GL_NO_ERROR) return true;
    return fail(e, KR3D_ERROR_STATE, message);
}

static GLuint compile_shader(GLenum kind, const char *source, kr3d_error *e)
{
    GLuint shader = glCreateShader(kind);
    GLint ok = GL_FALSE;
    if (!shader) { fail(e, KR3D_ERROR_STATE, "could not create GL shader"); return 0; }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    if (e) {
        GLchar log[72] = {0};
        GLsizei length = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof log, &length, log);
        (void)length;
        e->code = KR3D_ERROR_UNSUPPORTED;
        (void)snprintf(e->message, sizeof e->message, "GL shader failed: %.71s", log);
    }
    glDeleteShader(shader);
    return 0;
}

static GLuint make_program(kr3d_error *e)
{
    static const char vertex_source[] =
        "#version 330 core\n"
        "layout(location=0) in vec3 a_position;\n"
        "layout(location=1) in vec3 a_normal;\n"
        "layout(location=2) in vec2 a_uv;\n"
        "layout(location=3) in vec4 a_color;\n"
        "uniform mat4 u_mvp; uniform mat4 u_model;\n"
        "out vec3 v_normal; out vec2 v_uv; out vec4 v_color;\n"
        "void main(){ gl_Position=u_mvp*vec4(a_position,1.0);"
        "v_normal=mat3(u_model)*a_normal; v_uv=a_uv; v_color=a_color; }\n";
    static const char fragment_source[] =
        "#version 330 core\n"
        "in vec3 v_normal; in vec2 v_uv; in vec4 v_color;\n"
        "uniform vec3 u_light; uniform float u_ambient,u_directional;\n"
        "uniform vec4 u_material; uniform sampler2D u_texture;\n"
        "uniform int u_use_texture,u_unlit,u_alpha_mode;"
        "uniform float u_alpha_cutoff; out vec4 color;\n"
        "void main(){ vec4 texel=u_use_texture!=0?texture(u_texture,v_uv):vec4(1);"
        "vec4 c=texel*v_color*u_material;"
        "if(u_alpha_mode==1 && c.a<u_alpha_cutoff) discard;"
        "float l=u_unlit!=0?1.0:u_ambient+u_directional*max(0.0,dot(normalize(v_normal),-normalize(u_light)));"
        "color=vec4(c.rgb*l,c.a); }\n";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_source, e);
    GLuint fs = vs ? compile_shader(GL_FRAGMENT_SHADER, fragment_source, e) : 0;
    if (!vs || !fs) { if (vs) glDeleteShader(vs); return 0; }
    GLuint program = glCreateProgram();
    GLint ok = GL_FALSE;
    if (program) {
        glAttachShader(program, vs); glAttachShader(program, fs);
        glLinkProgram(program); glGetProgramiv(program, GL_LINK_STATUS, &ok);
    }
    glDeleteShader(vs); glDeleteShader(fs);
    if (ok == GL_TRUE) return program;
    if (program) glDeleteProgram(program);
    fail(e, KR3D_ERROR_UNSUPPORTED, "GL shader program link failed");
    return 0;
}

static void unpack_rgba(uint32_t value, GLfloat out[4])
{
    for (unsigned i = 0; i < 4u; ++i)
        out[i] = (GLfloat)((value >> (i * 8u)) & 255u) / 255.0f;
}

bool kr3d_gl_device_create(const kr3d_device_desc *desc,
                           const kr3d_gl_options *options,
                           kr3d_gl_device **out, kr3d_error *e)
{
    GLint major = 0, minor = 0;
    const GLubyte *renderer;
    if (out) *out = NULL;
    if (!desc || !out || desc->struct_size < sizeof *desc ||
        !desc->max_meshes || !desc->max_textures || !desc->max_materials ||
        !desc->max_width || !desc->max_height ||
        (options && options->struct_size < sizeof *options))
        return fail(e, KR3D_ERROR_ARGUMENT, "invalid GL device description");
    glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 3))
        return fail(e, KR3D_ERROR_UNSUPPORTED, "OpenGL 3.3 context required");
    renderer = glGetString(GL_RENDERER);
    if (!renderer) return fail(e, KR3D_ERROR_STATE, "no current OpenGL context");
    kr3d_gl_device *d = calloc(1u, sizeof *d);
    if (!d) return fail(e, KR3D_ERROR_MEMORY, "GL device allocation failed");
    d->max_meshes=desc->max_meshes; d->max_textures=desc->max_textures;
    d->max_materials=desc->max_materials; d->max_width=desc->max_width;
    d->max_height=desc->max_height;
    d->read_color=options && options->readback_color;
    d->read_depth=options && options->readback_depth;
    d->meshes=calloc((size_t)d->max_meshes+1u,sizeof *d->meshes);
    d->textures=calloc((size_t)d->max_textures+1u,sizeof *d->textures);
    d->materials=calloc((size_t)d->max_materials+1u,sizeof *d->materials);
    if (!d->meshes || !d->textures || !d->materials) {
        kr3d_gl_device_destroy(d);
        return fail(e, KR3D_ERROR_MEMORY, "GL resource table allocation failed");
    }
    d->program=make_program(e);
    if (!d->program) { kr3d_gl_device_destroy(d); return false; }
    d->u_mvp=glGetUniformLocation(d->program,"u_mvp");
    d->u_model=glGetUniformLocation(d->program,"u_model");
    d->u_light=glGetUniformLocation(d->program,"u_light");
    d->u_ambient=glGetUniformLocation(d->program,"u_ambient");
    d->u_directional=glGetUniformLocation(d->program,"u_directional");
    d->u_material=glGetUniformLocation(d->program,"u_material");
    d->u_texture=glGetUniformLocation(d->program,"u_texture");
    d->u_use_texture=glGetUniformLocation(d->program,"u_use_texture");
    d->u_unlit=glGetUniformLocation(d->program,"u_unlit");
    d->u_alpha_mode=glGetUniformLocation(d->program,"u_alpha_mode");
    d->u_alpha_cutoff=glGetUniformLocation(d->program,"u_alpha_cutoff");
    (void)snprintf(d->renderer,sizeof d->renderer,"%.95s",(const char *)renderer);
    *out=d; if(e)e->code=KR3D_OK; return gl_ok(e,"GL device initialization failed");
}

void kr3d_gl_device_destroy(kr3d_gl_device *d)
{
    if (!d) return;
    for (uint32_t i=1;i<=d->max_meshes;++i) {
        if(d->meshes[i].ebo)glDeleteBuffers(1,&d->meshes[i].ebo);
        if(d->meshes[i].vbo)glDeleteBuffers(1,&d->meshes[i].vbo);
        if(d->meshes[i].vao)glDeleteVertexArrays(1,&d->meshes[i].vao);
    }
    for(uint32_t i=1;i<=d->max_textures;++i)
        if(d->textures[i].name)glDeleteTextures(1,&d->textures[i].name);
    if(d->program)glDeleteProgram(d->program);
    free(d->color);free(d->depth);free(d->meshes);free(d->textures);
    free(d->materials);free(d);
}

bool kr3d_gl_mesh_create(kr3d_gl_device*d,const kr3d_mesh_desc*s,
                         kr3d_mesh_handle*out,kr3d_error*e)
{
    if(out)*out=0;
    if(!d||!s||!out||s->struct_size<sizeof*s||!s->vertices||!s->indices||
       s->vertex_count<3u||s->index_count<3u||s->index_count%3u||
       s->vertex_count>SIZE_MAX/sizeof(kr3d_vertex)||
       s->index_count>SIZE_MAX/sizeof(uint32_t)||s->index_count>(size_t)INT32_MAX)
        return fail(e,KR3D_ERROR_ARGUMENT,"invalid indexed GL mesh");
    for(size_t i=0;i<s->index_count;++i)if(s->indices[i]>=s->vertex_count)
        return fail(e,KR3D_ERROR_ARGUMENT,"GL mesh index out of range");
    for(size_t i=0;i<s->vertex_count;++i)for(unsigned k=0;k<3u;++k)
        if(!isfinite(s->vertices[i].position[k])||!isfinite(s->vertices[i].normal[k]))
            return fail(e,KR3D_ERROR_ARGUMENT,"non-finite GL mesh vertex");
    uint32_t h=1;while(h<=d->max_meshes&&d->meshes[h].vao)++h;
    if(h>d->max_meshes)return fail(e,KR3D_ERROR_LIMIT,"GL mesh limit reached");
    gl_mesh*m=&d->meshes[h];glGenVertexArrays(1,&m->vao);glBindVertexArray(m->vao);
    glGenBuffers(1,&m->vbo);glBindBuffer(GL_ARRAY_BUFFER,m->vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(s->vertex_count*sizeof *s->vertices),s->vertices,GL_STATIC_DRAW);
    glGenBuffers(1,&m->ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(s->index_count*sizeof *s->indices),s->indices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,(GLsizei)sizeof(kr3d_vertex),(void *)offsetof(kr3d_vertex,position));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,(GLsizei)sizeof(kr3d_vertex),(void *)offsetof(kr3d_vertex,normal));
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,(GLsizei)sizeof(kr3d_vertex),(void *)offsetof(kr3d_vertex,uv));
    glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,(GLsizei)sizeof(kr3d_vertex),(void *)offsetof(kr3d_vertex,color_rgba));
    glBindVertexArray(0);m->count=(GLsizei)s->index_count;
    if(!gl_ok(e,"GL mesh upload failed")){kr3d_gl_mesh_destroy(d,h);return false;}
    *out=h;return true;
}

void kr3d_gl_mesh_destroy(kr3d_gl_device*d,kr3d_mesh_handle h)
{
    if(!d||!h||h>d->max_meshes)return;
    gl_mesh*m=&d->meshes[h];
    if(m->ebo)glDeleteBuffers(1,&m->ebo);
    if(m->vbo)glDeleteBuffers(1,&m->vbo);
    if(m->vao)glDeleteVertexArrays(1,&m->vao);
    memset(m,0,sizeof *m);
}

bool kr3d_gl_texture_create(kr3d_gl_device*d,const kr3d_texture_desc*s,
                            kr3d_texture_handle*out,kr3d_error*e)
{
    if(out)*out=0;
    if(!d||!s||!out||s->struct_size<sizeof*s||!s->rgba||!s->width||!s->height||s->width>d->max_width||s->height>d->max_height||(size_t)s->width>SIZE_MAX/(size_t)s->height/4u)return fail(e,KR3D_ERROR_ARGUMENT,"invalid GL texture");
    uint32_t h=1;while(h<=d->max_textures&&d->textures[h].name)++h;
    if(h>d->max_textures)return fail(e,KR3D_ERROR_LIMIT,"GL texture limit reached");
    GLuint*n=&d->textures[h].name;glGenTextures(1,n);glBindTexture(GL_TEXTURE_2D,*n);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,s->filter==KR3D_FILTER_NEAREST?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,s->filter==KR3D_FILTER_NEAREST?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,s->address_u==KR3D_ADDRESS_REPEAT?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,s->address_v==KR3D_ADDRESS_REPEAT?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)s->width,(GLsizei)s->height,0,GL_RGBA,GL_UNSIGNED_BYTE,s->rgba);
    if(!gl_ok(e,"GL texture upload failed")){kr3d_gl_texture_destroy(d,h);return false;}*out=h;return true;
}

void kr3d_gl_texture_destroy(kr3d_gl_device*d,kr3d_texture_handle h)
{if(d&&h&&h<=d->max_textures&&d->textures[h].name){glDeleteTextures(1,&d->textures[h].name);d->textures[h].name=0;}}

bool kr3d_gl_material_create(kr3d_gl_device*d,const kr3d_material_desc*s,
                             kr3d_material_handle*out,kr3d_error*e)
{
    if(out)*out=0;
    if(!d||!s||!out||s->struct_size<sizeof*s||s->alpha_mode>KR3D_ALPHA_BLEND||!isfinite(s->alpha_cutoff)||s->alpha_cutoff<0||s->alpha_cutoff>1||(s->texture&&(s->texture>d->max_textures||!d->textures[s->texture].name)))return fail(e,KR3D_ERROR_ARGUMENT,"invalid GL material");
    uint32_t h=1;while(h<=d->max_materials&&d->materials[h].live)++h;if(h>d->max_materials)return fail(e,KR3D_ERROR_LIMIT,"GL material limit reached");
    d->materials[h]=(gl_material){s->texture,s->rgba?s->rgba:UINT32_C(0xffffffff),s->flags,s->alpha_mode,s->alpha_cutoff,true};*out=h;return true;
}
void kr3d_gl_material_destroy(kr3d_gl_device*d,kr3d_material_handle h){if(d&&h&&h<=d->max_materials)memset(&d->materials[h],0,sizeof d->materials[h]);}

bool kr3d_gl_frame_begin(kr3d_gl_device*d,const kr3d_frame_desc*s,kr3d_error*e)
{
    GLfloat c[4];if(!d||!s||s->struct_size<sizeof*s||d->frame||!s->width||!s->height||s->width>d->max_width||s->height>d->max_height||!finite_matrix(s->view)||!finite_matrix(s->projection)||!isfinite(s->ambient)||!isfinite(s->directional))return fail(e,KR3D_ERROR_ARGUMENT,"invalid GL frame");
    d->width=s->width;d->height=s->height;d->submitted=d->rasterized=0;unpack_rgba(s->clear_rgba,c);
    glViewport(0,0,(GLsizei)s->width,(GLsizei)s->height);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);glEnable(GL_FRAMEBUFFER_SRGB);glDisable(GL_BLEND);glClearColor(c[0],c[1],c[2],c[3]);glClearDepth(1.0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(d->program);
    glUniform3f(d->u_light,s->light_direction.x,s->light_direction.y,s->light_direction.z);glUniform1f(d->u_ambient,s->ambient);glUniform1f(d->u_directional,s->directional);glUniform1i(d->u_texture,0);
    d->frame=true;
    /* Store matrices in otherwise reusable host buffers after ensuring space. */
    size_t bytes=32u*sizeof(float);float*scratch=(float*)realloc(d->depth,bytes);if(!scratch){d->frame=false;return fail(e,KR3D_ERROR_MEMORY,"GL frame matrix allocation failed");}d->depth=scratch;memcpy(scratch,s->view.m,16u*sizeof(float));memcpy(scratch+16,s->projection.m,16u*sizeof(float));return gl_ok(e,"GL frame begin failed");
}

bool kr3d_gl_draw(kr3d_gl_device*d,const kr3d_draw_desc*s,kr3d_error*e)
{
    if(!d||!s||s->struct_size<sizeof*s||!d->frame||!s->mesh||s->mesh>d->max_meshes||!d->meshes[s->mesh].vao||!s->material||s->material>d->max_materials||!d->materials[s->material].live||!finite_matrix(s->model))return fail(e,KR3D_ERROR_STATE,"invalid GL draw");
    gl_mesh*m=&d->meshes[s->mesh];gl_material*mat=&d->materials[s->material];float*scratch=d->depth;kr3d_mat4 view,projection;memcpy(view.m,scratch,16u*sizeof(float));memcpy(projection.m,scratch+16,16u*sizeof(float));kr3d_mat4 mvp=kr3d_mat4_mul(projection,kr3d_mat4_mul(view,s->model));GLfloat rgba[4];unpack_rgba(mat->rgba,rgba);
    glUniformMatrix4fv(d->u_mvp,1,GL_FALSE,mvp.m);glUniformMatrix4fv(d->u_model,1,GL_FALSE,s->model.m);glUniform4fv(d->u_material,1,rgba);glUniform1i(d->u_use_texture,mat->texture?1:0);glUniform1i(d->u_unlit,(mat->flags&(KR3D_MATERIAL_UNLIT|KR3D_MATERIAL_EMISSIVE))?1:0);glUniform1i(d->u_alpha_mode,(GLint)mat->alpha);glUniform1f(d->u_alpha_cutoff,mat->cutoff);
    if(mat->flags&KR3D_MATERIAL_TWO_SIDED)glDisable(GL_CULL_FACE);else glEnable(GL_CULL_FACE);
    if(mat->alpha==KR3D_ALPHA_BLEND){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}else{glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,mat->texture?d->textures[mat->texture].name:0);glBindVertexArray(m->vao);glDrawElements(GL_TRIANGLES,m->count,GL_UNSIGNED_INT,NULL);glBindVertexArray(0);d->submitted+=(uint32_t)m->count/3u;d->rasterized+=(uint32_t)m->count/3u;return gl_ok(e,"GL draw failed");
}

static void flip_rows(void*data,uint32_t w,uint32_t h,size_t element)
{unsigned char*p=data;size_t row=(size_t)w*element;for(uint32_t y=0;y<h/2u;++y){unsigned char*a=p+(size_t)y*row,*b=p+(size_t)(h-1u-y)*row;for(size_t x=0;x<row;++x){unsigned char value=a[x];a[x]=b[x];b[x]=value;}}}

bool kr3d_gl_frame_end(kr3d_gl_device*d,kr3d_frame_result*r,kr3d_error*e)
{
    if(!d||!d->frame||!r||r->struct_size<sizeof*r)return fail(e,KR3D_ERROR_STATE,"no active GL frame or invalid result");
    size_t n=(size_t)d->width*d->height;
    if(d->read_color){uint32_t*p=realloc(d->color,n*sizeof*p);if(!p)return fail(e,KR3D_ERROR_MEMORY,"GL color readback allocation failed");d->color=p;glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,(GLsizei)d->width,(GLsizei)d->height,GL_RGBA,GL_UNSIGNED_BYTE,d->color);flip_rows(d->color,d->width,d->height,sizeof *d->color);}
    if(d->read_depth){float*p=realloc(d->depth,n*sizeof*p);if(!p)return fail(e,KR3D_ERROR_MEMORY,"GL depth readback allocation failed");d->depth=p;glReadPixels(0,0,(GLsizei)d->width,(GLsizei)d->height,GL_DEPTH_COMPONENT,GL_FLOAT,d->depth);flip_rows(d->depth,d->width,d->height,sizeof *d->depth);}
    glFlush();if(!gl_ok(e,"GL frame end failed"))return false;r->color=d->read_color?d->color:NULL;r->depth=d->read_depth?d->depth:NULL;r->width=d->width;r->height=d->height;r->submitted_triangles=d->submitted;r->rasterized_triangles=d->rasterized;r->shaded_pixels=0;d->frame=false;return true;
}

const char *kr3d_gl_renderer(const kr3d_gl_device*d){return d?d->renderer:NULL;}
