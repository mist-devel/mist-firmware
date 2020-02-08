/*
 * arc_file.h
 * Open/parse .arc (Arcade) files
 *
 */

#ifndef ARC_FILE_H
#define ARC_FILE_H

char arc_open(char *fname);
char *arc_get_rbfname();
char *arc_get_conf();

#endif // ARC_FILE_H
