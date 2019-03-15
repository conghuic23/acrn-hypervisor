/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DUMP_H
#define DUMP_H

struct intr_excp_ctx;

void dump_intr_excp_frame(const struct intr_excp_ctx *ctx);
void dump_exception(struct intr_excp_ctx *ctx, uint16_t pcpu_id);

void debug_dump_host_state(void);
void debug_dump_guest_cpu_regs(struct acrn_vcpu *vcpu);

#endif /* DUMP_H */
