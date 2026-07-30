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

static bool test_state_conditions_and_transactions(void)
{
    uint64_t flags[2] = {0};
    int32_t counters[3] = {4, 8, INT32_MAX};
    kilix_story_state state;
    bool value;
    int32_t counter;
    static const kilix_story_condition conditions[] = {
        {KILIX_STORY_CONDITION_FLAG_CLEAR, 65u, 0, 0},
        {KILIX_STORY_CONDITION_COUNTER_RANGE, 1u, 5, 10}
    };
    static const kilix_story_action actions[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 65u, 0},
        {KILIX_STORY_ACTION_ADD_COUNTER, 0u, 3},
        {KILIX_STORY_ACTION_SET_COUNTER, 1u, -2},
        {KILIX_STORY_ACTION_ADD_COUNTER, 1u, 7}
    };
    static const kilix_story_action overflow[] = {
        {KILIX_STORY_ACTION_SET_FLAG, 1u, 0},
        {KILIX_STORY_ACTION_ADD_COUNTER, 2u, 1}
    };

    CHECK(kilix_story_state_bind(&state, flags, 2u, counters, 3u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_conditions_all(&state, conditions, 2u, &value) ==
          KILIX_STORY_OK && value);
    CHECK(kilix_story_apply_actions(&state, actions, 4u) == KILIX_STORY_OK);
    CHECK(kilix_story_flag_get(&state, 65u, &value) == KILIX_STORY_OK &&
          value);
    CHECK(kilix_story_counter_get(&state, 0u, &counter) == KILIX_STORY_OK &&
          counter == 7);
    CHECK(kilix_story_counter_get(&state, 1u, &counter) == KILIX_STORY_OK &&
          counter == 5);
    CHECK(kilix_story_apply_actions(&state, overflow, 2u) ==
          KILIX_STORY_OVERFLOW);
    CHECK(kilix_story_flag_get(&state, 1u, &value) == KILIX_STORY_OK &&
          !value);
    CHECK(counters[2] == INT32_MAX);
    return true;
}

static bool test_dialogue(void)
{
    uint64_t flags[1] = {0};
    int32_t counters[1] = {2};
    kilix_story_state state;
    kilix_story_session session;
    kilix_story_event event;
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
    bool flag;

    CHECK(kilix_story_state_bind(&state, flags, 1u, counters, 1u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_graph_validate(&graph) == KILIX_STORY_OK);
    CHECK(kilix_story_session_start(&session, &graph, &state, 10u) ==
          KILIX_STORY_OK);
    CHECK(kilix_story_choice_available(&session, 0u));
    CHECK(kilix_story_session_choose(&session, 0u, &event) ==
          KILIX_STORY_OK);
    CHECK(event.node == 10u && event.choice == 0u && event.event == 700u &&
          event.next_node == 20u && !event.ended);
    CHECK(session.node && session.node->id == 20u);
    CHECK(kilix_story_flag_get(&state, 3u, &flag) == KILIX_STORY_OK && flag);
    CHECK(counters[0] == 3);
    CHECK(kilix_story_session_choose(&session, 0u, &event) ==
          KILIX_STORY_OK && event.ended);
    CHECK(!session.active && session.node == NULL);
    CHECK(kilix_story_session_choose(&session, 0u, NULL) ==
          KILIX_STORY_NOT_ACTIVE);
    return true;
}

static bool test_invalid_graph(void)
{
    static const kilix_story_choice missing[] = {
        {"Go", NULL, 0u, NULL, 0u, 99u, 0u}
    };
    static const kilix_story_node nodes[] = {
        {1u, NULL, "Broken", missing, 1u}
    };
    static const kilix_story_graph graph = {nodes, 1u};
    CHECK(kilix_story_graph_validate(&graph) == KILIX_STORY_INVALID_GRAPH);
    CHECK(strcmp(kilix_story_result_name(KILIX_STORY_OVERFLOW),
                 "counter overflow") == 0);
    return true;
}

int main(void)
{
    if (!test_state_conditions_and_transactions() || !test_dialogue() ||
        !test_invalid_graph()) return EXIT_FAILURE;
    (void)puts("PASS kilix-story state conditions actions dialogue");
    return EXIT_SUCCESS;
}
