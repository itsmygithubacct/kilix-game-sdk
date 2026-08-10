#include "kilix_story.h"

#include <limits.h>

enum {
    ACTION_LINEAR_LIMIT = 8,
    PROJECTED_COUNTER_CAPACITY = 64,
    GRAPH_LINEAR_NODE_LIMIT = 16,
    GRAPH_INDEX_CAPACITY = 512,
    GRAPH_INDEX_NODE_LIMIT = GRAPH_INDEX_CAPACITY / 2
};

typedef struct projected_counter_entry {
    int64_t value;
    uint32_t index;
    bool used;
} projected_counter_entry;

typedef struct graph_index_entry {
    uint32_t id;
    bool used;
} graph_index_entry;

_Static_assert(
    (PROJECTED_COUNTER_CAPACITY &
     (PROJECTED_COUNTER_CAPACITY - 1)) == 0,
    "projected counter capacity must be a power of two");
_Static_assert(
    (GRAPH_INDEX_CAPACITY & (GRAPH_INDEX_CAPACITY - 1)) == 0,
    "graph index capacity must be a power of two");

static bool buffer_span(
    const void *buffer, size_t count, size_t element_size,
    size_t alignment, uintptr_t *begin, uintptr_t *end)
{
    uintptr_t address;
    size_t size;
    if (count == 0u) {
        if (begin) *begin = 0u;
        if (end) *end = 0u;
        return true;
    }
    if (!buffer || element_size == 0u || alignment == 0u ||
        count > SIZE_MAX / element_size)
        return false;
    address = (uintptr_t)buffer;
    size = count * element_size;
    if (address % alignment != 0u || address > UINTPTR_MAX - size)
        return false;
    if (begin) *begin = address;
    if (end) *end = address + size;
    return true;
}

static bool spans_overlap(
    uintptr_t first_begin, uintptr_t first_end,
    uintptr_t second_begin, uintptr_t second_end)
{
    return first_begin < second_end && second_begin < first_end;
}

static bool state_layout_valid(
    const kilix_story_state *state, const void *state_storage)
{
    uintptr_t flag_begin;
    uintptr_t flag_end;
    uintptr_t counter_begin;
    uintptr_t counter_end;
    uintptr_t state_begin;
    uintptr_t state_end;
    if (!state || !state_storage ||
        !buffer_span(
            state->flag_words, state->flag_word_count,
            sizeof(*state->flag_words), _Alignof(uint64_t),
            &flag_begin, &flag_end) ||
        !buffer_span(
            state->counters, state->counter_count,
            sizeof(*state->counters), _Alignof(int32_t),
            &counter_begin, &counter_end) ||
        !buffer_span(
            state_storage, 1u, sizeof(kilix_story_state),
            _Alignof(kilix_story_state), &state_begin, &state_end))
        return false;
    if (state->flag_word_count != 0u &&
        state->counter_count != 0u &&
        spans_overlap(
            flag_begin, flag_end, counter_begin, counter_end))
        return false;
    if (state->flag_word_count != 0u &&
        spans_overlap(flag_begin, flag_end, state_begin, state_end))
        return false;
    return state->counter_count == 0u ||
           !spans_overlap(
               counter_begin, counter_end, state_begin, state_end);
}

static bool state_valid(const kilix_story_state *state)
{
    return state && state_layout_valid(state, state);
}

static bool array_valid(
    const void *array, size_t count, size_t element_size,
    size_t alignment)
{
    return buffer_span(
        array, count, element_size, alignment, NULL, NULL);
}

kilix_story_result kilix_story_state_bind(
    kilix_story_state *state, uint64_t *flag_words, size_t flag_word_count,
    int32_t *counters, size_t counter_count)
{
    kilix_story_state bound = {
        flag_words, flag_word_count, counters, counter_count
    };
    if (!state || !state_layout_valid(&bound, state))
        return KILIX_STORY_INVALID_ARGUMENT;
    *state = bound;
    return KILIX_STORY_OK;
}

