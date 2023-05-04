/*
 * arc_file.h
 * Open/parse .arc (Arcade) files
 *
 */

#ifndef ARC_FILE_H
#define ARC_FILE_H

char arc_open(const char *fname);
void arc_reset();
char *arc_get_rbfname();
char *arc_get_corename();
char *arc_get_dirname();
char *arc_get_vhdname();
char *arc_get_conf();
uint64_t arc_get_default();
const char *arc_get_buttons();
const char *arc_get_button(int index);

#endif // ARC_FILE_H
