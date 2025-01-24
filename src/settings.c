/*  settings.c
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#include <assert.h>
#include <ctype.h>
#include <libconfig.h>
#include <stdlib.h>
#include <string.h>

#include "conference.h"
#include "configdir.h"
#include "friendlist.h"
#include "groupchats.h"
#include "misc_tools.h"
#include "notify.h"
#include "toxic.h"
#include "windows.h"

#ifdef AUDIO
#include "audio_device.h"
#endif /* AUDIO */

#include "line_info.h"
#include "settings.h"

#ifndef PACKAGE_DATADIR
#define PACKAGE_DATADIR "."
#endif

#define TOXIC_CONF_FILE_EXT ".conf"
#define TOXIC_CONF_FILE_EXT_LENGTH (sizeof(TOXIC_CONF_FILE_EXT) - 1)

static_assert(MAX_STR_SIZE > TOXIC_CONF_FILE_EXT_LENGTH, "MAX_STR_SIZE <= TOXIC_CONF_FILE_EXT_LENGTH");

#ifdef SOUND_NOTIFY
#define NO_SOUND "silent"
#endif  /* SOUND_NOTIFY */

static struct ui_strings {
    const char *self;
    const char *timestamps;
    const char *time_format;
    const char *timestamp_format;
    const char *log_timestamp_format;
    const char *alerts;
    const char *show_notification_content;
    const char *bell_on_message;
    const char *bell_on_filetrans;
    const char *bell_on_filetrans_accept;
    const char *bell_on_invite;
    const char *native_colors;
    const char *autolog;
    const char *history_size;
    const char *notification_timeout;
    const char *show_typing_self;
    const char *show_typing_other;
    const char *show_welcome_msg;
    const char *show_connection_msg;
    const char *show_group_connection_msg;
    const char *nodeslist_update_freq;
    const char *autosave_freq;

    const char *line_padding;
    const char *line_join;
    const char *line_quit;
    const char *line_alert;
    const char *line_normal;
    const char *line_special;
    const char *group_part_message;

    const char *mplex_away;
    const char *mplex_away_note;

    const char *color_bar_bg;
    const char *color_bar_fg;
    const char *color_bar_accent;
    const char *color_bar_notify;
} ui_strings = {

    "ui",
    "timestamps",
    "time_format",
    "timestamp_format",
    "log_timestamp_format",
    "alerts",
    "show_notification_content",
    "bell_on_message",
    "bell_on_filetrans",
    "bell_on_filetrans_accept",
    "bell_on_invite",
    "native_colors",
    "autolog",
    "history_size",
    "notification_timeout",
    "show_typing_self",
    "show_typing_other",
    "show_welcome_msg",
    "show_connection_msg",
    "show_group_connection_msg",
    "nodeslist_update_freq",
    "autosave_freq",
    "line_padding",
    "line_join",
    "line_quit",
    "line_alert",
    "line_normal",
    "line_special",
    "group_part_message",

    "mplex_away",
    "mplex_away_note",

    "color_bar_bg",
    "color_bar_fg",
    "color_bar_accent",
    "color_bar_notify",
};

static void ui_defaults(Client_Config *settings)
{
    snprintf(settings->timestamp_format, sizeof(settings->timestamp_format), "%s", TIMESTAMP_DEFAULT);
    snprintf(settings->log_timestamp_format, sizeof(settings->log_timestamp_format), "%s", LOG_TIMESTAMP_DEFAULT);
    settings->show_timestamps = true;
    settings->autolog = false;
    settings->alerts = true;
    settings->show_notification_content = true;
    settings->native_colors = false;
    settings->bell_on_message = 0;
    settings->bell_on_filetrans = 0;
    settings->bell_on_filetrans_accept = 0;
    settings->bell_on_invite = 0;
    settings->history_size = 700;
    settings->notification_timeout = 6000;
    settings->show_typing_self = true;
    settings->show_typing_other = true;
    settings->show_welcome_msg = true;
    settings->show_connection_msg = true;
    settings->show_group_connection_msg = true;
    settings->nodeslist_update_freq = 1;
    settings->autosave_freq = 600;

    settings->line_padding = true;
    snprintf(settings->line_join, LINE_HINT_MAX + 1, "%s", LINE_JOIN);
    snprintf(settings->line_quit, LINE_HINT_MAX + 1, "%s", LINE_QUIT);
    snprintf(settings->line_alert, LINE_HINT_MAX + 1, "%s", LINE_ALERT);
    snprintf(settings->line_normal, LINE_HINT_MAX + 1, "%s", LINE_NORMAL);
    snprintf(settings->line_special, LINE_HINT_MAX + 1, "%s", LINE_SPECIAL);

    settings->mplex_away = true;
    snprintf(settings->mplex_away_note, sizeof(settings->mplex_away_note), "%s", MPLEX_AWAY_NOTE);
}

