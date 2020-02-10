/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
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

	uint64_t mcu;
	uint64_t mcu_ratio;
	uint16_t weight;
	/* context switch allowance */
	uint64_t cs_allow_mcu;
	int64_t run_mcu;
	/* scheduler virtual time */
	int64_t svt_mcu;
	/* actual virtual time */
	int64_t avt_mcu;
	/* effective virtual time */
	int64_t evt_mcu;

	uint64_t start;
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
	 * the earliest evt_mcu has highest priority,
	 * the runqueue is ordered by priority.
	 */

	if (list_empty(&bvt_ctl->runqueue)) {
		list_add(&data->list, &bvt_ctl->runqueue);
	} else {
		list_for_each(pos, &bvt_ctl->runqueue) {
			iter_obj = list_entry(pos, struct thread_object, data);
			iter_data = (struct sched_bvt_data *)iter_obj->data;
			if (iter_data->evt_mcu > data->evt_mcu) {
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
	int64_t svt_mcu = 0;

	if (!list_empty(&bvt_ctl->runqueue)) {
		tmp_obj = get_first_item(&bvt_ctl->runqueue, struct thread_object, data);
		obj_data = (struct sched_bvt_data *)tmp_obj->data;
		svt_mcu = obj_data->avt_mcu;
	}
	return svt_mcu;
}

static void sched_tick_handler(__unused void *param)
{
}

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

void sched_bvt_deinit(__unused struct sched_control *ctl)
{
}

void sched_bvt_init_data(struct thread_object *obj)
{
	struct sched_bvt_data *data;

	data = (struct sched_bvt_data *)obj->data;
	INIT_LIST_HEAD(&data->list);
	data->mcu = CONFIG_MCU_MS * CYCLES_PER_MS;
	/* TODO: mcu advance value should be proportional to weight. */
	data->mcu_ratio = 1;
	data->cs_allow_mcu = CONFIG_CSA_MCU_NUM;
	data->run_mcu = data->cs_allow_mcu;
}

static struct thread_object *sched_bvt_pick_next(__unused struct sched_control *ctl)
{
	return NULL;
}

static void sched_bvt_sleep(__unused struct thread_object *obj)
{
	runqueue_remove(obj);
}

static void sched_bvt_wake(__unused struct thread_object *obj)
{
	struct sched_bvt_data *data;

	/* update target not current thread's avt_mcu and evt_mcu */
	data = (struct sched_bvt_data *)obj->data;
	/* prevents a thread from claiming an excessive share
	 * of the CPU after sleeping for a long time as might happen
	 * if there was no adjustment */
	data->svt_mcu = get_svt(obj);
	data->avt_mcu = (data->avt_mcu > data->svt_mcu) ? data->avt_mcu : data->svt_mcu;
	/* TODO: evt_mcu = avt_mcu - (warp ? warpback : 0U) */
	data->evt_mcu = data->avt_mcu;
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
