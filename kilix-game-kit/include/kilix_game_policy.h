#ifndef KILIX_GAME_POLICY_H
#define KILIX_GAME_POLICY_H

/* Tiny dense policy networks for game agents.
 *
 * A policy is a fixed multilayer perceptron: ReLU on every hidden layer, a
 * linear output layer, float32 parameters. Games use one to pick an action
 * from a small feature vector each simulation step, so the contract is
 * bounded and allocation-free after load:
 *
 *   - at most KILIX_POLICY_MAX_LAYERS weight layers, each at most
 *     KILIX_POLICY_MAX_WIDTH wide;
 *   - evaluation order is fixed (row-major dot products, ascending index), so
 *     one binary replays one policy bit-identically;
 *   - load rejects truncation, trailing bytes, foreign magic, unknown
 *     versions, impossible shapes, non-finite parameters and digest
 *     mismatches instead of running a damaged network.
 *
 * Blob format, version 1, all integers and floats little-endian:
 *
 *   offset  size  field
 *   0       8     magic "KXPOLICY"
 *   8       4     version (1)
 *   12      4     layer count L (1..KILIX_POLICY_MAX_LAYERS)
 *   16      4*(L+1) widths: inputs, hidden..., outputs
 *   ..      4     calibration temperature (float32, > 0; 1 = raw logits)
 *   ..      ...   per layer: weights [out][in] then biases [out], float32
 *   end-8   8     FNV-1a-64 over every preceding byte
 *
 * tools/kilix_policy.py writes, verifies and embeds this format.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIX_POLICY_MAX_LAYERS 8u
#define KILIX_POLICY_MAX_WIDTH 256u
#define KILIX_POLICY_VERSION 1u

typedef enum kilix_policy_status {
    KILIX_POLICY_OK = 0,
    KILIX_POLICY_ERR_ARGUMENT,
    KILIX_POLICY_ERR_TRUNCATED,
    KILIX_POLICY_ERR_MAGIC,
    KILIX_POLICY_ERR_VERSION,
    KILIX_POLICY_ERR_SHAPE,
    KILIX_POLICY_ERR_SIZE,
    KILIX_POLICY_ERR_NONFINITE,
    KILIX_POLICY_ERR_DIGEST,
    KILIX_POLICY_ERR_MEMORY
} kilix_policy_status;

typedef struct kilix_policy {
    uint32_t layer_count;
    uint32_t widths[KILIX_POLICY_MAX_LAYERS + 1u];
    float temperature;
    uint64_t digest;          /* the blob's stored (and verified) digest */
    size_t parameter_count;
    float *parameters;        /* owned; decoded host-order floats */
} kilix_policy;

/* Decodes and validates a blob into an owned copy. On failure the policy is
 * left zeroed and nothing is allocated. */
kilix_policy_status kilix_policy_load(kilix_policy *policy,
                                      const uint8_t *blob, size_t size);
void kilix_policy_free(kilix_policy *policy);

size_t kilix_policy_input_count(const kilix_policy *policy);
size_t kilix_policy_output_count(const kilix_policy *policy);

/* Writes output_count logits. input_count and output_count must match the
 * policy exactly. */
kilix_policy_status kilix_policy_forward(const kilix_policy *policy,
                                         const float *inputs,
                                         size_t input_count,
                                         float *outputs,
                                         size_t output_count);

/* First index of the largest value; 0 for an empty or NULL array. */
size_t kilix_policy_argmax(const float *values, size_t count);

/* probabilities[i] = softmax(logits / temperature); temperature <= 0 or
 * non-finite uses 1. Returns false for bad arguments. */
bool kilix_policy_softmax(const float *logits, size_t count,
                          float temperature, float *probabilities);

/* FNV-1a-64, exposed so tools and tests share the blob digest. */
uint64_t kilix_policy_fnv1a64(const uint8_t *bytes, size_t size);

const char *kilix_policy_status_string(kilix_policy_status status);

#ifdef __cplusplus
}
#endif

#endif
