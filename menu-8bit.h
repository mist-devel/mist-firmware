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

#include "menu.h"

typedef struct {
  menu_get_page_t page_handler;
  menu_get_items_t items_handler;
  menu_key_event_t key_handler;
} menu_handler_t;

void set_sticky_menu_handler(menu_handler_t handler);

void clear_sticky_menu_handler();

void Setup8bitMenu();

#endif
