/*-
 * Copyright (c) 2013  Peter Grehan <grehan@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <linux/i2c-dev.h> 
#include <linux/i2c.h> 

#include "dm_string.h"
#include "i2c_core.h"
#include "acpi.h"


static int i2c_debug=1;
#define DPRINTF(params) do { if (i2c_debug) printf params; } while (0)
#define WPRINTF(params) (printf params)


#define I2C_MSG_OK	0
#define I2C_MSG_ERR	1
#define I2C_NO_DEV	2

static void
i2c_adap_process(struct i2c_adap_vdev *adap, struct i2c_adap_msg *imsg)
{
	int ret;
	uint16_t addr;
	struct i2c_msg *msg;
	struct i2c_rdwr_ioctl_data work_queue;
	uint8_t status;

	msg = &imsg->msg;
	addr = msg->addr;
	if (!adap->i2cdev_enable[addr]) {
		status = I2C_NO_DEV;
		goto done;
	}
		

	work_queue.nmsgs = 1;
	work_queue.msgs = msg;

	DPRINTF(("i2c_adap_process for addr=0x%x\n", addr));
	ret = ioctl(adap->fd, I2C_RDWR, work_queue);
	if (ret < 0)
		status = I2C_MSG_ERR;
	else
		status = I2C_MSG_OK;
done:
	(*imsg->callback)(imsg, status);
	return;
}

void
i2c_vdev_add_dsdt(struct i2c_adap_vdev *adap, uint8_t slot, uint8_t func)
{
	int i;

	/*hardcode pci bus to 0, i2c bus to 6*/
	if (!adap->adap_add) {
		I2C_ADAPTER(slot, func, 0, 6);
		adap->adap_add = true;
		DPRINTF(("add adapter on pci0:%x.%x i2c6", slot, func));
	}

	for (i = 0; i< 128; i++) {
		if (!adap->i2cdev_enable[i])
			continue;
		DPRINTF(("before add dsdt for 0x%x slot=%x  func=%x\n", i, slot, func));
		/*hardcode i2c bus to 6*/
		if (i == 0x1C) {
			I2C_HDAC(0, 6);
			DPRINTF(("add HDAC on pci0.i2c6"));
		} else if(i == 0x70) {
			I2C_CAM1(0, 6);
			DPRINTF(("add CAM1 on pci0.i2c6"));
		} else if(i == 0x71) {
			I2C_CAM2(0, 6);
			DPRINTF(("add CAM2 on pci0.i2c6"));
		} else
			continue;
	}
}

void
i2c_adap_queue_msg(struct i2c_adap_vdev *adap, struct i2c_adap_msg *imsg)
{
	pthread_mutex_lock(&adap->mtx);
	TAILQ_INSERT_TAIL(&adap->msg_queue, imsg, link);
	pthread_mutex_unlock(&adap->mtx);
	pthread_cond_signal(&adap->cond);
}

static void *
i2c_adap_thr(void *arg)
{
	struct i2c_adap_msg *imsg;
	struct i2c_adap_vdev *adap;

	adap = arg;
	for (;;) {
		pthread_mutex_lock(&adap->mtx);
		imsg = TAILQ_FIRST(&adap->msg_queue);
		pthread_mutex_unlock(&adap->mtx);
		if (imsg) {
			i2c_adap_process(adap, imsg);
		}
		if (adap->closing)
			break;
		pthread_cond_wait(&adap->cond, &adap->mtx);
	}
	pthread_exit(NULL);

	return NULL;
}

struct i2c_adap_vdev *
i2c_adap_open(const char *optstr)
{
	char *nopt, *xopts, *cp, *t;
	uint16_t slave_addr[128];
	int i, total;
	struct i2c_adap_vdev *adap;
	int fd;
	int tmp;

	nopt = xopts = strdup(optstr);
	printf("optstr = %s \n", optstr);
	if (!nopt) {
		WPRINTF(("i2c_open: strdup returns NULL \n"));
		return NULL;
	}
	while (xopts != NULL) {
		cp = strsep(&xopts, ",");
		if (cp == nopt)
			continue;
		else if (!strncmp(cp, "slave", strlen("slave"))) {
			printf("slave cp=%s\n",cp);
			i = 0;
			strsep(&cp, "=");
			while (cp != NULL && *cp !='\0') {
				if (*cp == ':') cp++;
				dm_strtoi(cp, &t, 16, &tmp);
				slave_addr[i++] = (uint16_t)(tmp & 0x7F); 
				if (t == cp)
					break;
				else
					cp = t;
			}
			total = i;
		}
		else
			WPRINTF(("not support options\n"));
	}

	fd = open(nopt, O_RDWR);
	if (fd < 0) {
		WPRINTF(("i2c_open: failed to open %s\n", nopt));
		goto err;
	}

	printf("after open\n");
	adap = calloc(1, sizeof(struct i2c_adap_vdev));
	if (adap == NULL) {
		perror("calloc");
		goto err;
	}
	
	printf("after alloc\n");
	adap->fd = fd;
	adap->adap_add = false;
	adap->closing = false;
	pthread_mutex_init(&adap->mtx, NULL);
	pthread_cond_init(&adap->cond, NULL);
	TAILQ_INIT(&adap->msg_queue);

	pthread_create(&adap->tid, NULL, i2c_adap_thr, adap);

	for (i = 0; i < total; i++) {
		if (slave_addr[i] > 0 && slave_addr[i] < 128) {
			adap->i2cdev_enable[slave_addr[i]] = 1;
			DPRINTF(("add slave 0x%x\n", slave_addr[i]));
		} else {
			WPRINTF(("slave_addr > 128, not support\n"));
			goto err;
		}
	}
	return adap;

err:
	if (fd >= 0)
		close(fd);
	return NULL;


}

int
i2c_adap_close(struct i2c_adap_vdev *adap)
{
	void *jval;

	pthread_mutex_lock(&adap->mtx);
	adap->closing = 1;
	pthread_mutex_unlock(&adap->mtx);
	pthread_cond_broadcast(&adap->cond);
	pthread_join(adap->tid, &jval);

	close(adap->fd);
	free(adap);

	return 0;
}
