/*
 * kilix_tactics.h — umbrella header for libkilix-tactics-core.
 *
 * The soft-raster adapter lives in kilix_tactics_soft.h and is deliberately
 * not included here, so a headless or test build never pulls in a renderer.
 */
#ifndef KILIX_TACTICS_H
#define KILIX_TACTICS_H

#include "kilix_tactics_map.h"
#include "kilix_tactics_projection.h"
#include "kilix_tactics_types.h"

#define KILIX_TACTICS_VERSION_MAJOR 0
#define KILIX_TACTICS_VERSION_MINOR 1
#define KILIX_TACTICS_VERSION_PATCH 0

#endif /* KILIX_TACTICS_H */
