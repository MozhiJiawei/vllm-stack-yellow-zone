#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PS_MAGIC UINT64_C(0x5650534348454433)
#define PS_VERSION 3U
#define PS_INSTANCES 2
#define PS_MAX_WORKERS 64
#define PS_NO_OWNER (-1)
#define PS_SNAPSHOT_COUNT (16 + PS_INSTANCES * 10 + PS_INSTANCES * PS_MAX_WORKERS * 4)

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "pair scheduler requires lock-free interprocess int atomics");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2,
               "pair scheduler requires lock-free interprocess 64-bit atomics");

enum global_state {
    PS_INITIALIZING = 0,
    PS_RUNNING = 1,
    PS_FAILED = 2,
    PS_SHUTDOWN = 3,
};

enum round_state {
    ROUND_OFFLINE = 0,
    ROUND_IDLE = 1,
    ROUND_INITIALIZING = 2,
    ROUND_COLLECTING = 3,
    ROUND_READY = 4,
    ROUND_RUNNING = 5,
    ROUND_COMPLETE = 6,
    ROUND_DRAINING = 7,
};

enum failure_reason {
    FAIL_GENERIC = 1,
    FAIL_FORWARD_TIMEOUT = 102,
    FAIL_PRIMARY_DEAD = 103,
    FAIL_WORKER_DEAD = 104,
    FAIL_CORRUPT_OWNER = 108,
    FAIL_CORRUPT_STATE = 109,
    FAIL_FENCING = 110,
    FAIL_DUPLICATE_RANK = 111,
    FAIL_BARRIER_TIMEOUT = 112,
    FAIL_CLOSE_ACTIVE = 113,
};

typedef struct {
    _Atomic int pid;
    _Atomic uint64_t session;
    _Atomic uint64_t last_seq;
    _Atomic uint64_t heartbeat_ns;
} worker_slot;

typedef struct {
    _Atomic int state;
    _Atomic int error;
    _Atomic uint64_t forward_seq;
    _Atomic uint64_t grant_id;
    _Atomic uint64_t ready_mask;
    _Atomic uint64_t complete_mask;
    _Atomic uint64_t exit_mask;
    _Atomic uint64_t online_mask;
    uint64_t expected_mask;
    _Atomic uint64_t publish_ns;
    worker_slot workers[PS_MAX_WORKERS];
} instance_group;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t epoch;
    uint32_t heartbeat_ms;
    uint32_t peer_timeout_ms;
    uint32_t forward_timeout_ms;
    uint32_t worker_count;
    _Atomic uint64_t status;
    _Atomic int owner;
    _Atomic int last_owner;
    _Atomic uint64_t deadline_ns;
    _Atomic uint64_t primary_heartbeat_ns;
    _Atomic uint64_t next_grant_id;
    _Atomic uint32_t coordinator_wake;
    _Atomic uint32_t group_wake[PS_INSTANCES];
    instance_group groups[PS_INSTANCES];
} shared_state;

typedef struct {
    int fd;
    int is_coordinator;
    int instance;
    int worker_rank;
    uint32_t worker_count;
    uint64_t session;
    uint64_t next_seq;
    uint64_t active_seq;
    uint64_t active_grant_id;
    uint32_t heartbeat_ms;
    uint32_t peer_timeout_ms;
    uint32_t forward_timeout_ms;
    _Atomic bool stopping;
    _Atomic int active_calls;
    _Atomic uint32_t call_wake;
    shared_state *shared;
    pthread_t heartbeat_thread;
    pthread_t coordinator_thread;
    bool heartbeat_started;
    bool coordinator_started;
} ps_context;

static uint64_t make_status(int state, int reason)
{
    return ((uint64_t)(uint32_t)state << 32) | (uint32_t)reason;
}

static int status_state(uint64_t status)
{
    return (int)(uint32_t)(status >> 32);
}

static int status_reason(uint64_t status)
{
    return (int)(uint32_t)status;
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void set_error(char *buffer, size_t size, const char *format, ...)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, size, format, args);
    va_end(args);
}

static int futex_wait_word(_Atomic uint32_t *word, uint32_t expected,
                           uint32_t timeout_ms)
{
    struct timespec timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_nsec = (long)(timeout_ms % 1000) * 1000000L,
    };
    return (int)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAIT, expected,
                        &timeout, NULL, 0);
}

static void futex_wake_all(_Atomic uint32_t *word)
{
    syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE, INT32_MAX, NULL, NULL, 0);
}

static void signal_word(_Atomic uint32_t *word)
{
    atomic_fetch_add_explicit(word, 1, memory_order_release);
    futex_wake_all(word);
}

static bool heartbeat_expired(uint64_t heartbeat, uint64_t now,
                              uint32_t timeout_ms)
{
    return heartbeat == 0 ||
           (now > heartbeat &&
            now - heartbeat > (uint64_t)timeout_ms * UINT64_C(1000000));
}

static uint64_t load_status(shared_state *shared)
{
    return atomic_load_explicit(&shared->status, memory_order_acquire);
}

static bool valid_round_state(int state)
{
    return state >= ROUND_OFFLINE && state <= ROUND_DRAINING;
}

