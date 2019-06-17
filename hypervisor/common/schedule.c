/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rtl.h>
#include <list.h>
#include <bits.h>
#include <cpu.h>
#include <per_cpu.h>
#include <lapic.h>
#include <schedule.h>
#include <sprintf.h>

bool sched_is_idle(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	return (obj == &per_cpu(idle, pcpu_id));
}

static inline bool is_blocked(struct sched_object *obj)
{
	return obj->status == SCHED_STS_BLOCKED;
}

static inline bool is_runnable(struct sched_object *obj)
{
	return obj->status == SCHED_STS_RUNNABLE;
}

static inline bool is_running(struct sched_object *obj)
{
	return obj->status == SCHED_STS_RUNNING;
}

static inline void sched_set_status(struct sched_object *obj, uint16_t status)
{
	obj->status = status;
}

void get_schedule_lock(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_obtain(&ctx->scheduler_lock);
}

void release_schedule_lock(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_release(&ctx->scheduler_lock);
}

static struct acrn_scheduler *get_scheduler(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	return ctx->scheduler;
}

/**
 * @pre obj != NULL
 */
uint16_t sched_get_pcpuid(const struct sched_object *obj)
{
	return obj->pcpu_id;
}

void init_sched(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	spinlock_init(&ctx->scheduler_lock);
	ctx->flags = 0UL;
	ctx->current = NULL;
	ctx->pcpu_id = pcpu_id;
	ctx->scheduler = &sched_noop;
	if (ctx->scheduler->init != NULL) {
		ctx->scheduler->init(ctx);
	}
}

void deinit_sched(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	if (ctx->scheduler->deinit != NULL) {
		ctx->scheduler->deinit(ctx);
	}
}

void sched_init_data(struct sched_object *obj)
{
	struct acrn_scheduler *scheduler = get_scheduler(obj->pcpu_id);
	get_schedule_lock(obj->pcpu_id);
	if (scheduler->init_data != NULL) {
		scheduler->init_data(obj);
	}
	/* initial as BLOCKED status, so we can wake it up to run */
	sched_set_status(obj, SCHED_STS_BLOCKED);
	release_schedule_lock(obj->pcpu_id);
}

void sched_deinit_data(struct sched_object *obj)
{
	struct acrn_scheduler *scheduler = get_scheduler(obj->pcpu_id);

	if (scheduler->deinit_data != NULL) {
		scheduler->deinit_data(obj);
	}
}

struct sched_object *sched_get_current(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	return ctx->current;
}

/**
 * @pre delmode == DEL_MODE_IPI || delmode == DEL_MODE_INIT
 */
void make_reschedule_request(uint16_t pcpu_id, uint16_t delmode)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	bitmap_set_lock(NEED_RESCHEDULE, &ctx->flags);
	if (get_pcpu_id() != pcpu_id) {
		switch (delmode) {
		case DEL_MODE_IPI:
			send_single_ipi(pcpu_id, VECTOR_NOTIFY_VCPU);
			break;
		case DEL_MODE_INIT:
			send_single_init(pcpu_id);
			break;
		default:
			ASSERT(false, "Unknown delivery mode %u for pCPU%u", delmode, pcpu_id);
			break;
		}
	}
}

bool need_reschedule(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	return bitmap_test(NEED_RESCHEDULE, &ctx->flags);
}

static void prepare_switch(struct sched_object *prev, struct sched_object *next)
{
	if ((prev != NULL) && (prev->switch_out != NULL)) {
		prev->switch_out(prev);
	}

	/* update current object */
	get_cpu_var(sched_ctx).current = next;

	if ((next != NULL) && (next->switch_in != NULL)) {
		next->switch_in(next);
	}
}

void schedule(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	struct sched_object *next = &per_cpu(idle, pcpu_id);
	struct sched_object *prev = ctx->current;

	get_schedule_lock(pcpu_id);
	if (ctx->scheduler->pick_next != NULL) {
		next = ctx->scheduler->pick_next(ctx);
	}
	bitmap_clear_lock(NEED_RESCHEDULE, &ctx->flags);

	/* Don't change prev object's status if it's not running */
	if (is_running(prev)) {
		sched_set_status(prev, SCHED_STS_RUNNABLE);
	}
	sched_set_status(next, SCHED_STS_RUNNING);

	if (prev == next) {
		release_schedule_lock(pcpu_id);
	} else {
		prepare_switch(prev, next);
		release_schedule_lock(pcpu_id);

		arch_switch_to(&prev->host_sp, &next->host_sp);
	}
}

void sched_sleep_obj(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);

	get_schedule_lock(pcpu_id);
	if (scheduler->sleep != NULL) {
		scheduler->sleep(obj);
	}
	if (is_running(obj)) {
		if (obj->notify_mode == SCHED_NOTIFY_INIT) {
			make_reschedule_request(pcpu_id, DEL_MODE_INIT);
		} else {
			make_reschedule_request(pcpu_id, DEL_MODE_IPI);
		}
	}
	sched_set_status(obj, SCHED_STS_BLOCKED);
	release_schedule_lock(pcpu_id);
}

void sched_wake_obj(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);

	get_schedule_lock(pcpu_id);
	if (is_blocked(obj)) {
		if (scheduler->wake != NULL) {
			scheduler->wake(obj);
		}
		sched_set_status(obj, SCHED_STS_RUNNABLE);
		make_reschedule_request(pcpu_id, DEL_MODE_IPI);
	}
	release_schedule_lock(pcpu_id);
}

void run_sched_thread(struct sched_object *obj)
{
	if (obj->thread != NULL) {
		obj->thread(obj);
	}

	ASSERT(false, "Shouldn't go here, invalid thread!");
}

void switch_to_idle(sched_thread_t idle_thread)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct sched_object *idle = &per_cpu(idle, pcpu_id);
	char idle_name[16];

	snprintf(idle_name, 16U, "idle%hu", pcpu_id);
	(void)strncpy_s(idle->name, 16U, idle_name, 16U);
	idle->pcpu_id = pcpu_id;
	idle->thread = idle_thread;
	idle->switch_out = NULL;
	idle->switch_in = NULL;
	get_cpu_var(sched_ctx).current = idle;
	sched_init_data(idle);
	sched_set_status(idle, SCHED_STS_RUNNING);

	run_sched_thread(idle);
}
