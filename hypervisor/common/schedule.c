/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rtl.h>
#include <list.h>
#include <bits.h>
#include <errno.h>
#include <cpu.h>
#include <per_cpu.h>
#include <lapic.h>
#include <schedule.h>
#include <sprintf.h>
#include <trace.h>

static struct acrn_scheduler *schedulers[SCHEDULER_MAX_NUMBER] = {
	&sched_noop,
	&sched_rr,
};

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

void get_schedule_lock(uint16_t pcpu_id, uint64_t *rflag)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_irqsave_obtain(&ctx->scheduler_lock, rflag);
}

void release_schedule_lock(uint16_t pcpu_id, uint64_t rflag)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	spinlock_irqrestore_release(&ctx->scheduler_lock, rflag);
}

static void set_scheduler(uint16_t pcpu_id, struct acrn_scheduler *scheduler)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	ctx->scheduler = scheduler;
}

static struct acrn_scheduler *get_scheduler(uint16_t pcpu_id)
{
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	return ctx->scheduler;
}

static struct acrn_scheduler *find_scheduler_by_name(const char *name)
{
	unsigned int i;
	struct acrn_scheduler *scheduler = NULL;

	for (i = 0U; i < SCHEDULER_MAX_NUMBER && schedulers[i] != NULL; i++) {
		if (strncmp(name, schedulers[i]->name, sizeof(schedulers[i]->name)) == 0) {
			scheduler = schedulers[i];
			break;
		}
	}

	return scheduler;
}

bool init_pcpu_schedulers(uint64_t pcpu_bitmap, const char *scheduler_name)
{
	bool ret = true;
	uint16_t pcpu_id;
	struct acrn_scheduler *curr_scheduler, *conf_scheduler;

	/* verify & set scheduler for all pcpu of this VM */
	pcpu_id = ffs64(pcpu_bitmap);
	while (pcpu_id != INVALID_BIT_INDEX) {
		curr_scheduler = get_scheduler(pcpu_id);
		conf_scheduler = find_scheduler_by_name(scheduler_name);
		if (curr_scheduler && conf_scheduler && curr_scheduler != conf_scheduler) {
			pr_warn("%s: detect scheduler conflict on pcpu%d!\n", __func__, pcpu_id);
			ret = false;
			break;
		}
		if (conf_scheduler) {
			pr_info("%s: Set pcpu%d scheduler: %s", __func__, pcpu_id, conf_scheduler->name);
			set_scheduler(pcpu_id, conf_scheduler);
		}
		bitmap_clear_nolock(pcpu_id, &pcpu_bitmap);
		pcpu_id = ffs64(pcpu_bitmap);
	}

	return ret;
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
	if (ctx->scheduler == NULL) {
		ctx->scheduler = &sched_noop;
	}
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
	uint64_t rflag;
	get_schedule_lock(obj->pcpu_id, &rflag);
	if (scheduler->init_data != NULL) {
		scheduler->init_data(obj);
	}
	/* initial as BLOCKED status, so we can wake it up to run */
	sched_set_status(obj, SCHED_STS_BLOCKED);
	release_schedule_lock(obj->pcpu_id, rflag);
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

void schedule(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);
	struct sched_object *next = &per_cpu(idle, pcpu_id);
	struct sched_object *prev = ctx->current;
	uint64_t rflag;

	get_schedule_lock(pcpu_id, &rflag);
	if (ctx->scheduler->pick_next != NULL) {
		next = ctx->scheduler->pick_next(ctx);
	}
	bitmap_clear_lock(NEED_RESCHEDULE, &ctx->flags);

	/* Don't change prev object's status if it's not running */
	if (is_running(prev)) {
		sched_set_status(prev, SCHED_STS_RUNNABLE);
	}
	sched_set_status(next, SCHED_STS_RUNNING);
	ctx->current = next;
	release_schedule_lock(pcpu_id, rflag);

	/* If we picked different sched object, switch context */
	if (prev != next) {
		if ((prev != NULL) && (prev->switch_out != NULL)) {
			TRACE_4I(TRACE_PCPU_SCHED_END, prev->pcpu_id, prev->vm_id, prev->vcpu_id, 0);
			prev->switch_out(prev);
		}

		if ((next != NULL) && (next->switch_in != NULL)) {
			next->switch_in(next);
			TRACE_4I(TRACE_PCPU_SCHED_START, next->pcpu_id, next->vm_id, next->vcpu_id, 0);
		}

		arch_switch_to(&prev->host_sp, &next->host_sp);
	}
}

void sched_sleep_obj(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);
	uint64_t rflag;

	get_schedule_lock(pcpu_id, &rflag);
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
	release_schedule_lock(pcpu_id, rflag);
}

void sched_wake_obj(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);
	uint64_t rflag;

	get_schedule_lock(pcpu_id, &rflag);
	if (is_blocked(obj)) {
		if (scheduler->wake != NULL) {
			scheduler->wake(obj);
		}
		sched_set_status(obj, SCHED_STS_RUNNABLE);
		make_reschedule_request(pcpu_id, DEL_MODE_IPI);
	}
	release_schedule_lock(pcpu_id, rflag);
}

void sched_poke_obj(struct sched_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);
	uint64_t rflag;

	get_schedule_lock(pcpu_id, &rflag);
	if (is_running(obj) && get_pcpu_id() != pcpu_id) {
		send_single_ipi(pcpu_id, VECTOR_NOTIFY_VCPU);
	} else if (is_runnable(obj)) {
		if (scheduler->poke != NULL) {
			scheduler->poke(obj);
		}
		make_reschedule_request(pcpu_id, DEL_MODE_IPI);
	}
	release_schedule_lock(pcpu_id, rflag);
}

void sched_yield(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct sched_context *ctx = &per_cpu(sched_ctx, pcpu_id);

	if (ctx->scheduler->yield != NULL) {
		ctx->scheduler->yield(ctx);
	}
	make_reschedule_request(pcpu_id, DEL_MODE_IPI);
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
	idle->vm_id = 0xFF;
	idle->vcpu_id =0xFF;
	idle->thread = idle_thread;
	idle->switch_out = NULL;
	idle->switch_in = NULL;
	get_cpu_var(sched_ctx).current = idle;
	sched_init_data(idle);
	sched_set_status(idle, SCHED_STS_RUNNING);

	run_sched_thread(idle);
}
