/*
 * test_soft.c — coverage for libkilix-tactics-soft.
 *
 * Before this review no test binary linked the soft adapter at all, so none
 * of its paths had ever executed. These cases drive it against a real canvas:
 * the ordinary draw path and its pixel results, the resolver contract, and
 * the boundary conversions between engine data and the rasterizer's int
 * coordinates and float coverage.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kilix_tactics_soft.h"

static unsigned long g_checks;
static unsigned long g_failures;

static void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        printf("    FAIL %s\n", what);
    }
}

static void section(const char *name)
{
    printf("%s\n", name);
}

static bool canvas_start(sr_canvas *canvas, int w, int h, uint32_t fill)
{
    if (!sr_canvas_init(canvas, w, h)) {
        return false;
    }
    sr_clear(canvas, fill);
    return true;
}

static uint32_t pixel_at(const sr_canvas *canvas, int x, int y)
{
    return canvas->px[(size_t)y * (size_t)canvas->w + (size_t)x];
}

static unsigned long count_non(const sr_canvas *canvas, uint32_t colour)
{
    unsigned long total = 0u;
    int y;
    int x;

    for (y = 0; y < canvas->h; ++y) {
        for (x = 0; x < canvas->w; ++x) {
            if (pixel_at(canvas, x, y) != colour) {
                ++total;
            }
        }
    }
    return total;
}

/* ------------------------------------------------------------- resolvers */

typedef struct resolve_plan {
    const sr_canvas *canvas;
    int32_t offset_x;
    int32_t offset_y;
    float alpha;
    bool tint_enabled;
    uint32_t tint_rgb;
    bool refuse;
    unsigned long calls;
    uint32_t last_handle;
} resolve_plan;

static bool resolve_plan_fn(void *user, const kt_draw_item *item,
                            kt_soft_sprite *out)
{
    resolve_plan *plan = (resolve_plan *)user;

    ++plan->calls;
    plan->last_handle = item->handle;
    if (plan->refuse) {
        return false;
    }
    out->canvas = plan->canvas;
    out->offset_x = plan->offset_x;
    out->offset_y = plan->offset_y;
    out->alpha = plan->alpha;
    out->tint_enabled = plan->tint_enabled;
    out->tint_rgb = plan->tint_rgb;
    return true;
}

/*
 * Two-sprite resolver for the ordering case: handle 1 resolves to the plan's
 * primary canvas, anything else to its alternate.
 */
static const sr_canvas *g_alternate;

static bool resolve_by_handle(void *user, const kt_draw_item *item,
                              kt_soft_sprite *out)
{
    resolve_plan *plan = (resolve_plan *)user;

    ++plan->calls;
    plan->last_handle = item->handle;
    out->canvas = item->handle == 1u ? plan->canvas : g_alternate;
    out->offset_x = 0;
    out->offset_y = 0;
    out->alpha = 1.0f;
    out->tint_enabled = false;
    out->tint_rgb = 0u;
    return true;
}

static void plan_init(resolve_plan *plan, const sr_canvas *sprite)
{
    memset(plan, 0, sizeof(*plan));
    plan->canvas = sprite;
    plan->alpha = 1.0f;
}

/* Builds a one-item sorted queue anchored at (x, y). */
static void one_item_queue(kt_draw_queue *queue, kt_draw_item *storage,
                           int32_t x, int32_t y)
{
    kt_draw_item *pushed = NULL;

    (void)kt_draw_queue_init(queue, storage, 1u);
    (void)kt_draw_submit_keyed(queue, 0, 0u, 1u, &pushed);
    pushed->at.x = x;
    pushed->at.y = y;
    (void)kt_draw_queue_sort(queue);
}

/* ------------------------------------------------------------ draw queue */