static void wake_everyone(shared_state *shared)
{
    signal_word(&shared->coordinator_wake);
    for (int i = 0; i < PS_INSTANCES; ++i) {
        signal_word(&shared->group_wake[i]);
    }
}

static void fail_pair(shared_state *shared, int reason)
{
    uint64_t expected = make_status(PS_RUNNING, 0);
    int effective_reason = reason == 0 ? FAIL_GENERIC : reason;
    if (atomic_compare_exchange_strong_explicit(
            &shared->status, &expected,
            make_status(PS_FAILED, effective_reason),
            memory_order_acq_rel, memory_order_acquire)) {
        fprintf(stderr,
                "{\"component\":\"vllm-pair-scheduler\",\"event\":\"failed\","
                "\"protocol\":3,\"epoch\":%llu,\"reason\":%d}\n",
                (unsigned long long)shared->epoch, effective_reason);
    }
    wake_everyone(shared);
}

static bool begin_call(ps_context *ctx, char *error, size_t error_size)
{
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        set_error(error, error_size, "scheduler context is closing");
        return false;
    }
    atomic_fetch_add_explicit(&ctx->active_calls, 1, memory_order_acq_rel);
    if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&ctx->active_calls, 1, memory_order_acq_rel);
        signal_word(&ctx->call_wake);
        set_error(error, error_size, "scheduler context is closing");
        return false;
    }
    return true;
}

static void end_call(ps_context *ctx)
{
    atomic_fetch_sub_explicit(&ctx->active_calls, 1, memory_order_acq_rel);
    signal_word(&ctx->call_wake);
}

static int check_running(shared_state *shared, char *error, size_t error_size)
{
    uint64_t status = load_status(shared);
    int state = status_state(status);
    if (state != PS_RUNNING) {
        set_error(error, error_size, "scheduler unavailable (state=%d reason=%d)",
                  state, status_reason(status));
        return -1;
    }
    return 0;
}

static bool worker_alive(worker_slot *slot, uint64_t now, uint32_t timeout_ms)
{
    int pid = atomic_load_explicit(&slot->pid, memory_order_acquire);
    uint64_t session =
        atomic_load_explicit(&slot->session, memory_order_acquire);
    uint64_t heartbeat =
        atomic_load_explicit(&slot->heartbeat_ns, memory_order_acquire);
    return pid > 0 && session != 0 &&
           !heartbeat_expired(heartbeat, now, timeout_ms);
}

static bool group_workers_alive(shared_state *shared, int instance,
                                uint64_t mask, uint64_t now)
{
    instance_group *group = &shared->groups[instance];
    for (uint32_t rank = 0; rank < shared->worker_count; ++rank) {
        uint64_t bit = UINT64_C(1) << rank;
        if ((mask & bit) != 0 &&
            !worker_alive(&group->workers[rank], now,
                          shared->peer_timeout_ms)) {
            return false;
        }
    }
    return true;
}

static bool validate_shared(shared_state *shared)
{
    int owner = atomic_load_explicit(&shared->owner, memory_order_acquire);
    if (owner != PS_NO_OWNER && (owner < 0 || owner >= PS_INSTANCES)) {
        fail_pair(shared, FAIL_CORRUPT_OWNER);
        return false;
    }
    for (int i = 0; i < PS_INSTANCES; ++i) {
        instance_group *group = &shared->groups[i];
        int state = atomic_load_explicit(&group->state, memory_order_acquire);
        uint64_t ready =
            atomic_load_explicit(&group->ready_mask, memory_order_acquire);
        uint64_t complete =
            atomic_load_explicit(&group->complete_mask, memory_order_acquire);
        uint64_t exited =
            atomic_load_explicit(&group->exit_mask, memory_order_acquire);
        uint64_t online =
            atomic_load_explicit(&group->online_mask, memory_order_acquire);
        uint64_t valid = group->expected_mask;
        if (!valid_round_state(state) || (ready & ~valid) != 0 ||
            (complete & ~valid) != 0 || (exited & ~valid) != 0 ||
            (online & ~valid) != 0) {
            fail_pair(shared, FAIL_CORRUPT_STATE);
            return false;
        }
    }
    return true;
}

static void recycle_complete(shared_state *shared, int owner, uint64_t now)
{
    instance_group *group = &shared->groups[owner];
    int state = atomic_load_explicit(&group->state, memory_order_acquire);
    uint64_t complete =
        atomic_load_explicit(&group->complete_mask, memory_order_acquire);
    uint64_t deadline =
        atomic_load_explicit(&shared->deadline_ns, memory_order_acquire);

    if (deadline != 0 && now > deadline) {
        fail_pair(shared, FAIL_FORWARD_TIMEOUT);
        return;
    }
    if (state != ROUND_COMPLETE || complete != group->expected_mask) {
        if ((state == ROUND_RUNNING || state == ROUND_COMPLETE) &&
            !group_workers_alive(shared, owner, group->expected_mask, now)) {
            fail_pair(shared, FAIL_WORKER_DEAD);
        }
        return;
    }

    atomic_store_explicit(&group->exit_mask, 0, memory_order_relaxed);
    atomic_store_explicit(&group->state, ROUND_DRAINING, memory_order_release);
    atomic_store_explicit(&shared->last_owner, owner, memory_order_relaxed);
    atomic_store_explicit(&shared->deadline_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&shared->owner, PS_NO_OWNER, memory_order_release);
    signal_word(&shared->group_wake[owner]);
    signal_word(&shared->coordinator_wake);
}

