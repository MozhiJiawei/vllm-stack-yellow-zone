/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <libgen.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <errno.h>
#include "common.h"
#include "config.h"
#include "log.h"
#include "npu_manager.h"
#include "utils.h"

#define CMD_SET "set"
#define CMD_GET "get"
#define CMD_WATCH "watch"
#define DEFAULT_CONFIG_PATH "/etc/enpu/vcann-rt/npu_info.config"

static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    keep_running = 0;
}

static void print_help_message(const char *prog_name)
{
    printf("Usage: %s <command> [options]\n", prog_name);
    printf("\nCommands:\n");
    printf("  set    Set AICore quota for a vNPU\n");
    printf("  get    Get AICore quota for a vNPU\n");
    printf("  watch  Watch config file and apply changes automatically\n");
    printf("\nOptions for 'set':\n");
    printf("  --vnpu-id <id>      Virtual NPU ID (0-99)\n");
    printf("  --quota <percent>   AICore quota percentage (1-100)\n");
    printf("  --config <path>     Config file path (default: %s)\n", DEFAULT_CONFIG_PATH);
    printf("\nOptions for 'get':\n");
    printf("  --vnpu-id <id>      Virtual NPU ID (0-99)\n");
    printf("  --config <path>     Config file path (default: %s)\n", DEFAULT_CONFIG_PATH);
    printf("\nOptions for 'watch':\n");
    printf("  --config <path>     Config file path (default: %s)\n", DEFAULT_CONFIG_PATH);
    printf("\nExamples:\n");
    printf("  %s set --vnpu-id 0 --quota 50    # Set vNPU 0 to 50%%\n", prog_name);
    printf("  %s get --vnpu-id 0               # Get vNPU 0 quota\n", prog_name);
    printf("  %s watch                         # Watch default config\n", prog_name);
    printf("  %s watch --config /path/to/config  # Watch custom config\n", prog_name);
}

static int enpu_update_core_quota_by_shm(const char *shm_id, int target_vnpu_id, uint8_t new_quota_percent)
{
    CHECK_COND_RETURN_ERROR_CODE(shm_id == NULL, "shm_id cannot be NULL.");
    CHECK_COND_RETURN_ERROR_CODE(target_vnpu_id < 0 || target_vnpu_id >= MAX_VNPU, "Invalid vnpu_id: %d (must be 0-%d).", target_vnpu_id, MAX_VNPU - 1);
    CHECK_COND_RETURN_ERROR_CODE(new_quota_percent > MAX_CORE_QUOTA, "Invalid quota: %d%% (must be 0-%d%%).", new_quota_percent, MAX_CORE_QUOTA);

    size_t shm_size = sizeof(vnpu_time_slice_sched_t);
    vnpu_time_slice_sched_t *vnpu_sched_shm = map_share_mem(shm_id, shm_size);
    CHECK_COND_RETURN_ERROR_CODE(vnpu_sched_shm == NULL, "Failed to attach to shared memory for shm_id: %s.", shm_id);

    atomic_store(&vnpu_sched_shm->vnpu_core_limit_quota[target_vnpu_id], new_quota_percent);

    CHECK_COND_RETURN_ERROR_CODE(munmap(vnpu_sched_shm, shm_size) == -1, "Failed to unmap shared memory.");

    return ENPU_SUCCESS;
}

static int enpu_get_core_quota_by_shm(const char *shm_id, int target_vnpu_id, uint8_t *quota_percent)
{
    CHECK_COND_RETURN_ERROR_CODE(shm_id == NULL, "shm_id cannot be NULL.");
    CHECK_COND_RETURN_ERROR_CODE(quota_percent == NULL, "quota_percent pointer cannot be NULL.");
    CHECK_COND_RETURN_ERROR_CODE(target_vnpu_id < 0 || target_vnpu_id >= MAX_VNPU, "Invalid vnpu_id: %d (must be 0-%d).", target_vnpu_id, MAX_VNPU - 1);

    size_t shm_size = sizeof(vnpu_time_slice_sched_t);
    vnpu_time_slice_sched_t *vnpu_sched_shm = map_share_mem(shm_id, shm_size);
    CHECK_COND_RETURN_ERROR_CODE(vnpu_sched_shm == NULL, "Failed to attach to shared memory for shm_id: %s.", shm_id);

    *quota_percent = atomic_load(&vnpu_sched_shm->vnpu_core_limit_quota[target_vnpu_id]);

    CHECK_COND_RETURN_ERROR_CODE(munmap(vnpu_sched_shm, shm_size) == -1, "Failed to unmap shared memory.");

    return ENPU_SUCCESS;
}

static const char* find_shm_id_by_vnpu_id(int vnpu_id)
{
    if (vnpu_id < 0 || vnpu_id >= MAX_VNPU) {
        LOG_ERROR("Invalid vnpu_id: %d (must be 0-%d).", vnpu_id, MAX_VNPU - 1);
        return NULL;
    }

    for (int i = 0; i < MAX_NPU_DEVICES; i++) {
        if (multi_config.sections[i].vnpu_id == vnpu_id &&
            multi_config.sections[i].phy_npu_id != INVALID_VALUE) {
            return multi_config.sections[i].shm_id;
        }
    }

    if (config.vnpu_id == vnpu_id && config.phy_npu_id != INVALID_VALUE) {
        return config.shm_id;
    }

    return NULL;
}

