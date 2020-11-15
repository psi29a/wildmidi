/*
 * out_oss.h -- OSS output
 *
 * Copyright (C) WildMidi Developers 2020
 *
 * This file is part of WildMIDI.
 *
 * WildMIDI is free software: you can redistribute and/or modify the player
 * under the terms of the GNU General Public License and you can redistribute
 * and/or modify the library under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the licenses, or(at your option) any later version.
 *
 * WildMIDI is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License and
 * the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License and the
 * GNU Lesser General Public License along with WildMIDI.  If not,  see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef OUT_OSS_H
#define OUT_OSS_H

#include "config.h"

#if (AUDIODRV_OSS == 1)

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#elif defined HAVE_MACHINE_SOUNDCARD_H
#include <machine/soundcard.h>
#elif defined HAVE_SOUNDCARD_H
#include <soundcard.h> /* less common, but exists. */
#endif


int open_oss_output(void);
int write_oss_output(int8_t *output_data, int output_size);
void close_oss_output(void);
void pause_oss_output(void);

#else

#define open_oss_output open_output_noout
#define pause_oss_output pause_output_noout
#define write_oss_output send_output_noout
#define close_oss_output close_output_noout

#endif


#endif // OUT_OSS_H