static const struct keys_strings {
    const char *self;
    const char *next_tab;
    const char *prev_tab;
    const char *scroll_line_up;
    const char *scroll_line_down;
    const char *half_page_up;
    const char *half_page_down;
    const char *page_bottom;
    const char *toggle_peerlist;
    const char *toggle_pastemode;
    const char *reload_config;
} key_strings = {

    "keys",
    "next_tab",
    "prev_tab",
    "scroll_line_up",
    "scroll_line_down",
    "half_page_up",
    "half_page_down",
    "page_bottom",
    "toggle_peerlist",
    "toggle_paste_mode",
    "reload_config",
};

/* defines from toxic.h */
static void key_defaults(Client_Config *settings)
{
    settings->key_next_tab = T_KEY_NEXT;
    settings->key_prev_tab = T_KEY_PREV;
    settings->key_scroll_line_up = T_KEY_C_F;
    settings->key_scroll_line_down = T_KEY_C_V;
    settings->key_half_page_up = KEY_PPAGE;
    settings->key_half_page_down = KEY_NPAGE;
    settings->key_page_bottom = T_KEY_C_H;
    settings->key_toggle_peerlist = T_KEY_C_B;
    settings->key_toggle_pastemode = T_KEY_C_T;
    settings->key_reload_config = T_KEY_C_R;
}

static const struct tox_strings {
    const char *self;
    const char *download_path;
    const char *chatlogs_path;
    const char *avatar_path;
    const char *autorun_path;
    const char *password_eval;
} tox_strings = {

    "tox",
    "download_path",
    "chatlogs_path",
    "avatar_path",
    "autorun_path",
    "password_eval",
};

static void tox_defaults(Client_Config *settings)
{
    strcpy(settings->download_path, "");
    strcpy(settings->chatlogs_path, "");
    strcpy(settings->avatar_path, "");
    strcpy(settings->autorun_path, "");
    strcpy(settings->password_eval, "");
}

#ifdef AUDIO
static const struct audio_strings {
    const char *self;
    const char *input_device;
    const char *output_device;
    const char *VAD_threshold;
    const char *conference_audio_channels;
    const char *chat_audio_channels;
    const char *push_to_talk;
} audio_strings = {

    "audio",
    "input_device",
    "output_device",
    "VAD_threshold",
    "conference_audio_channels",
    "chat_audio_channels",
    "push_to_talk",
};

static void audio_defaults(Client_Config *settings)
{
    settings->audio_in_dev = 0;
    settings->audio_out_dev = 0;
    settings->VAD_threshold = 5.0;
    settings->conference_audio_channels = 1;
    settings->chat_audio_channels = 2;
    settings->push_to_talk = 0;
}

#endif

#ifdef SOUND_NOTIFY
static const struct sound_strings {
    const char *self;
    const char *notif_error;
    const char *self_log_in;
    const char *self_log_out;
    const char *user_log_in;
    const char *user_log_out;
    const char *call_incoming;
    const char *call_outgoing;
    const char *generic_message;
    const char *transfer_pending;
    const char *transfer_completed;
} sound_strings = {

    "sounds",
    "notif_error",
    "self_log_in",
    "self_log_out",
    "user_log_in",
    "user_log_out",
    "call_incoming",
    "call_outgoing",
    "generic_message",
    "transfer_pending",
    "transfer_completed",
};
#endif

static const struct friend_strings {
    const char *self;
    const char *alias;
    const char *auto_accept_files;
    const char *autolog;
    const char *show_connection_msg;
    const char *tab_name_color;
} friend_strings = {

    "friends",
    "alias",
    "auto_accept_files",
    "autolog",
    "show_connection_msg",
    "tab_name_color",
};

