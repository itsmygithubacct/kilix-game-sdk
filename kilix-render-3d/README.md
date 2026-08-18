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

## Cooked mesh transport

`kr3d_mesh_file_encode` and `kr3d_mesh_file_decode` implement the portable
`K3DMESH1` format used between offline content cookers and games. Its canonical
little-endian payload contains the fixed position/normal/UV/color vertex layout
and 32-bit triangle indices. The header records exact bounds, byte counts,
material-slot count, and a SHA-256 payload digest.

Decoding is transactional and requires caller-selected limits for file bytes,
vertices, indices, and material slots. It rejects unknown flags or layout,
truncation and trailing data, non-finite or pathological vertices, dishonest
bounds, out-of-range indices, and digest mismatches. Decoded arrays are released
with `kr3d_mesh_data_release`; encoded bytes with
`kr3d_mesh_file_bytes_free`.

## Optional OpenGL backend

`kr3d_gl.h` implements the same resource and frame descriptors on OpenGL 3.3.
It consumes a caller-owned current context (including an EGL context), renders
to the current framebuffer, and does not create a window, switch contexts, or
swap buffers. The scalar library and consumer make fragment retain no EGL or
OpenGL dependency.

The optional backend is disabled in default builds. When the `egl` and `gl`
pkg-config packages are installed, build and exercise it through Mesa with:

```sh
make opengl
make test-opengl
make sanitize-opengl
# Or include it in the normal `all` target:
make ENABLE_OPENGL=1
```

This produces `build/libkilix-render-3d-gl.a`. Consumers link it alongside
`libkilix-render-3d.a` and the flags from `pkg-config --libs egl gl`. Creation
requires an already-current OpenGL 3.3 context. Optional color/depth readback
supports conformance tests and terminal bridges; without it, `frame_end`
returns null target pointers and presentation remains the context owner's job.
