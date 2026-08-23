#ifndef FOMOXA_DEMO_GAME_H
#define FOMOXA_DEMO_GAME_H

#include "fomoxa.h"

#include "../generated/runtime.h"

#include <stdbool.h>
#include <stdint.h>

FOMOXA_MODEL
FOMOXA_CODEC("state", "input")
struct Vector3 {
    FOMOXA_FIELD(f32)
    FOMOXA_CODEC("state", "input")
    float x;

    FOMOXA_FIELD(f32)
    FOMOXA_CODEC("state", "input")
    float y;

    FOMOXA_FIELD(f32)
    FOMOXA_CODEC("state", "input")
    float z;
};

FOMOXA_MODEL
FOMOXA_CODEC("state")
struct GameMessage {
    FOMOXA_FIELD(u32)
    FOMOXA_CODEC("state")
    uint32_t player_id;

    FOMOXA_FIELD(string)
    FOMOXA_CODEC("state")
    const char *player_name;

    FOMOXA_FIELD(Vector3)
    FOMOXA_CODEC("state")
    struct Vector3 position;

    FOMOXA_FIELD(u32)
    FOMOXA_CODEC("state")
    uint32_t health;

    FOMOXA_FIELD(bool)
    FOMOXA_CODEC("state")
    bool is_alive;

    uint64_t last_seen_at;
};

FOMOXA_MODEL
FOMOXA_CODEC("input")
struct PlayerInput {
    FOMOXA_FIELD(u64)
    FOMOXA_CODEC("input")
    uint64_t tick;

    FOMOXA_FIELD(Vector3)
    FOMOXA_CODEC("input")
    struct Vector3 direction;

    FOMOXA_FIELD(bool)
    FOMOXA_CODEC("input")
    bool firing;
};

#endif /* FOMOXA_DEMO_GAME_H */