static const struct groupchat_strings {
    const char *self;
    const char *tab_name_color;
    const char *autolog;
} groupchat_strings = {

    "groupchats",
    "tab_name_color",
    "autolog",
};

static const struct conference_strings {
    const char *self;
    const char *tab_name_color;
    const char *autolog;
} conference_strings = {

    "conferences",
    "tab_name_color",
    "autolog",
};

static const struct blocked_words {
    const char *self;
} blocked_words = {

    "blocked_words",
};

static int key_parse(const char **bind)
{
    int len = strlen(*bind);

    if (len > 5) {
        if (strncasecmp(*bind, "ctrl+", 5) == 0 && toupper(bind[0][5]) != 'M') { /* ctrl+m cannot be used */
            return toupper(bind[0][5]) - 'A' + 1;
        }
    }

    if (strncasecmp(*bind, "alt", 3) == 0) {
            return 0x300+toupper(bind[0][4]) - 'A' + 1;
    }

    if (strncasecmp(*bind, "tab", 3) == 0) {
        return T_KEY_TAB;
    }

    if (strncasecmp(*bind, "up", 2) == 0) {
        return KEY_UP;
    }

    if (strncasecmp(*bind, "down", 4) == 0) {
        return KEY_DOWN;
    }

    if (strncasecmp(*bind, "right", 5) == 0) {
        return KEY_RIGHT;
    }

    if (strncasecmp(*bind, "left", 4) == 0) {
        return KEY_LEFT;
    }

    if (strncasecmp(*bind, "page", 4) == 0) {
        return len == 6 ? KEY_PPAGE : KEY_NPAGE;
    }

    return -1;
}

static void set_key_binding(int *key, const char **bind)
{
    int code = key_parse(bind);

    if (code != -1) {
        *key = code;
    }
}

bool settings_load_config_file(Run_Options *run_opts, const char *data_path)
{
    char tmp_path[MAX_STR_SIZE] = {0};

    if (run_opts->use_custom_config_file) {
        snprintf(tmp_path, sizeof(tmp_path), "%s", run_opts->config_path);
    } else if (run_opts->use_custom_data) {
        char tmp_data[MAX_STR_SIZE - TOXIC_CONF_FILE_EXT_LENGTH];

        if (strlen(data_path) >= sizeof(tmp_data)) {
            return false;
        }

        snprintf(tmp_data, sizeof(tmp_data), "%s", data_path);

        const int dot_idx = char_rfind(tmp_data, '.', strlen(tmp_data));

        if (dot_idx > 0) {
            tmp_data[dot_idx] = '\0';  // remove .tox file extension (or any others) if it exists
        }

        snprintf(tmp_path, sizeof(tmp_path), "%s%s", tmp_data, TOXIC_CONF_FILE_EXT);
    } else {
        char *user_config_dir = get_user_config_dir();
        snprintf(tmp_path, sizeof(tmp_path), "%s%stoxic%s", user_config_dir, CONFIGDIR, TOXIC_CONF_FILE_EXT);
        free(user_config_dir);
    }

    /* make sure path exists or is created on first time running */
    if (!file_exists(tmp_path)) {
        FILE *fp = fopen(tmp_path, "w");

        if (fp == NULL) {
            fprintf(stderr, "failed to create config path `%s`\n", tmp_path);
            return false;
        }

        fclose(fp);
    }

    snprintf(run_opts->config_path, sizeof(run_opts->config_path), "%s", tmp_path);

    return true;
}

#define TOXIC_CONFIG_PUBLIC_KEY_PREFIX "pk_"

/* Extracts a public key from `keys`.
 *
 * Returns the public key as a string on success.
 * Returns NULL on error.
 */
static const char *extract_setting_public_key(const config_setting_t *keys)
{
    if (keys == NULL) {
        return NULL;
    }

    const char *public_key = config_setting_name(keys);

    if (public_key == NULL) {
        fprintf(stderr, "config error: failed to extract public key\n");
        return NULL;
    }

    const uint16_t prefix_len = (uint16_t) strlen(TOXIC_CONFIG_PUBLIC_KEY_PREFIX);

    if (strlen(public_key) != TOX_PUBLIC_KEY_SIZE * 2 + prefix_len) {
        fprintf(stderr, "config error: invalid public key: %s\n", public_key);
        return NULL;
    }

    if (memcmp(public_key, TOXIC_CONFIG_PUBLIC_KEY_PREFIX, prefix_len) != 0) {
        fprintf(stderr, "config error: invalid public key prefix\n");
        return NULL;
    }

    return &public_key[prefix_len];
}

