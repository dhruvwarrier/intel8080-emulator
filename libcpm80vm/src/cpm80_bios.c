
#include "i8080.h"
#include "cpm80_vm.h"
#include "cpm80_devices.h"
#include "cpm80_bios.h"

#define lsbit(buf) (buf & (unsigned)1) 
#define concatenate(byte1, byte2) ((unsigned)(byte1) << 8 | byte2)

#define cpm80_bios_get_args_bc(cpu) concatenate((cpu)->b, (cpu)->c)
#define cpm80_bios_get_args_de(cpu) concatenate((cpu)->d, (cpu)->e)

static inline void cpm80_bios_set_return_hl(struct i8080 *const cpu, unsigned val)
{
	cpu->h = (i8080_word_t)((val & 0xff00) >> 8);
	cpu->l = (i8080_word_t)(val & 0xff);
}

#define cpm80_serial_device_in(cpu, ldev)  ((cpu)->a = (i8080_word_t)((ldev)->in((ldev)->dev)))
#define cpm80_serial_device_out(cpu, ldev) ((ldev)->out((ldev)->dev, (char)((cpu)->c)))

static inline void cpm80_serial_device_status(struct i8080 *const cpu, struct cpm80_serial_device *const ldev)
{
	int status = ldev->status(ldev->dev);
	if (status) cpu->a = 0xff;
	else cpu->a = 0x00;
}

static inline void cpm80_set_disk_exitcode(struct i8080 *const cpu, int exitcode)
{
	switch (exitcode)
	{
		case -1: cpu->a = 0xff; break;
		default: cpu->a = (i8080_word_t)exitcode;
	}
}

static inline void cpm80_select_disk(struct cpm80_vm *const vm, int disknum)
{
	int err;
	struct cpm80_disk_device *disk = &vm->disks[disknum];

	if (!lsbit(vm->cpu->e)) {
		/* this will determine and generate the disk format, if possible */
		err = disk->init(disk->dev);
	}

	if (err) {
		cpm80_bios_set_return_hl(vm->cpu, 0);
		return;
	}

	cpm80_bios_set_return_hl(vm->cpu, disk->dph_addr);
	vm->selected_disk = disknum;
}

static inline void cpm80_disk_set_track(struct cpm80_vm *const vm, int disknum, unsigned track)
{
	struct cpm80_disk_device *disk = &vm->disks[disknum];
	disk->set_track(disk->dev, track);
}

static inline void cpm80_disk_set_sector(struct cpm80_vm *const vm, int disknum, unsigned sector)
{
	struct cpm80_disk_device *disk = &vm->disks[disknum];
	disk->set_sector(disk->dev, sector);
}

static inline void cpm80_disk_read(struct cpm80_vm *const vm, int disknum)
{
	int err;
	struct cpm80_disk_device *disk = &vm->disks[disknum];

	char buf[128];
	err = disk->readl(disk->dev, buf);

	cpm80_set_disk_exitcode(vm->cpu, err);
	if (err) return;

	unsigned long i;
	unsigned long start = (unsigned long)vm->disk_dma_addr, end = start + 128;
	for (i = start; i < end; ++i) {
		vm->cpu->memory_write((i8080_addr_t)i, buf[i]);
	}
}

static inline void cpm80_disk_write(struct cpm80_vm *vm, int disknum)
{
	int err;
	struct cpm80_disk_device *disk = &vm->disks[disknum];

	char buf[128];
	unsigned long i;
	unsigned long start = (unsigned long)vm->disk_dma_addr, end = start + 128;
	for (i = start; i < end; ++i) {
		buf[i] = vm->cpu->memory_read((i8080_addr_t)i);
	}

	err = disk->writel(disk->dev, buf, (int)vm->cpu->c);
	cpm80_set_disk_exitcode(vm->cpu, err);
}

static inline void cpm80_disk_sectran(struct cpm80_vm *vm)
{
	unsigned lsector = cpm80_bios_get_args_bc(vm->cpu);
	unsigned sectran_table_ptr = cpm80_bios_get_args_de(vm->cpu);

	unsigned psector;
	if (sectran_table_ptr == 0) {
		/* If the table ptr is NULL, this function should never have been called.
		 * But the BDOS has a bug where this is not validated before being passed to SECTRAN:
		 * http://cpuville.com/Code/CPM-on-a-new-computer.html
		 * Return the sector number without translation. */
		psector = lsector;
	} else {
		psector = vm->cpu->memory_read((i8080_addr_t)(sectran_table_ptr + lsector));
	}

	cpm80_bios_set_return_hl(vm->cpu, psector);
}