static bool flag_location(
    const kilix_story_state *state, uint32_t flag,
    size_t *word, uint64_t *mask)
{
    size_t selected = (size_t)(flag / UINT32_C(64));
    if (!state_valid(state) || selected >= state->flag_word_count)
        return false;
    if (word) *word = selected;
    if (mask) *mask = UINT64_C(1) << (flag % UINT32_C(64));
    return true;
}

static bool flag_index_valid(
    const kilix_story_state *state, uint32_t flag)
{
    return (size_t)(flag / UINT32_C(64)) < state->flag_word_count;
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
    if (!value || !state_valid(state))
        return KILIX_STORY_INVALID_ARGUMENT;
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

static bool condition_definition_valid(
    const kilix_story_condition *condition)
{
    if (!condition) return false;
    switch (condition->op) {
    case KILIX_STORY_CONDITION_ALWAYS:
    case KILIX_STORY_CONDITION_FLAG_SET:
    case KILIX_STORY_CONDITION_FLAG_CLEAR:
    case KILIX_STORY_CONDITION_COUNTER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_LESS:
    case KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_GREATER:
    case KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL:
        return true;
    case KILIX_STORY_CONDITION_COUNTER_RANGE:
        return condition->maximum >= condition->value;
    default:
        return false;
    }
}

static bool condition_index_valid(
    const kilix_story_state *state,
    const kilix_story_condition *condition)
{
    switch (condition->op) {
    case KILIX_STORY_CONDITION_ALWAYS:
        return true;
    case KILIX_STORY_CONDITION_FLAG_SET:
    case KILIX_STORY_CONDITION_FLAG_CLEAR:
        return flag_index_valid(state, condition->index);
    case KILIX_STORY_CONDITION_COUNTER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_LESS:
    case KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_GREATER:
    case KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_RANGE:
        return (size_t)condition->index < state->counter_count;
    default:
        return false;
    }
}

static kilix_story_result condition_matches(
    const kilix_story_state *state,
    const kilix_story_condition *condition, bool *matches)
{
    int32_t counter;
    if (!condition_definition_valid(condition))
        return KILIX_STORY_INVALID_ARGUMENT;
    if (!condition_index_valid(state, condition))
        return KILIX_STORY_OUT_OF_RANGE;
    switch (condition->op) {
    case KILIX_STORY_CONDITION_ALWAYS:
        *matches = true;
        return KILIX_STORY_OK;
    case KILIX_STORY_CONDITION_FLAG_SET:
    case KILIX_STORY_CONDITION_FLAG_CLEAR: {
        size_t word =
            (size_t)(condition->index / UINT32_C(64));
        uint64_t mask =
            UINT64_C(1) << (condition->index % UINT32_C(64));
        bool set = (state->flag_words[word] & mask) != 0u;
        *matches =
            condition->op == KILIX_STORY_CONDITION_FLAG_SET ?
            set : !set;
        return KILIX_STORY_OK;
    }
    case KILIX_STORY_CONDITION_COUNTER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_NOT_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_LESS:
    case KILIX_STORY_CONDITION_COUNTER_LESS_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_GREATER:
    case KILIX_STORY_CONDITION_COUNTER_GREATER_EQUAL:
    case KILIX_STORY_CONDITION_COUNTER_RANGE:
        counter = state->counters[condition->index];
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
        *matches =
            counter >= condition->value &&
            counter <= condition->maximum;
        break;
    default:
        return KILIX_STORY_INVALID_ARGUMENT;
    }
    return KILIX_STORY_OK;
}

static kilix_story_result conditions_evaluate(
    const kilix_story_state *state,
    const kilix_story_condition *conditions,
    size_t condition_count, bool *matches, bool require_all)
{
    bool aggregate = require_all;
    size_t index;
    if (!matches || !state_valid(state) ||
        !array_valid(
            conditions, condition_count, sizeof(*conditions),
            _Alignof(kilix_story_condition)))
        return KILIX_STORY_INVALID_ARGUMENT;
    for (index = 0u; index < condition_count; ++index) {
        bool current;
        kilix_story_result result =
            condition_matches(state, &conditions[index], &current);
        if (result != KILIX_STORY_OK) return result;
        if (require_all) {
            if (!current) aggregate = false;
        } else if (current) {
            aggregate = true;
        }
    }
    *matches = aggregate;
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_conditions_all(
    const kilix_story_state *state,
    const kilix_story_condition *conditions,
    size_t condition_count, bool *matches)
{
    return conditions_evaluate(
        state, conditions, condition_count, matches, true);
}

kilix_story_result kilix_story_conditions_any(
    const kilix_story_state *state,
    const kilix_story_condition *conditions,
    size_t condition_count, bool *matches)
{
    return conditions_evaluate(
        state, conditions, condition_count, matches, false);
}

static bool action_definition_valid(
    const kilix_story_action *action)
{
    if (!action) return false;
    switch (action->op) {
    case KILIX_STORY_ACTION_SET_FLAG:
    case KILIX_STORY_ACTION_CLEAR_FLAG:
    case KILIX_STORY_ACTION_TOGGLE_FLAG:
    case KILIX_STORY_ACTION_SET_COUNTER:
    case KILIX_STORY_ACTION_ADD_COUNTER:
        return true;
    default:
        return false;
    }
}

static bool action_index_valid(
    const kilix_story_state *state,
    const kilix_story_action *action)
{
    switch (action->op) {
    case KILIX_STORY_ACTION_SET_FLAG:
    case KILIX_STORY_ACTION_CLEAR_FLAG:
    case KILIX_STORY_ACTION_TOGGLE_FLAG:
        return flag_index_valid(state, action->index);
    case KILIX_STORY_ACTION_SET_COUNTER:
    case KILIX_STORY_ACTION_ADD_COUNTER:
        return (size_t)action->index < state->counter_count;
    default:
        return false;
    }
}

static size_t index_hash(uint32_t value, size_t mask)
{
    return (size_t)(value * UINT32_C(2654435761)) & mask;
}

static projected_counter_entry *projected_counter_entry_find(
    projected_counter_entry entries[PROJECTED_COUNTER_CAPACITY],
    uint32_t index)
{
    size_t slot = index_hash(
        index, PROJECTED_COUNTER_CAPACITY - 1u);
    size_t probe;
    for (probe = 0u; probe < PROJECTED_COUNTER_CAPACITY; ++probe) {
        projected_counter_entry *entry =
            &entries[(slot + probe) &
                     (PROJECTED_COUNTER_CAPACITY - 1u)];
        if (!entry->used || entry->index == index) return entry;
    }
    return NULL;
}

static kilix_story_result actions_validate_cached(
    const kilix_story_state *state,
    const kilix_story_action *actions, size_t action_count,
    bool *cache_exhausted)
{
    projected_counter_entry
        entries[PROJECTED_COUNTER_CAPACITY] = {{0}};
    size_t index;
    *cache_exhausted = false;
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        projected_counter_entry *entry;
        int64_t projected;
        if (!action_definition_valid(action))
            return KILIX_STORY_INVALID_ARGUMENT;
        if (!action_index_valid(state, action))
            return KILIX_STORY_OUT_OF_RANGE;
        if (action->op != KILIX_STORY_ACTION_SET_COUNTER &&
            action->op != KILIX_STORY_ACTION_ADD_COUNTER)
            continue;
        entry = projected_counter_entry_find(entries, action->index);
        if (!entry) {
            *cache_exhausted = true;
            return KILIX_STORY_OK;
        }
        if (!entry->used) {
            entry->used = true;
            entry->index = action->index;
            entry->value = state->counters[action->index];
        }
        projected =
            action->op == KILIX_STORY_ACTION_SET_COUNTER ?
            (int64_t)action->value :
            entry->value + (int64_t)action->value;
        if (projected < INT32_MIN || projected > INT32_MAX)
            return KILIX_STORY_OVERFLOW;
        entry->value = projected;
    }
    return KILIX_STORY_OK;
}

static kilix_story_result projected_counter_slow(
    const kilix_story_state *state,
    const kilix_story_action *actions, size_t through,
    uint32_t counter)
{
    int64_t projected = state->counters[counter];
    size_t index;
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
    return KILIX_STORY_OK;
}

static kilix_story_result actions_validate_linear(
    const kilix_story_state *state,
    const kilix_story_action *actions, size_t action_count)
{
    projected_counter_entry entries[ACTION_LINEAR_LIMIT];
    size_t entry_count = 0u;
    size_t index;
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        size_t entry_index;
        int64_t projected;
        if (!action_definition_valid(action))
            return KILIX_STORY_INVALID_ARGUMENT;
        if (!action_index_valid(state, action))
            return KILIX_STORY_OUT_OF_RANGE;
        if (action->op != KILIX_STORY_ACTION_SET_COUNTER &&
            action->op != KILIX_STORY_ACTION_ADD_COUNTER)
            continue;
        for (entry_index = 0u; entry_index < entry_count;
             ++entry_index)
            if (entries[entry_index].index == action->index)
                break;
        if (entry_index == entry_count) {
            if (entry_count >= ACTION_LINEAR_LIMIT)
                return KILIX_STORY_INVALID_ARGUMENT;
            entries[entry_count].index = action->index;
            entries[entry_count].value =
                state->counters[action->index];
            ++entry_count;
        }
        projected =
            action->op == KILIX_STORY_ACTION_SET_COUNTER ?
            (int64_t)action->value :
            entries[entry_index].value + (int64_t)action->value;
        if (projected < INT32_MIN || projected > INT32_MAX)
            return KILIX_STORY_OVERFLOW;
        entries[entry_index].value = projected;
    }
    return KILIX_STORY_OK;
}

