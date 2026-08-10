#include "kilix_story.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                     \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                      __FILE__, __LINE__, #condition);                      \
        return false;                                                       \
    }                                                                       \
} while (false)

static bool test_state_binding_and_access(void)
{
    uint64_t flags[2] = {0};
    int32_t counters[3] = {4, -8, 12};
    kilix_story_state state = {flags, 2u, counters, 3u};
    kilix_story_state unchanged = state;
    kilix_story_state invalid;
    bool flag = true;
    int32_t counter = 99;
    _Alignas(uint64_t) unsigned char misaligned[32] = {0};
    union overlapping_buffers {
        uint64_t flags[1];
        int32_t counters[2];
    } overlap = {{0}};
    union overlapping_state {
        kilix_story_state state;
        uint64_t words[4];
    } state_overlap;

    CHECK(kilix_story_state_bind(NULL, flags, 2u, counters, 3u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_state_bind(&state, NULL, 1u, counters, 3u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(memcmp(&state, &unchanged, sizeof state) == 0);
    CHECK(kilix_story_state_bind(&state, flags, 2u, NULL, 1u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(memcmp(&state, &unchanged, sizeof state) == 0);
    CHECK(kilix_story_state_bind(
              &state, flags, SIZE_MAX / sizeof(*flags) + 1u,
              counters, 3u) == KILIX_STORY_INVALID_ARGUMENT);
    CHECK(memcmp(&state, &unchanged, sizeof state) == 0);
    CHECK(kilix_story_state_bind(
              &state, (uint64_t *)(void *)(misaligned + 1u), 1u,
              counters, 3u) == KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_state_bind(
              &state, overlap.flags, 1u, overlap.counters, 2u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_state_bind(
              &state_overlap.state, state_overlap.words, 4u, NULL, 0u) ==
          KILIX_STORY_INVALID_ARGUMENT);

    CHECK(kilix_story_state_bind(&state, flags, 2u, counters, 3u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_flag_get(&state, 0u, NULL) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_flag_get(&state, 128u, &flag) ==
          KILIX_STORY_OUT_OF_RANGE && flag);
    CHECK(kilix_story_flag_set(&state, 65u, true) == KILIX_STORY_OK);
    CHECK(kilix_story_flag_get(&state, 65u, &flag) ==
          KILIX_STORY_OK && flag);
    CHECK(kilix_story_flag_set(&state, 65u, false) == KILIX_STORY_OK);
    CHECK(kilix_story_flag_get(&state, 65u, &flag) ==
          KILIX_STORY_OK && !flag);
    CHECK(kilix_story_flag_set(&state, UINT32_MAX, true) ==
          KILIX_STORY_OUT_OF_RANGE);

    CHECK(kilix_story_counter_get(&state, 0u, NULL) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_counter_get(&state, 3u, &counter) ==
          KILIX_STORY_OUT_OF_RANGE && counter == 99);
    CHECK(kilix_story_counter_set(&state, 1u, INT32_MIN) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_counter_get(&state, 1u, &counter) ==
          KILIX_STORY_OK && counter == INT32_MIN);
    CHECK(kilix_story_counter_set(&state, 3u, 0) ==
          KILIX_STORY_OUT_OF_RANGE);

    invalid = (kilix_story_state){
        flags, 2u, (int32_t *)(void *)flags, 4u
    };
    CHECK(kilix_story_flag_get(&invalid, 0u, &flag) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_counter_set(&invalid, 0u, 1) ==
          KILIX_STORY_INVALID_ARGUMENT);

    CHECK(kilix_story_state_bind(&state, NULL, 0u, NULL, 0u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_flag_get(&state, 0u, &flag) ==
          KILIX_STORY_OUT_OF_RANGE);
    CHECK(kilix_story_counter_get(&state, 0u, &counter) ==
          KILIX_STORY_OUT_OF_RANGE);
    return true;
}

static bool test_conditions(void)
{
    uint64_t flags[2] = {UINT64_C(1) << 1, UINT64_C(1) << 1};
    int32_t counters[3] = {-2, 5, 10};
    kilix_story_state state;
    bool matches;
    static const kilix_story_condition all_true[] = {
        {KILIX_STORY_CONDITION_ALWAYS, 999u, 0, 0},
        {KILIX_STORY_CONDITION_FLAG_SET, 1u, 0, 0},
        {KILIX_STORY_CONDITION_FLAG_SET, 65u, 0, 0},
        {KILIX_STORY_CONDITION_FLAG_CLEAR, 2u, 0, 0},
        {KILIX_STORY_CONDITION_COUNTER_EQUAL, 1u, 5, 0},
        {KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL, 0u, 0, 0},
        {KILIX_STORY_CONDITION_COUNTER_LESS, 0u, 0, 0},
        {KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL, 1u, 5, 0},
        {KILIX_STORY_CONDITION_COUNTER_GREATER, 2u, 5, 0},
        {KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL, 1u, 5, 0},
        {KILIX_STORY_CONDITION_COUNTER_RANGE, 1u, 0, 5}
    };
    static const kilix_story_condition all_false[] = {
        {KILIX_STORY_CONDITION_FLAG_CLEAR, 1u, 0, 0},
        {KILIX_STORY_CONDITION_COUNTER_EQUAL, 2u, 9, 0}
    };
    static const kilix_story_condition false_then_invalid[] = {
        {KILIX_STORY_CONDITION_COUNTER_EQUAL, 0u, 0, 0},
        {(kilix_story_condition_op)999, 0u, 0, 0}
    };
    static const kilix_story_condition true_then_invalid[] = {
        {KILIX_STORY_CONDITION_ALWAYS, 0u, 0, 0},
        {(kilix_story_condition_op)999, 0u, 0, 0}
    };
    static const kilix_story_condition invalid_range[] = {
        {KILIX_STORY_CONDITION_COUNTER_RANGE, 0u, 5, 4}
    };
    static const kilix_story_condition bad_flag[] = {
        {KILIX_STORY_CONDITION_FLAG_SET, 128u, 0, 0}
    };
    static const kilix_story_condition bad_counter[] = {
        {KILIX_STORY_CONDITION_COUNTER_EQUAL, 3u, 0, 0}
    };

    CHECK(kilix_story_state_bind(&state, flags, 2u, counters, 3u) ==
          KILIX_STORY_OK);
    matches = false;
    CHECK(kilix_story_conditions_all(
              &state, all_true,
              sizeof all_true / sizeof all_true[0], &matches) ==
          KILIX_STORY_OK && matches);
    matches = true;
    CHECK(kilix_story_conditions_all(
              &state, all_false,
              sizeof all_false / sizeof all_false[0], &matches) ==
          KILIX_STORY_OK && !matches);
    matches = false;
    CHECK(kilix_story_conditions_any(
              &state, all_false,
              sizeof all_false / sizeof all_false[0], &matches) ==
          KILIX_STORY_OK && !matches);
    matches = false;
    CHECK(kilix_story_conditions_any(
              &state, all_true,
              sizeof all_true / sizeof all_true[0], &matches) ==
          KILIX_STORY_OK && matches);

    matches = false;
    CHECK(kilix_story_conditions_all(&state, NULL, 0u, &matches) ==
          KILIX_STORY_OK && matches);
    matches = true;
    CHECK(kilix_story_conditions_any(&state, NULL, 0u, &matches) ==
          KILIX_STORY_OK && !matches);

    matches = true;
    CHECK(kilix_story_conditions_all(
              &state, false_then_invalid, 2u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT && matches);
    matches = false;
    CHECK(kilix_story_conditions_any(
              &state, true_then_invalid, 2u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT && !matches);
    matches = true;
    CHECK(kilix_story_conditions_all(
              &state, invalid_range, 1u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT && matches);
    CHECK(kilix_story_conditions_all(
              &state, bad_flag, 1u, &matches) ==
          KILIX_STORY_OUT_OF_RANGE && matches);
    CHECK(kilix_story_conditions_all(
              &state, bad_counter, 1u, &matches) ==
          KILIX_STORY_OUT_OF_RANGE && matches);
    CHECK(kilix_story_conditions_all(
              &state, all_true, 1u, NULL) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_conditions_all(
              NULL, all_true, 1u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_conditions_all(
              &state, NULL, 1u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_conditions_all(
              &state, all_true,
              SIZE_MAX / sizeof(*all_true) + 1u, &matches) ==
          KILIX_STORY_INVALID_ARGUMENT);
    return true;
}

static kilix_story_result reference_apply(
    uint64_t flag_words[2], int32_t counters[4],
    const kilix_story_action *actions, size_t action_count)
{
    uint64_t projected_flags[2] = {flag_words[0], flag_words[1]};
    int32_t projected_counters[4] = {
        counters[0], counters[1], counters[2], counters[3]
    };
    size_t index;
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        if (action->op == KILIX_STORY_ACTION_SET_FLAG ||
            action->op == KILIX_STORY_ACTION_CLEAR_FLAG ||
            action->op == KILIX_STORY_ACTION_TOGGLE_FLAG) {
            size_t word =
                (size_t)(action->index / UINT32_C(64));
            uint64_t mask =
                UINT64_C(1) << (action->index % UINT32_C(64));
            if (word >= 2u) return KILIX_STORY_OUT_OF_RANGE;
            if (action->op == KILIX_STORY_ACTION_SET_FLAG)
                projected_flags[word] |= mask;
            else if (action->op == KILIX_STORY_ACTION_CLEAR_FLAG)
                projected_flags[word] &= ~mask;
            else
                projected_flags[word] ^= mask;
        } else if (action->op == KILIX_STORY_ACTION_SET_COUNTER ||
                   action->op == KILIX_STORY_ACTION_ADD_COUNTER) {
            int64_t projected;
            if ((size_t)action->index >= 4u)
                return KILIX_STORY_OUT_OF_RANGE;
            projected =
                action->op == KILIX_STORY_ACTION_SET_COUNTER ?
                (int64_t)action->value :
                (int64_t)projected_counters[action->index] +
                (int64_t)action->value;
            if (projected < INT32_MIN || projected > INT32_MAX)
                return KILIX_STORY_OVERFLOW;
            projected_counters[action->index] = (int32_t)projected;
        } else {
            return KILIX_STORY_INVALID_ARGUMENT;
        }
    }
    (void)memcpy(flag_words, projected_flags, sizeof projected_flags);
    (void)memcpy(counters, projected_counters, sizeof projected_counters);
    return KILIX_STORY_OK;
}

static uint32_t random_value(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool test_actions_and_model(void)
{
    uint64_t flags[2] = {0, UINT64_C(1) << 1};
    int32_t counters[4] = {4, 8, INT32_MAX, INT32_MIN};
    kilix_story_state state;
    static const kilix_story_action success[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 1u, 0},
        {KILIX_STORY_ACTION_TOGGLE_FLAG, 65u, 0},
        {KILIX_STORY_ACTION_CLEAR_FLAG, 2u, 0},
        {KILIX_STORY_ACTION_SET_COUNTER, 0u, 10},
        {KILIX_STORY_ACTION_ADD_COUNTER, 0u, -3},
        {KILIX_STORY_ACTION_ADD_COUNTER, 1u, 7}
    };
    static const kilix_story_action invalid[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 3u, 0},
        {(kilix_story_action_op)999, 0u, 0}
    };
    static const kilix_story_action bad_flag[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 128u, 0}
    };
    static const kilix_story_action bad_counter[] = {
        {KILIX_STORY_ACTION_SET_COUNTER, 4u, 0}
    };
    static const kilix_story_action positive_overflow[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 3u, 0},
        {KILIX_STORY_ACTION_ADD_COUNTER, 2u, 1}
    };
    static const kilix_story_action negative_overflow[] = {
        {KILIX_STORY_ACTION_CLEAR_FLAG, 65u, 0},
        {KILIX_STORY_ACTION_ADD_COUNTER, 3u, -1}
    };
    static const kilix_story_action intermediate_overflow[] = {
        {KILIX_STORY_ACTION_ADD_COUNTER, 2u, 1},
        {KILIX_STORY_ACTION_SET_COUNTER, 2u, 0}
    };
    int32_t many_counters[66] = {0};
    kilix_story_action many_actions[66];
    kilix_story_action fallback_overflow[66];
    kilix_story_action fallback_invalid[66];
    uint64_t before_flags[2];
    int32_t before_counters[4];
    uint32_t seed = UINT32_C(0x13579bdf);
    static const int32_t values[] = {
        0, 1, -1, 17, -23, INT32_MAX, INT32_MIN
    };
    size_t index;
    size_t iteration;

    CHECK(kilix_story_state_bind(&state, flags, 2u, counters, 4u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_apply_actions(
              &state, success, sizeof success / sizeof success[0]) ==
          KILIX_STORY_OK);
    CHECK((flags[0] & (UINT64_C(1) << 1)) != 0u);
    CHECK((flags[1] & (UINT64_C(1) << 1)) == 0u);
    CHECK(counters[0] == 7 && counters[1] == 15);
    CHECK(kilix_story_apply_actions(&state, NULL, 0u) ==
          KILIX_STORY_OK);

    (void)memcpy(before_flags, flags, sizeof flags);
    (void)memcpy(before_counters, counters, sizeof counters);
    CHECK(kilix_story_apply_actions(&state, invalid, 2u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(memcmp(flags, before_flags, sizeof flags) == 0);
    CHECK(memcmp(counters, before_counters, sizeof counters) == 0);
    CHECK(kilix_story_apply_actions(&state, bad_flag, 1u) ==
          KILIX_STORY_OUT_OF_RANGE);
    CHECK(kilix_story_apply_actions(&state, bad_counter, 1u) ==
          KILIX_STORY_OUT_OF_RANGE);

    flags[0] = 0u;
    flags[1] = UINT64_C(1) << 1;
    counters[2] = INT32_MAX;
    counters[3] = INT32_MIN;
    CHECK(kilix_story_apply_actions(
              &state, positive_overflow, 2u) ==
          KILIX_STORY_OVERFLOW);
    CHECK(flags[0] == 0u && counters[2] == INT32_MAX);
    CHECK(kilix_story_apply_actions(
              &state, negative_overflow, 2u) ==
          KILIX_STORY_OVERFLOW);
    CHECK(flags[1] == (UINT64_C(1) << 1) &&
          counters[3] == INT32_MIN);
    CHECK(kilix_story_apply_actions(
              &state, intermediate_overflow, 2u) ==
          KILIX_STORY_OVERFLOW && counters[2] == INT32_MAX);
    CHECK(kilix_story_apply_actions(&state, NULL, 1u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_apply_actions(
              &state, success,
              SIZE_MAX / sizeof(*success) + 1u) ==
          KILIX_STORY_INVALID_ARGUMENT);

    for (index = 0u; index < 66u; ++index) {
        many_actions[index] = (kilix_story_action){
            KILIX_STORY_ACTION_SET_COUNTER, (uint32_t)index,
            (int32_t)index
        };
        fallback_overflow[index] = (kilix_story_action){
            KILIX_STORY_ACTION_ADD_COUNTER, (uint32_t)index, 0
        };
        fallback_invalid[index] = fallback_overflow[index];
    }
    CHECK(kilix_story_state_bind(
              &state, NULL, 0u, many_counters, 66u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_apply_actions(&state, many_actions, 66u) ==
          KILIX_STORY_OK);
    for (index = 0u; index < 66u; ++index)
        CHECK(many_counters[index] == (int32_t)index);
    many_counters[65] = INT32_MAX;
    fallback_overflow[65].value = 1;
    CHECK(kilix_story_apply_actions(
              &state, fallback_overflow, 66u) ==
          KILIX_STORY_OVERFLOW);
    CHECK(many_counters[65] == INT32_MAX);
    fallback_invalid[65].op = (kilix_story_action_op)999;
    CHECK(kilix_story_apply_actions(
              &state, fallback_invalid, 66u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    fallback_invalid[65] = (kilix_story_action){
        KILIX_STORY_ACTION_ADD_COUNTER, 66u, 0
    };
    CHECK(kilix_story_apply_actions(
              &state, fallback_invalid, 66u) ==
          KILIX_STORY_OUT_OF_RANGE);

    for (iteration = 0u; iteration < 20000u; ++iteration) {
        uint64_t actual_flags[2];
        uint64_t expected_flags[2];
        int32_t actual_counters[4];
        int32_t expected_counters[4];
        kilix_story_action actions[16];
        size_t action_count =
            (size_t)(random_value(&seed) % 17u);
        kilix_story_result actual;
        kilix_story_result expected;
        for (index = 0u; index < 2u; ++index) {
            actual_flags[index] =
                ((uint64_t)random_value(&seed) << 32) |
                random_value(&seed);
            expected_flags[index] = actual_flags[index];
        }
        for (index = 0u; index < 4u; ++index) {
            actual_counters[index] =
                values[random_value(&seed) %
                       (sizeof values / sizeof values[0])];
            expected_counters[index] = actual_counters[index];
        }
        for (index = 0u; index < action_count; ++index) {
            actions[index].op = (kilix_story_action_op)(
                random_value(&seed) % 7u);
            actions[index].index =
                random_value(&seed) % 140u;
            actions[index].value =
                values[random_value(&seed) %
                       (sizeof values / sizeof values[0])];
        }
        CHECK(kilix_story_state_bind(
                  &state, actual_flags, 2u,
                  actual_counters, 4u) == KILIX_STORY_OK);
        expected = reference_apply(
            expected_flags, expected_counters,
            actions, action_count);
        actual = kilix_story_apply_actions(
            &state, actions, action_count);
        CHECK(actual == expected);
        CHECK(memcmp(actual_flags, expected_flags,
                     sizeof actual_flags) == 0);
        CHECK(memcmp(actual_counters, expected_counters,
                     sizeof actual_counters) == 0);
    }
    return true;
}

static bool test_graph_validation(void)
{
    static const kilix_story_condition valid_condition[] = {
        {KILIX_STORY_CONDITION_COUNTER_RANGE, 0u, 1, 3}
    };
    static const kilix_story_action valid_action[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 0u, 0}
    };
    static const kilix_story_choice choices[] = {
        {"next", valid_condition, 1u, valid_action, 1u, 20u, 1u},
        {"end", NULL, 0u, NULL, 0u, KILIX_STORY_END, 2u}
    };
    static const kilix_story_choice cycle_choice[] = {
        {"back", NULL, 0u, NULL, 0u, 10u, 3u}
    };
    static const kilix_story_node nodes[] = {
        {10u, "one", "first", choices, 2u},
        {20u, NULL, "second", cycle_choice, 1u}
    };
    static const kilix_story_graph graph = {nodes, 2u};
    static const kilix_story_condition invalid_condition[] = {
        {(kilix_story_condition_op)999, 0u, 0, 0}
    };
    static const kilix_story_condition invalid_range[] = {
        {KILIX_STORY_CONDITION_COUNTER_RANGE, 0u, 3, 1}
    };
    static const kilix_story_action invalid_action[] = {
        {(kilix_story_action_op)999, 0u, 0}
    };
    kilix_story_choice bad_choice;
    kilix_story_node bad_nodes[2];
    kilix_story_graph bad_graph;
    kilix_story_node slow_nodes[257];
    kilix_story_choice slow_choice = {
        "last", NULL, 0u, NULL, 0u, 256u, 0u
    };
    kilix_story_choice fast_choice = {
        "linked", NULL, 0u, NULL, 0u, 16u, 0u
    };
    size_t index;

    CHECK(kilix_story_graph_validate(&graph) == KILIX_STORY_OK);
    CHECK(kilix_story_find_node(&graph, 10u) == &nodes[0]);
    CHECK(kilix_story_find_node(&graph, 20u) == &nodes[1]);
    CHECK(kilix_story_find_node(&graph, 99u) == NULL);
    CHECK(kilix_story_find_node(NULL, 10u) == NULL);
    CHECK(kilix_story_graph_validate(NULL) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_graph = (kilix_story_graph){NULL, 0u};
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_graph = (kilix_story_graph){
        nodes, SIZE_MAX / sizeof(*nodes) + 1u
    };
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);

    (void)memcpy(bad_nodes, nodes, sizeof bad_nodes);
    bad_graph = (kilix_story_graph){bad_nodes, 2u};
    bad_nodes[0].id = KILIX_STORY_END;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_nodes[0] = nodes[0];
    bad_nodes[0].text = NULL;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_nodes[0] = nodes[0];
    bad_nodes[0].choices = NULL;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_nodes[0] = nodes[0];
    bad_nodes[1].id = nodes[0].id;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);

    (void)memcpy(bad_nodes, nodes, sizeof bad_nodes);
    bad_choice = choices[0];
    bad_nodes[0].choices = &bad_choice;
    bad_nodes[0].choice_count = 1u;
    bad_choice.label = NULL;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice = choices[0];
    bad_choice.next_node = 99u;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice = choices[0];
    bad_choice.conditions = NULL;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice = choices[0];
    bad_choice.actions = NULL;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice = choices[0];
    bad_choice.conditions = invalid_condition;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice.conditions = invalid_range;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    bad_choice = choices[0];
    bad_choice.actions = invalid_action;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);

    for (index = 0u; index < 257u; ++index) {
        slow_nodes[index] = (kilix_story_node){
            (uint32_t)index, NULL, "large", NULL, 0u
        };
    }
    bad_graph = (kilix_story_graph){slow_nodes, 17u};
    slow_nodes[0].choices = &fast_choice;
    slow_nodes[0].choice_count = 1u;
    CHECK(kilix_story_graph_validate(&bad_graph) == KILIX_STORY_OK);
    fast_choice.next_node = 999u;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    fast_choice.next_node = 16u;
    slow_nodes[16].id = 0u;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    slow_nodes[16].id = 16u;
    slow_nodes[0].choices = NULL;
    slow_nodes[0].choice_count = 0u;
    bad_graph = (kilix_story_graph){slow_nodes, 257u};
    CHECK(kilix_story_graph_validate(&bad_graph) == KILIX_STORY_OK);
    slow_nodes[0].choices = &slow_choice;
    slow_nodes[0].choice_count = 1u;
    CHECK(kilix_story_graph_validate(&bad_graph) == KILIX_STORY_OK);
    slow_choice.next_node = 999u;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    slow_choice.next_node = 256u;
    slow_nodes[256].id = 0u;
    CHECK(kilix_story_graph_validate(&bad_graph) ==
          KILIX_STORY_INVALID_GRAPH);
    return true;
}

static bool sessions_equal(
    const kilix_story_session *first,
    const kilix_story_session *second)
{
    return first->graph == second->graph &&
           first->state == second->state &&
           first->node == second->node &&
           first->active == second->active;
}

static bool events_equal(
    const kilix_story_event *first,
    const kilix_story_event *second)
{
    return first->node == second->node &&
           first->choice == second->choice &&
           first->event == second->event &&
           first->next_node == second->next_node &&
           first->ended == second->ended;
}

static bool test_sessions(void)
{
    uint64_t flags[1] = {0};
    int32_t counters[1] = {2};
    kilix_story_state state;
    kilix_story_session session = {0};
    kilix_story_session before;
    kilix_story_event event;
    kilix_story_event unchanged_event = {
        9u, 8u, 7u, 6u, true
    };
    static const kilix_story_condition unlock[] = {
        {KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL, 0u, 2, 0}
    };
    static const kilix_story_action accept[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 3u, 0},
        {KILIX_STORY_ACTION_ADD_COUNTER, 0u, 1}
    };
    static const kilix_story_choice first_choices[] = {
        {"Accept", unlock, 1u, accept, 2u, 20u, 700u},
        {"Leave", NULL, 0u, NULL, 0u, KILIX_STORY_END, 701u}
    };
    static const kilix_story_choice final_choices[] = {
        {"Continue", NULL, 0u, NULL, 0u, KILIX_STORY_END, 702u}
    };
    static const kilix_story_node nodes[] = {
        {10u, "Fixer", "Take the contract?", first_choices, 2u},
        {20u, "Runner", "The route is open.", final_choices, 1u}
    };
    static const kilix_story_graph graph = {nodes, 2u};
    static const kilix_story_condition incompatible_condition[] = {
        {KILIX_STORY_CONDITION_COUNTER_EQUAL, 1u, 0, 0}
    };
    static const kilix_story_choice incompatible_condition_choice[] = {
        {"bad", incompatible_condition, 1u, NULL, 0u,
         KILIX_STORY_END, 0u}
    };
    static const kilix_story_node incompatible_condition_node[] = {
        {1u, NULL, "bad condition index",
         incompatible_condition_choice, 1u}
    };
    static const kilix_story_graph incompatible_condition_graph = {
        incompatible_condition_node, 1u
    };
    static const kilix_story_action incompatible_action[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 64u, 0}
    };
    static const kilix_story_choice incompatible_action_choice[] = {
        {"bad", NULL, 0u, incompatible_action, 1u,
         KILIX_STORY_END, 0u}
    };
    static const kilix_story_node incompatible_action_node[] = {
        {1u, NULL, "bad action index", incompatible_action_choice, 1u}
    };
    static const kilix_story_graph incompatible_action_graph = {
        incompatible_action_node, 1u
    };
    static const kilix_story_choice missing_choice[] = {
        {"missing", NULL, 0u, NULL, 0u, 99u, 0u}
    };
    static const kilix_story_node missing_node[] = {
        {1u, NULL, "missing", missing_choice, 1u}
    };
    static const kilix_story_graph missing_graph = {missing_node, 1u};

    CHECK(kilix_story_state_bind(&state, flags, 1u, counters, 1u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_session_start(NULL, &graph, &state, 10u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_session_start(&session, &graph, NULL, 10u) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(kilix_story_session_start(&session, &graph, &state, 10u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_session_choose(NULL, 0u, NULL) ==
          KILIX_STORY_INVALID_ARGUMENT);
    CHECK(session.active && session.node == &nodes[0]);
    CHECK(kilix_story_choice_available(&session, 0u));
    CHECK(kilix_story_choice_available(&session, 1u));
    CHECK(!kilix_story_choice_available(&session, 2u));

    before = session;
    CHECK(kilix_story_session_start(
              &session, &missing_graph, &state, 1u) ==
          KILIX_STORY_INVALID_GRAPH);
    CHECK(sessions_equal(&session, &before));
    CHECK(kilix_story_session_start(&session, &graph, &state, 99u) ==
          KILIX_STORY_NODE_NOT_FOUND);
    CHECK(sessions_equal(&session, &before));
    CHECK(kilix_story_session_start(
              &session, &incompatible_condition_graph,
              &state, 1u) == KILIX_STORY_OUT_OF_RANGE);
    CHECK(sessions_equal(&session, &before));
    CHECK(kilix_story_session_start(
              &session, &incompatible_action_graph,
              &state, 1u) == KILIX_STORY_OUT_OF_RANGE);
    CHECK(sessions_equal(&session, &before));

    event = unchanged_event;
    CHECK(kilix_story_session_choose(&session, 2u, &event) ==
          KILIX_STORY_CHOICE_UNAVAILABLE);
    CHECK(events_equal(&event, &unchanged_event));
    CHECK(kilix_story_session_choose(&session, 0u, &event) ==
          KILIX_STORY_OK);
    CHECK(event.node == 10u && event.choice == 0u &&
          event.event == 700u && event.next_node == 20u &&
          !event.ended);
    CHECK(session.active && session.node == &nodes[1]);
    CHECK((flags[0] & (UINT64_C(1) << 3)) != 0u);
    CHECK(counters[0] == 3);
    CHECK(kilix_story_session_choose(&session, 0u, &event) ==
          KILIX_STORY_OK && event.ended);
    CHECK(!session.active && session.node == NULL);
    CHECK(kilix_story_session_choose(&session, 0u, NULL) ==
          KILIX_STORY_NOT_ACTIVE);

    counters[0] = 1;
    CHECK(kilix_story_session_start(&session, &graph, &state, 10u) ==
          KILIX_STORY_OK);
    CHECK(!kilix_story_choice_available(&session, 0u));
    event = unchanged_event;
    CHECK(kilix_story_session_choose(&session, 0u, &event) ==
          KILIX_STORY_CHOICE_UNAVAILABLE);
    CHECK(events_equal(&event, &unchanged_event));
    CHECK(counters[0] == 1 && session.node == &nodes[0]);
    CHECK(kilix_story_session_choose(&session, 1u, &event) ==
          KILIX_STORY_OK && event.ended);

    CHECK(!kilix_story_choice_available(NULL, 0u));
    kilix_story_session_stop(&session);
    CHECK(!session.active && !session.graph &&
          !session.state && !session.node);
    kilix_story_session_stop(NULL);
    return true;
}

static bool test_result_names(void)
{
    static const char *const expected[] = {
        "ok",
        "invalid argument",
        "state index out of range",
        "counter overflow",
        "invalid story graph",
        "story node not found",
        "choice unavailable",
        "story session is not active"
    };
    size_t index;
    for (index = 0u; index < sizeof expected / sizeof expected[0];
         ++index)
        CHECK(strcmp(
                  kilix_story_result_name((kilix_story_result)index),
                  expected[index]) == 0);
    CHECK(strcmp(
              kilix_story_result_name((kilix_story_result)999),
              "unknown story result") == 0);
    return true;
}

typedef struct test_case {
    const char *name;
    bool (*run)(void);
} test_case;

int main(void)
{
    static const test_case tests[] = {
        {"state binding and access", test_state_binding_and_access},
        {"conditions", test_conditions},
        {"actions and model", test_actions_and_model},
        {"graph validation", test_graph_validation},
        {"sessions", test_sessions},
        {"result names", test_result_names}
    };
    size_t index;
    size_t failures = 0u;
    for (index = 0u; index < sizeof tests / sizeof tests[0]; ++index) {
        if (tests[index].run())
            (void)printf("PASS %s\n", tests[index].name);
        else {
            (void)printf("FAIL %s\n", tests[index].name);
            ++failures;
        }
    }
    if (failures != 0u) {
        (void)fprintf(
            stderr, "FAIL kilix-story: %zu suite(s) failed\n",
            failures);
        return EXIT_FAILURE;
    }
    (void)printf(
        "PASS kilix-story: %zu suites\n",
        sizeof tests / sizeof tests[0]);
    return EXIT_SUCCESS;
}
