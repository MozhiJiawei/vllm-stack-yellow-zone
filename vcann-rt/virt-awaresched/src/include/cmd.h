/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * VSched is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef CMD_H
#define CMD_H

#include <map>
#include <string>

namespace vas::common {
// Set
#define SET_CMD "set"
// Set -> Config
#define SET_CONFIG_TYPE "config"
#define SET_CONFIG_DES "Set config."
#define SCHED_POLICY "sched-policy"
#define SCHED_POLICY_DYNAMIC "dynamicAffinity"
#define SCHED_POLICY_STATIC "affinity"
#define SCHED_POLICY "sched-policy"
#define SCHED_POLICY_SHORT "sp"
#define SCHED_POLICY_DES                                                  \
    "Set scheduling policy, with valid values including dynamicAffinity " \
    "and affinity."

// Query
#define QUERY_CMD "query"
// Query -> Affinity
#define QUERY_AFFINITY_TYPE "affinity"
#define QUERY_AFFINITY_DES "Query vm cpu affinity info"
#define AFFINITY_SCOPE "scope"
#define AFFINITY_SCOPE_SHORT "s"
#define AFFINITY_SCOPE_DES                                              \
    "Query virtual machine vCPU affinity: "                             \
    "'all' to retrieve affinity information for all virtual machines, " \
    "'uuid' to fetch affinity details for a specific virtual machine."

// Option
#define OPTION_CMD "opt"
// Option -> Reassign
#define REASSIGN_TYPE "reassign"
#define REASSIGN_DES "Reassign vm cpu affinity"
#define REASSIGN_SCOPE "scope"
#define REASSIGN_SCOPE_SHORT "s"
#define REASSIGN_SCOPE_DES                       \
    "Realign virtual machine vCPU: "             \
    "'all' to reschedule all virtual machines, " \
    "'uuid' to reconfigure a specific virtual machine's CPU affinity."

// Option -> Recover
#define RECOVER_TYPE "recover"
#define RECOVER_DES "recover vm cpu affinity"
#define RECOVER_VM "vm"
#define RECOVER_VM_SHORT "v"
#define RECOVER_VM_DES                        \
    "recover virtual machine vCPU: "          \
    "'all' to recover all virtual machines, " \
    "'uuid' to recover a specific virtual machine's CPU affinity."

struct CmdOption {
    std::string option; // action
    std::map<std::string, std::string> params{};
};
} // namespace vas::common

#endif // CMD_H