static kilix_story_result actions_validate_slow(
    const kilix_story_state *state,
    const kilix_story_action *actions, size_t action_count)
{
    size_t index;
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        if (!action_definition_valid(action))
            return KILIX_STORY_INVALID_ARGUMENT;
        if (!action_index_valid(state, action))
            return KILIX_STORY_OUT_OF_RANGE;
        if (action->op == KILIX_STORY_ACTION_SET_COUNTER ||
            action->op == KILIX_STORY_ACTION_ADD_COUNTER) {
            kilix_story_result result = projected_counter_slow(
                state, actions, index, action->index);
            if (result != KILIX_STORY_OK) return result;
        }
    }
    return KILIX_STORY_OK;
}

kilix_story_result kilix_story_apply_actions(
    kilix_story_state *state, const kilix_story_action *actions,
    size_t action_count)
{
    bool cache_exhausted;
    kilix_story_result result;
    size_t index;
    if (!state_valid(state) ||
        !array_valid(
            actions, action_count, sizeof(*actions),
            _Alignof(kilix_story_action)))
        return KILIX_STORY_INVALID_ARGUMENT;
    if (action_count <= ACTION_LINEAR_LIMIT) {
        result = actions_validate_linear(
            state, actions, action_count);
        if (result != KILIX_STORY_OK) return result;
    } else {
        result = actions_validate_cached(
            state, actions, action_count, &cache_exhausted);
        if (result != KILIX_STORY_OK) return result;
        if (cache_exhausted) {
            result = actions_validate_slow(
                state, actions, action_count);
            if (result != KILIX_STORY_OK) return result;
        }
    }
    for (index = 0u; index < action_count; ++index) {
        const kilix_story_action *action = &actions[index];
        if (action->op == KILIX_STORY_ACTION_SET_FLAG ||
            action->op == KILIX_STORY_ACTION_CLEAR_FLAG ||
            action->op == KILIX_STORY_ACTION_TOGGLE_FLAG) {
            size_t word =
                (size_t)(action->index / UINT32_C(64));
            uint64_t mask =
                UINT64_C(1) << (action->index % UINT32_C(64));
            if (action->op == KILIX_STORY_ACTION_SET_FLAG)
                state->flag_words[word] |= mask;
            else if (action->op == KILIX_STORY_ACTION_CLEAR_FLAG)
                state->flag_words[word] &= ~mask;
            else
                state->flag_words[word] ^= mask;
        } else if (action->op == KILIX_STORY_ACTION_SET_COUNTER) {
            state->counters[action->index] = action->value;
        } else {
            state->counters[action->index] = (int32_t)(
                (int64_t)state->counters[action->index] +
                (int64_t)action->value);
        }
    }
    return KILIX_STORY_OK;
}

