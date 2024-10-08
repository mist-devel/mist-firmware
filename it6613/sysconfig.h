//
// Copyright (C) 2015-2016  Markus Hiienkari <mhiienka@niksula.hut.fi>
//
// This file is part of Open Source Scan Converter project.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef SYSCONFIG_H_
#define SYSCONFIG_H_

//#define I2C_DEBUG

#include <stdio.h>
#include "utils.h"
#include "debug.h"

#define OS_PRINTF iprintf
#define ErrorF hdmi_debugf
#define printf iprintf

#define WAITLOOP_SLEEP_US   10000

#endif /* SYSCONFIG_H_ */
