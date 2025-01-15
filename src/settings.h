/*  settings.h
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <limits.h>

#include <tox/tox.h>

#include "run_options.h"
#include "toxic_constants.h"

/* Represents line_* hints max strlen */
#define LINE_HINT_MAX 3

#define PASSWORD_EVAL_MAX 512

typedef struct Toxic Toxic;
typedef struct Client_Data Client_Data;

/* Holds user setting values defined in the toxic config file. */
typedef struct Client_Config {
    bool autolog;
    bool alerts;
    bool show_notification_content;
    bool show_typing_self;
    bool show_typing_other;
    bool show_welcome_msg;
    bool show_connection_msg;
    bool show_group_connection_msg;
    bool show_timestamps;
    bool show_network_info;

    int bell_on_message;
    int bell_on_filetrans;
    int bell_on_filetrans_accept;
    int bell_on_invite;

    char timestamp_format[TIME_STR_SIZE];
    char log_timestamp_format[TIME_STR_SIZE];

    int history_size;      /* int between MIN_HISTORY and MAX_HISTORY */
    int notification_timeout;
    int nodeslist_update_freq;  /* <= 0 to disable updates */
    int autosave_freq; /* <= 0 to disable autosave */

    bool line_padding;
    char line_join[LINE_HINT_MAX + 1];
    char line_quit[LINE_HINT_MAX + 1];
    char line_alert[LINE_HINT_MAX + 1];
    char line_normal[LINE_HINT_MAX + 1];
    char line_special[LINE_HINT_MAX + 1];

    char download_path[PATH_MAX];
    char chatlogs_path[PATH_MAX];
    char avatar_path[PATH_MAX];
    char autorun_path[PATH_MAX];
    char password_eval[PASSWORD_EVAL_MAX];

    bool native_colors;
    char color_bar_bg[COLOR_STR_SIZE];
    char color_bar_fg[COLOR_STR_SIZE];
    char color_bar_accent[COLOR_STR_SIZE];
    char color_bar_notify[COLOR_STR_SIZE];

    int key_next_tab;
    int key_prev_tab;
    int key_scroll_line_up;
    int key_scroll_line_down;
    int key_half_page_up;
    int key_half_page_down;
    int key_page_bottom;
    int key_toggle_peerlist;
    int key_toggle_pastemode;
    int key_reload_config;

    bool mplex_away; /*  true for reaction to terminal attach/detach */
    char mplex_away_note [TOX_MAX_STATUS_MESSAGE_LENGTH];
    char group_part_message[TOX_GROUP_MAX_PART_LENGTH];

#ifdef AUDIO
    int audio_in_dev;
    int audio_out_dev;
    double VAD_threshold;
    int conference_audio_channels;
    int chat_audio_channels;
    bool push_to_talk;
#endif
} Client_Config;

#define LINE_JOIN    "-->"
#define LINE_QUIT    "<--"
#define LINE_ALERT   "-!-"
#define LINE_NORMAL  "-"
#define LINE_SPECIAL ">"
#define TIMESTAMP_DEFAULT      "%H:%M"
#define LOG_TIMESTAMP_DEFAULT  "%Y/%m/%d [%H:%M]"
#define MPLEX_AWAY_NOTE "Away from keyboard, be back soon!"

typedef struct Windows Windows;

/*
 * Loads the config file into `run_opts` and creates an empty file if it does not
 * already exist. This function must be called before any other `settings_load` function.
 *
 * If the client has set a custom config file via the run option it will be
 * prioritized.
 *
 * If the client is using the default tox data file and has not specified a custom
 * config file, the default config file in the user config directory is used
 *
 * If the client is using a custom tox data file, the config path and filename will be
 * identical to those of the data file, but with a `.conf` file extension.
 *
 * `data_path` is the name of the tox profile being used.
 *
 * Returns true if config file is successfully loaded.
 */
bool settings_load_config_file(Run_Options *run_opts, const char *data_path);

/*
 * Loads general toxic settings from the toxic config file pointed to by `patharg'.
 *
 * Return 0 on success.
 * Return -1 if we fail to open the file path.
 * Return -2 if libconfig fails to read the config file.
 */
int settings_load_main(Client_Config *s, const Run_Options *run_opts);

/*
 * Loads friend config settings from the toxic config file pointed to by `patharg`.
 * If `patharg` is null, the default config path is used.
 *
 * Return 0 on success (or if no config entry for friends exists).
 * Return -1 if we fail to open the file path.
 * Return -2 if libconfig fails to read the config file.
 *
 * This function will have no effect on friends that are added in the future.
 */
int settings_load_friends(const Run_Options *run_opts);

/*
 * Loads groupchat config settings from the toxic config file pointed to by `patharg`.
 * If `patharg` is null, the default config path is used.
 *
 * Return 0 on success (or if no config entry for groupchats exists).
 * Return -1 if we fail to open the file path.
 * Return -2 if libconfig fails to read the config file.
 *
 * This function will have no effect on groupchat instances that are created in the future.
 */
int settings_load_groups(Windows *windows, const Run_Options *run_opts);

/*
 * Loads conference config settings from the toxic config file pointed to by `patharg`.
 * If `patharg` is null, the default config path is used.
 *
 * Return 0 on success (or if no config entry for conferences exists).
 * Return -1 if we fail to open the file path.
 * Return -2 if libconfig fails to read the config file.
 *
 * This function will have no effect on conference instances that are created in the future.
 */
int settings_load_conferences(Windows *windows, const Run_Options *run_opts);

/*
 * Loads the blocked words list from the toxic config file pointed to by `patharg`.
 * If `patharg` is null, the default config path is used.
 *
 * Return 0 on success (or if no list exists in the config file).
 * Return -1 if we fail to open the file path.
 * Return -2 if libconfig fails to read the config file.
 * Return -3 if memory allocation fails.
 */
int settings_load_blocked_words(Client_Data *client_data, const Run_Options *run_opts);

/*
 * Reloads config settings.
 */
void settings_reload(Toxic *toxic);

#endif /* SETTINGS_H */
