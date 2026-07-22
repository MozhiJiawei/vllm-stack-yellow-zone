/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
* ubs-virt-enpu is licensed under Mulan PSL v2.
*/
#include <acl/acl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "core_limiter.h"
#include "log.h"
#include "runtime_hook.h"
static int det_lock(void)
{
    int rc = pthread_mutex_lock(&g_vnpu_sched_context->det_mutex);
    if (rc == EOWNERDEAD) {
        atomic_store(&g_vnpu_sched_context->det_state, DET_STATE_FAILED);
        pthread_mutex_consistent(&g_vnpu_sched_context->det_mutex);
        return 0;
    }
    return rc;
}
static void det_fail_locked(const char *reason)
{
    LOG_ERROR("Deterministic scheduler failed: %s", reason);
    atomic_store(&g_vnpu_sched_context->det_state, DET_STATE_FAILED);
}
static int det_slot(int vnpu_id)
{
    for (int i = 0; i < DET_SCHED_PARTICIPANTS; ++i) {
        if (atomic_load(&g_vnpu_sched_context->det_participants[i]) == vnpu_id) return i;
    }
    return -1;
}
static int gcd(int a, int b)
{
    while (b != 0) {
        int next = a % b;
        a = b; b = next;
    }
    return a;
}
int det_sched_weighted_owner(int a, int b, int a_quota, int b_quota, uint64_t turn)
{
    if (a_quota <= 0 || b_quota <= 0) {
        return DET_SCHED_NO_OWNER;
    }
    int divisor = gcd(a_quota, b_quota);
    int a_weight = a_quota / divisor;
    int cycle = a_weight + b_quota / divisor;
    return (int)(turn % (uint64_t)cycle) < a_weight ? a : b;
}
static void decide_locked(void)
{
    if (atomic_load(&g_vnpu_sched_context->det_state) != DET_STATE_PARK) {
        return;
    }
    int a = g_vnpu_sched_context->det_snapshot[0];
    int b = g_vnpu_sched_context->det_snapshot[1];
    if (a == DET_SNAPSHOT_UNKNOWN || b == DET_SNAPSHOT_UNKNOWN) {
        return;
    }
    if (a == DET_SNAPSHOT_IDLE && b == DET_SNAPSHOT_IDLE) {
        g_vnpu_sched_context->det_snapshot[0] = g_vnpu_sched_context->det_snapshot[1] = DET_SNAPSHOT_UNKNOWN;
        return;
    }
    int winner;
    if (a != b) {
        winner = a == DET_SNAPSHOT_READY ? 0 : 1;
    } else {
        int a_id = atomic_load(&g_vnpu_sched_context->det_participants[0]);
        int b_id = atomic_load(&g_vnpu_sched_context->det_participants[1]);
        uint64_t turn = g_vnpu_sched_context->det_weighted_turn++;
        int owner = det_sched_weighted_owner(
            a_id, b_id,
            atomic_load(&g_vnpu_sched_context->vnpu_core_limit_quota[a_id]),
            atomic_load(&g_vnpu_sched_context->vnpu_core_limit_quota[b_id]), turn);
        if (owner == DET_SCHED_NO_OWNER) {
            det_fail_locked("participant quota is zero");
            return;
        }
        winner = owner == a_id ? 0 : 1;
    }

    int loser = 1 - winner;
    g_vnpu_sched_context->det_snapshot[winner] = DET_SNAPSHOT_UNKNOWN;
    if (g_vnpu_sched_context->det_snapshot[loser] == DET_SNAPSHOT_IDLE) {
        g_vnpu_sched_context->det_snapshot[loser] = DET_SNAPSHOT_UNKNOWN;
    }
    atomic_store(&g_vnpu_sched_context->det_state, winner == 0 ? DET_STATE_GRANTED_0 : DET_STATE_GRANTED_1);
}
static int register_locked(int vnpu_id)
{
    int slot = det_slot(vnpu_id);
    if (slot >= 0) {
        return slot;
    }
    int a = atomic_load(&g_vnpu_sched_context->det_participants[0]);
    int b = atomic_load(&g_vnpu_sched_context->det_participants[1]);
    if (a < 0) {
        atomic_store(&g_vnpu_sched_context->det_participants[0], vnpu_id);
        return 0;
    }
    if (b >= 0) {
        det_fail_locked("more than two participants");
        return -1;
    }
    if (vnpu_id < a) {
        int old_snapshot = g_vnpu_sched_context->det_snapshot[0];
        atomic_store(&g_vnpu_sched_context->det_participants[0], vnpu_id);
        atomic_store(&g_vnpu_sched_context->det_participants[1], a);
        g_vnpu_sched_context->det_snapshot[0] = DET_SNAPSHOT_UNKNOWN;
        g_vnpu_sched_context->det_snapshot[1] = old_snapshot;
        return 0;
    }
    atomic_store(&g_vnpu_sched_context->det_participants[1], vnpu_id);
    return 1;
}
void det_sched_init(pthread_mutexattr_t *attr)
{
    pthread_mutex_init(&g_vnpu_sched_context->det_mutex, attr);
    for (int i = 0; i < DET_SCHED_PARTICIPANTS; ++i) {
        atomic_store(&g_vnpu_sched_context->det_participants[i], -1);
        g_vnpu_sched_context->det_snapshot[i] = DET_SNAPSHOT_UNKNOWN;
    }
    g_vnpu_sched_context->det_weighted_turn = 0;
    atomic_store(&g_vnpu_sched_context->det_state, DET_STATE_DISABLED);
}
bool det_sched_requested(void) {
    return g_vnpu_sched_context != NULL && atomic_load(&g_vnpu_sched_context->det_state) != DET_STATE_DISABLED;
}
bool det_sched_has_lease(void) {
    int slot = det_slot(g_vnpu_id);
    int state = atomic_load(&g_vnpu_sched_context->det_state);
    return (slot == 0 && state == DET_STATE_RUNNING_0) || (slot == 1 && state == DET_STATE_RUNNING_1);
}
void det_sched_fail_if_participant_lost(void) {
    if (!det_sched_requested() || atomic_load(&g_vnpu_sched_context->det_state) == DET_STATE_FAILED) {
        return;
    }
    int a = atomic_load(&g_vnpu_sched_context->det_participants[0]);
    int b = atomic_load(&g_vnpu_sched_context->det_participants[1]);
    if ((a >= 0 && !is_vnpu_alive(a)) || (b >= 0 && !is_vnpu_alive(b))) {
        if (det_lock() == 0) {
            det_fail_locked("participant exited");
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
        }
    }
}
RUNTIME_HOOK_DEFINE(rtDetSchedEnter, bool ready, bool *enabled)
{
    if (enabled == NULL || g_vnpu_sched_context == NULL) {
        return ACL_ERROR_FAILURE;
    }
    if (get_sched_policy() != SCHED_POLICY_ELASTIC) {
        *enabled = false;
        return ACL_RT_SUCCESS;
    }
    *enabled = true;
    int desired = ready ? DET_SNAPSHOT_READY : DET_SNAPSHOT_IDLE;
    while (!g_terminate) {
        if (det_lock() != 0) {
            return ACL_ERROR_FAILURE;
        }
        int state = atomic_load(&g_vnpu_sched_context->det_state);
        if (state == DET_STATE_FAILED) {
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
            return ACL_ERROR_FAILURE;
        }
        if (state == DET_STATE_DISABLED) {
            atomic_store(&g_vnpu_sched_context->det_state, DET_STATE_PARK);
        }
        int slot = register_locked(g_vnpu_id);
        state = atomic_load(&g_vnpu_sched_context->det_state);
        if (slot < 0 || state == DET_STATE_FAILED) {
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
            return ACL_ERROR_FAILURE;
        }
        if (ready && state == (slot == 0 ? DET_STATE_GRANTED_0 : DET_STATE_GRANTED_1)) {
            atomic_store(&g_vnpu_sched_context->det_state, slot == 0 ? DET_STATE_RUNNING_0 : DET_STATE_RUNNING_1);
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
            return ACL_RT_SUCCESS;
        }
        int current = g_vnpu_sched_context->det_snapshot[slot];
        if (current == DET_SNAPSHOT_UNKNOWN) {
            g_vnpu_sched_context->det_snapshot[slot] = desired;
            decide_locked();
        } else if (current != desired) {
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
            ns_sleep(WAITING_SLEEP_PERIOD);
            continue;
        }
        if (!ready) {
            pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
            return ACL_RT_SUCCESS;
        }
        pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
        ns_sleep(WAITING_SLEEP_PERIOD);
    }
    return ACL_ERROR_FAILURE;
}
RUNTIME_HOOK_DEFINE(rtDetSchedEnd)
{
    if (!det_sched_requested()) {
        return ACL_ERROR_FAILURE;
    }
    atomic_store(&g_sched_locking, true);
    int gate_rc = pthread_mutex_lock(&g_sched_mutex);
    atomic_store(&g_sched_locking, false);
    if (gate_rc != 0) {
        return ACL_ERROR_FAILURE;
    }
    if (det_lock() != 0) {
        pthread_mutex_unlock(&g_sched_mutex);
        return ACL_ERROR_FAILURE;
    }
    if (!det_sched_has_lease()) {
        pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
        pthread_mutex_unlock(&g_sched_mutex);
        return ACL_ERROR_FAILURE;
    }
    int sync_rc = synchronize_and_clear_streams_checked();
    if (sync_rc == ACL_RT_SUCCESS) {
        atomic_store(&g_vnpu_sched_context->det_state, DET_STATE_PARK);
    } else {
        det_fail_locked("device fence failed");
    }
    pthread_mutex_unlock(&g_vnpu_sched_context->det_mutex);
    pthread_mutex_unlock(&g_sched_mutex);
    return sync_rc == ACL_RT_SUCCESS ? ACL_RT_SUCCESS : ACL_ERROR_FAILURE;
}