static void dispatch_next(shared_state *shared, uint64_t now)
{
    if (atomic_load_explicit(&shared->owner, memory_order_acquire) !=
        PS_NO_OWNER) {
        return;
    }
    bool ready[PS_INSTANCES] = {false, false};
    uint64_t published[PS_INSTANCES] = {0, 0};
    for (int i = 0; i < PS_INSTANCES; ++i) {
        instance_group *group = &shared->groups[i];
        int state = atomic_load_explicit(&group->state, memory_order_acquire);
        uint64_t ready_mask =
            atomic_load_explicit(&group->ready_mask, memory_order_acquire);
        uint64_t online_mask =
            atomic_load_explicit(&group->online_mask, memory_order_acquire);
        uint64_t publish =
            atomic_load_explicit(&group->publish_ns, memory_order_acquire);

        if ((state == ROUND_COLLECTING || state == ROUND_READY) &&
            publish != 0 &&
            now > publish &&
            now - publish >
                (uint64_t)shared->peer_timeout_ms * UINT64_C(1000000)) {
            if (ready_mask != group->expected_mask ||
                online_mask != group->expected_mask ||
                !group_workers_alive(shared, i, ready_mask, now)) {
                fail_pair(shared, FAIL_BARRIER_TIMEOUT);
                return;
            }
        }
        ready[i] = state == ROUND_READY &&
                   ready_mask == group->expected_mask &&
                   online_mask == group->expected_mask &&
                   group_workers_alive(shared, i, group->expected_mask, now);
        published[i] = publish;
    }
    if (!ready[0] && !ready[1]) {
        return;
    }

    int selected;
    if (ready[0] && ready[1]) {
        if (published[0] < published[1]) {
            selected = 0;
        } else if (published[1] < published[0]) {
            selected = 1;
        } else {
            int last =
                atomic_load_explicit(&shared->last_owner, memory_order_acquire);
            selected = last == 0 ? 1 : 0;
        }
    } else {
        selected = ready[0] ? 0 : 1;
    }

    instance_group *group = &shared->groups[selected];
    int expected_state = ROUND_READY;
    if (!atomic_compare_exchange_strong_explicit(
            &group->state, &expected_state, ROUND_RUNNING,
            memory_order_acq_rel, memory_order_acquire)) {
        return;
    }
    uint64_t grant =
        atomic_fetch_add_explicit(&shared->next_grant_id, 1,
                                  memory_order_acq_rel) +
        1;
    atomic_store_explicit(&group->grant_id, grant, memory_order_relaxed);
    atomic_store_explicit(
        &shared->deadline_ns,
        now + (uint64_t)shared->forward_timeout_ms * UINT64_C(1000000),
        memory_order_relaxed);
    atomic_store_explicit(&shared->owner, selected, memory_order_release);
    signal_word(&shared->group_wake[selected]);
}

static void coordinator_tick(shared_state *shared)
{
    if (status_state(load_status(shared)) != PS_RUNNING ||
        !validate_shared(shared)) {
        return;
    }
    uint64_t now = monotonic_ns();
    atomic_store_explicit(&shared->primary_heartbeat_ns, now,
                          memory_order_release);
    int owner = atomic_load_explicit(&shared->owner, memory_order_acquire);
    if (owner != PS_NO_OWNER) {
        recycle_complete(shared, owner, now);
    }
    if (status_state(load_status(shared)) == PS_RUNNING &&
        atomic_load_explicit(&shared->owner, memory_order_acquire) ==
            PS_NO_OWNER) {
        dispatch_next(shared, now);
    }
}

static void *coordinator_main(void *opaque)
{
    ps_context *ctx = opaque;
    shared_state *shared = ctx->shared;
    while (!atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        uint32_t wake =
            atomic_load_explicit(&shared->coordinator_wake,
                                 memory_order_acquire);
        coordinator_tick(shared);
        futex_wait_word(&shared->coordinator_wake, wake,
                        ctx->heartbeat_ms);
    }
    return NULL;
}

static void *heartbeat_main(void *opaque)
{
    ps_context *ctx = opaque;
    worker_slot *slot =
        &ctx->shared->groups[ctx->instance].workers[ctx->worker_rank];
    struct timespec pause = {
        .tv_sec = ctx->heartbeat_ms / 1000,
        .tv_nsec = (long)(ctx->heartbeat_ms % 1000) * 1000000L,
    };
    while (!atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        atomic_store_explicit(&slot->heartbeat_ns, monotonic_ns(),
                              memory_order_release);
        nanosleep(&pause, NULL);
    }
    return NULL;
}

static int validate_layout(shared_state *shared, uint64_t epoch,
                           uint32_t heartbeat_ms, uint32_t peer_timeout_ms,
                           uint32_t forward_timeout_ms,
                           uint32_t worker_count,
                           char *error, size_t error_size)
{
    if (shared->magic != PS_MAGIC || shared->version != PS_VERSION ||
        shared->size != sizeof(*shared)) {
        set_error(error, error_size,
                  "shared-memory protocol mismatch (need v3)");
        return -1;
    }
    if (shared->epoch != epoch) {
        set_error(error, error_size, "shared-memory epoch mismatch");
        return -1;
    }
    if (shared->heartbeat_ms != heartbeat_ms ||
        shared->peer_timeout_ms != peer_timeout_ms ||
        shared->forward_timeout_ms != forward_timeout_ms ||
        shared->worker_count != worker_count) {
        set_error(error, error_size,
                  "shared-memory configuration mismatch");
        return -1;
    }
    return 0;
}

