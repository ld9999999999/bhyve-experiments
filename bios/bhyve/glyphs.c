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

// psf glyphs renderer
// For VGA graphics, the glyph needs to be an 8x16 fontset.

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include "vga.h"


#define FONT_GLYPHS     256
#define FONT_SCANLINES  16
uint8_t glyphs[FONT_GLYPHS][FONT_SCANLINES];

struct psf1_header {
	uint8_t magic[2];
	uint8_t mode;
	uint8_t charsize;
};

static inline uint32_t *
bitmap_to_32bpp(uint8_t bits, uint32_t *output, uint32_t fg, uint32_t bg)
{
	// Unrolled loop
	*output++ = (bits & (1 << 7)) ? fg : bg;
	*output++ = (bits & (1 << 6)) ? fg : bg;
	*output++ = (bits & (1 << 5)) ? fg : bg;
	*output++ = (bits & (1 << 4)) ? fg : bg;
	*output++ = (bits & (1 << 3)) ? fg : bg;
	*output++ = (bits & (1 << 2)) ? fg : bg;
	*output++ = (bits & (1 << 1)) ? fg : bg;
	*output++ = (bits & 1) ? fg : bg;

	return (output);
}

uint32_t *
glyph_render_line(uint16_t *row, uint32_t cols, uint32_t *output)
{
	// prepare using glyph pointers
	uint8_t *text_glyphs[80];
	int initialized = 0;
	uint32_t fg, bg;
	static uint32_t colors[16] = {
		0x00000000, 0x000000dd, 0x0000dd00, 0x0000dddd,
		0x00dd0000, 0x00dd00dd, 0x00dddd00, 0x00dddddd,
		0x00555555, 0x000000f0, 0x0000f000, 0x0000f0f0,
		0x00f00000, 0x00f000f0, 0x00f0f000, 0x00ffffff
	};

	if (cols > 80) {
		printf("Invalid text line length: %u\n", cols);	
		return NULL; // ERROR
	}

	for (int y = 0; y < 16; y++) {
		for (int i = 0; i < cols; i++) {
			uint8_t ch = row[i] & 0xff;
			uint8_t attr = (row[i] >> 8) & 0xff;
			uint8_t g;

			if (!initialized) {
				text_glyphs[i] = glyphs[ch];
			}

			fg = colors[attr & 0xf];
			bg = colors[(attr >> 4) & 0xf];
			g = text_glyphs[i][y];

			output = bitmap_to_32bpp(g, output, fg, bg);
		}
		initialized = 1;
	}
	return output;
}

int
glyph_load_psf(char *psffile)
{
	int fd, n, err = 0;
	struct psf1_header psfh;

	fd = open(psffile, O_RDONLY);
	if (fd < 0) {
		printf("%d\n", __LINE__);
		return fd;
	}

	n = read(fd, &psfh, 4);
	if (n != 4) {
		printf("%d\n", __LINE__);
		err = n;
		goto out;
	}

	if (psfh.magic[0] != 0x36 && psfh.magic[1] != 0x04 && psfh.charsize != 16) {
		err = -1;
		printf("Invalid or unsupported psf file. Must be an 8x16 format.\n");
		goto out;
	}

	// Read into glyphs
	n = read(fd, glyphs, sizeof(glyphs));
	if (n != sizeof(glyphs)) {
		printf("Invalid psf file.\n");
		err = n;
	}
out:
	close(fd);

	return err;
}


#if 0
// BITMAP OUTPUT TEST

#include <time.h>

#pragma pack(1)
struct bmpheader {
  uint16_t  bfType;
  uint32_t  bfSize;
  uint16_t  bfReserved1;
  uint16_t  bfReserved2;
  uint32_t  bfOffBits;

  uint32_t biSize;
  int32_t  biWidth;
  int32_t  biHeight;
  uint16_t  biPlanes;
  uint16_t  biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  uint32_t  biXPelsPerMeter;
  uint32_t  biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
};
#pragma packed()

static uint32_t buf[16*8*80];
int main() {
	static uint16_t textblob[25][80];
	int i, j;
	size_t n;
	int f;

	memset(textblob, 0, sizeof(textblob));
	for (i = 0; i < 25; i++) {
		for (j = 0; j < 80; j++) {
			textblob[i][j] = ('A' + ((i + j) % 26)) | ((j % 16) << 8) | (((j+4) % 16) << 12);
		}
	}
	if (glyph_load_psf("zap-vga16.psf")) {
		printf("FAILED\n");
		return -1;
	}

	f = open("x.bmp", O_RDWR | O_CREAT | O_TRUNC);
	struct bmpheader bmph = {
		.bfType = 0x4d42,
		.bfSize = sizeof(struct bmpheader) + 4*80*25,
		.bfReserved1 = 0,
		.bfReserved2 = 0,
		.bfOffBits = sizeof(struct bmpheader),

		.biSize = 40,
		.biWidth = 640,
		.biHeight = -400, // needed for bmp to be upright
		.biPlanes = 1,
		.biBitCount = 32,
		.biCompression = 0,
		.biSizeImage = 0,
		.biXPelsPerMeter = 0,
		.biYPelsPerMeter = 0,
		.biClrUsed = 0,
		.biClrImportant = 0
	};

	printf("BMPH:\n");
	printf("%x\n", bmph.bfType);
	printf("%x %lu\n", bmph.bfSize, (uint64_t)&bmph.bfSize - (uint64_t)&bmph);
	printf("%x\n", bmph.bfReserved1);
	printf("%x\n", bmph.bfReserved2);
	printf("%x\n", bmph.bfOffBits);
	printf("%x\n", bmph.biSize);
	printf("%x\n", bmph.biWidth);
	printf("%x\n", bmph.biHeight);
	printf("%x\n", bmph.biPlanes);
	printf("%x\n", bmph.biBitCount);
	printf("%x\n", bmph.biCompression);
	printf("%x\n", bmph.biSizeImage);
	printf("%x\n", bmph.biXPelsPerMeter);
	printf("%x\n", bmph.biYPelsPerMeter);
	printf("%x\n", bmph.biClrUsed);
	printf("%x\n", bmph.biClrImportant);

	printf("sizeof bmph: %lu\n", sizeof(bmph));
	write(f, &bmph, sizeof(bmph));

	clock_t start = clock() ;
	for (i = 0; i < 25; i++) {
		glyph_render_line(textblob[i], 80, buf);
	}
	clock_t end = clock() ;
	double elapsed_time = (end-start)/(double)CLOCKS_PER_SEC ;
	printf("TIME: %lf\n", elapsed_time);

	close(f);
}

#endif
