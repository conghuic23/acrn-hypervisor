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
#include <linux/i2c-dev.h> 
#include <linux/i2c.h> 

#include "dm_string.h"
#include "i2c_core.h"
#include "acpi.h"


static int i2c_debug;
#define DPRINTF(params) do { if (i2c_debug) printf params; } while (0)
#define WPRINTF(params) (printf params)


#define I2C_MSG_OK	0
#define I2C_MSG_ERR	1
#define I2C_NO_DEV	2

uint8_t
i2c_adap_process(struct i2c_adap_vdev *adap, uint16_t addr, struct i2c_rdwr_ioctl_data *work_queue)
{
	int ret;

	if (!adap->i2cdev_enable[addr])
		return I2C_NO_DEV;

	DPRINTF(("i2c_adap_process for addr=0x%x\n", addr));
	ret = ioctl(adap->fd, I2C_RDWR, work_queue);
	if (ret < 0)
		return I2C_MSG_ERR;
	else
		return I2C_MSG_OK;
}

void
i2c_vdev_add_dsdt(struct i2c_adap_vdev *adap, uint8_t slot, uint8_t func)
{
	return;

	int i;
	for (i = 0; i< 128; i++) {
		if (!adap->i2cdev_enable[i])
			continue;
		DPRINTF(("before add dsdt for 0x%x slot=%x  func=%x\n", i, slot, func));
		dsdt_line("Device (I2C6)");
		dsdt_line("{");
		dsdt_line("    Name (_ADR, 0x%04X%04X)", slot, func);
		dsdt_line("    Name (_DDN, \"Intel(R) I2C Controller #6\")");
		dsdt_line("    Name (_UID, One)  // _UID: Unique ID");
		dsdt_line("    Name (LINK, \"\\\\_SB.PCI0.I2C6\")");
	
		dsdt_line("    Name (RBUF, ResourceTemplate ()");
		dsdt_line("    {");
		dsdt_line("    })");
		dsdt_line("    Name (IC4S, 0x00061A80)");
		dsdt_line("    Name (_DSD, Package (0x02)");
		dsdt_line("    {");
		dsdt_line("        ToUUID (\"daffd814-6eba-4d8c-8a91-bc9bbf4aa301\")"
					" ,");
		dsdt_line("        Package (0x01)");
		dsdt_line("        {");
		dsdt_line("            Package (0x02)");
		dsdt_line("            {");
		dsdt_line("                \"clock-frequency\", ");
		dsdt_line("                IC4S");
		dsdt_line("            }");
		dsdt_line("        }");
		dsdt_line("    })");
		dsdt_line("    Method (FMCN, 0, Serialized)");
		dsdt_line("    {");
		dsdt_line("        Name (PKG, Package (0x03)");
		dsdt_line("        {");
		dsdt_line("            0x64, ");
		dsdt_line("            0xD6, ");
		dsdt_line("            0x1C");
		dsdt_line("        })");
		dsdt_line("        Return (PKG)");
		dsdt_line("    }");
		dsdt_line("");
		dsdt_line("    Method (FPCN, 0, Serialized)");
		dsdt_line("    {");
		dsdt_line("        Name (PKG, Package (0x03)");
		dsdt_line("        {");
		dsdt_line("            0x26, ");
		dsdt_line("            0x50, ");
		dsdt_line("            0x0C");
		dsdt_line("        })");
		dsdt_line("        Return (PKG)");
		dsdt_line("    }");
		dsdt_line("");
		dsdt_line("    Method (HSCN, 0, Serialized)");
		dsdt_line("    {");
		dsdt_line("        Name (PKG, Package (0x03)");
		dsdt_line("        {");
		dsdt_line("            0x05, ");
		dsdt_line("            0x18, ");
		dsdt_line("            0x0C");
		dsdt_line("        })");
		dsdt_line("        Return (PKG)");
		dsdt_line("    }");
		dsdt_line("");
		dsdt_line("    Method (SSCN, 0, Serialized)");
		dsdt_line("    {");
		dsdt_line("        Name (PKG, Package (0x03)");
		dsdt_line("        {");
		dsdt_line("            0x0244, ");
		dsdt_line("            0x02DA, ");
		dsdt_line("            0x1C");
		dsdt_line("        })");
		dsdt_line("        Return (PKG)");
		dsdt_line("    }");
		dsdt_line("");
		dsdt_line("    Method (_CRS, 0, NotSerialized)");
		dsdt_line("    {");
		dsdt_line("        Return (RBUF)");
		dsdt_line("    }");
	
		dsdt_line("    Device (HDAC)");
		dsdt_line("    {");
		dsdt_line("        Name (_HID, \"INT34C3\")  // _HID: Hardware ID");
		dsdt_line("        Name (_CID, \"INT34C3\")  // _CID: Compatible ID");
		dsdt_line("        Name (_DDN, \"Intel(R) Smart Sound Technology "
				"Audio Codec\")  // _DDN: DOS Device Name");
		dsdt_line("        Name (_UID, One)  // _UID: Unique ID");
		dsdt_line("        Method (_INI, 0, NotSerialized)");
		dsdt_line("        {");
		dsdt_line("        }");
		dsdt_line("");
		dsdt_line("        Method (_CRS, 0, NotSerialized)");
		dsdt_line("        {");
		dsdt_line("            Name (SBFB, ResourceTemplate ()");
		dsdt_line("            {");
		dsdt_line("                I2cSerialBusV2 (0x006C, "
						"ControllerInitiated, 0x00061A80,");
		dsdt_line("                    AddressingMode7Bit, "
							"\"\\\\_SB.PCI0.I2C6\",");
		dsdt_line("                    0x00, ResourceConsumer, , Exclusive,");
		dsdt_line("                    )");
		dsdt_line("            })");
		dsdt_line("            Name (SBFI, ResourceTemplate ()");
		dsdt_line("            {");
		dsdt_line("            })");
		dsdt_line("            Return (ConcatenateResTemplate (SBFB, SBFI))");
		dsdt_line("        }");
		dsdt_line("");
		dsdt_line("        Method (_STA, 0, NotSerialized)  // _STA: Status");
		dsdt_line("        {");
		dsdt_line("            Return (0x0F)");
		dsdt_line("        }");
		dsdt_line("    }");
		dsdt_line("");
		dsdt_line("}");
		printf("after dsdt\n");

	}
}

struct i2c_adap_vdev *
i2c_adap_open(const char *optstr)
{
	char *nopt, *xopts, *cp;
	uint16_t slave_addr;
	struct i2c_adap_vdev *adap;
	int fd;
	int tmp;

	slave_addr = 0;
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
			strsep(&cp, "=");
			if (cp != NULL) {
				dm_strtoi(cp, NULL, 16, &tmp);
				slave_addr = (uint16_t)(tmp & 0x7F); 
			}
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
	printf("addr=%d \n", slave_addr);
	if (slave_addr > 0 && slave_addr < 128) {
		adap->i2cdev_enable[slave_addr] = 1;

		printf("after add dsdt\n");
	} else {
		WPRINTF(("slave_addr > 128, not support\n"));
		goto err;
	}
	return adap;

err:
	if (fd >= 0)
		close(fd);
	return NULL;


}
