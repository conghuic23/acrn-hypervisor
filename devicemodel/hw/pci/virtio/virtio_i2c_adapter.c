/*-
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <openssl/md5.h>

#include "dm.h"
#include "pci_core.h"
#include "virtio.h"

static int virtio_i2c_debug=1;
#define DPRINTF(params) do { if (virtio_i2c_debug) printf params; } while (0)
#define WPRINTF(params) (printf params)


struct virtio_i2cadap_msg {
	struct virtio_i2cadap *i2cadap;
	uint8_t *status;
	uint16_t idx;
};

/*
 * Per-device struct
 */
struct virtio_i2cadap {
	struct virtio_base base;
	pthread_mutex_t mtx;
	struct virtio_vq_info vq;
	char ident[256];
	struct virtio_i2cadap_msg msgs[64];
};

static void virtio_i2cadap_reset(void *);
static void virtio_i2cadap_notify(void *, struct virtio_vq_info *);

static struct virtio_ops virtio_i2cadap_ops = {
	"virtio_i2cadap",		/* our name */
	1,			/* we support 1 virtqueue */
	0, /* config reg size */
	virtio_i2cadap_reset,	/* reset */
	virtio_i2cadap_notify,	/* device-wide qnotify */
	NULL,	/* read PCI config */
	NULL,	/* write PCI config */
	NULL,			/* apply negotiated features */
	NULL,			/* called on guest set status */
};

static void
virtio_i2cadap_reset(void *vdev)
{
	struct virtio_i2cadap *i2cadap = vdev;

	DPRINTF(("virtio_i2cadap: device reset requested !\n"));
	virtio_reset_dev(&i2cadap->base);
}

static void
virtio_i2cadap_done()
{
	/*
	 * Return the descriptor back to the host.
	 * We wrote 1 byte (our status) to host.
	 */
	printf("virtio_i2cadap_done \n");
}

static void
virtio_i2cadap_proc(struct virtio_i2cadap *i2cadap, struct virtio_vq_info *vq)
{
	printf("virtio_i2cadap_proc enter\n");
	virtio_i2cadap_done();
}

static void
virtio_i2cadap_notify(void *vdev, struct virtio_vq_info *vq)
{
	struct virtio_i2cadap *i2cadap = vdev;

	while (vq_has_descs(vq))
		virtio_i2cadap_proc(i2cadap, vq);
}

static int
virtio_i2cadap_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	MD5_CTX mdctx;
	u_char digest[16];
	struct virtio_i2cadap *i2cadap;
	pthread_mutexattr_t attr;
	int rc;
	int i;

	DPRINTF(("virtio_i2cadap_init enter \n"));
	i2cadap = calloc(1, sizeof(struct virtio_i2cadap));
	if (!i2cadap) {
		WPRINTF(("virtio_i2cadap: calloc returns NULL\n"));
		return -1;
	}

	for (i = 0; i < 64; i++) {
		struct virtio_i2cadap_msg *msg = &i2cadap->msgs[i];
		msg->i2cadap = i2cadap;
		msg->idx = i;
	}

	DPRINTF(("virtio_i2cadap_init enter 2 \n"));
	/* init mutex attribute properly to avoid deadlock */
	rc = pthread_mutexattr_init(&attr);
	if (rc)
		DPRINTF(("mutexattr init failed with erro %d!\n", rc));
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (rc)
		DPRINTF(("virtio_i2cadap: mutexattr_settype failed with "
					"error %d!\n", rc));

	rc = pthread_mutex_init(&i2cadap->mtx, &attr);
	if (rc)
		DPRINTF(("virtio_i2cadap: pthread_mutex_init failed with "
					"error %d!\n", rc));

	DPRINTF(("virtio_i2cadap_init enter 3 \n"));
	/* init virtio struct and virtqueues */
	virtio_linkup(&i2cadap->base, &virtio_i2cadap_ops, i2cadap, dev, &i2cadap->vq, BACKEND_VBSU);
	DPRINTF(("virtio_i2cadap_init enter 4 \n"));
	i2cadap->base.mtx = &i2cadap->mtx;

	i2cadap->vq.qsize = 64;
	/* i2cadap->vq.vq_notify = we have no per-queue notify */

	/*
	 * Create an identifier for the backing file. Use parts of the
	 * md5 sum of the filename
	 */
	DPRINTF(("virtio_i2cadap_init enter 5 \n"));
	MD5_Init(&mdctx);
	MD5_Update(&mdctx, "i2cadap", strlen("i2cadap"));
	MD5_Final(digest, &mdctx);
	DPRINTF(("virtio_i2cadap_init enter 6 \n"));
	if (snprintf(i2cadap->ident, sizeof(i2cadap->ident),
		"ACRN--%02X%02X-%02X%02X-%02X%02X", digest[0],
		digest[1], digest[2], digest[3], digest[4],
		digest[5]) >= sizeof(i2cadap->ident)) {
		WPRINTF(("virtio_i2cadap: block ident too long\n"));
	}

	/*
	 * Should we move some of this into virtio.c?  Could
	 * have the device, class, and subdev_0 as fields in
	 * the virtio constants structure.
	 */
	pci_set_cfgdata16(dev, PCIR_DEVICE, VIRTIO_DEV_I2CADAP);
	pci_set_cfgdata16(dev, PCIR_VENDOR, INTEL_VENDOR_ID);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_I2C);
	pci_set_cfgdata16(dev, PCIR_SUBDEV_0, VIRTIO_TYPE_I2CADAP);
	pci_set_cfgdata16(dev, PCIR_SUBVEND_0, INTEL_VENDOR_ID);

	if (virtio_interrupt_init(&i2cadap->base, virtio_uses_msix())) {
		free(i2cadap);
		return -1;
	}
	virtio_set_io_bar(&i2cadap->base, 0);
	DPRINTF(("init done\n"));
	return 0;
}

static void
virtio_i2cadap_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_i2cadap *i2cadap;

	if (dev->arg) {
		DPRINTF(("virtio_i2cadap: deinit\n"));
		i2cadap = (struct virtio_i2cadap *) dev->arg;
		free(i2cadap);
	}
}

struct pci_vdev_ops pci_ops_virtio_i2cadap = {
	.class_name	= "virtio-i2cadapt",
	.vdev_init	= virtio_i2cadap_init,
	.vdev_deinit	= virtio_i2cadap_deinit,
	.vdev_barwrite	= virtio_pci_write,
	.vdev_barread	= virtio_pci_read
};
DEFINE_PCI_DEVTYPE(pci_ops_virtio_i2cadap);
