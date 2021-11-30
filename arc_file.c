#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini_parser.h"
#include "arc_file.h"
#include "debug.h"

#define MAX_CONF_SIZE 512

typedef struct {
	char mod;
	uint64_t conf_default;
	char rbfname[9];
	char corename[9];
	char dirname[9];
	char vhdname[9];
	char conf[MAX_CONF_SIZE];
} arc_t;

static arc_t arc;
static int conf_ptr;

void arc_set_conf(char *);

// arc ini sections
const ini_section_t arc_ini_sections[] = {
	{1, "ARC"}
};

// arc ini vars
const ini_var_t arc_ini_vars[] = {
	{"MOD", (void*)(&arc.mod), UINT8, 0, 127, 1},
	{"DEFAULT", (void*)(&arc.conf_default), UINT64, 0, ~0, 1},
	{"RBF", (void*)arc.rbfname, STRING, 1, 8, 1},
	{"NAME", (void*)arc.corename, STRING, 1, 8, 1},
	{"DIR", (void*)arc.dirname, STRING, 1, 8, 1},
	{"VHD", (void*)arc.vhdname, STRING, 1, 8, 1},
	{"CONF", (void*)arc_set_conf, CUSTOM_HANDLER, 0, 0, 1},
};

void arc_set_conf(char *c)
{
	if ((conf_ptr+strlen(c))<MAX_CONF_SIZE-1) {
		strcpy(&arc.conf[conf_ptr], c);
		strcat(arc.conf, ";");
		conf_ptr += strlen(c) + 1;
	}
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
	ini_parse(&arc_ini_cfg, 0);
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