int cpm80_bios_call_function(struct cpm80_vm *const vm, int call_code)
{
	int err = 0;
	struct i8080 *const cpu = vm->cpu; 
	struct cpm80_disk_device *const disks = vm->disks;
	struct cpm80_serial_device *const con = vm->con,
		*const lst = vm->lst, *const pun = vm->pun, *const rdr = vm->rdr;

	switch (call_code)
	{
		case BIOS_BOOT:
			/* todo */
			break;

		case BIOS_WBOOT:
			/* todo */
			break;

		case BIOS_CONST:  cpm80_serial_device_status(cpu, con); break;
		case BIOS_LISTST: cpm80_serial_device_status(cpu, lst); break;

		case BIOS_CONIN:  cpm80_serial_device_in(cpu, con); break;
		case BIOS_CONOUT: cpm80_serial_device_out(cpu, con); break;
		case BIOS_LIST:   cpm80_serial_device_out(cpu, lst); break;
		case BIOS_PUNCH:  cpm80_serial_device_out(cpu, pun); break;
		case BIOS_READER: cpm80_serial_device_in(cpu, rdr); break;

		case BIOS_SELDSK: cpm80_select_disk(vm, cpu->c); break;
		case BIOS_HOME:   cpm80_disk_set_track(vm, vm->selected_disk, 0); break;
		case BIOS_SETTRK: cpm80_disk_set_track(vm, vm->selected_disk, cpm80_bios_get_args_bc(cpu)); break;
		case BIOS_SETSEC: cpm80_disk_set_sector(vm, vm->selected_disk, cpm80_bios_get_args_bc(cpu)); break;

		case BIOS_SETDMA: vm->disk_dma_addr = (cpm80_addr_t)cpm80_bios_get_args_bc(cpu); break;

		case BIOS_READ:    cpm80_disk_read(vm, vm->selected_disk); break;
		case BIOS_WRITE:   cpm80_disk_write(vm, vm->selected_disk); break;
		case BIOS_SECTRAN: cpm80_disk_sectran(vm); break;

		default: err = 1; /* unrecognized BIOS call */
	}

	return err;
}

static void write_zeros(struct cpm80_vm *const vm, cpm80_addr_t buf, unsigned long buflen)
{
	unsigned long i;
	const unsigned long end = (unsigned long)buf + buflen;
	for (i = (unsigned long)buf; i < end; ++i) {
		vm->cpu->memory_write((i8080_addr_t)i, 0x00);
	}
}

static inline void write_word(void(*write_byte)(i8080_addr_t, i8080_word_t), cpm80_addr_t addr, cpm80_word_t word)
{
	i8080_word_t lo = (i8080_word_t)(word & 0x00ff), hi = (i8080_word_t)((word & 0xff00) >> 8);
	write_byte((i8080_addr_t)addr++, hi);
	write_byte((i8080_addr_t)addr, lo);
}

/* see cpm80_bios.h */
struct disk_params
{
	unsigned dn, dm, fsc, lsc, skf,
		bls, dks, dir, cks, ofs, k16;
};

struct disk_param_block
{
	/* Sectors per track, Disk size max, Directory max, Checksum size, Track offset */
	cpm80_word_t spt, dsm, drm, cks, off;
	/* Block shift, Block mask, Extent mask, Allocation bitmap bytes 0 & 1 */
	cpm80_byte_t bsh, blm, exm, al0, al1;
};

struct disk_header
{
	/* Addresses of: sector translate table, directory buffer,
	                 disk parameter block, checksum vector, allocation vector */
	cpm80_addr_t xlt, dirbuf, dpb, csv, alv;
};

struct disk_shared_params
{
	/* allocation vector size, checksum vector size */
	unsigned als, css;
};

