/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sxmlc/sxmlc.h"
#include "fat_compat.h"
#include "data_io.h"
#include "menu.h"

static unsigned char *zx_col_table;
//static char *col_state_s[] = {"None", "Border", "Entry", "Line", "Paper", "Ink"};

typedef enum _zx_col_state {STATE_NONE, STATE_BORDER, STATE_ENTRY, STATE_LINE, STATE_PAPER, STATE_INK} zx_col_state;
typedef enum _zx_col_text_state {TEXT_NONE, TEXT_LINE, TEXT_COLOUR, TEXT_BRIGHT} zx_col_text_state;

static zx_col_state col_state;
static zx_col_text_state col_text_state;
static unsigned int zx_col_code;
static unsigned int zx_col_quantity;
static unsigned int zx_col_line;
static unsigned char zx_col_color;
static unsigned char zx_col_bright;
static unsigned char zx_col_attr;

//////////////////////
// .COL file parser //
//////////////////////

static int zx_col_start_doc(SAX_Data* sd)
{
	//printf("start_doc\n");
	col_state = STATE_NONE;
	col_text_state = STATE_NONE;
	return true;
}

static int zx_col_end_doc(SAX_Data* sd)
{
	//printf("end_doc\n");
	return true;
}

static int zx_col_start_node(const XMLNode* node, SAX_Data* sd)
{
	//printf("start_node tag=%s text=%s n_attr=%d\n", node->tag, node->text, node->n_attributes);
	switch (col_state) {
		case STATE_NONE:
			if (!strcmp(node->tag, "border")) col_state = STATE_BORDER;
			else if (!strcmp(node->tag, "entry")) {
				col_state = STATE_ENTRY;
				zx_col_quantity = 1;
				for (int i = 0; i < node->n_attributes; i++) {
					if (!(strcmp(node->attributes[i].name, "code"))) {
						//printf("code %s\n", node->attributes[i].value);
						zx_col_code = strtol(node->attributes[i].value, NULL, 0);
					}
					if (!(strcmp(node->attributes[i].name, "quantity"))) {
						//printf("code %s\n", node->attributes[i].value);
						zx_col_quantity = strtol(node->attributes[i].value, NULL, 0);
					}
				}
			}
			break;
		case STATE_ENTRY:
			if (!strcmp(node->tag, "line")) {
				col_state = STATE_LINE;
				for (int i = 0; i < node->n_attributes; i++) {
					if (!(strcmp(node->attributes[i].name, "index"))) {
						//printf("line %s\n", node->attributes[i].value);
						zx_col_line = strtol(node->attributes[i].value, NULL, 0);
						zx_col_color = zx_col_bright = zx_col_attr = 0;
					}
				}
			}
			break;
		case STATE_LINE:
			if (!strcmp(node->tag, "paper")) col_state = STATE_PAPER;
			else if (!strcmp(node->tag, "ink")) col_state = STATE_INK;
			break;
		case STATE_BORDER:
		case STATE_PAPER:
		case STATE_INK:
			if (!strcmp(node->tag, "colour")) {
				col_text_state = TEXT_COLOUR;
			} else if (!strcmp(node->tag, "bright")) {
				col_text_state = TEXT_BRIGHT;
			}
			break;
	}
	return true;
}

static int zx_col_end_node(const XMLNode* node, SAX_Data* sd)
{

	switch (col_state) {
		case STATE_BORDER:
		case STATE_PAPER:
		case STATE_INK:
			if (!strcmp(node->tag, "colour") || !strcmp(node->tag, "bright")) {
				col_text_state = STATE_NONE;
			}
	}

	switch (col_state) {
		case STATE_ENTRY:
			if (!strcmp(node->tag, "entry")) {
				col_state = STATE_NONE;
				while (zx_col_quantity-- > 1) {
					if (zx_col_code>=127) break;
					memcpy(&zx_col_table[(zx_col_code+1)*8], &zx_col_table[zx_col_code*8], 8);
					zx_col_code++;
				}
			}
			break;
		case STATE_LINE:
			if (!strcmp(node->tag, "line")) {
				col_state = STATE_ENTRY;
				//printf("code=%d line=%d color=%d\n", zx_col_code, zx_col_line, zx_col_attr);
				if (zx_col_code < 128 && zx_col_line < 8) zx_col_table[zx_col_code*8+zx_col_line] = zx_col_attr;
			}
			break;
		case STATE_BORDER:
			if (!strcmp(node->tag, "border")) {
				col_state = STATE_NONE;
				zx_col_table[1025] = zx_col_attr;
			}
			break;
		case STATE_PAPER:
			if (!strcmp(node->tag, "paper")) {
				col_state = STATE_LINE;
				zx_col_attr |= (zx_col_color | zx_col_bright << 3) << 4;
			}
			break;
		case STATE_INK:
			if (!strcmp(node->tag, "ink")) {
				col_state = STATE_LINE;
				zx_col_attr |= (zx_col_color | zx_col_bright << 3);
			}
			break;
	}
	return true;
}