static int register_worker(ps_context *ctx, char *error, size_t error_size)
{
    shared_state *shared = ctx->shared;
    instance_group *group = &shared->groups[ctx->instance];
    worker_slot *slot = &group->workers[ctx->worker_rank];
    uint64_t now = monotonic_ns();
    uint64_t old_session =
        atomic_load_explicit(&slot->session, memory_order_acquire);
    uint64_t old_heartbeat =
        atomic_load_explicit(&slot->heartbeat_ns, memory_order_acquire);
    if (old_session != 0 &&
        !heartbeat_expired(old_heartbeat, now, ctx->peer_timeout_ms)) {
        set_error(error, error_size,
                  "worker rank %d is already active for instance %c",
                  ctx->worker_rank, 'A' + ctx->instance);
        return -1;
    }

    atomic_store_explicit(&slot->pid, (int)getpid(), memory_order_relaxed);
    atomic_store_explicit(&slot->last_seq, 0, memory_order_relaxed);
    atomic_store_explicit(&slot->heartbeat_ns, now, memory_order_relaxed);
    atomic_store_explicit(&slot->session, ctx->session, memory_order_release);
    uint64_t bit = UINT64_C(1) << ctx->worker_rank;
    atomic_fetch_or_explicit(&group->online_mask, bit, memory_order_acq_rel);
    int state = atomic_load_explicit(&group->state, memory_order_acquire);
    if (state == ROUND_OFFLINE) {
        int expected = ROUND_OFFLINE;
        atomic_compare_exchange_strong_explicit(
            &group->state, &expected, ROUND_IDLE,
            memory_order_acq_rel, memory_order_acquire);
    }
    signal_word(&shared->coordinator_wake);
    return 0;
}

void *ps_open(const char *path, int create, int instance,
              int worker_rank, uint32_t worker_count,
              uint64_t epoch, uint64_t session,
              uint32_t heartbeat_ms, uint32_t peer_timeout_ms,
              uint32_t forward_timeout_ms,
              char *error, size_t error_size)
{
    if (path == NULL || instance < 0 || instance >= PS_INSTANCES ||
        worker_count == 0 || worker_count > PS_MAX_WORKERS ||
        worker_rank < 0 || (uint32_t)worker_rank >= worker_count ||
        epoch == 0 || session == 0 || heartbeat_ms == 0 ||
        peer_timeout_ms <= heartbeat_ms * 2 || forward_timeout_ms == 0) {
        set_error(error, error_size, "invalid scheduler open arguments");
        return NULL;
    }
    if (create && (instance != 0 || worker_rank != 0)) {
        set_error(error, error_size,
                  "only instance A worker rank 0 may create a generation");
        return NULL;
    }

    int flags = O_RDWR | O_CLOEXEC | (create ? O_CREAT | O_EXCL : 0);
    int fd = open(path, flags, 0660);
    if (fd < 0) {
        set_error(error, error_size, "open(%s): %s", path, strerror(errno));
        return NULL;
    }
    if (create && ftruncate(fd, (off_t)sizeof(shared_state)) != 0) {
        set_error(error, error_size, "ftruncate(%s): %s", path,
                  strerror(errno));
        close(fd);
        unlink(path);
        return NULL;
    }
    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0 ||
        statbuf.st_size != (off_t)sizeof(shared_state)) {
        set_error(error, error_size, "invalid shared-memory layout size");
        close(fd);
        if (create) {
            unlink(path);
        }
        return NULL;
    }
    shared_state *shared = mmap(NULL, sizeof(*shared),
                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        set_error(error, error_size, "mmap(%s): %s", path, strerror(errno));
        close(fd);
        if (create) {
            unlink(path);
        }
        return NULL;
    }

    if (create) {
        memset(shared, 0, sizeof(*shared));
        shared->magic = PS_MAGIC;
        shared->version = PS_VERSION;
        shared->size = sizeof(*shared);
        shared->epoch = epoch;
        shared->heartbeat_ms = heartbeat_ms;
        shared->peer_timeout_ms = peer_timeout_ms;
        shared->forward_timeout_ms = forward_timeout_ms;
        shared->worker_count = worker_count;
        atomic_init(&shared->status, make_status(PS_INITIALIZING, 0));
        atomic_init(&shared->owner, PS_NO_OWNER);
        atomic_init(&shared->last_owner, PS_NO_OWNER);
        atomic_init(&shared->deadline_ns, 0);
        atomic_init(&shared->primary_heartbeat_ns, monotonic_ns());
        atomic_init(&shared->next_grant_id, 0);
        atomic_init(&shared->coordinator_wake, 0);
        uint64_t expected_mask =
            worker_count == 64 ? UINT64_MAX :
            ((UINT64_C(1) << worker_count) - 1);
        for (int i = 0; i < PS_INSTANCES; ++i) {
            atomic_init(&shared->group_wake[i], 0);
            shared->groups[i].expected_mask = expected_mask;
            atomic_init(&shared->groups[i].state, ROUND_OFFLINE);
        }
        atomic_store_explicit(&shared->status, make_status(PS_RUNNING, 0),
                              memory_order_release);
    } else if (validate_layout(shared, epoch, heartbeat_ms, peer_timeout_ms,
                               forward_timeout_ms, worker_count,
                               error, error_size) != 0) {
        munmap(shared, sizeof(*shared));
        close(fd);
        return NULL;
    }

    uint64_t primary_hb =
        atomic_load_explicit(&shared->primary_heartbeat_ns,
                             memory_order_acquire);
    if (!create &&
        (status_state(load_status(shared)) != PS_RUNNING ||
         heartbeat_expired(primary_hb, monotonic_ns(), peer_timeout_ms))) {
        set_error(error, error_size, "primary coordinator is not alive");
        munmap(shared, sizeof(*shared));
        close(fd);
        return NULL;
    }

    ps_context *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        set_error(error, error_size, "calloc: %s", strerror(errno));
        munmap(shared, sizeof(*shared));
        close(fd);
        return NULL;
    }
    ctx->fd = fd;
    ctx->is_coordinator = create;
    ctx->instance = instance;
    ctx->worker_rank = worker_rank;
    ctx->worker_count = worker_count;
    ctx->session = session;
    ctx->heartbeat_ms = heartbeat_ms;
    ctx->peer_timeout_ms = peer_timeout_ms;
    ctx->forward_timeout_ms = forward_timeout_ms;
    ctx->shared = shared;
    atomic_init(&ctx->stopping, false);
    atomic_init(&ctx->active_calls, 0);
    atomic_init(&ctx->call_wake, 0);

    if (register_worker(ctx, error, error_size) != 0) {
        munmap(shared, sizeof(*shared));
        close(fd);
        free(ctx);
        return NULL;
    }
    ctx->next_seq = atomic_load_explicit(
        &shared->groups[instance].forward_seq, memory_order_acquire);
    if (pthread_create(&ctx->heartbeat_thread, NULL,
                       heartbeat_main, ctx) != 0) {
        set_error(error, error_size, "could not start heartbeat thread");
        goto open_failure;
    }
    ctx->heartbeat_started = true;
    if (create) {
        if (pthread_create(&ctx->coordinator_thread, NULL,
                           coordinator_main, ctx) != 0) {
            set_error(error, error_size,
                      "could not start coordinator thread");
            goto open_failure;
        }
        ctx->coordinator_started = true;
        fprintf(stderr,
                "{\"component\":\"vllm-pair-scheduler\",\"event\":\"started\","
                "\"protocol\":3,\"epoch\":%llu,\"workers\":%u}\n",
                (unsigned long long)epoch, worker_count);
    }
    return ctx;

