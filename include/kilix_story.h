#ifndef KILIX_STORY_H
#define KILIX_STORY_H

/*
 * Game-rule-free story state, conditions, actions, and dialogue traversal.
 *
 * The caller owns every buffer and string. The library performs no allocation
 * or I/O and does not define quests, rewards, combat, inventory, or UI.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_STORY_VERSION_MAJOR 0
#define KILIX_STORY_VERSION_MINOR 1
#define KILIX_STORY_VERSION_PATCH 0
#define KILIX_STORY_END UINT32_MAX

typedef enum kilix_story_result {
    KILIX_STORY_OK = 0,
    KILIX_STORY_INVALID_ARGUMENT = 1,
    KILIX_STORY_OUT_OF_RANGE = 2,
    KILIX_STORY_OVERFLOW = 3,
    KILIX_STORY_INVALID_GRAPH = 4,
    KILIX_STORY_NODE_NOT_FOUND = 5,
    KILIX_STORY_CHOICE_UNAVAILABLE = 6,
    KILIX_STORY_NOT_ACTIVE = 7
} kilix_story_result;

typedef struct kilix_story_state {
    uint64_t *flag_words;
    size_t flag_word_count;
    int32_t *counters;
    size_t counter_count;
} kilix_story_state;

kilix_story_result kilix_story_state_bind(
    kilix_story_state *state, uint64_t *flag_words, size_t flag_word_count,
    int32_t *counters, size_t counter_count);
kilix_story_result kilix_story_flag_get(
    const kilix_story_state *state, uint32_t flag, bool *value);
kilix_story_result kilix_story_flag_set(
    kilix_story_state *state, uint32_t flag, bool value);
kilix_story_result kilix_story_counter_get(
    const kilix_story_state *state, uint32_t counter, int32_t *value);
kilix_story_result kilix_story_counter_set(
    kilix_story_state *state, uint32_t counter, int32_t value);

typedef enum kilix_story_condition_op {
    KILIX_STORY_CONDITION_ALWAYS = 0,
    KILIX_STORY_CONDITION_FLAG_SET,
    KILIX_STORY_CONDITION_FLAG_CLEAR,
    KILIX_STORY_CONDITION_COUNTER_EQUAL,
    KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL,
    KILIX_STORY_CONDITION_COUNTER_LESS,
    KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL,
    KILIX_STORY_CONDITION_COUNTER_GREATER,
    KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL,
    KILIX_STORY_CONDITION_COUNTER_RANGE
} kilix_story_condition_op;

typedef struct kilix_story_condition {
    kilix_story_condition_op op;
    uint32_t index;
    int32_t value;
    int32_t maximum;
} kilix_story_condition;

kilix_story_result kilix_story_conditions_all(
    const kilix_story_state *state, const kilix_story_condition *conditions,
    size_t condition_count, bool *matches);
kilix_story_result kilix_story_conditions_any(
    const kilix_story_state *state, const kilix_story_condition *conditions,
    size_t condition_count, bool *matches);

typedef enum kilix_story_action_op {
    KILIX_STORY_ACTION_SET_FLAG = 0,
    KILIX_STORY_ACTION_CLEAR_FLAG,
    KILIX_STORY_ACTION_TOGGLE_FLAG,
    KILIX_STORY_ACTION_SET_COUNTER,
    KILIX_STORY_ACTION_ADD_COUNTER
} kilix_story_action_op;

typedef struct kilix_story_action {
    kilix_story_action_op op;
    uint32_t index;
    int32_t value;
} kilix_story_action;

/*
 * Validate the entire sequence before changing state. Counter overflow or an
 * invalid index leaves every flag and counter untouched.
 */
kilix_story_result kilix_story_apply_actions(
    kilix_story_state *state, const kilix_story_action *actions,
    size_t action_count);

typedef struct kilix_story_choice {
    const char *label;
    const kilix_story_condition *conditions;
    size_t condition_count;
    const kilix_story_action *actions;
    size_t action_count;
    uint32_t next_node;
    uint32_t event;
} kilix_story_choice;

typedef struct kilix_story_node {
    uint32_t id;
    const char *speaker;
    const char *text;
    const kilix_story_choice *choices;
    size_t choice_count;
} kilix_story_node;

typedef struct kilix_story_graph {
    const kilix_story_node *nodes;
    size_t node_count;
} kilix_story_graph;

kilix_story_result kilix_story_graph_validate(
    const kilix_story_graph *graph);
const kilix_story_node *kilix_story_find_node(
    const kilix_story_graph *graph, uint32_t id);

typedef struct kilix_story_session {
    const kilix_story_graph *graph;
    kilix_story_state *state;
    const kilix_story_node *node;
    bool active;
} kilix_story_session;

typedef struct kilix_story_event {
    uint32_t node;
    size_t choice;
    uint32_t event;
    uint32_t next_node;
    bool ended;
} kilix_story_event;

kilix_story_result kilix_story_session_start(
    kilix_story_session *session, const kilix_story_graph *graph,
    kilix_story_state *state, uint32_t first_node);
bool kilix_story_choice_available(
    const kilix_story_session *session, size_t choice);
kilix_story_result kilix_story_session_choose(
    kilix_story_session *session, size_t choice, kilix_story_event *event);
void kilix_story_session_stop(kilix_story_session *session);

const char *kilix_story_result_name(kilix_story_result result);

#ifdef __cplusplus
}
#endif

#endif
