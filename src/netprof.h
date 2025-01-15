/*  netprof.h
 *
 *  Copyright (C) 2025 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef TOXIC_NETPROF
#define TOXIC_NETPROF

#include <stdio.h>
#include <time.h>
#include <tox/tox.h>

void netprof_log_dump(const Tox *m, FILE *fp, time_t run_time);

uint64_t netprof_get_bytes_down(const Tox *m);
uint64_t netprof_get_bytes_up(const Tox *m);

#endif  // TOXIC_NETPROF