open_failure:
    atomic_store_explicit(&ctx->stopping, true, memory_order_release);
    if (ctx->heartbeat_started) {
        pthread_join(ctx->heartbeat_thread, NULL);
    }
    atomic_fetch_and_explicit(
        &shared->groups[instance].online_mask,
        ~(UINT64_C(1) << worker_rank), memory_order_acq_rel);
    munmap(shared, sizeof(*shared));
    close(fd);
    free(ctx);
    return NULL;
}

static int validate_local_worker(ps_context *ctx, uint64_t seq,
                                 char *error, size_t error_size)
{
    worker_slot *slot =
        &ctx->shared->groups[ctx->instance].workers[ctx->worker_rank];
    if (atomic_load_explicit(&slot->session, memory_order_acquire) !=
            ctx->session ||
        atomic_load_explicit(&slot->pid, memory_order_acquire) !=
            (int)getpid()) {
        set_error(error, error_size, "stale worker session");
        return -1;
    }
    uint64_t last =
        atomic_load_explicit(&slot->last_seq, memory_order_acquire);
    if (last != seq) {
        set_error(error, error_size,
                  "worker sequence fencing failed (expected=%llu actual=%llu)",
                  (unsigned long long)seq, (unsigned long long)last);
        return -1;
    }
    return 0;
}

