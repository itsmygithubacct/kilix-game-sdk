#include "kilix_game_policy.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t policy_magic[8] = {
    'K', 'X', 'P', 'O', 'L', 'I', 'C', 'Y'
};

uint64_t kilix_policy_fnv1a64(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!bytes) return hash;
    for (size_t index = 0; index < size; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) | (uint64_t)read_u32(bytes + 4) << 32;
}

static float read_f32(const uint8_t *bytes)
{
    uint32_t bits = read_u32(bytes);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

kilix_policy_status kilix_policy_load(kilix_policy *policy,
                                      const uint8_t *blob, size_t size)
{
    if (!policy) return KILIX_POLICY_ERR_ARGUMENT;
    memset(policy, 0, sizeof *policy);
    if (!blob) return KILIX_POLICY_ERR_ARGUMENT;
    if (size < 16u) return KILIX_POLICY_ERR_TRUNCATED;
    if (memcmp(blob, policy_magic, sizeof policy_magic) != 0)
        return KILIX_POLICY_ERR_MAGIC;
    if (read_u32(blob + 8) != KILIX_POLICY_VERSION)
        return KILIX_POLICY_ERR_VERSION;

    uint32_t layers = read_u32(blob + 12);
    if (layers == 0u || layers > KILIX_POLICY_MAX_LAYERS)
        return KILIX_POLICY_ERR_SHAPE;
    size_t header = 16u + 4u * ((size_t)layers + 1u) + 4u;
    if (size < header) return KILIX_POLICY_ERR_TRUNCATED;

    uint32_t widths[KILIX_POLICY_MAX_LAYERS + 1u];
    size_t parameters = 0;
    for (uint32_t index = 0; index <= layers; index++) {
        widths[index] = read_u32(blob + 16u + 4u * index);
        if (widths[index] == 0u || widths[index] > KILIX_POLICY_MAX_WIDTH)
            return KILIX_POLICY_ERR_SHAPE;
        if (index > 0u)
            parameters += (size_t)widths[index] * widths[index - 1u] +
                          widths[index];
    }
    /* Widths are bounded, so this cannot overflow size_t. */
    size_t expected = header + parameters * 4u + 8u;
    if (size < expected) return KILIX_POLICY_ERR_TRUNCATED;
    if (size > expected) return KILIX_POLICY_ERR_SIZE;

    float temperature = read_f32(blob + header - 4u);
    if (!isfinite(temperature) || temperature <= 0.0f)
        return KILIX_POLICY_ERR_NONFINITE;
    uint64_t stored = read_u64(blob + expected - 8u);
    if (kilix_policy_fnv1a64(blob, expected - 8u) != stored)
        return KILIX_POLICY_ERR_DIGEST;

    float *values = malloc(parameters * sizeof *values);
    if (!values) return KILIX_POLICY_ERR_MEMORY;
    for (size_t index = 0; index < parameters; index++) {
        values[index] = read_f32(blob + header + 4u * index);
        if (!isfinite(values[index])) {
            free(values);
            return KILIX_POLICY_ERR_NONFINITE;
        }
    }

    policy->layer_count = layers;
    memcpy(policy->widths, widths, sizeof(uint32_t) * ((size_t)layers + 1u));
    policy->temperature = temperature;
    policy->digest = stored;
    policy->parameter_count = parameters;
    policy->parameters = values;
    return KILIX_POLICY_OK;
}

void kilix_policy_free(kilix_policy *policy)
{
    if (!policy) return;
    free(policy->parameters);
    memset(policy, 0, sizeof *policy);
}

size_t kilix_policy_input_count(const kilix_policy *policy)
{
    return policy && policy->parameters ? policy->widths[0] : 0u;
}

size_t kilix_policy_output_count(const kilix_policy *policy)
{
    return policy && policy->parameters ?
           policy->widths[policy->layer_count] : 0u;
}

kilix_policy_status kilix_policy_forward(const kilix_policy *policy,
                                         const float *inputs,
                                         size_t input_count,
                                         float *outputs,
                                         size_t output_count)
{
    if (!policy || !policy->parameters || !inputs || !outputs ||
        input_count != kilix_policy_input_count(policy) ||
        output_count != kilix_policy_output_count(policy))
        return KILIX_POLICY_ERR_ARGUMENT;

    float buffers[2][KILIX_POLICY_MAX_WIDTH];
    const float *in = inputs;
    const float *cursor = policy->parameters;
    for (uint32_t layer = 0; layer < policy->layer_count; layer++) {
        size_t fan_in = policy->widths[layer];
        size_t fan_out = policy->widths[layer + 1u];
        bool last = layer + 1u == policy->layer_count;
        float *out = last ? outputs : buffers[layer & 1u];
        const float *weights = cursor;
        const float *biases = cursor + fan_in * fan_out;
        for (size_t row = 0; row < fan_out; row++) {
            float sum = biases[row];
            const float *w = weights + row * fan_in;
            for (size_t column = 0; column < fan_in; column++)
                sum += w[column] * in[column];
            out[row] = (!last && sum < 0.0f) ? 0.0f : sum;
        }
        cursor = biases + fan_out;
        in = out;
    }
    return KILIX_POLICY_OK;
}

size_t kilix_policy_argmax(const float *values, size_t count)
{
    size_t best = 0;
    if (!values) return 0;
    for (size_t index = 1; index < count; index++)
        if (values[index] > values[best]) best = index;
    return best;
}

bool kilix_policy_softmax(const float *logits, size_t count,
                          float temperature, float *probabilities)
{
    if (!logits || !probabilities || count == 0u) return false;
    if (!isfinite(temperature) || temperature <= 0.0f) temperature = 1.0f;
    float peak = logits[kilix_policy_argmax(logits, count)];
    double total = 0.0;
    for (size_t index = 0; index < count; index++) {
        probabilities[index] = expf((logits[index] - peak) / temperature);
        total += probabilities[index];
    }
    for (size_t index = 0; index < count; index++)
        probabilities[index] = (float)(probabilities[index] / total);
    return true;
}

const char *kilix_policy_status_string(kilix_policy_status status)
{
    switch (status) {
    case KILIX_POLICY_OK: return "ok";
    case KILIX_POLICY_ERR_ARGUMENT: return "invalid argument";
    case KILIX_POLICY_ERR_TRUNCATED: return "truncated policy blob";
    case KILIX_POLICY_ERR_MAGIC: return "not a policy blob";
    case KILIX_POLICY_ERR_VERSION: return "unsupported policy version";
    case KILIX_POLICY_ERR_SHAPE: return "policy shape out of bounds";
    case KILIX_POLICY_ERR_SIZE: return "trailing bytes after policy";
    case KILIX_POLICY_ERR_NONFINITE: return "non-finite policy value";
    case KILIX_POLICY_ERR_DIGEST: return "policy digest mismatch";
    case KILIX_POLICY_ERR_MEMORY: return "out of memory";
    }
    return "unknown policy status";
}
