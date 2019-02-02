/*-
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * Copyright (c) 2009-2012 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _I2C_CORE_H_
#define _I2C_CORE_H_

#include "types.h"
#include <stdbool.h>
#include <sys/queue.h>

#define MAX_I2c_VDEV		128

/*i2c device acpi desc*/
#define I2C_ADAPTER(slot, func, pci_bus, i2c_bus)					\
	do {										\
		dsdt_line("Device (I2C6)");						\
		dsdt_line("{");								\
		dsdt_line("    Name (_ADR, 0x%04X%04X)", slot, func);			\
		dsdt_line("    Name (_DDN, \"Intel(R) I2C Controller #6\")");		\
		dsdt_line("    Name (_UID, One)");					\
		dsdt_line("    Name (LINK, \"\\\\_SB.PCI%d.I2C%d\")",			\
						pci_bus, i2c_bus);			\
		dsdt_line("    Name (RBUF, ResourceTemplate ()");			\
		dsdt_line("    {");							\
		dsdt_line("    })");							\
		dsdt_line("    Name (IC0S, 0x00061A80)");				\
		dsdt_line("    Name (_DSD, Package (0x02)");				\
		dsdt_line("    {");							\
		dsdt_line("        ToUUID (\"daffd814-6eba-4d8c-8a91-bc9bbf4aa301\")"	\
					" ,");						\
		dsdt_line("        Package (0x01)");					\
		dsdt_line("        {");							\
		dsdt_line("            Package (0x02)");				\
		dsdt_line("            {");						\
		dsdt_line("                \"clock-frequency\", ");			\
		dsdt_line("                IC0S");					\
		dsdt_line("            }");						\
		dsdt_line("        }");							\
		dsdt_line("    })");							\
		dsdt_line("");								\
		dsdt_line("    Method (_CRS, 0, NotSerialized)");			\
		dsdt_line("    {");							\
		dsdt_line("        Return (RBUF)");					\
		dsdt_line("    }");							\
		dsdt_line("}");								\
	} while(0)

	/* CAM1 */
#define I2C_CAM1(pci_bus, i2c_bus)							\
	do {										\
		dsdt_line("Scope(I2C%d)", i2c_bus);			\
		dsdt_line("{");								\
		dsdt_line("    Device (CAM1)");						\
		dsdt_line("    {");							\
		dsdt_line("        Name (_ADR, Zero)  // _ADR: Address");		\
		dsdt_line("        Name (_HID, \"ADV7481A\")  // _HID: Hardware ID");	\
		dsdt_line("        Name (_CID, \"ADV7481A\")  // _CID: Compatible ID");	\
		dsdt_line("        Name (_UID, One)  // _UID: Unique ID");		\
		dsdt_line("        Method (_CRS, 0, Serialized)");			\
		dsdt_line("        {");							\
		dsdt_line("            Name (SBUF, ResourceTemplate ()");		\
		dsdt_line("            {");						\
		dsdt_line("                GpioIo (Exclusive, PullDefault, 0x0000, "	\
						"0x0000, IoRestrictionInputOnly,");	\
		dsdt_line("                    \"\\\\_SB.GPO0\", 0x00, "		\
						"ResourceConsumer, ,");			\
		dsdt_line("                    )");					\
		dsdt_line("                    {   // Pin list");			\
		dsdt_line("                        0x001E");				\
		dsdt_line("                    }");					\
		dsdt_line("                I2cSerialBusV2 (0x0070, "			\
						"ControllerInitiated, 0x00061A80,");	\
		dsdt_line("                    AddressingMode7Bit, "			\
							"\"\\\\_SB.PCI%d.I2C%d\",",	\
								pci_bus, i2c_bus);	\
		dsdt_line("                    0x00, ResourceConsumer, , Exclusive,");	\
		dsdt_line("                    )");					\
		dsdt_line("            })");						\
		dsdt_line("            Return (SBUF)");					\
		dsdt_line("        }");							\
		dsdt_line("        Method (_DSM, 4, NotSerialized)");			\
		dsdt_line("        {");							\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"377ba76a-f390-4aff-ab38-9b1bf33a3015\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (\"ADV7481A\")");			\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"ea3b7bd8-e09b-4239-ad6e-ed525f3f26ab\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (0x40)");				\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"8dbe2651-70c1-4c6f-ac87-a37cb46e4af6\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (0xFF)");				\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"26257549-9271-4ca4-bb43-c4899d5a4881\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                If (Arg2 == One)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02)");				\
		dsdt_line("                }");						\
		dsdt_line("                If (Arg2 == 0x02)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02001000)");			\
		dsdt_line("                }");						\
		dsdt_line("                If (Arg2 == 0x03)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02000E01)");			\
		dsdt_line("                }");						\
		dsdt_line("            }");						\
		dsdt_line("            Return (Zero)");					\
		dsdt_line("        }");							\
		dsdt_line("    }");							\
		dsdt_line("}");								\
	} while (0)

	/* CAM2 */