/*
 * Initializes `cfg` with the contents from the toxic config file.
 *
 * Return 0 on success.
 * Return -1 if the config file was not set by the client.
 * Return -2 if the config file cannot be read or is invalid.
 */
static int settings_init_config(config_t *cfg, const Run_Options *run_opts)
{
    if (string_is_empty(run_opts->config_path)) {
        return -1;
    }

    if (!config_read_file(cfg, run_opts->config_path)) {
        fprintf(stderr, "config_read_file() error: %s:%d - %s\n", config_error_file(cfg),
                config_error_line(cfg), config_error_text(cfg));
        return -2;
    }

    return 0;
}

int settings_load_conferences(Windows *windows, const Run_Options *run_opts)
{
    config_t cfg[1];
    config_init(cfg);

    const int c_ret = settings_init_config(cfg, run_opts);

    if (c_ret < 0) {
        config_destroy(cfg);
        return c_ret;
    }

    const char *str = NULL;

    const config_setting_t *setting = config_lookup(cfg, conference_strings.self);

    if (setting == NULL) {
        config_destroy(cfg);
        return 0;
    }

    const int num_conferences = config_setting_length(setting);

    for (int i = 0; i < num_conferences; ++i) {
        const config_setting_t *keys = config_setting_get_elem(setting, i);

        const char *public_key = extract_setting_public_key(keys);

        if (public_key == NULL) {
            continue;
        }

        if (config_setting_lookup_string(keys, conference_strings.tab_name_color, &str)) {
            if (!conference_config_set_tab_name_colour(windows, public_key, str)) {
                fprintf(stderr, "config error: failed to set conference tab name color for %s: (color: %s)\n", public_key, str);
            }
        }

        int autolog_enabled;

        if (config_setting_lookup_bool(keys, conference_strings.autolog, &autolog_enabled)) {
            if (!conference_config_set_autolog(windows, public_key, autolog_enabled != 0)) {
                fprintf(stderr, "config error: failed to apply conference autolog setting for %s\n", public_key);
            }
        }
    }

    config_destroy(cfg);

    return 0;
}

int settings_load_groups(Windows *windows, const Run_Options *run_opts)
{
    config_t cfg[1];
    config_init(cfg);

    const int c_ret = settings_init_config(cfg, run_opts);

    if (c_ret < 0) {
        config_destroy(cfg);
        return c_ret;
    }

    const char *str = NULL;

    const config_setting_t *setting = config_lookup(cfg, groupchat_strings.self);

    if (setting == NULL) {
        config_destroy(cfg);
        return 0;
    }

    const int num_groups = config_setting_length(setting);

    for (int i = 0; i < num_groups; ++i) {
        const config_setting_t *keys = config_setting_get_elem(setting, i);

        const char *public_key = extract_setting_public_key(keys);

        if (public_key == NULL) {
            continue;
        }

        if (config_setting_lookup_string(keys, groupchat_strings.tab_name_color, &str)) {
            if (!groupchat_config_set_tab_name_colour(windows, public_key, str)) {
                fprintf(stderr, "config error: failed to set groupchat tab name color for %s: (color: %s)\n", public_key, str);
            }
        }

        int autolog_enabled;

        if (config_setting_lookup_bool(keys, groupchat_strings.autolog, &autolog_enabled)) {
            if (!groupchat_config_set_autolog(windows, public_key, autolog_enabled != 0)) {
                fprintf(stderr, "config error: failed to apply groupchat autolog setting for %s\n", public_key);
            }
        }
    }

    config_destroy(cfg);

    return 0;
}

