#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <libgen.h>

#include "dm.h"
#include "vmmapi.h"
#include "sw_load.h"
#include "acpi.h"

static char rit_path[STR_LEN];


/* RIT will get the address the APIs from the following address */
#define RIT_BIOS_HB_ADDR	(0xF2028)
#define RIT_BIOS_EOT_ADDR	(0xF202C)

/* RIT will call the APIs loaded at the following address */
#define RIT_HB_API_ADDR		(0xA1000)
#define RIT_EOT_API_ADDR	(0xA0000)

extern int rit_agent_init(struct vmctx *ctx, const char* dir);

struct obj_field_info_t
{
	uint8_t id;
	uint8_t addr_len;
	uint8_t length_len;
	uint8_t checksum_len;
};

enum {
	OBJ_FILE_TYPE       = 0x40,
	OBJ_MEM_32          = 0x41,
	OBJ_IO_MEM_32       = 0x42,
	OBJ_SYMBOL_32       = 0x43,
	OBJ_MEM_64          = 0x44,
	OBJ_IO_MEM_64       = 0x45,
	OBJ_SYMBOL_64       = 0x46,
	OBJ_EOF             = 0x4f,
};

enum bios_api_type {
	BIOS_API_HB	= 0,
	BIOS_API_EOT	= 1,
};

/* Handle only 0x41 and 0x44 at this moment */
static struct obj_field_info_t obj_fields_info[] = {
	{ OBJ_FILE_TYPE,    0,	0,	0},
	{ OBJ_MEM_32,       4,	4,	0},
	{ OBJ_IO_MEM_32,    4,	4,	0},
	{ OBJ_SYMBOL_32,    4,	1,	0},
	{ OBJ_MEM_64,       8,	4,	0},
	{ OBJ_IO_MEM_64,    8,	4,	0},
	{ OBJ_SYMBOL_64,    8,	1,	0},
	{ 0 /* 0x47 */,     0,	0,	0},
	{ 0 /* 0x48 */,     0,	0,	0},
	{ 0 /* 0x49 */,     0,	0,	0},
	{ 0 /* 0x4a */,     0,	0,	0},
	{ 0 /* 0x4b */,     0,	0,	0},
	{ 0 /* 0x4c */,     0,	0,	0},
	{ 0 /* 0x4e */,     0,	0,	0},
	{ OBJ_EOF,          0,	0,	4},
};

