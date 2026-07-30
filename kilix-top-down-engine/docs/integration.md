# Integration contract

## Lifetime

Zero-initialize `ki_td_soft_renderer`, call
`ki_td_soft_renderer_init`, and destroy it exactly once when the game exits.
`ki_td_soft_renderer_resize` is transactional: an invalid size or allocation
failure leaves the active canvas and packed buffer usable. Calling resize with
the active dimensions is a no-op.

The canvas stores `0xAARRGGBB` pixels. Call `ki_td_soft_pack_rgba` after drawing
to obtain stable `R,G,B,A` bytes for a terminal or window presenter. The packed
pointer belongs to the renderer and remains valid until resize or destroy.

## A frame

For a HUD above a logical world, configure the two rectangles independently:

```c
ki_td_fit_spec fit;
ki_td_view view;

ki_td_fit_spec_init(&fit, world_width, world_height,
                    framebuffer_width, framebuffer_height);
fit.fit_bounds = (ki_td_rect){0, hud_height, framebuffer_width,
                              framebuffer_height - hud_height - gutter};
fit.align_bounds = (ki_td_rect){0, hud_height, framebuffer_width,
                                framebuffer_height - hud_height};
fit.scale_policy = KI_TD_SCALE_PIXEL_ART;
fit.minimum_scale = 1.0f;
fit.integer_scale_threshold = 2.0f;
fit.clamp_origin_y = true;
ki_td_view_fit(&view, &fit);
```

Then draw in a fixed order:

1. Clear the renderer.
2. Draw screen-space HUD or background elements.
3. Apply a camera or shake offset with `ki_td_view_set_offset`.
4. Draw the game's terrain and actors through world-space helpers.
5. Reset the offset before drawing screen-space overlays.
6. Pack and present the RGBA buffer.

For an actor pass, assign each `ki_td_sprite_command` a semantic `layer`,
`sort_y`, and deterministic `order`, then pass caller-owned index scratch to
`ki_td_soft_sprite_layers`. The engine performs stable ordering without an
allocation. The game still owns the meanings of layers and the decision about
which actors enter the pass.

## Determinism and threading

View fitting, coordinate conversion, shake, primitives, and sprite drawing are
deterministic for the same inputs and linked `soft-raster` version. Shake is a
hash of frame, magnitude, and salt; it never reads game RNG. Do not compile
byte-golden builds with fast-math transformations.

A renderer and its view are caller-owned and not internally synchronized. One
thread may own a renderer at a time. Separate renderers may be used on separate
threads subject to the caller's allocator and presentation rules.

## RGBA sprites

`ki_td_rgba8` references caller-owned bytes. Its pixels are straight-alpha
RGBA, not premultiplied. The data must remain alive for the duration of a draw
call. A stride larger than `width * 4` is supported. Invalid descriptors and
non-positive resized dimensions draw nothing.

`ki_td_rgba8_subimage` creates a borrowed descriptor with the parent stride;
the source pixels must outlive every draw. `ki_td_soft_tile_batch` divides an
atlas into an exact grid and draws a row-major cell array. `ki_td_nine_slice`
preserves corner sizes and nearest-stretches its edges and center; destination
dimensions smaller than the combined borders draw nothing.

The logical sprite functions draw each source pixel as one logical unit. The
pixel-art fast path writes integer-sized blocks when the view scale is within
0.01 of the nearest positive integer; otherwise it uses the general logical
path. All paths clip through `soft-raster`.

## Shared library linkage

`libkilix-top-down-soft.a` references but does not contain `soft-raster`.
When using `kilix-game-kit`, link in this order:

```text
game objects
libkilix-top-down-soft.a
libkilix-top-down-core.a
libkilix-game-kit.a
-lz -lpthread -lm
```

An integration test should use `nm` to require one `sr_canvas_init`
definition in the executable. This catches accidental duplicate rasterizer
archives.