int settings_load_friends(const Run_Options *run_opts)
{
    config_t cfg[1];
    config_init(cfg);

    const int c_ret = settings_init_config(cfg, run_opts);

    if (c_ret < 0) {
        config_destroy(cfg);
        return c_ret;
    }

    const char *str = NULL;

    const config_setting_t *setting = config_lookup(cfg, friend_strings.self);

    if (setting == NULL) {
        config_destroy(cfg);
        return 0;
    }

    const int num_friends = config_setting_length(setting);

    for (int i = 0; i < num_friends; ++i) {
        const config_setting_t *keys = config_setting_get_elem(setting, i);

        const char *public_key = extract_setting_public_key(keys);

        if (public_key == NULL) {
            continue;
        }

        if (config_setting_lookup_string(keys, friend_strings.tab_name_color, &str)) {
            if (!friend_config_set_tab_name_colour(public_key, str)) {
                fprintf(stderr, "config error: failed to set friend tab name color for %s: (color: %s)\n",
                        public_key, str);
            }
        }

        int autolog_enabled;

        if (config_setting_lookup_bool(keys, friend_strings.autolog, &autolog_enabled)) {
            if (!friend_config_set_autolog(public_key, autolog_enabled != 0)) {
                fprintf(stderr, "config error: failed to apply friend autolog setting for: %s\n", public_key);
            }
        }

        int auto_accept_files;

        if (config_setting_lookup_bool(keys, friend_strings.auto_accept_files, &auto_accept_files)) {
            if (!friend_config_set_auto_accept_files(public_key, auto_accept_files != 0)) {
                fprintf(stderr,
                        "config error: failed to apply friend auto-accept filetransfers setting for: %s\n",
                        public_key);
            }
        }

        int show_connection_msg;

        if (config_setting_lookup_bool(keys, friend_strings.show_connection_msg, &show_connection_msg)) {
            if (!friend_config_set_show_connection_msg(public_key, show_connection_msg != 0)) {
                fprintf(stderr,
                        "config error: failed to apply friend show connection message setting for: %s\n",
                        public_key);
            }
        }

        if (config_setting_lookup_string(keys, friend_strings.alias, &str)) {
            if (!friend_config_set_alias(public_key, str, strlen(str))) {
                fprintf(stderr, "config error: failed to apply alias '%s' for: %s\n", str, public_key);
            }
        }
    }

    config_destroy(cfg);

    return 0;
}

int settings_load_blocked_words(Client_Data *client_data, const Run_Options *run_opts)
{
    config_t cfg[1];
    config_init(cfg);

    const int c_ret = settings_init_config(cfg, run_opts);

    if (c_ret < 0) {
        config_destroy(cfg);
        return c_ret;
    }

    const config_setting_t *setting = config_lookup(cfg, blocked_words.self);

    if (setting == NULL) {
        config_destroy(cfg);
        return 0;
    }

    const int list_size = config_setting_length(setting);

    if (list_size <= 0) {
        return 0;
    }

    const size_t size_of_list_item = MAX_BLOCKED_WORD_LENGTH + 1;
    char **words_list = (char **) malloc_ptr_array(list_size, size_of_list_item);

    if (words_list == NULL) {
        fprintf(stderr, "config error: failed to allocate memory for blocked words list.\n");
        return -3;
    }

    size_t num_blocked_words = 0;

    for (int i = 0; i < list_size; ++i) {
        const char *word = config_setting_get_string_elem(setting, i);

        if (word == NULL) {
            continue;
        }

        if (strlen(word) > MAX_BLOCKED_WORD_LENGTH) {
            fprintf(stderr, "Warning: blocked word '%s' exceeds maximum length\n", word);
        }

        snprintf(words_list[num_blocked_words], size_of_list_item, "%s", word);
        ++num_blocked_words;
    }

    client_data->blocked_words = words_list;
    client_data->num_blocked_words = num_blocked_words;

    config_destroy(cfg);

    return 0;
}