int ps_enter_forward(void *opaque, uint64_t *sequence_out,
                     uint64_t *grant_out, char *error, size_t error_size)
{
    ps_context *ctx = opaque;
    if (ctx == NULL || sequence_out == NULL || grant_out == NULL) {
        set_error(error, error_size, "invalid enter arguments");
        return -1;
    }
    if (!begin_call(ctx, error, error_size)) {
        return -1;
    }
    int result = -1;
    shared_state *shared = ctx->shared;
    instance_group *group = &shared->groups[ctx->instance];
    worker_slot *slot = &group->workers[ctx->worker_rank];
    uint64_t sequence = ++ctx->next_seq;
    uint64_t bit = UINT64_C(1) << ctx->worker_rank;

    if (ctx->active_seq != 0) {
        set_error(error, error_size, "worker already owns a forward round");
        fail_pair(shared, FAIL_FENCING);
        goto done;
    }
    if (check_running(shared, error, error_size) != 0) {
        goto done;
    }

    for (;;) {
        uint32_t wake =
            atomic_load_explicit(&shared->group_wake[ctx->instance],
                                 memory_order_acquire);
        int state = atomic_load_explicit(&group->state, memory_order_acquire);
        if (state == ROUND_IDLE) {
            int expected = ROUND_IDLE;
            if (atomic_compare_exchange_strong_explicit(
                    &group->state, &expected, ROUND_INITIALIZING,
                    memory_order_acq_rel, memory_order_acquire)) {
                atomic_store_explicit(&group->forward_seq, sequence,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->grant_id, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->ready_mask, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->complete_mask, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->exit_mask, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->publish_ns, monotonic_ns(),
                                      memory_order_relaxed);
                atomic_store_explicit(&group->state, ROUND_COLLECTING,
                                      memory_order_release);
                signal_word(&shared->group_wake[ctx->instance]);
                break;
            }
            continue;
        }
        if (state == ROUND_INITIALIZING || state == ROUND_DRAINING) {
            futex_wait_word(&shared->group_wake[ctx->instance], wake,
                            ctx->heartbeat_ms);
            continue;
        }
        uint64_t group_seq =
            atomic_load_explicit(&group->forward_seq, memory_order_acquire);
        if (state == ROUND_COLLECTING && group_seq == sequence) {
            break;
        }
        set_error(error, error_size,
                  "cross-round entry (worker_seq=%llu group_seq=%llu state=%d)",
                  (unsigned long long)sequence,
                  (unsigned long long)group_seq, state);
        fail_pair(shared, FAIL_FENCING);
        goto done;
    }

    atomic_store_explicit(&slot->last_seq, sequence, memory_order_release);
    uint64_t previous =
        atomic_fetch_or_explicit(&group->ready_mask, bit,
                                 memory_order_acq_rel);
    if ((previous & bit) != 0) {
        set_error(error, error_size, "duplicate ready bit for rank %d",
                  ctx->worker_rank);
        fail_pair(shared, FAIL_DUPLICATE_RANK);
        goto done;
    }
    if ((previous | bit) == group->expected_mask) {
        int expected = ROUND_COLLECTING;
        if (!atomic_compare_exchange_strong_explicit(
                &group->state, &expected, ROUND_READY,
                memory_order_acq_rel, memory_order_acquire)) {
            set_error(error, error_size, "could not publish READY round");
            fail_pair(shared, FAIL_FENCING);
            goto done;
        }
        signal_word(&shared->coordinator_wake);
        signal_word(&shared->group_wake[ctx->instance]);
    }

    for (;;) {
        uint32_t wake =
            atomic_load_explicit(&shared->group_wake[ctx->instance],
                                 memory_order_acquire);
        if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
            set_error(error, error_size, "scheduler context is closing");
            goto done;
        }
        if (check_running(shared, error, error_size) != 0) {
            goto done;
        }
        uint64_t primary_hb =
            atomic_load_explicit(&shared->primary_heartbeat_ns,
                                 memory_order_acquire);
        if (heartbeat_expired(primary_hb, monotonic_ns(),
                              ctx->peer_timeout_ms)) {
            fail_pair(shared, FAIL_PRIMARY_DEAD);
            set_error(error, error_size, "primary coordinator heartbeat expired");
            goto done;
        }
        int state = atomic_load_explicit(&group->state, memory_order_acquire);
        uint64_t group_seq =
            atomic_load_explicit(&group->forward_seq, memory_order_acquire);
        uint64_t grant =
            atomic_load_explicit(&group->grant_id, memory_order_acquire);
        int owner = atomic_load_explicit(&shared->owner, memory_order_acquire);
        if (state == ROUND_RUNNING && owner == ctx->instance &&
            group_seq == sequence && grant != 0) {
            if (validate_local_worker(ctx, sequence, error, error_size) != 0) {
                fail_pair(shared, FAIL_FENCING);
                goto done;
            }
            ctx->active_seq = sequence;
            ctx->active_grant_id = grant;
            *sequence_out = sequence;
            *grant_out = grant;
            result = 0;
            goto done;
        }
        if (group_seq != sequence ||
            (state != ROUND_COLLECTING && state != ROUND_READY &&
             state != ROUND_RUNNING)) {
            set_error(error, error_size, "round changed while waiting for grant");
            fail_pair(shared, FAIL_FENCING);
            goto done;
        }
        futex_wait_word(&shared->group_wake[ctx->instance], wake,
                        ctx->heartbeat_ms);
    }

done:
    end_call(ctx);
    return result;
}