const kilix_story_node *kilix_story_find_node(
    const kilix_story_graph *graph, uint32_t id)
{
    size_t index;
    if (!graph ||
        !array_valid(
            graph->nodes, graph->node_count,
            sizeof(*graph->nodes), _Alignof(kilix_story_node)))
        return NULL;
    for (index = 0u; index < graph->node_count; ++index)
        if (graph->nodes[index].id == id)
            return &graph->nodes[index];
    return NULL;
}

static bool choice_definition_valid(
    const kilix_story_choice *choice)
{
    size_t index;
    if (!choice || !choice->label ||
        !array_valid(
            choice->conditions, choice->condition_count,
            sizeof(*choice->conditions),
            _Alignof(kilix_story_condition)) ||
        !array_valid(
            choice->actions, choice->action_count,
            sizeof(*choice->actions),
            _Alignof(kilix_story_action)))
        return false;
    for (index = 0u; index < choice->condition_count; ++index)
        if (!condition_definition_valid(&choice->conditions[index]))
            return false;
    for (index = 0u; index < choice->action_count; ++index)
        if (!action_definition_valid(&choice->actions[index]))
            return false;
    return true;
}

static bool node_definition_valid(
    const kilix_story_node *node)
{
    size_t index;
    if (!node || node->id == KILIX_STORY_END || !node->text ||
        !array_valid(
            node->choices, node->choice_count,
            sizeof(*node->choices), _Alignof(kilix_story_choice)))
        return false;
    for (index = 0u; index < node->choice_count; ++index)
        if (!choice_definition_valid(&node->choices[index]))
            return false;
    return true;
}

