#ifndef KR3D_CONFORMANCE_SCENE_H
#define KR3D_CONFORMANCE_SCENE_H

#include "kr3d.h"

#define CONF_CLEAR UINT32_C(0xff000000)
#define CONF_RED UINT32_C(0xff0000ff)
#define CONF_GREEN UINT32_C(0xff00ff00)
#define CONF_BLUE UINT32_C(0xffff0000)
#define CONF_YELLOW UINT32_C(0xff00ffff)

typedef struct {
    float x, y;
    uint32_t expected;
    const char *name;
} conf_probe;

/* One backend-neutral scene description is consumed by both renderer tests.
   Coordinates are NDC because view/projection are identity. */
static const kr3d_vertex conf_quad_vertices[] = {
    {{-.90f,-.90f,0},{0,0,1},{0,0},UINT32_MAX},
    {{-.10f,-.90f,0},{0,0,1},{1,0},UINT32_MAX},
    {{-.10f,-.10f,0},{0,0,1},{1,1},UINT32_MAX},
    {{-.90f,-.10f,0},{0,0,1},{0,1},UINT32_MAX}
};
static const uint32_t conf_quad_indices[] = {0,1,2,0,2,3};
static const kr3d_vertex conf_triangle_vertices[] = {
    {{-.18f,-.18f,0},{0,0,1},{0,0},UINT32_MAX},
    {{ .18f,-.18f,0},{0,0,1},{0,0},UINT32_MAX},
    {{  0.f, .18f,0},{0,0,1},{0,0},UINT32_MAX}
};
static const uint32_t conf_front_indices[] = {0,1,2};
static const uint32_t conf_back_indices[] = {2,1,0};
static const kr3d_vertex conf_clip_vertices[] = {
    {{-1.30f,.20f,0},{0,0,1},{0,0},UINT32_MAX},
    {{ -.55f,.20f,0},{0,0,1},{0,0},UINT32_MAX},
    {{ -.55f,.80f,0},{0,0,1},{0,0},UINT32_MAX}
};
static const uint32_t conf_clip_indices[] = {0,1,2};
static const uint32_t conf_texels[] = {
    CONF_RED, CONF_GREEN,
    CONF_BLUE, CONF_YELLOW
};
static const conf_probe conf_probes[] = {
    {-.72f,-.72f,CONF_RED,"uv top-left"},
    {-.28f,-.72f,CONF_GREEN,"uv top-right"},
    {-.72f,-.28f,CONF_BLUE,"uv bottom-left"},
    {-.28f,-.28f,CONF_YELLOW,"uv bottom-right"},
    { .55f,-.55f,CONF_GREEN,"depth ordering"},
    { .55f, .55f,CONF_RED,"model transform/material"},
    {-.72f, .42f,CONF_BLUE,"six-plane clipping"},
    { .00f, .00f,CONF_CLEAR,"back-face winding/visibility"},
    { .90f, .90f,CONF_CLEAR,"clear visibility"}
};

static size_t conf_pixel(float ndc, uint32_t extent)
{
    float unit = ndc * .5f + .5f;
    return (size_t)(unit * (float)extent);
}

static uint64_t conf_hash(const uint32_t *pixels, size_t count)
{
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = pixels[i];
        for (unsigned b = 0; b < 4; ++b) {
            h ^= (uint8_t)(v >> (b * 8u));
            h *= UINT64_C(1099511628211);
        }
    }
    return h;
}

#endif