int settings_load_main(Client_Config *s, const Run_Options *run_opts)
{
    config_t cfg[1];
    config_init(cfg);

    config_setting_t *setting = NULL;

    /* Load default settings */
    ui_defaults(s);
    tox_defaults(s);
    key_defaults(s);

#ifdef AUDIO
    audio_defaults(s);
#endif

    const int c_ret = settings_init_config(cfg, run_opts);

    if (c_ret < 0) {
        config_destroy(cfg);
        return c_ret;
    }

    const char *str = NULL;
    int bool_val;

    /* ui */
    if ((setting = config_lookup(cfg, ui_strings.self)) != NULL) {
        if (config_setting_lookup_bool(setting, ui_strings.timestamps, &bool_val)) {
            s->show_timestamps = bool_val != 0;
        }

        int time = 24;

        if (config_setting_lookup_int(setting, ui_strings.time_format, &time)) {
            if (time == 12) {
                snprintf(s->timestamp_format, sizeof(s->timestamp_format), "%s", "%I:%M %p");
                snprintf(s->log_timestamp_format, sizeof(s->log_timestamp_format), "%s", "%Y/%m/%d [%I:%M %p]");
            }
        }

        if (config_setting_lookup_string(setting, ui_strings.timestamp_format, &str)) {
            snprintf(s->timestamp_format, sizeof(s->timestamp_format), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.color_bar_bg, &str)) {
            snprintf(s->color_bar_bg, sizeof(s->color_bar_bg), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.color_bar_fg, &str)) {
            snprintf(s->color_bar_fg, sizeof(s->color_bar_fg), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.color_bar_accent, &str)) {
            snprintf(s->color_bar_accent, sizeof(s->color_bar_accent), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.color_bar_notify, &str)) {
            snprintf(s->color_bar_notify, sizeof(s->color_bar_notify), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.log_timestamp_format, &str)) {
            snprintf(s->log_timestamp_format, sizeof(s->log_timestamp_format), "%s", str);
        }

        if (config_setting_lookup_bool(setting, ui_strings.alerts, &bool_val)) {
            s->alerts = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_notification_content, &bool_val)) {
            s->show_notification_content = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.bell_on_message, &bool_val)) {
            s->bell_on_message = bool_val != 0 ? NT_BEEP : 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.bell_on_filetrans, &bool_val)) {
            s->bell_on_filetrans = bool_val != 0 ? NT_BEEP : 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.bell_on_filetrans_accept, &bool_val)) {
            s->bell_on_filetrans_accept = bool_val != 0 ? NT_BEEP : 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.bell_on_invite, &bool_val)) {
            s->bell_on_invite = bool_val != 0 ? NT_BEEP : 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.autolog, &bool_val)) {
            s->autolog = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.native_colors, &bool_val)) {
            s->native_colors = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_typing_self, &bool_val)) {
            s->show_typing_self = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_typing_other, &bool_val)) {
            s->show_typing_other = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_welcome_msg, &bool_val)) {
            s->show_welcome_msg = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_connection_msg, &bool_val)) {
            s->show_connection_msg = bool_val != 0;
        }

        if (config_setting_lookup_bool(setting, ui_strings.show_group_connection_msg, &bool_val)) {
            s->show_group_connection_msg = bool_val != 0;
        }

        config_setting_lookup_int(setting, ui_strings.history_size, &s->history_size);
        config_setting_lookup_int(setting, ui_strings.notification_timeout, &s->notification_timeout);
        config_setting_lookup_int(setting, ui_strings.nodeslist_update_freq, &s->nodeslist_update_freq);
        config_setting_lookup_int(setting, ui_strings.autosave_freq, &s->autosave_freq);

        if (config_setting_lookup_bool(setting, ui_strings.line_padding, &bool_val)) {
            s->line_padding = bool_val != 0;
        }

        if (config_setting_lookup_string(setting, ui_strings.line_join, &str)) {
            snprintf(s->line_join, sizeof(s->line_join), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.line_quit, &str)) {
            snprintf(s->line_quit, sizeof(s->line_quit), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.line_alert, &str)) {
            snprintf(s->line_alert, sizeof(s->line_alert), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.line_normal, &str)) {
            snprintf(s->line_normal, sizeof(s->line_normal), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.line_special, &str)) {
            snprintf(s->line_special, sizeof(s->line_special), "%s", str);
        }

        if (config_setting_lookup_bool(setting, ui_strings.mplex_away, &bool_val)) {
            s->mplex_away = bool_val != 0;
        }

        if (config_setting_lookup_string(setting, ui_strings.mplex_away_note, &str)) {
            snprintf(s->mplex_away_note, sizeof(s->mplex_away_note), "%s", str);
        }

        if (config_setting_lookup_string(setting, ui_strings.group_part_message, &str)) {
            snprintf(s->group_part_message, sizeof(s->group_part_message), "%s", str);
        }
    }

    /* paths */
    if ((setting = config_lookup(cfg, tox_strings.self)) != NULL) {
        if (config_setting_lookup_string(setting, tox_strings.download_path, &str)) {
            snprintf(s->download_path, sizeof(s->download_path), "%s", str);
            const size_t len = strlen(s->download_path);

            /* make sure path ends with a '/' */
            if (len >= sizeof(s->download_path) - 2) {
                s->download_path[0] = '\0';
            } else if (len > 0 && s->download_path[len - 1] != '/') {
                strcat(&s->download_path[len - 1], "/");
            }
        }

        if (config_setting_lookup_string(setting, tox_strings.chatlogs_path, &str)) {
            snprintf(s->chatlogs_path, sizeof(s->chatlogs_path), "%s", str);
            const size_t len = strlen(s->chatlogs_path);

            if (len >= sizeof(s->chatlogs_path) - 2) {
                s->chatlogs_path[0] = '\0';
            } else if (len > 0 && s->chatlogs_path[len - 1] != '/') {
                strcat(&s->chatlogs_path[len - 1], "/");
            }
        }

        if (config_setting_lookup_string(setting, tox_strings.avatar_path, &str)) {
            snprintf(s->avatar_path, sizeof(s->avatar_path), "%s", str);
            const size_t len = strlen(str);

            if (len >= sizeof(s->avatar_path)) {
                s->avatar_path[0] = '\0';
            }
        }

#ifdef PYTHON

        if (config_setting_lookup_string(setting, tox_strings.autorun_path, &str)) {
            snprintf(s->autorun_path, sizeof(s->autorun_path), "%s", str);
            const size_t len = strlen(str);

            if (len >= sizeof(s->autorun_path) - 2) {
                s->autorun_path[0] = '\0';
            } else if (len > 0 && s->autorun_path[len - 1] != '/') {
                strcat(&s->autorun_path[len - 1], "/");
            }
        }

#endif

        if (config_setting_lookup_string(setting, tox_strings.password_eval, &str)) {
            snprintf(s->password_eval, sizeof(s->password_eval), "%s", str);
            const size_t len = strlen(str);

            if (len >= sizeof(s->password_eval)) {
                s->password_eval[0] = '\0';
            }
        }
    }

    /* keys */
    if ((setting = config_lookup(cfg, key_strings.self)) != NULL) {
        const char *tmp = NULL;

        if (config_setting_lookup_string(setting, key_strings.next_tab, &tmp)) {
            set_key_binding(&s->key_next_tab, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.prev_tab, &tmp)) {
            set_key_binding(&s->key_prev_tab, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.scroll_line_up, &tmp)) {
            set_key_binding(&s->key_scroll_line_up, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.scroll_line_down, &tmp)) {
            set_key_binding(&s->key_scroll_line_down, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.half_page_up, &tmp)) {
            set_key_binding(&s->key_half_page_up, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.half_page_down, &tmp)) {
            set_key_binding(&s->key_half_page_down, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.page_bottom, &tmp)) {
            set_key_binding(&s->key_page_bottom, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.toggle_peerlist, &tmp)) {
            set_key_binding(&s->key_toggle_peerlist, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.toggle_pastemode, &tmp)) {
            set_key_binding(&s->key_toggle_pastemode, &tmp);
        }

        if (config_setting_lookup_string(setting, key_strings.reload_config, &tmp)) {
            set_key_binding(&s->key_reload_config, &tmp);
        }
    }

