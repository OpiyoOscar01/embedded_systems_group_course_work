/**
 * @file door_fsm.c
 * @brief Door lock timing state machine (pure logic).
 * @author Opiyo Oscar
 * @requirements FR-ACC-03, FR-ACC-04
 */

#include "door_fsm.h"

#include "cocis_types.h"

void cocis_door_fsm_init(cocis_door_fsm_t *door, uint32_t open_ms)
{
    if (door == NULL) {
        return;
    }
    door->open = false;
    door->opened_at_ms = 0;
    door->open_ms = (open_ms == 0) ? COCIS_DOOR_OPEN_MS : open_ms;
}

bool cocis_door_fsm_request(cocis_door_fsm_t *door, uint32_t now_ms)
{
    if (door == NULL || door->open) {
        return false;
    }
    door->open = true;
    door->opened_at_ms = now_ms;
    return true;
}

bool cocis_door_fsm_tick(cocis_door_fsm_t *door, uint32_t now_ms)
{
    if (door == NULL || !door->open) {
        return false;
    }

    /* Unsigned subtraction handles the 32-bit millisecond wrap correctly. */
    uint32_t elapsed = now_ms - door->opened_at_ms;
    if (elapsed < door->open_ms) {
        return false;
    }

    door->open = false;
    return true;
}

bool cocis_door_fsm_is_open(const cocis_door_fsm_t *door)
{
    return door != NULL && door->open;
}