static bool graph_index_insert(
    graph_index_entry entries[GRAPH_INDEX_CAPACITY], uint32_t id)
{
    size_t slot = index_hash(id, GRAPH_INDEX_CAPACITY - 1u);
    size_t probe;
    for (probe = 0u; probe < GRAPH_INDEX_CAPACITY; ++probe) {
        graph_index_entry *entry =
            &entries[(slot + probe) & (GRAPH_INDEX_CAPACITY - 1u)];
        if (!entry->used) {
            entry->used = true;
            entry->id = id;
            return true;
        }
        if (entry->id == id) return false;
    }
    return false;
}

static bool graph_index_contains(
    const graph_index_entry entries[GRAPH_INDEX_CAPACITY],
    uint32_t id)
{
    size_t slot = index_hash(id, GRAPH_INDEX_CAPACITY - 1u);
    size_t probe;
    for (probe = 0u; probe < GRAPH_INDEX_CAPACITY; ++probe) {
        const graph_index_entry *entry =
            &entries[(slot + probe) & (GRAPH_INDEX_CAPACITY - 1u)];
        if (!entry->used) return false;
        if (entry->id == id) return true;
    }
    return false;
}

static bool graph_validate_fast(const kilix_story_graph *graph)
{
    graph_index_entry entries[GRAPH_INDEX_CAPACITY] = {{0}};
    size_t node_index;
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        if (!node_definition_valid(node) ||
            !graph_index_insert(entries, node->id))
            return false;
    }
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        size_t choice_index;
        for (choice_index = 0u;
             choice_index < node->choice_count; ++choice_index) {
            uint32_t next = node->choices[choice_index].next_node;
            if (next != KILIX_STORY_END &&
                !graph_index_contains(entries, next))
                return false;
        }
    }
    return true;
}

