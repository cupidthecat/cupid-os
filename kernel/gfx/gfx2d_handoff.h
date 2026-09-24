#ifndef GFX2D_HANDOFF_H
#define GFX2D_HANDOFF_H

#include "types.h"

/*
 * The desktop and fullscreen apps share the VGA back buffer. A fullscreen
 * request closes the desktop gate before it waits for the current desktop
 * writer to finish. State-lock serialization makes gate inspection and owner
 * publication one operation. An existing desktop owner may nest while a
 * fullscreen request waits, but a new desktop owner cannot cross the gate.
 */
typedef struct {
    volatile uint32_t state_lock;
    volatile uint32_t fullscreen_requested;
    uint32_t fullscreen_owner;
    uint32_t fullscreen_depth;
    volatile uint32_t fullscreen_entered;
    volatile uint32_t desktop_owner;
    uint32_t desktop_depth;
} gfx2d_handoff_t;

#define GFX2D_HANDOFF_WRITER_BUSY       0
#define GFX2D_HANDOFF_WRITER_DESKTOP    1
#define GFX2D_HANDOFF_WRITER_FULLSCREEN 2

typedef enum {
    GFX2D_HANDOFF_RELEASE_NON_OWNER = -1,
    GFX2D_HANDOFF_RELEASE_BUSY = 0,
    GFX2D_HANDOFF_RELEASE_NESTED = 1,
    GFX2D_HANDOFF_RELEASE_ENTERED_FINAL = 2,
    GFX2D_HANDOFF_RELEASE_PENDING_FINAL = 3,
} gfx2d_handoff_release_result_t;

static inline int gfx2d_handoff_state_try_lock(gfx2d_handoff_t *handoff) {
    return __atomic_exchange_n(
        &handoff->state_lock,
        1u,
        __ATOMIC_ACQ_REL
    ) == 0u;
}

static inline void gfx2d_handoff_state_unlock(gfx2d_handoff_t *handoff) {
    __atomic_store_n(&handoff->state_lock, 0u, __ATOMIC_RELEASE);
}

static inline int gfx2d_handoff_fullscreen_active(
    const gfx2d_handoff_t *handoff
) {
    return __atomic_load_n(
        &handoff->fullscreen_requested,
        __ATOMIC_ACQUIRE
    ) != 0u;
}

static inline int gfx2d_handoff_fullscreen_entered(
    const gfx2d_handoff_t *handoff
) {
    return __atomic_load_n(
        &handoff->fullscreen_entered,
        __ATOMIC_ACQUIRE
    ) != 0u;
}

/* Returns -1 while busy, otherwise whether owner holds either lease. */
static inline int gfx2d_handoff_owner_has_state(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int has_state;
    if (owner == 0u) return 0;
    if (!gfx2d_handoff_state_try_lock(handoff)) return -1;
    has_state = handoff->fullscreen_owner == owner
        || __atomic_load_n(
            &handoff->desktop_owner,
            __ATOMIC_ACQUIRE
        ) == owner;
    gfx2d_handoff_state_unlock(handoff);
    return has_state;
}

/* Returns -1 while busy, otherwise whether owner holds a desktop lease. */
static inline int gfx2d_handoff_owner_has_desktop(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int has_desktop;
    if (owner == 0u) return 0;
    if (!gfx2d_handoff_state_try_lock(handoff)) return -1;
    has_desktop = __atomic_load_n(
        &handoff->desktop_owner,
        __ATOMIC_ACQUIRE
    ) == owner;
    gfx2d_handoff_state_unlock(handoff);
    return has_desktop;
}

/*
 * Returns BUSY for a conflicting owner, DESKTOP for a new or nested desktop
 * lease, and FULLSCREEN when the caller may borrow its own fullscreen lease.
 */
static inline int gfx2d_handoff_writer_begin(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int acquired = GFX2D_HANDOFF_WRITER_BUSY;
    uint32_t current;
    if (owner == 0u || !gfx2d_handoff_state_try_lock(handoff)) return 0;
    current = __atomic_load_n(&handoff->desktop_owner, __ATOMIC_ACQUIRE);
    if (current == owner && handoff->desktop_depth != 0xFFFFFFFFu) {
        handoff->desktop_depth++;
        acquired = GFX2D_HANDOFF_WRITER_DESKTOP;
    } else if (
        handoff->fullscreen_owner == owner
        && handoff->fullscreen_depth != 0u
        && gfx2d_handoff_fullscreen_entered(handoff)
    ) {
        acquired = GFX2D_HANDOFF_WRITER_FULLSCREEN;
    } else if (current == 0u && !gfx2d_handoff_fullscreen_active(handoff)) {
        handoff->desktop_depth = 1u;
        __atomic_store_n(&handoff->desktop_owner, owner, __ATOMIC_RELEASE);
        acquired = GFX2D_HANDOFF_WRITER_DESKTOP;
    }
    gfx2d_handoff_state_unlock(handoff);
    return acquired;
}

static inline int gfx2d_handoff_desktop_begin(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    return gfx2d_handoff_writer_begin(handoff, owner)
        == GFX2D_HANDOFF_WRITER_DESKTOP;
}

/*
 * Returns zero while the state lock is busy, -1 for a non-owner, 1 while a
 * nested lease remains, and 2 when the final lease is released.
 */
static inline int gfx2d_handoff_desktop_end(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int released = -1;
    if (!gfx2d_handoff_state_try_lock(handoff)) return 0;
    if (owner != 0u
        && __atomic_load_n(
            &handoff->desktop_owner,
            __ATOMIC_ACQUIRE
        ) == owner) {
        if (handoff->desktop_depth > 1u) {
            handoff->desktop_depth--;
            released = 1;
        } else {
            handoff->desktop_depth = 0u;
            __atomic_store_n(&handoff->desktop_owner, 0u, __ATOMIC_RELEASE);
            released = 2;
        }
    }
    gfx2d_handoff_state_unlock(handoff);
    return released;
}