static void get_disk_parameter_block(struct disk_params *params, struct disk_param_block *param_block)
{
	/* end - start + 1 */
	param_block->spt = (cpm80_word_t)(params->lsc - params->fsc + 1);

	param_block->dsm = (cpm80_word_t)(params->dks - 1);
	param_block->drm = (cpm80_word_t)(params->dir - 1);
	/* 2 checksum bits for each directory entry */
	param_block->cks = (cpm80_word_t)(params->cks / 4);
	param_block->off = (cpm80_word_t)params->ofs;

	int i;
	/* blk_shift shifts a block number to the first sector of that block.
	 * blk_mask masks a sector number to gets the sector offset into the block it resides in
	 * For eg. blk_shift=3, blk_mask=0x07 implies 8 * 128 = 1K blocks
	 *         blk_shift=4, blk_mask=0x0f implies 16 * 128 = 2K blocks
	 *         blk_shift=5, blk_mask=0x1f implies 32 * 128 = 4K blocks
	 *         and so on. 
	 */
	unsigned blk_shift = 0, blk_mask = 0;
	unsigned sectors_per_block = params->bls / 128;
	for (i = 0; i < 16; ++i) {
		if (sectors_per_block == 1) break;
		blk_shift += 1;
		blk_mask = (blk_mask << 1) | 1;
		sectors_per_block >>= 1;
	}

	param_block->bsh = (cpm80_byte_t)blk_shift;
	param_block->blm = (cpm80_byte_t)blk_mask;

	/* Files can be composed of multiple logical "extents," each of which span 128 sectors (16K).
	 * A directory entry can map 16 blocks, so if the block size is 2K every entry corresponds to
	 * two logical extents. An entry has 2 bytes (ex/s2) for the number of extents mapped by it.
	 * The extent_mask masks ex to limit it to the number of extents mappable by an entry. */
	unsigned extent_mask = 0;
	if (params->k16 != 0) {
		unsigned kilobytes_per_block = params->bls / 1024;
		for (i = 0; i < 16; ++i) {
			if (kilobytes_per_block == 1) break;
			extent_mask = (extent_mask << 1) | 1;
			kilobytes_per_block >>= 1;
		}
		/* The entry allocation map is 16 bytes - each byte is the address of a block. 
		 * For more than 256 blocks a disk, we need two bytes per address resulting
		 * in half the number of blocks. As a result the available extents are also halved. */
		if (params->dks > 256) extent_mask >>= 1;
	}

	param_block->exm = (cpm80_byte_t)extent_mask;

	/* Generate the directory allocation bitmap (maps blocks allocated to the disk directory) */
	unsigned dir_alloc = 0;
	unsigned dir_remaining = params->dir;
	unsigned dir_block_size = params->bls / 32;
	for (i = 0; i < 16; ++i) {
		if (dir_remaining == 0) break;
		dir_alloc = (dir_alloc >> 1) | 0x8000; /* bits fill in from the left */
		if (dir_remaining > dir_block_size) dir_remaining -= dir_block_size;
		else dir_remaining = 0;
	}

	param_block->al0 = (cpm80_byte_t)(dir_alloc >> 8);
	param_block->al1 = (cpm80_byte_t)(dir_alloc & 0xff);
}

/* Written in order: spt, bsh, blm, exm, dsm, drm, al0, al1, cks, off. */
static void write_disk_param_block(struct cpm80_vm *vm, struct disk_param_block *param_block, cpm80_addr_t buf_ptr)
{
	void(*write_byte)(i8080_addr_t, i8080_word_t) = vm->cpu->memory_write;
	write_word(write_byte, buf_ptr, param_block->spt); buf_ptr += 2;
	write_byte(buf_ptr, param_block->bsh); buf_ptr += 1;
	write_byte(buf_ptr, param_block->blm); buf_ptr += 1;
	write_byte(buf_ptr, param_block->exm); buf_ptr += 1;
	write_word(write_byte, buf_ptr, param_block->dsm); buf_ptr += 2;
	write_word(write_byte, buf_ptr, param_block->drm); buf_ptr += 2;
	write_byte(buf_ptr, param_block->al0); buf_ptr += 1;
	write_byte(buf_ptr, param_block->al1); buf_ptr += 1;
	write_word(write_byte, buf_ptr, param_block->cks); buf_ptr += 2;
	write_word(write_byte, buf_ptr, param_block->off);
}

/* Returns total vector size (als + css) */
static unsigned write_alv_csv_with_size(struct cpm80_vm *vm, 
	struct disk_header *headers, int disknum, unsigned als, unsigned css, cpm80_addr_t buf)
{
	unsigned vtotal_size = als + css;

