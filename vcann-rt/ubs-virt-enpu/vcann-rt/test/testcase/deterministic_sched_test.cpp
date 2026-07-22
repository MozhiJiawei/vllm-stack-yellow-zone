/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-virt-enpu is licensed under Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "core_limiter.h"

extern "C" {
extern vnpu_time_slice_sched_t *g_vnpu_sched_context;
extern uint8_t g_vnpu_id;
extern volatile int g_terminate;
}

class DeterministicSchedulerStateTest : public testing::Test {
protected:
    void SetUp() override
    {
        g_vnpu_sched_context = &context_;
        g_terminate = 0;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        det_sched_init(&attr);
        pthread_mutexattr_destroy(&attr);
        MOCKER(get_sched_policy).stubs().will(returnValue(SCHED_POLICY_ELASTIC));
        atomic_store(&context_.vnpu_core_limit_quota[2], 50);
        atomic_store(&context_.vnpu_core_limit_quota[9], 50);
    }

    void TearDown() override
    {
        pthread_mutex_destroy(&context_.det_mutex);
        GlobalMockObject::verify();
        GlobalMockObject::reset();
    }

    void SetParticipants()
    {
        atomic_store(&context_.det_participants[0], 2);
        atomic_store(&context_.det_participants[1], 9);
        atomic_store(&context_.det_state, DET_STATE_PARK);
    }

    vnpu_time_slice_sched_t context_{};
};

TEST(DeterministicSchedulerTest, EqualQuotaAlternates)
{
    EXPECT_EQ(det_sched_weighted_owner(2, 9, 50, 50, 0), 2);
    EXPECT_EQ(det_sched_weighted_owner(2, 9, 50, 50, 1), 9);
    EXPECT_EQ(det_sched_weighted_owner(2, 9, 50, 50, 2), 2);
}

TEST(DeterministicSchedulerTest, GcdNormalizedQuotaIsStable)
{
    const int expected[] = {2, 2, 9, 2, 2, 9};
    for (uint64_t turn = 0; turn < 6; ++turn) {
        EXPECT_EQ(det_sched_weighted_owner(2, 9, 50, 25, turn), expected[turn]);
    }
}

TEST(DeterministicSchedulerTest, InvalidQuotaFailsClosed)
{
    EXPECT_EQ(det_sched_weighted_owner(2, 9, 0, 50, 0), DET_SCHED_NO_OWNER);
}

TEST_F(DeterministicSchedulerStateTest, SingleReadyRunsWithoutQuotaDecision)
{
    bool enabled = false;
    g_vnpu_id = 2;
    ASSERT_EQ(rtDetSchedEnter(false, &enabled), ACL_RT_SUCCESS);
    g_vnpu_id = 9;
    ASSERT_EQ(rtDetSchedEnter(true, &enabled), ACL_RT_SUCCESS);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(atomic_load(&context_.det_state), DET_STATE_RUNNING_1);
    EXPECT_EQ(context_.det_weighted_turn, 0U);
}

TEST_F(DeterministicSchedulerStateTest, DualReadyUsesQuotaAndKeepsLoser)
{
    SetParticipants();
    context_.det_snapshot[1] = DET_SNAPSHOT_READY;
    bool enabled = false;
    g_vnpu_id = 2;
    ASSERT_EQ(rtDetSchedEnter(true, &enabled), ACL_RT_SUCCESS);
    EXPECT_EQ(atomic_load(&context_.det_state), DET_STATE_RUNNING_0);
    EXPECT_EQ(context_.det_snapshot[1], DET_SNAPSHOT_READY);
    EXPECT_EQ(context_.det_weighted_turn, 1U);
}

TEST_F(DeterministicSchedulerStateTest, ReverseArrivalMakesSameWeightedDecision)
{
    SetParticipants();
    context_.det_snapshot[0] = DET_SNAPSHOT_READY;
    context_.det_weighted_turn = 1;
    bool enabled = false;
    g_vnpu_id = 9;
    ASSERT_EQ(rtDetSchedEnter(true, &enabled), ACL_RT_SUCCESS);
    EXPECT_EQ(atomic_load(&context_.det_state), DET_STATE_RUNNING_1);
    EXPECT_EQ(context_.det_snapshot[0], DET_SNAPSHOT_READY);
}

TEST_F(DeterministicSchedulerStateTest, BothIdleAreConsumed)
{
    SetParticipants();
    context_.det_snapshot[1] = DET_SNAPSHOT_IDLE;
    bool enabled = false;
    g_vnpu_id = 2;
    ASSERT_EQ(rtDetSchedEnter(false, &enabled), ACL_RT_SUCCESS);
    EXPECT_EQ(atomic_load(&context_.det_state), DET_STATE_PARK);
    EXPECT_EQ(context_.det_snapshot[0], DET_SNAPSHOT_UNKNOWN);
    EXPECT_EQ(context_.det_snapshot[1], DET_SNAPSHOT_UNKNOWN);
}
