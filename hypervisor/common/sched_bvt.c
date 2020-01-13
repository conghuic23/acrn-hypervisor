/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <list.h>
#include <per_cpu.h>
#include <schedule.h>

#define CONFIG_MCU 10UL
struct sched_bvt_data {
	/* keep list as the first item */
	struct list_head list;

	uint64_t mcu;
	uint64_t mcu_inc;
	/* 1 ~ 100 */
	uint16_t weight;
	uint64_t cs_allow;
	uint64_t count_down;
	int64_t svt;
	int64_t avt;
	int64_t evt;

	bool warp;
	uint64_t warpback;
	uint64_t warp_limit;
	uint64_t unwarp_time;
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
void runqueue_add_head(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;

	if (!is_inqueue(obj)) {
		list_add(&data->list, &bvt_ctl->runqueue);
	}
}

/*
 * @pre obj != NULL
 * @pre obj->data != NULL
 * @pre obj->sched_ctl != NULL
 * @pre obj->sched_ctl->priv != NULL
 */
void runqueue_add_tail(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;

	if (!is_inqueue(obj)) {
		list_add_tail(&data->list, &bvt_ctl->runqueue);
	}
}

static void runqueue_add(struct sched_object *obj)
{
	struct sched_bvt_control *bvt_ctl =
		(struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;
	struct list_head *pos;
	struct sched_object *iter_obj;
	struct sched_bvt_data *iter_data;
	bool insert = false;

	/* 
	 * According to EDF,
	 * the earliest evt has highest priority,
	 * the runqueue is ordered by priority.
	 */

	if (list_empty(&bvt_ctl->runqueue)) {
		list_add(&data->list, &bvt_ctl->runqueue);
	} else {
		list_for_each(pos, &bvt_ctl->runqueue) {
			iter_obj = list_entry(pos, struct sched_object, data);
			iter_data = (struct sched_data *)iter_obj->data;
			if (iter_data->evt > data->evt) {
				list_add_node(&data->list, pos->prev, pos);
				insert = true;
				break;
			}
		}
		if (!insert) {
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

static void sched_tick_handler(void *param)
{
	struct sched_control  *ctl = (struct sched_control *)param;
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct sched_bvt_data *data;
	struct thread_object *current;
	uint16_t pcpu_id = get_pcpu_id();
	uint64_t now = rdtsc();
	uint64_t rflags;

	obtain_schedule_lock(pcpu_id, &rflags);
	current = ctl->curr_obj;
	/* If no vCPU start scheduling, ignore this tick */
	if (current != NULL ) {
		if (!(is_idle_thread(current) && list_empty(&bvt_ctl->runqueue))) {
			data = (struct sched_bvt_data *)current->data;
			/* consume the left_cycles of current thread_object if it is not idle */
			if (!is_idle_thread(current)) {
				data->left_cycles -= now - data->last_cycles;
				data->last_cycles = now;
			}
			/* make reschedule request if current ran out of its cycles */
			if (is_idle_thread(current) || data->left_cycles <= 0) {
				make_reschedule_request(pcpu_id, DEL_MODE_IPI);
			}
		}
	}
	release_schedule_lock(pcpu_id, rflags);
}

/*
 * @pre ctl->pcpu_id == get_pcpu_id()
 */
int sched_bvt_init(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = &per_cpu(sched_bvt_ctl, ctl->pcpu_id);
	uint64_t tick_period = CYCLES_PER_MS;
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

void sched_iorr_deinit(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	del_timer(&bvt_ctl->tick_timer);
}

void sched_bvt_init_data(struct thread_object *obj)
{
	struct sched_bvt_data *data;

	data = (struct sched_bvt_data *)obj->data;
	INIT_LIST_HEAD(&data->list);
	data->weight = 0U;
	data->mcu = CONFIG_MCU * CYCLES_PER_MS;
	if (data->weight != 0U) {
		data->mcu_inc = (uint64_t) (10000U / data->weight);
	} else {
		data->mcu_inc = 1;
	}
	data->cs_allow = data->mcu * CONFIG_MCU_NUM;
	data->warp = false;
}

static struct thread_object *sched_bvt_pick_next(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct thread_object *next = NULL;

	/*
	 * Pick the next runnable sched object
	 * 1) get the first item in runqueue firstly
	 * 2) if object picked has no time_cycles, replenish it pick this one
	 * 3) At least take one idle sched object if we have no runnable one after step 1) and 2)
	 */
	if (!list_empty(&bvt_ctl->runqueue)) {
		next = get_first_item(&bvt_ctl->runqueue, struct thread_object, data);
		data = (struct sched_bvt_data *)next->data;
		data->last_cycles = now;
		while (data->left_cycles <= 0) {
			data->left_cycles += data->slice_cycles;
		}
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

	data = (struct sched_bvt_data *)obj->data;
	data->avt = data->svt;
	data->evt -= data->warp ? data->warpback : 0U;
	/* add to runqueue in order */
	runqueue_add(obj);
}

static void sched_bvt_do_schedule(struct thread_object *obj)
{
	struct sched_bvt_data *data, *obj_data;
	struct thread_object *current = NULL;
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct thread_object *obj = NULL;
	uint64_t now = rdtsc();

	data = (struct sched_bvt_data *)obj->data;
	data->avt = (now - data->start) / data->mcu * data->mcu_inc;

	current = ctl->curr_obj;
	data = (struct sched_bvt_data *)current->data;
	/* Ignore the idle object, inactive objects */
	if (!is_idle_thread(current) && is_inqueue(current)) {
		runqueue_remove(current);
		runqueue_add(current);
	}

	obj = get_first_item(&bvt_ctl->runqueue, struct thread_object, data);
	obj_data = (struct sched_bvt_data *)obj->data;
	data->svt = obj_data->avt;
}
struct acrn_scheduler sched_iorr = {
	.name		= "sched_bvt",
	.init		= sched_bvt_init,
	.init_data	= sched_bvt_init_data,
	.pick_next	= sched_bvt_pick_next,
	.schedule	= sched_bvt_do_schedule,
	.sleep		= sched_bvt_sleep,
	.wake		= sched_bvt_wake,
	.deinit		= sched_iorr_deinit,
};