/* Prepare End-of-test API, which will be called at the end of test, to VM */
static int load_bios_api(struct vmctx *ctx, char* dir,
	uint64_t bios_addr, uint64_t load_addr, enum bios_api_type api_type)
{
	FILE *fp;
	char path[STR_LEN];
	uint8_t *target_addr;
	uint32_t *bios_api_addr;
	uint64_t length;
	uint64_t actual_len;

	/* RIT will get the address of bios API from bios_addr */
	bios_api_addr = (uint32_t *) (ctx->baseaddr + bios_addr);
	*bios_api_addr = load_addr;

	if (api_type == BIOS_API_HB)
		sprintf(path, "%s/rit_hb.bin", dir);
	else if (api_type == BIOS_API_EOT)
		sprintf(path, "%s/rit_eot.bin", dir);
	else
		return -1;

	fp = fopen(path, "r");
	if (fp == NULL) {
		printf("SW_LOAD ERR: could not open %s\r\n", path);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	length = ftell(fp);

	fseek(fp, 0, SEEK_SET);
	target_addr = (uint8_t *)(ctx->baseaddr + load_addr);
	if ((actual_len = fread(target_addr, 1, length, fp)) != length) {
		printf("SW_LOAD ERR: failed to load %s\r\n", path);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

static struct obj_field_info_t *get_obj_fields_info(uint8_t id)
{
	assert(id >= OBJ_FILE_TYPE && id <= OBJ_EOF);
	return &obj_fields_info[id - OBJ_FILE_TYPE];
}

static int fread_non_zero_len(unsigned long *pval, size_t len, FILE *fp)
{
    *pval = 0;
    if (len == 0)
        return 0;
    return (fread(pval, len, 1, fp) == len);
}

static inline bool is_memory_region(struct vmctx *ctx, uint64_t address)
{
	return ((address < ctx->lowmem) || ((address >= 4 * GB) && (address < (4 * GB + ctx->highmem))));
}

static int load_seed(struct vmctx *ctx, char* path)
{
	FILE *fp;
	uint8_t id;
	uint64_t address;
	uint64_t length;
	uint64_t checksum;
	uint8_t *target_addr;
	const struct obj_field_info_t *obj_field_info;

	/* Load RIT Seed to VM */
	fp = fopen(path, "r");
	if (fp == NULL) {
		fprintf(stderr, "SW_LOAD: could not open %s\r\n", path);
		return -1;
	}

	do {
		/* Get object type */
		if (fread(&id, 1, 1, fp) != 1)
			goto ERROR;

		obj_field_info = get_obj_fields_info(id);

		/* address of the object should be loaded to in VM */
		fread_non_zero_len(&address, obj_field_info->addr_len, fp);

		/* length of the object */
		fread_non_zero_len(&length, obj_field_info->length_len, fp);

		/* checksum */
		fread_non_zero_len(&checksum, obj_field_info->checksum_len, fp);

		target_addr = (uint8_t *)(ctx->baseaddr + address);

		if (address == 0xFFFFFFF0UL) {
			if (fread(target_addr, 1, length, fp) != length) {
				goto ERROR;
			}
		} else if (is_memory_region(ctx, address)) {
			if ((id == OBJ_MEM_64) || (id == OBJ_MEM_32)) {
				if(!((address > 0xF202F) || ((address + length) <= 0xF2028))){
					printf("SW_LOAD: rit is using bios region, check the seed please!\r\n");
					goto ERROR;
				}
				if (fread(target_addr, 1, length, fp) != length){
					printf("SW_LOAD: failed to load memory object!\r\n");
					goto ERROR;
				}
			} else {
				fseek(fp, length, SEEK_CUR);
				printf("SW_LOAD: skip object type 0x%x,  0x%lx bytes @ 0x%lx\r\n", id, length, address);
			}
		} else {
			printf("SW_LOAD: Cannot load object to non-memory region!\r\n");
			goto ERROR;
		}

	} while (id != OBJ_EOF);

	fclose(fp);
	return 0;
ERROR:
	fclose(fp);
	return -1;
}

static int
check_rit_image(char *path)
{
	FILE *fp;
	long len;

	fp = fopen(path, "r");

	if (fp == NULL) {
		fprintf(stderr,
			"SW_LOAD ERR: image file failed to open\n");
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);

	if (len == 0) {
		fprintf(stderr, "SW_LOAD ERR: file is empty\n");
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

size_t rit_bios_size()
{
	return 4;
}

int acrn_parse_rit(char *arg)
{
	size_t len = strlen(arg);

	if (len < STR_LEN) {
		strncpy(rit_path, arg, len + 1);
		assert(check_rit_image(rit_path) == 0);
		rit_file_name = rit_path;
		return 0;
	} else
		return -1;
}

extern bool debug_reg_enable;
int acrn_sw_load_rit(struct vmctx *ctx)
{
	char *rit_dirc;
	char *rit_dname;

	rit_dirc = strdup(rit_path);
	rit_dname = dirname(rit_dirc);

	rit_agent_init(ctx, rit_dname);

	if (load_seed(ctx, rit_path)) {
		printf("SW_LOAD: failed to load RIT seed!!\r\n");
		return -1;
	}

	if (load_bios_api(ctx, rit_dname, RIT_BIOS_HB_ADDR, RIT_HB_API_ADDR, BIOS_API_HB)) {
		printf("SW_LOAD: failed to load heart beat api!!\r\n");
		return -1;
	}

	if (load_bios_api(ctx, rit_dname, RIT_BIOS_EOT_ADDR, RIT_EOT_API_ADDR, BIOS_API_EOT)) {
		printf("SW_LOAD: failed to load end-of-test api!!\r\n");
		return -1;
	}

	free(rit_dirc);

	/* set guest bsp state. Will call hypercall set bsp state after bsp is created. */
	memset(&ctx->bsp_regs, 0, sizeof( struct acrn_set_vcpu_regs));
	ctx->bsp_regs.vcpu_id = 0;

	/* CR0_ET | CR0_NE */
	ctx->bsp_regs.vcpu_regs.cr0 = 0x30U;
	ctx->bsp_regs.vcpu_regs.cs_ar = 0x009FU;
	ctx->bsp_regs.vcpu_regs.cs_sel = 0xF000U;
	ctx->bsp_regs.vcpu_regs.cs_limit = 0xFFFFU;
	ctx->bsp_regs.vcpu_regs.cs_base = 0xFFFF0000UL;
	ctx->bsp_regs.vcpu_regs.rip = 0xFFF0UL;

	if (debug_reg_enable)
		ctx->bsp_regs.debug_flags = 1;

	printf("SW_LOAD: RIT loaded!\r\n");

	return 0;
}
