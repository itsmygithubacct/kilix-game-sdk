#include "kilix_story.h"

#include <limits.h>

static bool state_valid(const kilix_story_state *state)
{
    return state &&
           (state->flag_word_count == 0u || state->flag_words) &&
           (state->counter_count == 0u || state->counters);
}

kilix_story_result kilix_story_state_bind(
    kilix_story_state *state, uint64_t *flag_words, size_t flag_word_count,
    int32_t *counters, size_t counter_count)
{
    if (!state || (flag_word_count != 0u && !flag_words) ||
        (counter_count != 0u && !counters))
        return KILIX_STORY_INVALID_ARGUMENT;
    state->flag_words = flag_words;
    state->flag_word_count = flag_word_count;
    state->counters = counters;
    state->counter_count = counter_count;
    return KILIX_STORY_OK;
}

static bool flag_location(const kilix_story_state *state, uint32_t flag,
                          size_t *word, uint64_t *mask)
{
    size_t selected = (size_t)(flag / UINT32_C(64));
    if (!state_valid(state) || selected >= state->flag_word_count)
        return false;
    if (word) *word = selected;
    if (mask) *mask = UINT64_C(1) << (flag % UINT32_C(64));
    return true;
}

kilix_story_result kilix_story_flag_get(
    const kilix_story_state *state, uint32_t flag, bool *value)
{
    size_t word;
    uint64_t mask;
    if (!value) return KILIX_STORY_INVALID_ARGUMENT;
    if (!flag_location(state, flag, &word, &mask))
        return state_valid(state) ? KILIX_STORY_OUT_OF_RANGE :
               KILIX_STORY_INVALID_ARGUMENT;
    *value = (state->flag_words[word] & mask) != 0u;
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_flag_set(
    kilix_story_state *state, uint32_t flag, bool value)
{
    size_t word;
    uint64_t mask;
    if (!flag_location(state, flag, &word, &mask))
        return state_valid(state) ? KILIX_STORY_OUT_OF_RANGE :
               KILIX_STORY_INVALID_ARGUMENT;
    if (value) state->flag_words[word] |= mask;
    else state->flag_words[word] &= ~mask;
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_counter_get(
    const kilix_story_state *state, uint32_t counter, int32_t *value)
{
    if (!value || !state_valid(state)) return KILIX_STORY_INVALID_ARGUMENT;
    if ((size_t)counter >= state->counter_count)
        return KILIX_STORY_OUT_OF_RANGE;
    *value = state->counters[counter];
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_counter_set(
    kilix_story_state *state, uint32_t counter, int32_t value)
{
    if (!state_valid(state)) return KILIX_STORY_INVALID_ARGUMENT;
    if ((size_t)counter >= state->counter_count)
        return KILIX_STORY_OUT_OF_RANGE;
    state->counters[counter] = value;
    return KILIX_STORY_OK;
}

static kilix_story_result condition_matches(
    const kilix_story_state *state, const kilix_story_condition *condition,
    bool *matches)
{
    bool flag;
    int32_t counter;
    if (!condition || !matches || !state_valid(state))
        return KILIX_STORY_INVALID_ARGUMENT;
    switch (condition->op) {
    case KILIX_STORY_CONDITION_ALWAYS:
        *matches = true;
        return KILIX_STORY_OK;
    case KILIX_STORY_CONDITION_FLAG_SET:
    case KILIX_STORY_CONDITION_FLAG_CLEAR:
        if (kilix_story_flag_get(state, condition->index, &flag) !=
            KILIX_STORY_OK) return KILIX_STORY_OUT_OF_RANGE;
        *matches = condition->op == KILIX_STORY_CONDITION_FLAG_SET ?
                   flag : !flag;
        return KILIX_STORY_OK;
    case KILIX_STORY_CONDITION_COUNTER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_LESS:
    case KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_GREATER:
    case KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_RANGE:
        if (kilix_story_counter_get(state, condition->index, &counter) !=
            KILIX_STORY_OK) return KILIX_STORY_OUT_OF_RANGE;
        break;
    default:
        return KILIX_STORY_INVALID_ARGUMENT;
    }
    switch (condition->op) {
    case KILIX_STORY_CONDITION_COUNTER_EQUAL:
        *matches = counter == condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL:
        *matches = counter != condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_LESS:
        *matches = counter < condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL:
        *matches = counter <= condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_GREATER:
        *matches = counter > condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL:
        *matches = counter >= condition->value;
        break;
    case KILIX_STORY_CONDITION_COUNTER_RANGE:
        if (condition->maximum < condition->value)
            return KILIX_STORY_INVALID_ARGUMENT;
        *matches = counter >= condition->value &&
                   counter <= condition->maximum;
        break;
    default:
        return KILIX_STORY_INVALID_ARGUMENT;
    }
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_conditions_all(
    const kilix_story_state *state, const kilix_story_condition *conditions,
    size_t condition_count, bool *matches)
{
    size_t index;
    if (!matches || !state_valid(state) ||
        (condition_count != 0u && !conditions))
        return KILIX_STORY_INVALID_ARGUMENT;
    *matches = true;
    for (index = 0u; index < condition_count; ++index) {
        bool current;
        kilix_story_result result =
            condition_matches(state, &conditions[index], &current);
        if (result != KILIX_STORY_OK) return result;
        if (!current) *matches = false;
    }
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_conditions_any(
    const kilix_story_state *state, const kilix_story_condition *conditions,
    size_t condition_count, bool *matches)
{
    size_t index;
    if (!matches || !state_valid(state) ||
        (condition_count != 0u && !conditions))
        return KILIX_STORY_INVALID_ARGUMENT;
    *matches = condition_count == 0u;
    for (index = 0u; index < condition_count; ++index) {
        bool current;
        kilix_story_result result =
            condition_matches(state, &conditions[index], &current);
        if (result != KILIX_STORY_OK) return result;
        if (current) *matches = true;
    }
    return KILIX_STORY_OK;
}

static kilix_story_result projected_counter(
    const kilix_story_state *state, const kilix_story_action *actions,
    size_t through, uint32_t counter, int32_t *value)
{
    size_t index;
    int64_t projected;
    kilix_story_result result =
        kilix_story_counter_get(state, counter, value);
    if (result != KILIX_STORY_OK) return result;
    projected = *value;
    for (index = 0u; index <= through; ++index) {
        const kilix_story_action *action = &actions[index];
        if (action->index != counter) continue;
        if (action->op == KILIX_STORY_ACTION_SET_COUNTER)
            projected = action->value;
        else if (action->op == KILIX_STORY_ACTION_ADD_COUNTER)
            projected += action->value;
        else
            continue;
        if (projected < INT32_MIN || projected > INT32_MAX)
            return KILIX_STORY_OVERFLOW;
    }
    *value = (int32_t)projected;
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_apply_actions(
    kilix_story_state *state, const kilix_story_action *actions,
    size_t action_count)
{
    size_t index;
    if (!state_valid(state) || (action_count != 0u && !actions))
        return KILIX_STORY_INVALID_ARGUMENT;
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        if (action->op == KILIX_STORY_ACTION_SET_FLAG ||
            action->op == KILIX_STORY_ACTION_CLEAR_FLAG ||
            action->op == KILIX_STORY_ACTION_TOGGLE_FLAG) {
            if (!flag_location(state, action->index, NULL, NULL))
                return KILIX_STORY_OUT_OF_RANGE;
        } else if (action->op == KILIX_STORY_ACTION_SET_COUNTER ||
                   action->op == KILIX_STORY_ACTION_ADD_COUNTER) {
            int32_t projected;
            kilix_story_result result = projected_counter(
                state, actions, index, action->index, &projected);
            if (result != KILIX_STORY_OK) return result;
        } else {
            return KILIX_STORY_INVALID_ARGUMENT;
        }
    }
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        if (action->op == KILIX_STORY_ACTION_SET_FLAG)
            (void)kilix_story_flag_set(state, action->index, true);
        else if (action->op == KILIX_STORY_ACTION_CLEAR_FLAG)
            (void)kilix_story_flag_set(state, action->index, false);
        else if (action->op == KILIX_STORY_ACTION_TOGGLE_FLAG) {
            bool flag = false;
            (void)kilix_story_flag_get(state, action->index, &flag);
            (void)kilix_story_flag_set(state, action->index, !flag);
        } else if (action->op == KILIX_STORY_ACTION_SET_COUNTER)
            state->counters[action->index] = action->value;
        else
            state->counters[action->index] += action->value;
    }
    return KILIX_STORY_OK;
}

const kilix_story_node *kilix_story_find_node(
    const kilix_story_graph *graph, uint32_t id)
{
    size_t index;
    if (!graph || (graph->node_count != 0u && !graph->nodes)) return NULL;
    for (index = 0u; index < graph->node_count; ++index)
        if (graph->nodes[index].id == id) return &graph->nodes[index];
    return NULL;
}

kilix_story_result kilix_story_graph_validate(
    const kilix_story_graph *graph)
{
    size_t node_index;
    if (!graph || !graph->nodes || graph->node_count == 0u)
        return KILIX_STORY_INVALID_GRAPH;
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        size_t previous;
        size_t choice_index;
        if (node->id == KILIX_STORY_END || !node->text ||
            (node->choice_count != 0u && !node->choices))
            return KILIX_STORY_INVALID_GRAPH;
        for (previous = 0u; previous < node_index; ++previous)
            if (graph->nodes[previous].id == node->id)
                return KILIX_STORY_INVALID_GRAPH;
        for (choice_index = 0u; choice_index < node->choice_count;
             ++choice_index) {
            const kilix_story_choice *choice = &node->choices[choice_index];
            if (!choice->label ||
                (choice->condition_count != 0u && !choice->conditions) ||
                (choice->action_count != 0u && !choice->actions) ||
                (choice->next_node != KILIX_STORY_END &&
                 !kilix_story_find_node(graph, choice->next_node)))
                return KILIX_STORY_INVALID_GRAPH;
        }
    }
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_session_start(
    kilix_story_session *session, const kilix_story_graph *graph,
    kilix_story_state *state, uint32_t first_node)
{
    const kilix_story_node *node;
    if (!session || !state_valid(state))
        return KILIX_STORY_INVALID_ARGUMENT;
    *session = (kilix_story_session){0};
    if (kilix_story_graph_validate(graph) != KILIX_STORY_OK)
        return KILIX_STORY_INVALID_GRAPH;
    node = kilix_story_find_node(graph, first_node);
    if (!node) return KILIX_STORY_NODE_NOT_FOUND;
    session->graph = graph;
    session->state = state;
    session->node = node;
    session->active = true;
    return KILIX_STORY_OK;
}

bool kilix_story_choice_available(
    const kilix_story_session *session, size_t choice)
{
    bool matches = false;
    if (!session || !session->active || !session->node ||
        choice >= session->node->choice_count)
        return false;
    return kilix_story_conditions_all(
        session->state, session->node->choices[choice].conditions,
        session->node->choices[choice].condition_count, &matches) ==
        KILIX_STORY_OK && matches;
}

kilix_story_result kilix_story_session_choose(
    kilix_story_session *session, size_t choice_index,
    kilix_story_event *event)
{
    const kilix_story_choice *choice;
    const kilix_story_node *next = NULL;
    kilix_story_result result;
    uint32_t node_id;
    if (!session || !session->state || !session->graph)
        return KILIX_STORY_INVALID_ARGUMENT;
    if (!session->active || !session->node) return KILIX_STORY_NOT_ACTIVE;
    if (choice_index >= session->node->choice_count ||
        !kilix_story_choice_available(session, choice_index))
        return KILIX_STORY_CHOICE_UNAVAILABLE;
    choice = &session->node->choices[choice_index];
    if (choice->next_node != KILIX_STORY_END) {
        next = kilix_story_find_node(session->graph, choice->next_node);
        if (!next) return KILIX_STORY_INVALID_GRAPH;
    }
    result = kilix_story_apply_actions(
        session->state, choice->actions, choice->action_count);
    if (result != KILIX_STORY_OK) return result;
    node_id = session->node->id;
    session->node = next;
    session->active = next != NULL;
    if (event) {
        event->node = node_id;
        event->choice = choice_index;
        event->event = choice->event;
        event->next_node = choice->next_node;
        event->ended = !session->active;
    }
    return KILIX_STORY_OK;
}

void kilix_story_session_stop(kilix_story_session *session)
{
    if (session) *session = (kilix_story_session){0};
}

const char *kilix_story_result_name(kilix_story_result result)
{
    switch (result) {
    case KILIX_STORY_OK: return "ok";
    case KILIX_STORY_INVALID_ARGUMENT: return "invalid argument";
    case KILIX_STORY_OUT_OF_RANGE: return "state index out of range";
    case KILIX_STORY_OVERFLOW: return "counter overflow";
    case KILIX_STORY_INVALID_GRAPH: return "invalid story graph";
    case KILIX_STORY_NODE_NOT_FOUND: return "story node not found";
    case KILIX_STORY_CHOICE_UNAVAILABLE: return "choice unavailable";
    case KILIX_STORY_NOT_ACTIVE: return "story session is not active";
    default: return "unknown story result";
    }
}
