# kilix-render-3d

A dependency-free C11 scalar 3D renderer for Kilix games. It provides shared
vector, quaternion, matrix and frustum math; checked indexed mesh, RGBA texture
and material resources; homogeneous six-plane clipping; a reciprocal-W depth
buffer; perspective-correct color, normal and UV interpolation; nearest and
bilinear sampling; clamp/repeat addressing; alpha test/blend; and ambient plus
directional lighting.

The coordinate contract is right-handed, matrices are column-major and act on
column vectors, the camera looks down negative Z, and clip depth is OpenGL's
`[-W,+W]`. Texture `(0,0)` addresses the first supplied texel. RGBA values put
red in the least-significant byte.

```sh
make test
make sanitize
```

Include `include/kr3d.h`, initialize every extensible descriptor's
`struct_size`, create resources, then call `frame_begin`, one or more `draw`
calls, and `frame_end`. A frame may use caller-owned color/depth buffers or let
the device retain reusable targets. All calls are render-thread confined.