static void test_draw_arguments(void)
{
    sr_canvas dst;
    sr_canvas sprite;
    kt_draw_item storage[1];
    kt_draw_queue queue;
    resolve_plan plan;
    size_t drawn = 99u;

    section("kt_soft_draw_queue argument and state contract");
    if (!canvas_start(&dst, 32, 32, 0x000000u) ||
        !canvas_start(&sprite, 4, 4, 0xff00ffu)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }
    plan_init(&plan, &sprite);
    one_item_queue(&queue, storage, 0, 0);

    check(kt_soft_draw_queue(NULL, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_ERR_ARGUMENT, "a null canvas is refused");
    check(kt_soft_draw_queue(&dst, NULL, resolve_plan_fn, &plan, &drawn) ==
              KT_ERR_ARGUMENT, "a null queue is refused");
    check(kt_soft_draw_queue(&dst, &queue, NULL, &plan, &drawn) ==
              KT_ERR_ARGUMENT, "a null resolver is refused");

    queue.sorted = false;
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_ERR_STATE, "an unsorted queue is refused");
    queue.sorted = true;

    queue.count = 4u;   /* past the single item actually bound */
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_ERR_STATE, "a count past the bound storage is refused");
    queue.count = 1u;

    check(count_non(&dst, 0xff000000u) == 0u,
          "no refused call painted anything");

    sr_canvas_free(&sprite);
    sr_canvas_free(&dst);
}

static void test_draw_paints(void)
{
    sr_canvas dst;
    sr_canvas sprite;
    kt_draw_item storage[1];
    kt_draw_queue queue;
    resolve_plan plan;
    size_t drawn = 0u;

    section("the ordinary draw path paints where the item says");
    if (!canvas_start(&dst, 32, 32, 0x000000u) ||
        !canvas_start(&sprite, 4, 4, 0xff00ffu)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }
    plan_init(&plan, &sprite);
    one_item_queue(&queue, storage, 8, 6);

    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_OK, "the draw succeeds");
    check(drawn == 1u, "one item is reported drawn");
    check(plan.calls == 1u, "the resolver saw the item once");
    check(plan.last_handle == 1u, "and received its handle");
    check(pixel_at(&dst, 8, 6) == 0xffff00ffu, "the sprite landed at (8, 6)");
    check(pixel_at(&dst, 11, 9) == 0xffff00ffu, "and covers its full extent");
    check(pixel_at(&dst, 12, 10) == 0xff000000u, "but no further");
    check(count_non(&dst, 0xff000000u) == 16u, "exactly 4x4 pixels changed");

    section("the anchor offset moves the sprite");
    sr_clear(&dst, 0x000000u);
    plan.offset_x = 2;
    plan.offset_y = 3;
    drawn = 0u;
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_OK, "the offset draw succeeds");
    check(pixel_at(&dst, 10, 9) == 0xffff00ffu, "the sprite moved by the offset");
    check(drawn == 1u, "and one item is reported drawn");

    section("a resolver may refuse an item");
    sr_clear(&dst, 0x000000u);
    plan.refuse = true;
    drawn = 99u;
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_OK, "a refused item is not an error");
    check(drawn == 0u, "nothing is reported drawn");
    check(count_non(&dst, 0xff000000u) == 0u, "and nothing is painted");

    sr_canvas_free(&sprite);
    sr_canvas_free(&dst);
}