#define I2C_CAM2(pci_bus, i2c_bus)							\
	do {										\
		dsdt_line("Scope(I2C%d)", i2c_bus);			\
		dsdt_line("{");								\
		dsdt_line("    Device (CAM2)");						\
		dsdt_line("    {");							\
		dsdt_line("        Name (_ADR, Zero)  // _ADR: Address");		\
		dsdt_line("        Name (_HID, \"ADV7481B\")  // _HID: Hardware ID");	\
		dsdt_line("        Name (_CID, \"ADV7481B\")  // _CID: Compatible ID");	\
		dsdt_line("        Name (_UID, One)  // _UID: Unique ID");		\
		dsdt_line("        Method (_CRS, 0, Serialized)");			\
		dsdt_line("        {");							\
		dsdt_line("            Name (SBUF, ResourceTemplate ()");		\
		dsdt_line("            {");						\
		dsdt_line("                GpioIo (Exclusive, PullDefault, 0x000, "	\
						"0x0000, IoRestrictionInputOnly,");	\
		dsdt_line("                    \"\\\\_SB.GPO0\", 0x00, "		\
						"ResourceConsumer, ,");			\
		dsdt_line("                    )");					\
		dsdt_line("                    {   // Pin list");			\
		dsdt_line("                        0x001E");				\
		dsdt_line("                    }");					\
		dsdt_line("                I2cSerialBusV2 (0x0071, "			\
						"ControllerInitiated, 0x00061A80,");	\
		dsdt_line("                    AddressingMode7Bit, "			\
							"\"\\\\_SB.PCI%d.I2C%d\",",	\
								pci_bus, i2c_bus);	\
		dsdt_line("                    0x00, ResourceConsumer, , Exclusive,");	\
		dsdt_line("                    )");					\
		dsdt_line("            })");						\
		dsdt_line("            Return (SBUF)");					\
		dsdt_line("        }");							\
		dsdt_line("        Method (_DSM, 4, NotSerialized) ");			\
		dsdt_line("        {");							\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"377ba76a-f390-4aff-ab38-9b1bf33a3015\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (\"ADV7481B\")");			\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"ea3b7bd8-e09b-4239-ad6e-ed525f3f26ab\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (0x14)");				\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"8dbe2651-70c1-4c6f-ac87-a37cb46e4af6\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                Return (0xFF)");				\
		dsdt_line("            }");						\
		dsdt_line("");								\
		dsdt_line("            If ((Arg0 == ToUUID ("				\
					"\"26257549-9271-4ca4-bb43-c4899d5a4881\")))");	\
		dsdt_line("            {");						\
		dsdt_line("                If (Arg2 == One)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02)");				\
		dsdt_line("                }");						\
		dsdt_line("                If (Arg2 == 0x02)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02001000)");			\
		dsdt_line("                }");						\
		dsdt_line("                If (Arg2 == 0x03)");				\
		dsdt_line("                {");						\
		dsdt_line("                    Return (0x02000E01)");			\
		dsdt_line("                }");						\
		dsdt_line("            }");						\
		dsdt_line("            Return (Zero)");					\
		dsdt_line("        }");							\
		dsdt_line("    }");							\
		dsdt_line("");								\
		dsdt_line("}");								\
	} while (0)


#define I2C_HDAC(pci_bus, i2c_bus)							\
	do {										\
		dsdt_line("Scope(I2C%d)", i2c_bus);			\
		dsdt_line("{");								\
		dsdt_line("    Device (HDAC)");						\
		dsdt_line("    {");							\
		dsdt_line("        Name (_HID, \"INT34C3\")  // _HID: Hardware ID");	\
		dsdt_line("        Name (_CID, \"INT34C3\")  // _CID: Compatible ID");	\
		dsdt_line("        Name (_DDN, \"Intel(R) Smart Sound Technology "	\
				"Audio Codec\")  // _DDN: DOS Device Name");		\
		dsdt_line("        Name (_UID, One)  // _UID: Unique ID");		\
		dsdt_line("        Method (_INI, 0, NotSerialized)");			\
		dsdt_line("        {");							\
		dsdt_line("        }");							\
		dsdt_line("");								\
		dsdt_line("        Method (_CRS, 0, NotSerialized)");			\
		dsdt_line("        {");							\
		dsdt_line("            Name (SBFB, ResourceTemplate ()");		\
		dsdt_line("            {");						\
		dsdt_line("                I2cSerialBusV2 (0x006C, "			\
						"ControllerInitiated, 0x00061A80,");	\
		dsdt_line("                    AddressingMode7Bit, "			\
							"\"\\\\_SB.PCI%d.I2C%d\",",	\
								pci_bus, i2c_bus);	\
		dsdt_line("                    0x00, ResourceConsumer, , Exclusive,");	\
		dsdt_line("                    )");					\
		dsdt_line("            })");						\
		dsdt_line("            Name (SBFI, ResourceTemplate ()");		\
		dsdt_line("            {");						\
		dsdt_line("            })");						\
		dsdt_line("            Return (ConcatenateResTemplate (SBFB, SBFI))");	\
		dsdt_line("        }");							\
		dsdt_line("");								\
		dsdt_line("        Method (_STA, 0, NotSerialized)  // _STA: Status");	\
		dsdt_line("        {");							\
		dsdt_line("            Return (0x0F)");					\
		dsdt_line("        }");							\
		dsdt_line("    }");							\
		dsdt_line("}");								\
	} while (0)


struct i2c_adap_msg {
	TAILQ_ENTRY(i2c_adap_msg) link;
	struct	i2c_msg msg;
	void 	*params;
	void 	(*callback)(struct i2c_adap_msg *iamsg, uint8_t status);
};

#define MAX_MSG_NUM 32
struct i2c_adap_vdev {
	int		fd;
	int		i2cdev_enable[MAX_I2c_VDEV];
	bool		adap_add;
	pthread_t	tid;
	pthread_mutex_t	mtx;
	pthread_cond_t	cond;
	TAILQ_HEAD(, i2c_adap_msg) msg_queue;
	bool		closing;
};

struct i2c_adap_vdev *i2c_adap_open(const char *optstr);
void i2c_adap_queue_msg(struct i2c_adap_vdev *adap, struct i2c_adap_msg *imsg);
void i2c_vdev_add_dsdt(struct i2c_adap_vdev *adap, uint8_t slot, uint8_t func);
int i2c_adap_close(struct i2c_adap_vdev *adap);
#endif