int ps_leave_forward(void *opaque, uint64_t sequence, uint64_t grant_id,
                     char *error, size_t error_size)
{
    ps_context *ctx = opaque;
    if (ctx == NULL) {
        set_error(error, error_size, "invalid leave arguments");
        return -1;
    }
    if (!begin_call(ctx, error, error_size)) {
        return -1;
    }
    int result = -1;
    shared_state *shared = ctx->shared;
    instance_group *group = &shared->groups[ctx->instance];
    uint64_t bit = UINT64_C(1) << ctx->worker_rank;
    uint64_t deadline =
        atomic_load_explicit(&shared->deadline_ns, memory_order_acquire);

    if (check_running(shared, error, error_size) != 0) {
        goto done;
    }
    if (deadline != 0 && monotonic_ns() > deadline) {
        fail_pair(shared, FAIL_FORWARD_TIMEOUT);
        set_error(error, error_size, "forward deadline expired");
        goto done;
    }
    if (ctx->active_seq != sequence ||
        ctx->active_grant_id != grant_id ||
        atomic_load_explicit(&shared->owner, memory_order_acquire) !=
            ctx->instance ||
        atomic_load_explicit(&group->forward_seq, memory_order_acquire) !=
            sequence ||
        atomic_load_explicit(&group->grant_id, memory_order_acquire) !=
            grant_id ||
        atomic_load_explicit(&group->state, memory_order_acquire) !=
            ROUND_RUNNING ||
        validate_local_worker(ctx, sequence, error, error_size) != 0) {
        if (error != NULL && error[0] == '\0') {
            set_error(error, error_size, "leave fencing failed");
        }
        fail_pair(shared, FAIL_FENCING);
        goto done;
    }

    uint64_t previous =
        atomic_fetch_or_explicit(&group->complete_mask, bit,
                                 memory_order_acq_rel);
    if ((previous & bit) != 0) {
        set_error(error, error_size, "duplicate complete bit for rank %d",
                  ctx->worker_rank);
        fail_pair(shared, FAIL_DUPLICATE_RANK);
        goto done;
    }
    if ((previous | bit) == group->expected_mask) {
        int expected = ROUND_RUNNING;
        if (!atomic_compare_exchange_strong_explicit(
                &group->state, &expected, ROUND_COMPLETE,
                memory_order_acq_rel, memory_order_acquire)) {
            set_error(error, error_size, "could not publish COMPLETE round");
            fail_pair(shared, FAIL_FENCING);
            goto done;
        }
        signal_word(&shared->coordinator_wake);
        signal_word(&shared->group_wake[ctx->instance]);
    }

    for (;;) {
        uint32_t wake =
            atomic_load_explicit(&shared->group_wake[ctx->instance],
                                 memory_order_acquire);
        if (atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
            set_error(error, error_size, "scheduler context is closing");
            goto done;
        }
        if (check_running(shared, error, error_size) != 0) {
            goto done;
        }
        int state = atomic_load_explicit(&group->state, memory_order_acquire);
        uint64_t group_seq =
            atomic_load_explicit(&group->forward_seq, memory_order_acquire);
        if (state == ROUND_DRAINING && group_seq == sequence) {
            uint64_t exited =
                atomic_fetch_or_explicit(&group->exit_mask, bit,
                                         memory_order_acq_rel) |
                bit;
            if (exited == group->expected_mask) {
                atomic_store_explicit(&group->ready_mask, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->complete_mask, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->grant_id, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->publish_ns, 0,
                                      memory_order_relaxed);
                atomic_store_explicit(&group->state, ROUND_IDLE,
                                      memory_order_release);
                signal_word(&shared->group_wake[ctx->instance]);
                signal_word(&shared->coordinator_wake);
            }
            ctx->active_seq = 0;
            ctx->active_grant_id = 0;
            result = 0;
            goto done;
        }
        if (group_seq != sequence ||
            (state != ROUND_RUNNING && state != ROUND_COMPLETE &&
             state != ROUND_DRAINING)) {
            set_error(error, error_size, "round changed while completing");
            fail_pair(shared, FAIL_FENCING);
            goto done;
        }
        futex_wait_word(&shared->group_wake[ctx->instance], wake,
                        ctx->heartbeat_ms);
    }

done:
    end_call(ctx);
    return result;
}

int ps_fail(void *opaque, int reason)
{
    ps_context *ctx = opaque;
    if (ctx == NULL) {
        return -1;
    }
    fail_pair(ctx->shared, reason);
    return 0;
}

static size_t copy_snapshot(shared_state *shared, uint64_t *out, size_t count)
{
    if (count < PS_SNAPSHOT_COUNT) {
        return 0;
    }
    out[0] = shared->magic;
    out[1] = shared->version;
    out[2] = shared->epoch;
    uint64_t status = load_status(shared);
    out[3] = (uint64_t)status_state(status);
    out[4] = (uint64_t)status_reason(status);
    out[5] = (uint64_t)(int64_t)
        atomic_load_explicit(&shared->owner, memory_order_acquire);
    out[6] = (uint64_t)(int64_t)
        atomic_load_explicit(&shared->last_owner, memory_order_acquire);
    out[7] = atomic_load_explicit(&shared->deadline_ns, memory_order_acquire);
    out[8] =
        atomic_load_explicit(&shared->primary_heartbeat_ns,
                             memory_order_acquire);
    out[9] = shared->heartbeat_ms;
    out[10] = shared->peer_timeout_ms;
    out[11] = shared->forward_timeout_ms;
    out[12] =
        atomic_load_explicit(&shared->next_grant_id, memory_order_acquire);
    out[13] = shared->size;
    out[14] = shared->worker_count;
    out[15] = 0;
    size_t offset = 16;
    for (int instance = 0; instance < PS_INSTANCES; ++instance) {
        instance_group *group = &shared->groups[instance];
        out[offset++] = (uint64_t)(int64_t)
            atomic_load_explicit(&group->state, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->forward_seq, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->grant_id, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->ready_mask, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->complete_mask, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->exit_mask, memory_order_acquire);
        out[offset++] =
            atomic_load_explicit(&group->online_mask, memory_order_acquire);
        out[offset++] = group->expected_mask;
        out[offset++] =
            atomic_load_explicit(&group->publish_ns, memory_order_acquire);
        out[offset++] = (uint64_t)(int64_t)
            atomic_load_explicit(&group->error, memory_order_acquire);
    }
    for (int instance = 0; instance < PS_INSTANCES; ++instance) {
        for (int rank = 0; rank < PS_MAX_WORKERS; ++rank) {
            worker_slot *slot = &shared->groups[instance].workers[rank];
            out[offset++] = (uint64_t)(int64_t)
                atomic_load_explicit(&slot->pid, memory_order_acquire);
            out[offset++] =
                atomic_load_explicit(&slot->session, memory_order_acquire);
            out[offset++] =
                atomic_load_explicit(&slot->last_seq, memory_order_acquire);
            out[offset++] =
                atomic_load_explicit(&slot->heartbeat_ns,
                                     memory_order_acquire);
        }
    }
    return offset;
}