static void test_draw_coverage(void)
{
    sr_canvas dst;
    sr_canvas sprite;
    kt_draw_item storage[1];
    kt_draw_queue queue;
    resolve_plan plan;
    size_t drawn = 0u;

    section("coverage outside (0, 1] is normalised, never passed through");
    if (!canvas_start(&dst, 32, 32, 0x000000u) ||
        !canvas_start(&sprite, 4, 4, 0xff00ffu)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }
    plan_init(&plan, &sprite);
    one_item_queue(&queue, storage, 4, 4);

    plan.alpha = (float)NAN;
    drawn = 99u;
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_OK, "a non-finite alpha is not an error");
    check(drawn == 0u, "a non-finite alpha draws nothing");
    check(count_non(&dst, 0xff000000u) == 0u, "and paints nothing");

    plan.alpha = -1.0f;
    drawn = 99u;
    (void)kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn);
    check(drawn == 0u, "a negative alpha draws nothing");
    check(count_non(&dst, 0xff000000u) == 0u, "and paints nothing");

    plan.alpha = 0.0f;
    drawn = 99u;
    (void)kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn);
    check(drawn == 0u, "a zero alpha draws nothing");

    /* Above one is the opaque path both games already rely on. */
    sr_clear(&dst, 0x000000u);
    plan.alpha = 4.0f;
    drawn = 0u;
    (void)kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn);
    check(drawn == 1u, "an alpha above one still draws");
    check(pixel_at(&dst, 4, 4) == 0xffff00ffu, "opaquely");

    section("an anchor that cannot address a pixel is skipped");
    sr_clear(&dst, 0x000000u);
    one_item_queue(&queue, storage, INT32_MAX, INT32_MAX);
    plan.alpha = 1.0f;
    plan.offset_x = 64;
    plan.offset_y = 64;
    drawn = 99u;
    check(kt_soft_draw_queue(&dst, &queue, resolve_plan_fn, &plan, &drawn) ==
              KT_OK, "an unrepresentable anchor is not an error");
    check(drawn == 0u, "and draws nothing");
    check(count_non(&dst, 0xff000000u) == 0u, "and paints nothing");

    sr_canvas_free(&sprite);
    sr_canvas_free(&dst);
}

static void test_draw_order(void)
{
    sr_canvas dst;
    sr_canvas first;
    sr_canvas second;
    kt_draw_item storage[2];
    kt_draw_queue queue;
    size_t drawn = 0u;
    resolve_plan plan;

    section("items paint in queue order, so the later one wins");
    if (!canvas_start(&dst, 16, 16, 0x000000u) ||
        !canvas_start(&first, 8, 8, 0x112233u) ||
        !canvas_start(&second, 8, 8, 0x445566u)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }
    plan_init(&plan, &first);
    g_alternate = &second;

    {
        kt_draw_item *pushed = NULL;

        (void)kt_draw_queue_init(&queue, storage, 2u);
        /* Handle 1 sorts last, so it paints over handle 2. Both land on the
         * same pixels. */
        (void)kt_draw_submit_keyed(&queue, 10, 0u, 1u, &pushed);
        pushed->at.x = 0;
        pushed->at.y = 0;
        (void)kt_draw_submit_keyed(&queue, 1, 0u, 2u, &pushed);
        pushed->at.x = 0;
        pushed->at.y = 0;
        (void)kt_draw_queue_sort(&queue);
    }
    check(queue.items[0].handle == 2u, "the nearer depth sorts first");

    (void)kt_soft_draw_queue(&dst, &queue, resolve_by_handle, &plan, &drawn);
    check(drawn == 2u, "both items drew");
    check(plan.calls == 2u, "and the resolver saw both");
    check(pixel_at(&dst, 0, 0) == 0xff112233u,
          "the later item in painter order is on top");

    sr_canvas_free(&second);
    sr_canvas_free(&first);
    sr_canvas_free(&dst);
}

/* --------------------------------------------------------------- overlay */

static void test_diamond_paints(void)
{
    sr_canvas dst;

    section("the floor diamond paints inside its tile box");
    if (!canvas_start(&dst, 64, 64, 0x000000u)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }
    kt_soft_diamond_fill(&dst, 8, 8, 32, 16, 0x00ff00u, 1.0f);
    check(count_non(&dst, 0xff000000u) > 0u, "the fill painted");
    check(pixel_at(&dst, 24, 16) != 0xff000000u, "the centre is covered");
    check(pixel_at(&dst, 8, 8) == 0xff000000u, "the box corner is not");
    check(pixel_at(&dst, 0, 0) == 0xff000000u, "and nothing outside the box");

    sr_clear(&dst, 0x000000u);
    kt_soft_diamond_outline(&dst, 8, 8, 32, 16, 1.0f, 0x00ff00u, 1.0f, 0, 0);
    check(count_non(&dst, 0xff000000u) > 0u, "the outline painted");
    check(pixel_at(&dst, 24, 16) == 0xff000000u, "and left the centre empty");

    sr_canvas_free(&dst);
}

