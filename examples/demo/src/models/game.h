#ifndef CYCLONE_DEMO_GAME_H
#define CYCLONE_DEMO_GAME_H

#include "cyclone.h"

#include "../generated/runtime.h"

#include <stdbool.h>
#include <stdint.h>

CYCLONE_MODEL
CYCLONE_CODEC("state", "input")
struct Vector3 {
    CYCLONE_FIELD(f32)
    CYCLONE_CODEC("state", "input")
    float x;

    CYCLONE_FIELD(f32)
    CYCLONE_CODEC("state", "input")
    float y;

    CYCLONE_FIELD(f32)
    CYCLONE_CODEC("state", "input")
    float z;
};

CYCLONE_MODEL
CYCLONE_CODEC("state")
struct GameMessage {
    CYCLONE_FIELD(u32)
    CYCLONE_CODEC("state")
    uint32_t player_id;

    CYCLONE_FIELD(string)
    CYCLONE_CODEC("state")
    const char *player_name;

    CYCLONE_FIELD(Vector3)
    CYCLONE_CODEC("state")
    struct Vector3 position;

    CYCLONE_FIELD(u32)
    CYCLONE_CODEC("state")
    uint32_t health;

    CYCLONE_FIELD(bool)
    CYCLONE_CODEC("state")
    bool is_alive;

    uint64_t last_seen_at;
};

CYCLONE_MODEL
CYCLONE_CODEC("input")
struct PlayerInput {
    CYCLONE_FIELD(u64)
    CYCLONE_CODEC("input")
    uint64_t tick;

    CYCLONE_FIELD(Vector3)
    CYCLONE_CODEC("input")
    struct Vector3 direction;

    CYCLONE_FIELD(bool)
    CYCLONE_CODEC("input")
    bool firing;
};

#endif /* CYCLONE_DEMO_GAME_H */
