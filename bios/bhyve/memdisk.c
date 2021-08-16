/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang
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
 */

// memory disk - load disk into RAM an I/O from there.

#include <sys/cdefs.h>

#include <sys/queue.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "memdisk.h"


typedef struct mdisk {
	const char *fname;
	uint8_t    disknum; // BIOS disk number (0x00-0x7f == floppy, 0x80-0xff fixed disk)
	uint8_t    *buf;
	ssize_t    bufsize;
	ssize_t    sectsize;
} mdisk;

#define MAX_MDISKS 8
static mdisk mdisks[MAX_MDISKS];
static int num_mds = 0;

int
md_num_disks()
{
	return (num_mds);
}

int
md_create(const char *src_img)
{
	struct stat sb;
	int fd, totalread;
	uint8_t *p;

	fd = open(src_img, O_RDONLY);
	if (fd < 0) {
		return (fd);
	}

	if (fstat(fd, &sb) < 0) {
		return (-1);
	}

	printf("** BHYVE: ADDING MEMDISK %s\r\n", src_img);
	mdisks[num_mds].fname = strdup(src_img);
	mdisks[num_mds].buf = malloc(sb.st_size);
	mdisks[num_mds].bufsize = sb.st_size;
	totalread = 0;
	p = mdisks[num_mds].buf;
	while (totalread < sb.st_size) {
		ssize_t nread;
		nread = read(fd, p, sb.st_size - totalread);
		if (nread < 0) {
			close(fd);
			free(mdisks[num_mds].buf);
			return (-1);
		} else if (nread == 0) {
			// this shouldn't happen
			break;
		}

		totalread += nread;
		p += nread;
	}

	// if ISO image, then sector size is 2k
	mdisks[num_mds].sectsize = (strstr(src_img, ".iso") != NULL)
                                   ? 2048 : 512;

	close(fd);
	num_mds++;
	printf("** BHYVE: ADDED MEMDISK OK!\r\n");
	return (num_mds-1);
}

int
md_chs(int mdunit, uint16_t *c, uint8_t *h, uint8_t *s)
{
	ssize_t sects;		/* total sectors of the block dev */
	ssize_t hcyl;		/* cylinders times heads */
	uint16_t secpt;		/* sectors per track */
	uint8_t heads;
	mdisk *md;

	assert(mdunit >= 0 && mdunit < num_mds);

	md = &mdisks[mdunit];
	sects = md->bufsize / md->sectsize;

	/* Floppy size CHS */
	if (md->bufsize <= (2880*512)) {
		secpt = 18;
		hcyl = sects / secpt;
		heads = 2;
		goto calc;
	}

	/* Clamp the size to the largest possible with CHS */
	if (sects > 65535UL*16*255)
		sects = 65535UL*16*255;

	if (sects >= 65536UL*16*63) {
		secpt = 63;
		heads = 32;
		hcyl = sects / secpt;
	} else {
		secpt = 63;
		hcyl = sects / secpt;
		heads = 16;
	}

calc:
	*c = hcyl / heads;
	*h = heads;
	*s = secpt;

	return 0;
}

ssize_t
md_sectsz(int mdunit)
{
	assert(mdunit >= 0 && mdunit < num_mds);
	return (mdisks[mdunit].sectsize);
}

ssize_t
md_sectors(int mdunit)
{
	assert(mdunit >= 0 && mdunit < num_mds);

	return (mdisks[mdunit].bufsize / mdisks[mdunit].sectsize);
}


uint64_t
md_lba_to_offset(int mdunit, ssize_t lba)
{
	assert(mdunit >= 0 && mdunit < num_mds);
	return (mdisks[mdunit].sectsize * lba);
}

static int
md_rw(int mdunit, int do_write, uint64_t offset, void *buf, uint64_t len)
{
	mdisk *md;

	assert(mdunit >= 0 && mdunit < num_mds);
	assert(len > 0);

	md = &mdisks[mdunit];

	// len should be a multiple of sector size
	assert(len % md->sectsize == 0);

	if ((offset + len) > md->bufsize) {
		return (-1);
	}

	if (do_write) {
		memcpy(&md->buf[offset], buf, len);
	} else {
		memcpy(buf, &md->buf[offset], len);
	}
	return (0);
}

int
md_write(int mdunit, uint64_t offset, void *buf, uint64_t len)
{
	return (md_rw(mdunit, 1, offset, buf, len));
}

int
md_read(int mdunit, uint64_t offset, void *buf, uint64_t len)
{
	return (md_rw(mdunit, 0, offset, buf, len));
}