int ps_snapshot(void *opaque, uint64_t *out, size_t count)
{
    ps_context *ctx = opaque;
    if (ctx == NULL || out == NULL) {
        return -1;
    }
    size_t copied = copy_snapshot(ctx->shared, out, count);
    return copied == 0 ? -1 : (int)copied;
}

int ps_inspect(const char *path, uint64_t *out, size_t count,
               char *error, size_t error_size)
{
    if (path == NULL || out == NULL) {
        set_error(error, error_size, "invalid inspect arguments");
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_error(error, error_size, "open(%s): %s", path, strerror(errno));
        return -1;
    }
    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0 ||
        statbuf.st_size != (off_t)sizeof(shared_state)) {
        set_error(error, error_size, "invalid shared-memory layout size");
        close(fd);
        return -1;
    }
    shared_state *shared = mmap(NULL, sizeof(*shared), PROT_READ,
                                MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        set_error(error, error_size, "mmap(%s): %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    if (shared->magic != PS_MAGIC || shared->version != PS_VERSION ||
        shared->size != sizeof(*shared)) {
        set_error(error, error_size,
                  "shared-memory protocol mismatch (need v3)");
        munmap(shared, sizeof(*shared));
        close(fd);
        return -1;
    }
    size_t copied = copy_snapshot(shared, out, count);
    munmap(shared, sizeof(*shared));
    close(fd);
    if (copied == 0) {
        set_error(error, error_size, "snapshot buffer is too small");
        return -1;
    }
    return (int)copied;
}

void ps_close(void *opaque)
{
    ps_context *ctx = opaque;
    if (ctx == NULL) {
        return;
    }
    bool already =
        atomic_exchange_explicit(&ctx->stopping, true, memory_order_acq_rel);
    if (already) {
        return;
    }
    shared_state *shared = ctx->shared;
    signal_word(&shared->group_wake[ctx->instance]);
    signal_word(&shared->coordinator_wake);
    if (ctx->coordinator_started) {
        pthread_join(ctx->coordinator_thread, NULL);
    }
    if (ctx->heartbeat_started) {
        pthread_join(ctx->heartbeat_thread, NULL);
    }
    while (atomic_load_explicit(&ctx->active_calls, memory_order_acquire) != 0) {
        uint32_t wake =
            atomic_load_explicit(&ctx->call_wake, memory_order_acquire);
        futex_wait_word(&ctx->call_wake, wake, ctx->heartbeat_ms);
    }

    instance_group *group = &shared->groups[ctx->instance];
    uint64_t bit = UINT64_C(1) << ctx->worker_rank;
    int owner = atomic_load_explicit(&shared->owner, memory_order_acquire);
    int round_state =
        atomic_load_explicit(&group->state, memory_order_acquire);
    uint64_t ready =
        atomic_load_explicit(&group->ready_mask, memory_order_acquire);
    worker_slot *slot = &group->workers[ctx->worker_rank];
    if (atomic_load_explicit(&slot->session, memory_order_acquire) ==
        ctx->session) {
        if (ctx->active_seq != 0 || owner == ctx->instance ||
            ((round_state == ROUND_COLLECTING ||
              round_state == ROUND_READY) &&
             (ready & bit) != 0)) {
            fail_pair(shared, FAIL_CLOSE_ACTIVE);
        }
        atomic_fetch_and_explicit(&group->online_mask, ~bit,
                                  memory_order_acq_rel);
        atomic_store_explicit(&slot->heartbeat_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&slot->pid, 0, memory_order_relaxed);
        atomic_store_explicit(&slot->session, 0, memory_order_release);
        if (atomic_load_explicit(&group->online_mask, memory_order_acquire) ==
                0 &&
            round_state == ROUND_IDLE) {
            atomic_store_explicit(&group->state, ROUND_OFFLINE,
                                  memory_order_release);
        }
    }
    if (ctx->is_coordinator) {
        uint64_t expected = make_status(PS_RUNNING, 0);
        atomic_compare_exchange_strong_explicit(
            &shared->status, &expected, make_status(PS_SHUTDOWN, 0),
            memory_order_acq_rel, memory_order_acquire);
        wake_everyone(shared);
    }
    munmap(shared, sizeof(*shared));
    close(ctx->fd);
    free(ctx);
}