	headers[disknum].alv = buf;
	headers[disknum].csv = buf + (cpm80_addr_t)als;
	write_zeros(vm, buf, (unsigned long)vtotal_size);

	return vtotal_size;
}

/* Returns total vector size (als + css) */
static unsigned write_alv_csv(struct cpm80_vm *vm, struct disk_params *params, 
	struct disk_shared_params *shared_params, struct disk_header *headers, int disknum, cpm80_addr_t buf)
{
	unsigned csv_size = params->cks / 4;
	unsigned alv_size = params->dks / 8;
	if (alv_size % 8 != 0) alv_size++; /* ciel */

	shared_params[disknum].als = alv_size;
	shared_params[disknum].css = csv_size;

	return write_alv_csv_with_size(vm, headers, disknum, alv_size, csv_size, buf);
}

/* Euclid's GCD algorithm */
static inline unsigned greatest_common_divisor(unsigned a, unsigned b)
{
	/* divide by the remainder till we can divide no more */
	unsigned rem;
	while ((rem = b % a) > 0) {
		b = a;
		a = rem;
	}
	return a;
}

/* Returns size of the generated translate table. 
 * The sector translate table maps logical sectors to
 * physical sectors. For a file the logical sectors appear sequential, but
 * for faster disk access the physical sectors that map these may be skewed
 * over the disk so the disk seek can reach them faster while the disk is spinning.
 * eg. with skew factor 2 the mapping may be (logical->physical): 0->0, 1->2, 2->4, 3->6. */
static unsigned
write_sector_translate_table(struct cpm80_vm *const vm, struct disk_params *params, struct disk_param_block *param_block, cpm80_addr_t buf_ptr)
{
	void(*write_byte)(i8080_addr_t, i8080_word_t) = vm->cpu->memory_write;

	unsigned skew = params->skf;
	unsigned sectors = param_block->spt;
	unsigned sector_offset = params->fsc;
	/* Number of sector mappings to generate before we start overlapping previously mapped sectors.
	 * For eg. with spt=72 and skf=10:
	 * 0,10,20,...70,8,18,...68,6,16,...66,4,14,...64,2,12,...62,0,10...
	 *                                                          ^^^
	 * we overlap every 5 rotations (every 36 mapped sectors)
	 */
	const unsigned num_before_overlap = sectors / greatest_common_divisor(sectors, skew);

	/* Next base sector to start mapping at after every rotation */
	unsigned next_base = 0;

	unsigned i;
	unsigned next_sector = next_base, num_assigned = 0;
	/* one mapping per sector */
	for (i = 0; i < sectors; ++i) {
		unsigned sector_alloc = next_sector + sector_offset;

		if (sectors < 256) {
			write_byte(buf_ptr, sector_alloc);
			buf_ptr += 1;
		} else {
			write_word(write_byte, buf_ptr, sector_alloc);
			buf_ptr += 2;
		}

		next_sector += skew;
		if (next_sector >= sectors) {
			next_sector -= sectors;
		}
		num_assigned++;

		if (num_assigned == num_before_overlap) {
			next_base += 1;
			next_sector = next_base;
			num_assigned = 0;
		}
	}

	return sectors;
}

/* Written in order: xlt, 0, 0, 0, dirbuf, dpb, csv, alv */
static void write_disk_header(struct cpm80_vm *const vm, struct disk_header *header, cpm80_addr_t buf_ptr)
{
	void(*write_byte)(i8080_addr_t, i8080_word_t) = vm->cpu->memory_write;
	write_word(write_byte, buf_ptr, (cpm80_word_t)header->xlt); buf_ptr += 2;
	write_word(write_byte, buf_ptr, 0x0000); buf_ptr += 2;
	write_word(write_byte, buf_ptr, 0x0000); buf_ptr += 2; /* 3 blank words, used as a workspace by CP/M */
	write_word(write_byte, buf_ptr, 0x0000); buf_ptr += 2;
	write_word(write_byte, buf_ptr, (cpm80_word_t)header->dirbuf); buf_ptr += 2;
	write_word(write_byte, buf_ptr, (cpm80_word_t)header->dpb); buf_ptr += 2;
	write_word(write_byte, buf_ptr, (cpm80_word_t)header->csv); buf_ptr += 2;
	write_word(write_byte, buf_ptr, (cpm80_word_t)header->alv);
}

