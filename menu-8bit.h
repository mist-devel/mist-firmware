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

#ifndef MENU_8BIT_H
#define MENU_8BIT_H

#define MAX_PAGE_PLUGINS 10

typedef struct {
    char id[4];
    void (*init_menu)(const char *arg1, const char *arg2);
} menu_page_plugin_t;

void page_plugin_init();
char page_plugin_add(menu_page_plugin_t *plugin);


void Setup8bitMenu();

#endif
