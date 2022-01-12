// ini_parser.h
// 2015, rok.krajnc@gmail.com

#ifndef __INI_PARSER_H__
#define __INI_PARSER_H__

// float support adds over 20kBytes to the firmware size
// #define INI_ENABLE_FLOAT

//// includes ////
#include <inttypes.h>


//// type definitions ////
typedef struct {
  int id;
  char* name;
} ini_section_t;

typedef enum {UINT8=0, INT8, UINT16, INT16, UINT32, INT32, UINT64, INT64,
#ifdef INI_ENABLE_FLOAT
	      FLOAT, 
#endif
	      STRING, CUSTOM_HANDLER} ini_vartypes_t;

#define INI_LOAD 0
#define INI_SAVE 1

typedef char custom_handler_t(char*, char, int);

typedef struct {
  char* name;
  void* var;
  ini_vartypes_t type;
  uint64_t min;
  uint64_t max;
  int section_id;
} ini_var_t;

typedef struct {
  const char* filename;
  const ini_section_t* sections;
  const ini_var_t* vars;
  int nsections;
  int nvars;
} ini_cfg_t;


//// functions ////
void ini_parse(const ini_cfg_t* cfg, const char *alter_section, int tag);
void ini_save(const ini_cfg_t* cfg, int tag);

#endif // __INI_PARSER_H__