static int zx_col_new_text(SXML_CHAR* text, SAX_Data* sd)
{
	//printf("new_text (%s)\n", text);
	if (col_text_state == TEXT_COLOUR) {
		//printf("state=%s colour=%s\n", col_state_s[col_state], text);
		zx_col_color = strtol(text, NULL, 0);
	} else if (col_text_state == TEXT_BRIGHT) {
		//printf("state=%s bright=%s\n", col_state_s[col_state], text);
		zx_col_bright = strtol(text, NULL, 0);
	}
	return true;
}

//////////////////////
// .CHR file parser //
//////////////////////

static int zx_chr_start_doc(SAX_Data* sd)
{
	//printf("start_doc\n");
	col_state = STATE_NONE;
	return true;
}

static int zx_chr_end_doc(SAX_Data* sd)
{
	//printf("end_doc\n");
	return true;
}

static int zx_chr_start_node(const XMLNode* node, SAX_Data* sd)
{
	//printf("start_node tag=%s text=%s n_attr=%d\n", node->tag, node->text, node->n_attributes);
	switch (col_state) {
		case STATE_NONE:
			if (!strcmp(node->tag, "entry")) {
				col_state = STATE_ENTRY;
				for (int i = 0; i < node->n_attributes; i++) {
					if (!(strcmp(node->attributes[i].name, "code"))) {
						zx_col_code = strtol(node->attributes[i].value, NULL, 0);
						zx_col_code = (zx_col_code & 0x3f) | ((zx_col_code & 0x80) >> 1);
					}
				}
			}
			break;
		case STATE_ENTRY:
			if (!strcmp(node->tag, "line")) {
				col_state = STATE_LINE;
				for (int i = 0; i < node->n_attributes; i++) {
					if (!(strcmp(node->attributes[i].name, "index"))) {
						zx_col_line = strtol(node->attributes[i].value, NULL, 0);
					}
				}
			}
			break;
	}
	return true;
}

static int zx_chr_end_node(const XMLNode* node, SAX_Data* sd)
{
	switch (col_state) {
		case STATE_ENTRY:
			if (!strcmp(node->tag, "entry")) col_state = STATE_NONE;
			break;
		case STATE_LINE:
			if (!strcmp(node->tag, "line")) {
				col_state = STATE_ENTRY;
				if (zx_col_code < 128 && zx_col_line < 8) zx_col_table[zx_col_code*8+zx_col_line] = zx_col_attr;
			}
			break;
	}
	return true;
}

static int zx_chr_new_text(SXML_CHAR* text, SAX_Data* sd)
{
	if (col_state == STATE_LINE) {
		zx_col_attr = strtol(text, NULL, 2);
		//printf("code=%d line=%d line=%s(%02x)\n", zx_col_code, zx_col_line, text, zx_col_attr);
	}
	return true;
}

///////////////////////

static const SAX_Callbacks zx_col_sax_callbacks = {
	.start_doc  = zx_col_start_doc,
	.end_doc    = zx_col_end_doc,
	.start_node = zx_col_start_node,
	.end_node   = zx_col_end_node,
	.new_text   = zx_col_new_text
	};

static const SAX_Callbacks zx_chr_sax_callbacks = {
	.start_doc  = zx_chr_start_doc,
	.end_doc    = zx_chr_end_doc,
	.start_node = zx_chr_start_node,
	.end_node   = zx_chr_end_node,
	.new_text   = zx_chr_new_text
	};

#ifdef ZX_COL_TEST
int main() {
	zx_col_table=malloc(128*8);
	memset(zx_col_table, 15, sizeof(zx_col_table));
	XMLDoc_parse_file_SAX("ZX80_Kong.col", &zx_col_sax_callbacks, &zx_col_sax_callbacks);
	FILE *f = fopen("output.col", "wb");
	fwrite(zx_col_table, 128*8, 1, f);
	free(zx_col_table);
	fclose(f);
}
#else
static int zx_col_load(FIL *fil, unsigned char *buf)
{
	zx_col_table = buf;
	memset(zx_col_table, 0xf0, 128*8+1);
#ifdef HAVE_XML
	return XMLDoc_parse_fd_SAX(fil, &zx_col_sax_callbacks, &zx_col_sax_callbacks);
#else
	return 0;
#endif
}

