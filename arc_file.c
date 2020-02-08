#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini_parser.h"
#include "arc_file.h"
#include "debug.h"

#define MAX_CONF_SIZE 256

static char mod;
static char rbfname[9];
static char conf[MAX_CONF_SIZE];
static int conf_ptr;

void arc_set_conf(char *);

// arc ini sections
const ini_section_t arc_ini_sections[] = {
	{1, "ARC"}
};

// arc ini vars
const ini_var_t arc_ini_vars[] = {
	{"MOD", (void*)(&mod), UINT8, 0, 127, 1},
	{"RBF", (void*)rbfname, STRING, 1, 8, 1},
	{"CONF", (void*)arc_set_conf, CUSTOM_HANDLER, 0, 0, 1},
};


extern unsigned long iCurrentDirectory;    // cluster number of current directory, 0 for root

void arc_set_conf(char *c)
{
	if ((conf_ptr+strlen(c))<MAX_CONF_SIZE-1) {
		strcpy(&conf[conf_ptr], c);
		strcat(conf, ";");
		conf_ptr += strlen(c) + 1;
	}
}

char arc_open(char *fname)
{
	ini_cfg_t arc_ini_cfg;

	arc_ini_cfg.filename = fname;
	arc_ini_cfg.dir = iCurrentDirectory;
	arc_ini_cfg.sections = arc_ini_sections;
	arc_ini_cfg.vars = arc_ini_vars;
	arc_ini_cfg.nsections = (int)(sizeof(arc_ini_sections) / sizeof(ini_section_t));
	arc_ini_cfg.nvars =  (int)(sizeof(arc_ini_vars) / sizeof(ini_var_t));

	rbfname[8] = 0;
	conf[0] = 0;
	conf_ptr = 0;
	mod = -1;
	ini_parse(&arc_ini_cfg, 0);
	iprintf("arc conf=%s\n",conf);
	return mod;
}

char *arc_get_rbfname()
{
	return rbfname;
}

char *arc_get_conf()
{
	return conf;
}