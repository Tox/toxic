/*  run_options.h
 *
 *
 *  Copyright (C) 2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef RUN_OPTIONS_H
#define RUN_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>  // needed for FILE
#include <stdint.h>
#include <time.h>

#include "toxic_constants.h"

typedef struct Run_Options {
    bool use_ipv4;
    bool force_tcp;
    bool disable_local_discovery;
    bool debug;
    bool default_locale;
    bool use_custom_data;
    bool use_custom_config_file;
    bool no_connect;
    bool encrypt_data;
    bool unencrypt_data;

    char nameserver_path[MAX_STR_SIZE];
    char config_path[MAX_STR_SIZE];
    char nodes_path[MAX_STR_SIZE];

    bool logging;
    FILE *log_fp;

    bool netprof_log_dump;
    FILE *netprof_fp;
    time_t netprof_start_time;

    char proxy_address[256];
    uint8_t proxy_type;
    uint16_t proxy_port;

    uint16_t tcp_port;
} Run_Options;

#endif /* RUN_OPTIONS_H */