static int parse_int_arg(const char *str, int *out_val) {
    if (str == NULL || *str == '\0') {
        return ENPU_FAIL;
    }

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (*endptr != '\0' || str == endptr) {
        return ENPU_FAIL;
    }
    if (errno == ERANGE) {
        return ENPU_FAIL;
    }
    if (val > INT_MAX || val < INT_MIN) {
        return ENPU_FAIL;
    }

    *out_val = (int)val;
    return ENPU_SUCCESS;
}

static int cmd_set(int argc, char *argv[])
{
    int vnpu_id = -1;
    int quota = -1;
    int ret = 0;
    char *config_path = DEFAULT_CONFIG_PATH;
    int opt;

    static struct option long_options[] = {
        {"vnpu-id", required_argument, 0, 'i'},
        {"quota",   required_argument, 0, 'q'},
        {"config",  required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:q:c:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                ret = parse_int_arg(optarg, &vnpu_id);
                CHECK_RETURN_ERROR_CODE(ret, "Failed to parse value %s for %c argument.", optarg, opt);
                break;
            case 'q':
                ret = parse_int_arg(optarg, &quota);
                CHECK_RETURN_ERROR_CODE(ret, "Failed to parse value %s for %c argument.", optarg, opt);
                break;
            case 'c':
                config_path = optarg;
                break;
            default:
                return ENPU_FAIL;
        }
    }

    CHECK_COND_RETURN_ERROR_CODE(vnpu_id < 0 || vnpu_id >= MAX_VNPU, "Invalid vnpu_id: %d (must be 0-%d).", vnpu_id, MAX_VNPU - 1);
    CHECK_COND_RETURN_ERROR_CODE(quota < 0 || quota > MAX_CORE_QUOTA, "Invalid quota: %d%% (must be 0-%d%%).", quota, MAX_CORE_QUOTA);

    ret = log_init();
    CHECK_RETURN_ERROR_CODE(ret, "Log init failed.");

    ret = load_config(config_path);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to load config from %s.", config_path);

    const char *shm_id = find_shm_id_by_vnpu_id(vnpu_id);
    CHECK_COND_RETURN_ERROR_CODE(shm_id == NULL, "No shm_id found for vNPU %d in config %s.", vnpu_id, config_path);

    ret = enpu_update_core_quota_by_shm(shm_id, vnpu_id, (uint8_t)quota);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to update quota for vNPU %d.", vnpu_id);

    LOG_INFO("Successfully set vNPU %d quota to %d%% (shm_id: %s).", vnpu_id, quota, shm_id);

    return ENPU_SUCCESS;
}

static int cmd_get(int argc, char *argv[])
{
    int vnpu_id = -1;
    int ret = 0;
    char *config_path = DEFAULT_CONFIG_PATH;
    int opt;

    static struct option long_options[] = {
        {"vnpu-id", required_argument, 0, 'i'},
        {"config",  required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:c:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                ret = parse_int_arg(optarg, &vnpu_id);
                CHECK_RETURN_ERROR_CODE(ret, "Failed to parse value %s for %c argument.", optarg, opt);
                break;
            case 'c':
                config_path = optarg;
                break;
            default:
                return ENPU_FAIL;
        }
    }

    CHECK_COND_RETURN_ERROR_CODE(vnpu_id < 0 || vnpu_id >= MAX_VNPU, "Invalid vnpu_id: %d (must be 0-%d).", vnpu_id, MAX_VNPU - 1);

    ret = log_init();
    CHECK_RETURN_ERROR_CODE(ret, "Log init failed.");

    ret = load_config(config_path);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to load config from %s.", config_path);

    const char *shm_id = find_shm_id_by_vnpu_id(vnpu_id);
    CHECK_COND_RETURN_ERROR_CODE(shm_id == NULL, "No shm_id found for vNPU %d in config %s.", vnpu_id, config_path);

    uint8_t quota = 0;
    ret = enpu_get_core_quota_by_shm(shm_id, vnpu_id, &quota);
    CHECK_RETURN_ERROR_CODE(ret, "Failed to get quota for vNPU %d.", vnpu_id);

    LOG_INFO("vNPU %d quota: %d%% (shm_id: %s).", vnpu_id, quota, shm_id);

    return ENPU_SUCCESS;
}

