/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <list.h>
#include <per_cpu.h>
#include <schedule.h>

#define CONFIG_MCU_MS	1U
#define CONFIG_CSA_MCU_NUM 5U
struct sched_bvt_data {
	/* keep list as the first item */
	struct list_head list;
	/* minimum charging unit */
	uint64_t mcu;
	/* a thread receives a share of cpu in proportion to its weight */
	uint16_t weight;
	/* virtual time advance variable, proportional to 1 / weight */
	uint64_t vt_ratio;
	/* context switch allowance in units of mcu */
	uint64_t cs_allow;
	/* the count down number of mcu until reschedule should take place */
	int64_t run_countdown;
	/* actual virtual time in units of mcu */
	int64_t avt;
	/* effective virtual time in units of mcu */
	int64_t evt;
	uint64_t residual;

	uint64_t start_tsc;
};

/*
 * @pre obj != NULL
 * @pre obj->data != NULL
 */
bool is_inqueue(struct thread_object *obj)
{
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;
	return !list_empty(&data->list);
}

/*
 * @pre obj != NULL
 * @pre obj->data != NULL
 * @pre obj->sched_ctl != NULL
 * @pre obj->sched_ctl->priv != NULL
 */
static void runqueue_add(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl =
		(struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;
	struct list_head *pos;
	struct thread_object *iter_obj;
	struct sched_bvt_data *iter_data;

	/*
	 * the earliest evt has highest priority,
	 * the runqueue is ordered by priority.
	 */

	if (list_empty(&bvt_ctl->runqueue)) {
		list_add(&data->list, &bvt_ctl->runqueue);
	} else {
		list_for_each(pos, &bvt_ctl->runqueue) {
			iter_obj = list_entry(pos, struct thread_object, data);
			iter_data = (struct sched_bvt_data *)iter_obj->data;
			if (iter_data->evt > data->evt) {
				list_add_node(&data->list, pos->prev, pos);
				break;
			}
		}
		if (!is_inqueue(obj)) {
			list_add_tail(&data->list, &bvt_ctl->runqueue);
		}
	}
}

/*
 * @pre obj != NULL
 * @pre obj->data != NULL
 */
void runqueue_remove(struct thread_object *obj)
{
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;

	list_del_init(&data->list);
}

/*
 * @brief Get the SVT (scheduler virtual time) which indicates the
 * minimum AVT of any runnable threads.
 * @pre obj != NULL
 * @pre obj->data != NULL
 * @pre obj->sched_ctl != NULL
 * @pre obj->sched_ctl->priv != NULL
 */

int64_t get_svt(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *obj_data;
	struct thread_object *tmp_obj;
	int64_t svt = 0;

	if (!list_empty(&bvt_ctl->runqueue)) {
		tmp_obj = get_first_item(&bvt_ctl->runqueue, struct thread_object, data);
		obj_data = (struct sched_bvt_data *)tmp_obj->data;
		svt = obj_data->avt;
	}
	return svt;
}

static void sched_tick_handler(__unused void *param)
{
}

int sched_bvt_init(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = &per_cpu(sched_bvt_ctl, ctl->pcpu_id);
	uint64_t tick_period = CONFIG_MCU_MS * CYCLES_PER_MS;
	int ret = 0;

	ASSERT(get_pcpu_id() == ctl->pcpu_id, "Init scheduler on wrong CPU!");

	ctl->priv = bvt_ctl;
	INIT_LIST_HEAD(&bvt_ctl->runqueue);

	/* The tick_timer is periodically */
	initialize_timer(&bvt_ctl->tick_timer, sched_tick_handler, ctl,
			rdtsc() + tick_period, TICK_MODE_PERIODIC, tick_period);

	if (add_timer(&bvt_ctl->tick_timer) < 0) {
		pr_err("Failed to add schedule tick timer!");
		ret = -1;
	}

	return ret;
}

void sched_bvt_deinit(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	del_timer(&bvt_ctl->tick_timer);
}

void sched_bvt_init_data(struct thread_object *obj)
{
	struct sched_bvt_data *data;

	data = (struct sched_bvt_data *)obj->data;
	INIT_LIST_HEAD(&data->list);
	data->mcu = CONFIG_MCU_MS * CYCLES_PER_MS;
	/* TODO: mcu advance value should be proportional to weight. */
	data->vt_ratio = 1;
	data->residual = 0U;
	data->cs_allow = CONFIG_CSA_MCU_NUM;
	data->run_countdown = data->cs_allow;
}

static uint64_t v2p(uint64_t virt_time, uint64_t ratio)
{
	return (uint64_t)(virt_time / ratio);
}

static uint64_t p2v(uint64_t phy_time, uint64_t ratio)
{
	return (uint64_t)(phy_time * ratio);
}

static void update_vt(struct thread_object *obj)
{
	struct sched_bvt_data *data;
	uint64_t now_tsc = rdtsc();
	uint64_t v_delta, delta_mcu = 0U;

	data = (struct sched_bvt_data *)obj->data;

	/* update current thread's avt and evt */
	if (now_tsc > data->start_tsc) {
		v_delta = p2v(now_tsc - data->start_tsc, data->vt_ratio) + data->residual;
		delta_mcu = (uint64_t)(v_delta / data->mcu);
		data->residual = v_delta % data->mcu;
	}
	data->avt += delta_mcu;
	/* TODO: evt = avt - (warp ? warpback : 0U) */
	data->evt = data->avt;

	/* Ignore the idle object, inactive objects */
	if (is_inqueue(obj)) {
		runqueue_remove(obj);
		runqueue_add(obj);
	}
}

static struct thread_object *sched_bvt_pick_next(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct thread_object *first_obj = NULL, *second_obj = NULL;
	struct sched_bvt_data *first_data = NULL, *second_data = NULL;
	struct list_head *first, *sec;
	struct thread_object *next = NULL;
	struct thread_object *current = ctl->curr_obj;
	uint64_t now_tsc = rdtsc();
	uint64_t delta_mcu = 0U;

	if (!is_idle_thread(current)) {
		update_vt(current);
	}

	if (!list_empty(&bvt_ctl->runqueue)) {
		first = bvt_ctl->runqueue.next;
		sec = (first->next == &bvt_ctl->runqueue) ? NULL : first->next;

		first_obj = list_entry(first, struct thread_object, data);
		first_data = (struct sched_bvt_data *)first_obj->data;

		if (sec != NULL) {
			second_obj = list_entry(sec, struct thread_object, data);
			second_data = (struct sched_bvt_data *)second_obj->data;
			delta_mcu = second_data->evt - first_data->evt;
			/* run_countdown is the real time the thread can run */
			first_data->run_countdown = v2p(delta_mcu, first_data->vt_ratio)
				+ first_data->cs_allow;
		} else {
			/* there is only one object in runqueue, it can run for a long
			 * time before reschedule */
			first_data->run_countdown = UINT64_MAX;
		}
		first_data->start_tsc = now_tsc;
		next = first_obj;
	} else {
		next = &get_cpu_var(idle);
	}

	return next;

}

static void sched_bvt_sleep(struct thread_object *obj)
{
	runqueue_remove(obj);
}

static void sched_bvt_wake(struct thread_object *obj)
{
	struct sched_bvt_data *data;
	int64_t svt, threadhold;

	/* update target not current thread's avt and evt */
	data = (struct sched_bvt_data *)obj->data;
	/* prevents a thread from claiming an excessive share
	 * of the CPU after sleeping for a long time as might happen
	 * if there was no adjustment */
	svt = get_svt(obj);
	threadhold = svt - data->cs_allow;
	/* adjusting AVT for a thread after a long sleep */
	data->avt = (data->avt > threadhold) ? data->avt : svt;
	/* TODO: evt = avt - (warp ? warpback : 0U) */
	data->evt = data->avt;
	/* add to runqueue in order */
	runqueue_add(obj);

}

struct acrn_scheduler sched_bvt = {
	.name		= "sched_bvt",
	.init		= sched_bvt_init,
	.init_data	= sched_bvt_init_data,
	.pick_next	= sched_bvt_pick_next,
	.sleep		= sched_bvt_sleep,
	.wake		= sched_bvt_wake,
	.deinit		= sched_bvt_deinit,
};