#ifdef AUDIO

    /* Audio */
    if ((setting = config_lookup(cfg, audio_strings.self)) != NULL) {
        config_setting_lookup_int(setting, audio_strings.input_device, &s->audio_in_dev);
        s->audio_in_dev = s->audio_in_dev < 0 || s->audio_in_dev > MAX_DEVICES ? 0 : s->audio_in_dev;

        config_setting_lookup_int(setting, audio_strings.output_device, &s->audio_out_dev);
        s->audio_out_dev = s->audio_out_dev < 0 || s->audio_out_dev > MAX_DEVICES ? 0 : s->audio_out_dev;

        config_setting_lookup_float(setting, audio_strings.VAD_threshold, &s->VAD_threshold);

        config_setting_lookup_int(setting, audio_strings.conference_audio_channels, &s->conference_audio_channels);
        s->conference_audio_channels = s->conference_audio_channels <= 0
                                       || s->conference_audio_channels > 2 ? 1 : s->conference_audio_channels;

        config_setting_lookup_int(setting, audio_strings.chat_audio_channels, &s->chat_audio_channels);
        s->chat_audio_channels = s->chat_audio_channels <= 0 || s->chat_audio_channels > 2 ? 2 : s->chat_audio_channels;

        if (config_setting_lookup_bool(setting, audio_strings.push_to_talk, &bool_val)) {
            s->push_to_talk = bool_val != 0;
        }
    }

