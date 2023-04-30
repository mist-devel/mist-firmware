#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini_parser.h"
#include "arc_file.h"
#include "debug.h"

#define MAX_CONF_SIZE 512
#define MAX_BUTTONS_SIZE 128

typedef struct {
	char mod;
	uint64_t conf_default;
	char rbfname[33];
	char corename[17];
	char dirname[17];
	char vhdname[17];
	char conf[MAX_CONF_SIZE];
	char buttons_str[MAX_BUTTONS_SIZE+1];
} arc_t;

static arc_t arc;
static int conf_ptr;

char arc_set_conf(char *, char, int);

// arc ini sections
const ini_section_t arc_ini_sections[] = {
	{1, "ARC"}
};

// arc ini vars
const ini_var_t arc_ini_vars[] = {
	{"MOD", (void*)(&arc.mod), UINT8, 0, 127, 1},
	{"DEFAULT", (void*)(&arc.conf_default), UINT64, 0, ~0, 1},
	{"RBF", (void*)arc.rbfname, STRING, 1, 32, 1},
	{"NAME", (void*)arc.corename, STRING, 1, 16, 1},
	{"DIR", (void*)arc.dirname, STRING, 1, 16, 1},
	{"VHD", (void*)arc.vhdname, STRING, 1, 16, 1},
	{"CONF", (void*)arc_set_conf, CUSTOM_HANDLER, 0, 0, 1},
	{"BUTTONS", (void*)arc.buttons_str, STRING, 1, MAX_BUTTONS_SIZE, 1}
};

char arc_set_conf(char *c, char action, int tag)
{
	if (action == INI_SAVE) return 0;
	if ((conf_ptr+strlen(c))<MAX_CONF_SIZE-1) {
		strcpy(&arc.conf[conf_ptr], c);
		strcat(arc.conf, ";");
		conf_ptr += strlen(c) + 1;
	}
	return 0;
}

char arc_open(const char *fname)
{
	ini_cfg_t arc_ini_cfg;

	arc_ini_cfg.filename = fname;
	arc_ini_cfg.sections = arc_ini_sections;
	arc_ini_cfg.vars = arc_ini_vars;
	arc_ini_cfg.nsections = (int)(sizeof(arc_ini_sections) / sizeof(ini_section_t));
	arc_ini_cfg.nvars =  (int)(sizeof(arc_ini_vars) / sizeof(ini_var_t));

	arc_reset();
	arc.mod = -1; // indicate error by default, valid ARC file will overrdide with the correct MOD value
	ini_parse(&arc_ini_cfg, 0, 0);
	iprintf("ARC CONF STR: %s\n",arc.conf);
	return arc.mod;
}

void arc_reset()
{
	memset(&arc, 0, sizeof(arc_t));
	conf_ptr = 0;
}

char *arc_get_rbfname()
{
	return arc.rbfname;
}

char *arc_get_corename()
{
	return arc.corename;
}

char *arc_get_dirname()
{
	return arc.dirname;
}

char *arc_get_vhdname()
{
	return arc.vhdname;
}

char *arc_get_conf()
{
	return arc.conf;
}

uint64_t arc_get_default()
{
	return arc.conf_default;
}

const char *arc_get_buttons()
{
	return arc.buttons_str;
}

const char *arc_get_button(int index)
{
	int i = 0;
	char *str = arc.buttons_str;
	static char btn[15];
	if (!str) return 0;
	while (*str) {
		if (i == index) {
			i = 0;
			while (*str && *str != ',' && i < 14) {
				btn[i] = *str++;
				i++;
			}
			btn[i] = 0;
			return btn;
		}
		if (*str == ',') i++;
		str++;
	}
	return 0;
}