static int apply_quotas_from_config(void)
{
    int device_count = get_device_count();
    CHECK_COND_RETURN_ERROR_CODE(device_count <= 0, "No devices configured.");

    LOG_INFO("Applying quotas for %d device(s).", device_count);

    for (int i = 0; i < device_count; i++) {
        struct ConfigSection cfg;
        int ret = get_device_config(i, &cfg);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("Failed to get config for device %d.", i);
            continue;
        }

        if (cfg.aicore_quota == INVALID_VALUE) {
            LOG_ERROR("Device %d (vnpu-id=%d) has no aicore-quota set, skipping.", i, cfg.vnpu_id);
            continue;
        }
        if (cfg.vnpu_id < 0 || cfg.vnpu_id >= MAX_VNPU) {
            LOG_ERROR("Device %d has invalid vnpu_id: %d (must be 0-%d), skipping.", i, cfg.vnpu_id, MAX_VNPU - 1);
            continue;
        }
        if (cfg.aicore_quota < 0 || cfg.aicore_quota > MAX_CORE_QUOTA) {
            LOG_ERROR("Device %d has invalid quota: %d%% (must be 0-%d%%), skipping.", i, cfg.aicore_quota, MAX_CORE_QUOTA);
            continue;
        }

        ret = enpu_update_core_quota_by_shm(cfg.shm_id, cfg.vnpu_id, (uint8_t)cfg.aicore_quota);
        if (ret != ENPU_SUCCESS) {
            LOG_ERROR("Failed to update quota for vNPU %d (shm_id=%s).",
                      cfg.vnpu_id, cfg.shm_id);
            continue;
        }

        LOG_INFO("Applied quota %d%% for vNPU %d (shm_id=%s).",
                 cfg.aicore_quota, cfg.vnpu_id, cfg.shm_id);
    }

    return ENPU_SUCCESS;
}

static int cmd_watch(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    CHECK_COND_RETURN_ERROR_CODE(sigaction(SIGINT, &sa, NULL) == -1, "Failed to set sigaction for SIGINT: %s", strerror(errno));
    CHECK_COND_RETURN_ERROR_CODE(sigaction(SIGTERM, &sa, NULL) == -1, "Failed to set sigaction for SIGTERM: %s", strerror(errno));

    char *config_path = DEFAULT_CONFIG_PATH;
    int opt;

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "c:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            default:
                return ENPU_FAIL;
        }
    }

    int ret = log_init();
    CHECK_RETURN_ERROR_CODE(ret, "Log init failed.");

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    CHECK_COND_RETURN_ERROR_CODE(inotify_fd == -1, "Failed to initialize inotify: %s.", strerror(errno));

    char *path_copy1 = strdup(config_path);
    char *path_copy2 = strdup(config_path);
    if (path_copy1 == NULL || path_copy2 == NULL) {
        free(path_copy2);
        free(path_copy1);
        close(inotify_fd);
        return ENPU_FAIL;
    }

    char *dir_path = dirname(path_copy1);
    char *base_name = basename(path_copy2);

    int watch_descriptor = inotify_add_watch(inotify_fd, dir_path,
                                             IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM);
    if (watch_descriptor == -1) {
        LOG_ERROR("Failed to add watch for directory %s: %s.", dir_path, strerror(errno));
        free(path_copy2);
        free(path_copy1);
        close(inotify_fd);
        return ENPU_FAIL;
    }

    struct pollfd fds[1];
    fds[0].fd = inotify_fd;
    fds[0].events = POLLIN;

    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    LOG_INFO("Watching config file: %s. Press Ctrl+C to stop...", config_path);

    while (keep_running) {
        ret = poll(fds, 1, -1);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("Error polling from inotify: %s.", strerror(errno));
            break;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            LOG_ERROR("Inotify descriptor error (POLLERR/POLLHUP/POLLNVAL).");
            break;
        }

        if (fds[0].revents & POLLIN) {
            while (true) {
                ssize_t length = read(inotify_fd, buffer, sizeof(buffer));

                if (length == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    LOG_ERROR("Error reading from inotify: %s.", strerror(errno));
                    keep_running = 0;
                    break;
                }

                ssize_t i = 0;
                while (i < length) {
                    struct inotify_event *event = (struct inotify_event *)&buffer[i];

                    if (event->len > 0 && strcmp(event->name, base_name) == 0) {
                        if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
                            LOG_INFO("Config file modified, reloading...");
                            reset_single_config();
                            reset_multi_config();

                            ret = load_config(config_path);
                            if (ret != ENPU_SUCCESS) {
                                LOG_ERROR("Failed to reload config, keeping old values.");
                            } else {
                                apply_quotas_from_config();
                            }
                        } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            LOG_WARN("Config file '%s' was deleted or moved away. Waiting for it to reappear...", base_name);
                        }
                    }

                    i += sizeof(struct inotify_event) + event->len;
                }
            }
        }
    }

    inotify_rm_watch(inotify_fd, watch_descriptor);
    free(path_copy2);
    free(path_copy1);
    close(inotify_fd);

    LOG_INFO("Watcher stopped.");
    return ENPU_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_help_message(argv[0]);
        return ENPU_FAIL;
    }

    if (strcmp(argv[1], CMD_SET) == 0) {
        return cmd_set(argc, argv);
    } else if (strcmp(argv[1], CMD_GET) == 0) {
        return cmd_get(argc, argv);
    } else if (strcmp(argv[1], CMD_WATCH) == 0) {
        return cmd_watch(argc, argv);
    } else {
        LOG_ERROR("Unknown command: %s.", argv[1]);
        print_help_message(argv[0]);
        return ENPU_FAIL;
    }
}