#endif

#ifdef SOUND_NOTIFY

    /* Sound notifications */
    if ((setting = config_lookup(cfg, sound_strings.self)) != NULL) {
        if ((config_setting_lookup_string(setting, sound_strings.notif_error, &str) != CONFIG_TRUE) ||
                !set_sound(notif_error, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(notif_error, PACKAGE_DATADIR "/sounds/ToxicError.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.user_log_in, &str) ||
                !set_sound(user_log_in, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(user_log_in, PACKAGE_DATADIR "/sounds/ToxicContactOnline.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.user_log_out, &str) ||
                !set_sound(user_log_out, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(user_log_out, PACKAGE_DATADIR "/sounds/ToxicContactOffline.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.call_incoming, &str) ||
                !set_sound(call_incoming, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(call_incoming, PACKAGE_DATADIR "/sounds/ToxicIncomingCall.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.call_outgoing, &str) ||
                !set_sound(call_outgoing, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(call_outgoing, PACKAGE_DATADIR "/sounds/ToxicOutgoingCall.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.generic_message, &str) ||
                !set_sound(generic_message, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(generic_message, PACKAGE_DATADIR "/sounds/ToxicRecvMessage.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.transfer_pending, &str) ||
                !set_sound(transfer_pending, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(transfer_pending, PACKAGE_DATADIR "/sounds/ToxicTransferStart.wav");
            }
        }

        if (!config_setting_lookup_string(setting, sound_strings.transfer_completed, &str) ||
                !set_sound(transfer_completed, str)) {
            if (str && strcasecmp(str, NO_SOUND) != 0) {
                set_sound(transfer_completed, PACKAGE_DATADIR "/sounds/ToxicTransferComplete.wav");
            }
        }
    } else {
        set_sound(notif_error, PACKAGE_DATADIR "/sounds/ToxicError.wav");
        set_sound(user_log_in, PACKAGE_DATADIR "/sounds/ToxicContactOnline.wav");
        set_sound(user_log_out, PACKAGE_DATADIR "/sounds/ToxicContactOffline.wav");
        set_sound(call_incoming, PACKAGE_DATADIR "/sounds/ToxicIncomingCall.wav");
        set_sound(call_outgoing, PACKAGE_DATADIR "/sounds/ToxicOutgoingCall.wav");
        set_sound(generic_message, PACKAGE_DATADIR "/sounds/ToxicRecvMessage.wav");
        set_sound(transfer_pending, PACKAGE_DATADIR "/sounds/ToxicTransferStart.wav");
        set_sound(transfer_completed, PACKAGE_DATADIR "/sounds/ToxicTransferComplete.wav");
    }

#endif

    config_destroy(cfg);
    return 0;
}

void settings_reload(Toxic *toxic)
{
    Client_Config *c_config = toxic->c_config;
    const Run_Options *run_opts = toxic->run_opts;
    Windows *windows = toxic->windows;

    int ret = settings_load_main(c_config, run_opts);

    if (ret < 0) {
        fprintf(stderr, "Failed to reload global settings (error %d)\n", ret);
    }

    friend_reset_default_config_settings(c_config);

    ret = settings_load_friends(run_opts);

    if (ret < 0) {
        fprintf(stderr, "Failed to reload friend settings (error %d)\n", ret);
    }

    ret = settings_load_conferences(windows, run_opts);

    if (ret < 0) {
        fprintf(stderr, "Failed to reload conference settings (error %d)\n", ret);
    }

    ret = settings_load_groups(windows, run_opts);

    if (ret < 0) {
        fprintf(stderr, "Failed to reload group settings (error %d)\n", ret);
    }


    ret = settings_load_blocked_words(&toxic->client_data, run_opts);

    if (ret < 0) {
        fprintf(stderr, "Failed to reload blocked words list (error %d)\n", ret);
    }

    endwin();
    init_term(c_config, NULL, run_opts->default_locale);
    refresh_window_names(toxic);
}