static const unsigned char zx81_charset[512] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xF0, 0xF0, 0xF0, 0x00, 0x00, 0x00, 0x00,
	0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
	0x0F, 0x0F, 0x0F, 0x0F, 0xF0, 0xF0, 0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0xF0, 0xF0,
	0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x55, 0xAA, 0x55,
	0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1C, 0x22, 0x78, 0x20, 0x20, 0x7E, 0x00, 0x00, 0x08, 0x3E, 0x28, 0x3E, 0x0A, 0x3E, 0x08,
	0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x3C, 0x42, 0x04, 0x08, 0x00, 0x08, 0x00,
	0x00, 0x04, 0x08, 0x08, 0x08, 0x08, 0x04, 0x00, 0x00, 0x20, 0x10, 0x10, 0x10, 0x10, 0x20, 0x00,
	0x00, 0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00,
	0x00, 0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x08, 0x3E, 0x08, 0x14, 0x00,
	0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x10, 0x20,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
	0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00, 0x00, 0x18, 0x28, 0x08, 0x08, 0x08, 0x3E, 0x00,
	0x00, 0x3C, 0x42, 0x02, 0x3C, 0x40, 0x7E, 0x00, 0x00, 0x3C, 0x42, 0x0C, 0x02, 0x42, 0x3C, 0x00,
	0x00, 0x08, 0x18, 0x28, 0x48, 0x7E, 0x08, 0x00, 0x00, 0x7E, 0x40, 0x7C, 0x02, 0x42, 0x3C, 0x00,
	0x00, 0x3C, 0x40, 0x7C, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x7E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00,
	0x00, 0x3C, 0x42, 0x3C, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x3E, 0x02, 0x3C, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00, 0x00, 0x7C, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00,
	0x00, 0x3C, 0x42, 0x40, 0x40, 0x42, 0x3C, 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00,
	0x00, 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00, 0x00, 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00,
	0x00, 0x3C, 0x42, 0x40, 0x4E, 0x42, 0x3C, 0x00, 0x00, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00,
	0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00, 0x00, 0x02, 0x02, 0x02, 0x42, 0x42, 0x3C, 0x00,
	0x00, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00,
	0x00, 0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x00, 0x00, 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x52, 0x4A, 0x3C, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x7C, 0x44, 0x42, 0x00,
	0x00, 0x3C, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00, 0x00, 0xFE, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00,
	0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00,
	0x00, 0x42, 0x42, 0x42, 0x42, 0x5A, 0x24, 0x00, 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00,
	0x00, 0x82, 0x44, 0x28, 0x10, 0x10, 0x10, 0x00, 0x00, 0x7E, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x00
};

static int zx_chr_load(FIL *fil, unsigned char *buf)
{
#ifdef HAVE_XML
	zx_col_table = buf;
	for (int i=0;i<1024;i++) {
		zx_col_table[i] = zx81_charset[i&0x1ff];
	}
	return XMLDoc_parse_fd_SAX(fil, &zx_chr_sax_callbacks, &zx_chr_sax_callbacks);
#else
	return 0;
#endif
}

#endif

static void zx_sendfile(FIL *file, int index, const char *ext, int len)
{
	data_io_file_tx_prepare(file, index, ext);
	EnableFpga();
	SPI(DIO_FILE_TX_DAT);
	spi_write(sector_buffer, len);
	DisableFpga();
	data_io_file_tx_done();
}

static void zx_handlecol(FIL *file, int index, const char *name, const char *ext)
{
	if (!zx_col_load(file, sector_buffer)) {
		ErrorMessage("\n   Error parsing CHR file!\n", 0);
		f_close(file);
	} else {
		zx_sendfile(file, index, ext, 1025);
		f_close(file);
		CloseMenu();
	}
}

static void zx_handlechr(FIL *file, int index, const char *name, const char *ext)
{
	if (!zx_chr_load(file, sector_buffer)) {
		ErrorMessage("\n   Error parsing CHR file!\n", 0);
		f_close(file);
	} else {
		zx_sendfile(file, index, ext, 1024);
		f_close(file);
		CloseMenu();
	}
}

static data_io_processor_t zx_colfile = {"COL", &zx_handlecol};
static data_io_processor_t zx_chrfile = {"CHR", &zx_handlechr};

void zx_init()
{
	data_io_add_processor(&zx_colfile);
	data_io_add_processor(&zx_chrfile);
}