/*
 * Fast owner release for the process that currently holds the desktop lease.
 * A PID executes on only one CPU, so its nesting depth has a single writer.
 * Competing fullscreen requests only observe desktop_owner; publishing zero
 * after the final depth update is therefore sufficient and avoids spinning
 * on state_lock while the process reaper holds the BKL.
 */
static inline int gfx2d_handoff_desktop_end_owned(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    uint32_t current;
    uint32_t depth;
    if (owner == 0u) return -1;
    current = __atomic_load_n(&handoff->desktop_owner, __ATOMIC_ACQUIRE);
    if (current != owner) return -1;
    depth = __atomic_load_n(&handoff->desktop_depth, __ATOMIC_ACQUIRE);
    if (depth > 1u) {
        __atomic_store_n(
            &handoff->desktop_depth,
            depth - 1u,
            __ATOMIC_RELEASE
        );
        return 1;
    }
    if (depth == 1u) {
        __atomic_store_n(&handoff->desktop_depth, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&handoff->desktop_owner, 0u, __ATOMIC_RELEASE);
        return 2;
    }
    return -1;
}

static inline int gfx2d_handoff_try_request_fullscreen(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int acquired = 0;
    if (owner == 0u || !gfx2d_handoff_state_try_lock(handoff)) return 0;
    if (handoff->fullscreen_owner == 0u) {
        handoff->fullscreen_owner = owner;
        handoff->fullscreen_depth = 1u;
        __atomic_store_n(&handoff->fullscreen_entered, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(
            &handoff->fullscreen_requested,
            1u,
            __ATOMIC_RELEASE
        );
        acquired = 1;
    } else if (
        handoff->fullscreen_owner == owner
        && handoff->fullscreen_depth != 0u
        && handoff->fullscreen_depth != 0xFFFFFFFFu
    ) {
        handoff->fullscreen_depth++;
        acquired = 1;
    }
    gfx2d_handoff_state_unlock(handoff);
    return acquired;
}

/* Publishes exclusive render-state ownership after the desktop drains. */
static inline int gfx2d_handoff_try_mark_fullscreen_entered(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int entered = 0;
    if (owner == 0u || !gfx2d_handoff_state_try_lock(handoff)) return 0;
    if (
        handoff->fullscreen_owner == owner
        && handoff->fullscreen_depth != 0u
        && gfx2d_handoff_fullscreen_active(handoff)
        && __atomic_load_n(
            &handoff->desktop_owner,
            __ATOMIC_ACQUIRE
        ) == 0u
    ) {
        __atomic_store_n(&handoff->fullscreen_entered, 1u, __ATOMIC_RELEASE);
        entered = 1;
    }
    gfx2d_handoff_state_unlock(handoff);
    return entered;
}

static inline int gfx2d_handoff_desktop_quiescent(
    const gfx2d_handoff_t *handoff
) {
    return __atomic_load_n(
        &handoff->desktop_owner,
        __ATOMIC_ACQUIRE
    ) == 0u;
}

static inline int gfx2d_handoff_desktop_owned_by_other(
    const gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    uint32_t current = __atomic_load_n(
        &handoff->desktop_owner,
        __ATOMIC_ACQUIRE
    );
    return current != 0u && current != owner;
}

/*
 * A final release keeps the desktop gate closed until cleanup finishes.
 */
static inline int gfx2d_handoff_try_prepare_fullscreen_release(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int released = GFX2D_HANDOFF_RELEASE_NON_OWNER;
    if (!gfx2d_handoff_state_try_lock(handoff))
        return GFX2D_HANDOFF_RELEASE_BUSY;
    if (
        owner != 0u
        && handoff->fullscreen_owner == owner
        && gfx2d_handoff_fullscreen_active(handoff)
    ) {
        if (handoff->fullscreen_depth == 0u) {
            released = gfx2d_handoff_fullscreen_entered(handoff)
                ? GFX2D_HANDOFF_RELEASE_ENTERED_FINAL
                : GFX2D_HANDOFF_RELEASE_PENDING_FINAL;
        } else if (handoff->fullscreen_depth == 1u) {
            handoff->fullscreen_depth = 0u;
            released = gfx2d_handoff_fullscreen_entered(handoff)
                ? GFX2D_HANDOFF_RELEASE_ENTERED_FINAL
                : GFX2D_HANDOFF_RELEASE_PENDING_FINAL;
        } else {
            handoff->fullscreen_depth--;
            released = GFX2D_HANDOFF_RELEASE_NESTED;
        }
    }
    gfx2d_handoff_state_unlock(handoff);
    return released;
}

/* Opens the desktop gate after a prepared final release has restored state. */
static inline int gfx2d_handoff_try_finish_fullscreen_release(
    gfx2d_handoff_t *handoff,
    uint32_t owner
) {
    int released = GFX2D_HANDOFF_RELEASE_NON_OWNER;
    if (!gfx2d_handoff_state_try_lock(handoff))
        return GFX2D_HANDOFF_RELEASE_BUSY;
    if (
        owner != 0u
        && handoff->fullscreen_owner == owner
        && handoff->fullscreen_depth == 0u
        && gfx2d_handoff_fullscreen_active(handoff)
    ) {
        handoff->fullscreen_owner = 0u;
        __atomic_store_n(&handoff->fullscreen_entered, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(
            &handoff->fullscreen_requested,
            0u,
            __ATOMIC_RELEASE
        );
        released = GFX2D_HANDOFF_RELEASE_ENTERED_FINAL;
    }
    gfx2d_handoff_state_unlock(handoff);
    return released;
}

#endif