/* Returns 0 if parameter list if complete i.e. no defns are shared, 1 if defns are shared, and -1 for a invalid parameter list. */
static int extract_disk_params(const unsigned *param_list, const unsigned num_params, struct disk_params *params)
{
	if (num_params == 10) {
		params->dn = param_list[1];
		params->fsc = param_list[2];
		params->lsc = param_list[3];
		params->skf = param_list[4];
		params->bls = param_list[5];
		params->dks = param_list[6];
		params->dir = param_list[7];
		params->cks = param_list[8];
		params->ofs = param_list[9];
		params->k16 = param_list[10];
	} else if (num_params == 2) {
		params->dn = param_list[1];
		params->dm = param_list[2];
		return 1;
	} else {
		/* invalid param list */
		return -1;
	}
	return 0;
}

#define DIRBUF_LEN (128)
#define DPB_LEN (15)
#define DPH_LEN (16) /* usual length. more data can go here, but assume defaults */

int cpm80_bios_redefine_disks(struct cpm80_vm *const vm, const int ndisks,
	const unsigned *const *disk_params, cpm80_addr_t disk_header_ptr, cpm80_addr_t disk_ram_ptr, cpm80_addr_t *disk_dph_out)
{
	/* all disks have the same dirbuf */
	const cpm80_addr_t dirbuf_all = disk_ram_ptr;
	write_zeros(vm, disk_ram_ptr, DIRBUF_LEN);
	disk_ram_ptr += DIRBUF_LEN;

	struct disk_header headers[CPM80_MAX_DISKS];
	
	/* some disks share dpb, xlt, als, css with other disks */
	int i;
	int shared_params_dm[CPM80_MAX_DISKS];
	struct disk_shared_params shared_params[CPM80_MAX_DISKS];
	for (i = 0; i < ndisks; ++i) {
		shared_params_dm[i] = -1;
	}

	cpm80_addr_t disk_param_block_ptr = disk_header_ptr + ndisks * DPH_LEN;

	struct disk_params params;
	struct disk_param_block param_block;
	for (i = 0; i < ndisks; ++i) {
		const unsigned *param_list = disk_params[i];
		const unsigned num_params = param_list[0];

		int is_shared = extract_disk_params(param_list, num_params, &params);
		if (is_shared == -1) return -1;

		headers[i].dirbuf = dirbuf_all;

		if (is_shared) {
			/* disk dn shares the params of disk dm */
			shared_params_dm[params.dn] = params.dm;
			/* dm may be an as yet undefined disk, define dn later */
			continue;
		}

		/* allocate checksum/alloc vectors */
		unsigned vtotal_size = write_alv_csv(vm, &params, shared_params, headers, i, disk_ram_ptr);
		disk_ram_ptr += (cpm80_addr_t)vtotal_size;

		/* write disk parameter block */
		headers[i].dpb = disk_param_block_ptr;
		get_disk_parameter_block(&params, &param_block);
		write_disk_param_block(vm, &param_block, disk_param_block_ptr);
		disk_param_block_ptr += DPB_LEN;

		/* write translate table */
		if (params.skf != 0) {
			headers[i].xlt = disk_param_block_ptr;
			unsigned xlt_size = write_sector_translate_table(vm, &params, &param_block, disk_param_block_ptr);
			disk_param_block_ptr += (cpm80_addr_t)xlt_size;
		} else {
			headers[i].xlt = 0x0000;
		}
	}

	/* now that all disks are defined, complete the shared disk header defns */
	for (i = 0; i < ndisks; ++i) {
		int dn = i;
		int dm = shared_params_dm[i];
		if (dm != -1) {
			/* dpb, xlt */
			headers[dn].dpb = headers[dm].dpb;
			headers[dn].xlt = headers[dm].xlt;
			/* alv, csv */
			unsigned als = shared_params[dm].als, css = shared_params[dm].css;
			unsigned vtotal_size = write_alv_csv_with_size(vm, headers, dn, als, css, disk_ram_ptr);
			disk_ram_ptr += (cpm80_addr_t)vtotal_size;
		}
	}

	/* write out disk headers */
	for (i = 0; i < ndisks; ++i) {
		disk_dph_out[i] = disk_header_ptr;
		write_disk_header(vm, &headers[i], disk_header_ptr);
		disk_header_ptr += DPH_LEN;
	}

	return 0;
}