static void test_diamond_boundaries(void)
{
    sr_canvas dst;

    section("overlay geometry that cannot reach the canvas is skipped");
    if (!canvas_start(&dst, 64, 64, 0x000000u)) {
        printf("    FAIL canvas allocation\n");
        ++g_failures;
        return;
    }

    /* Each of these previously reached the rasterizer with a coordinate it
     * could not convert; one ran without terminating. */
    kt_soft_diamond_outline(&dst, INT32_MAX, INT32_MAX, 2147483646, 2147483646,
                            1.0f, 0xffffffu, 1.0f, 0, 0);
    kt_soft_diamond_fill(&dst, INT32_MIN, INT32_MIN, 2147483646, 2147483646,
                         0xffffffu, 1.0f);
    kt_soft_diamond_outline(&dst, INT32_MIN, 0, 32, 16, 1.0f, 0xffffffu, 1.0f,
                            0, 0);
    kt_soft_diamond_fill(&dst, 0, INT32_MAX, 32, 16, 0xffffffu, 1.0f);
    check(count_non(&dst, 0xff000000u) == 0u,
          "no off-canvas overlay painted anything");

    section("overlay coverage and stroke are validated");
    kt_soft_diamond_outline(&dst, 8, 8, 32, 16, 1.0f, 0xffffffu, (float)NAN, 0,
                            0);
    kt_soft_diamond_fill(&dst, 8, 8, 32, 16, 0xffffffu, (float)NAN);
    kt_soft_diamond_outline(&dst, 8, 8, 32, 16, (float)NAN, 0xffffffu, 1.0f, 0,
                            0);
    kt_soft_diamond_outline(&dst, 8, 8, 32, 16, 1.0f, 0xffffffu, -1.0f, 0, 0);
    kt_soft_diamond_fill(&dst, 8, 8, 32, 16, 0xffffffu, 0.0f);
    kt_soft_diamond_outline(&dst, 8, 8, 32, 16, 1.0f, 0xffffffu, 1.0f, -1, -1);
    check(count_non(&dst, 0xff000000u) == 0u,
          "non-finite or non-positive coverage paints nothing");

    section("degenerate tile extents are refused");
    kt_soft_diamond_fill(&dst, 8, 8, 0, 16, 0xffffffu, 1.0f);
    kt_soft_diamond_fill(&dst, 8, 8, 32, 0, 0xffffffu, 1.0f);
    kt_soft_diamond_fill(&dst, 8, 8, -32, -16, 0xffffffu, 1.0f);
    kt_soft_diamond_outline(&dst, 8, 8, -1, -1, 1.0f, 0xffffffu, 1.0f, 0, 0);
    kt_soft_diamond_fill(NULL, 8, 8, 32, 16, 0xffffffu, 1.0f);
    kt_soft_diamond_outline(NULL, 8, 8, 32, 16, 1.0f, 0xffffffu, 1.0f, 0, 0);
    check(count_non(&dst, 0xff000000u) == 0u,
          "a degenerate or absent target paints nothing");

    section("a diamond larger than the canvas still paints the visible part");
    kt_soft_diamond_fill(&dst, -64, -32, 256, 128, 0x0000ffu, 1.0f);
    check(count_non(&dst, 0xff000000u) > 0u,
          "a straddling diamond is not skipped");

    sr_canvas_free(&dst);
}

int main(void)
{
    test_draw_arguments();
    test_draw_paints();
    test_draw_coverage();
    test_draw_order();
    test_diamond_paints();
    test_diamond_boundaries();

    printf("\n%lu checks, %lu failures\n", g_checks, g_failures);
    return g_failures == 0u ? 0 : 1;
}