static bool graph_validate_slow(const kilix_story_graph *graph)
{
    size_t node_index;
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        size_t previous;
        if (!node_definition_valid(node)) return false;
        for (previous = 0u; previous < node_index; ++previous)
            if (graph->nodes[previous].id == node->id)
                return false;
    }
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        size_t choice_index;
        for (choice_index = 0u;
             choice_index < node->choice_count; ++choice_index) {
            uint32_t next = node->choices[choice_index].next_node;
            if (next != KILIX_STORY_END) {
                size_t target;
                for (target = 0u; target < graph->node_count; ++target)
                    if (graph->nodes[target].id == next) break;
                if (target == graph->node_count) return false;
            }
        }
    }
    return true;
}

kilix_story_result kilix_story_graph_validate(
    const kilix_story_graph *graph)
{
    bool valid;
    if (!graph || graph->node_count == 0u ||
        !array_valid(
            graph->nodes, graph->node_count,
            sizeof(*graph->nodes), _Alignof(kilix_story_node)))
        return KILIX_STORY_INVALID_GRAPH;
    if (graph->node_count <= GRAPH_LINEAR_NODE_LIMIT ||
        graph->node_count > GRAPH_INDEX_NODE_LIMIT)
        valid = graph_validate_slow(graph);
    else
        valid = graph_validate_fast(graph);
    return valid ? KILIX_STORY_OK : KILIX_STORY_INVALID_GRAPH;
}

static bool graph_indices_fit_state(
    const kilix_story_graph *graph,
    const kilix_story_state *state)
{
    size_t node_index;
    for (node_index = 0u; node_index < graph->node_count; ++node_index) {
        const kilix_story_node *node = &graph->nodes[node_index];
        size_t choice_index;
        for (choice_index = 0u;
             choice_index < node->choice_count; ++choice_index) {
            const kilix_story_choice *choice =
                &node->choices[choice_index];
            size_t item;
            for (item = 0u; item < choice->condition_count; ++item)
                if (!condition_index_valid(
                        state, &choice->conditions[item]))
                    return false;
            for (item = 0u; item < choice->action_count; ++item)
                if (!action_index_valid(state, &choice->actions[item]))
                    return false;
        }
    }
    return true;
}

kilix_story_result kilix_story_session_start(
    kilix_story_session *session, const kilix_story_graph *graph,
    kilix_story_state *state, uint32_t first_node)
{
    kilix_story_session started = {0};
    const kilix_story_node *node;
    if (!session || !state_valid(state))
        return KILIX_STORY_INVALID_ARGUMENT;
    if (kilix_story_graph_validate(graph) != KILIX_STORY_OK)
        return KILIX_STORY_INVALID_GRAPH;
    node = kilix_story_find_node(graph, first_node);
    if (!node) return KILIX_STORY_NODE_NOT_FOUND;
    if (!graph_indices_fit_state(graph, state))
        return KILIX_STORY_OUT_OF_RANGE;
    started.graph = graph;
    started.state = state;
    started.node = node;
    started.active = true;
    *session = started;
    return KILIX_STORY_OK;
}

bool kilix_story_choice_available(
    const kilix_story_session *session, size_t choice)
{
    bool matches = false;
    if (!session || !session->active || !session->graph ||
        !session->node || !state_valid(session->state) ||
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
    bool available;
    if (!session || !session->graph || !state_valid(session->state))
        return KILIX_STORY_INVALID_ARGUMENT;
    if (!session->active || !session->node)
        return KILIX_STORY_NOT_ACTIVE;
    if (choice_index >= session->node->choice_count)
        return KILIX_STORY_CHOICE_UNAVAILABLE;
    choice = &session->node->choices[choice_index];
    result = kilix_story_conditions_all(
        session->state, choice->conditions,
        choice->condition_count, &available);
    if (result != KILIX_STORY_OK) return result;
    if (!available) return KILIX_STORY_CHOICE_UNAVAILABLE;
    if (choice->next_node != KILIX_STORY_END) {
        next = kilix_story_find_node(
            session->graph, choice->next_node);
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
    case KILIX_STORY_NOT_ACTIVE:
        return "story session is not active";
    default: return "unknown story result";
    }
}
