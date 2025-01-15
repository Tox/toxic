/*  toxic.c
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <tox/toxencryptsave.h>
#include <tox/tox.h>

#include "audio_device.h"
#include "bootstrap.h"
#include "conference.h"
#include "configdir.h"
#include "execute.h"
#include "file_transfers.h"
#include "friendlist.h"
#include "groupchats.h"
#include "init_queue.h"
#include "line_info.h"
#include "log.h"
#include "message_queue.h"
#include "misc_tools.h"
#include "name_lookup.h"
#include "netprof.h"
#include "notify.h"
#include "prompt.h"
#include "run_options.h"
#include "settings.h"
#include "term_mplex.h"
#include "toxic.h"
#include "windows.h"

#ifdef X11
#include "x11focus.h"
#endif

#ifdef AUDIO
#include "audio_call.h"
#ifdef VIDEO
#include "video_call.h"
#endif /* VIDEO */
#endif /* AUDIO */

#ifdef PYTHON
#include "api.h"
#include "python_api.h"
#endif

struct Winthread Winthread;

static void kill_toxic(Toxic *toxic)
{
    if (toxic == NULL) {
        return;
    }

    Client_Data *client_data = &toxic->client_data;

    free(client_data->data_path);
    free(client_data->block_path);
    free_ptr_array((void **) client_data->blocked_words);
    free(toxic->c_config);
    free(toxic->run_opts);
    free(toxic->windows);
    free(toxic);
}

void exit_toxic_success(Toxic *toxic)
{
    if (toxic == NULL) {
        exit(EXIT_FAILURE);
    }

    Run_Options *run_opts = toxic->run_opts;

    if (run_opts->netprof_log_dump) {
        netprof_log_dump(toxic->tox, run_opts->netprof_fp, get_unix_time() - run_opts->netprof_start_time);
    }

    store_data(toxic);

    terminate_notify();

    kill_all_file_transfers(toxic);
    kill_all_windows(toxic);

#ifdef AUDIO
#ifdef VIDEO
    terminate_video();
#endif /* VIDEO */
    terminate_audio(toxic->av);
#endif /* AUDIO */

#ifdef PYTHON
    terminate_python();
#endif /* PYTHON */

    tox_kill(toxic->tox);

    if (run_opts->log_fp != NULL) {
        fclose(run_opts->log_fp);
        run_opts->log_fp = NULL;
    }

    if (run_opts->netprof_fp != NULL) {
        fclose(run_opts->netprof_fp);
        run_opts->netprof_fp = NULL;
    }

    endwin();
    curl_global_cleanup();

#ifdef X11
    terminate_x11focus(&toxic->x11_focus);
#endif /* X11 */

    kill_toxic(toxic);

    exit(EXIT_SUCCESS);
}

void exit_toxic_err(int errcode, const char *errmsg, ...)
{
    endwin();

    if (freopen("/dev/tty", "w", stderr)) {
        va_list args;
        va_start(args, errmsg);
        vfprintf(stderr, errmsg, args);
        va_end(args);

        fprintf(stderr, "; toxic session aborted with error code %d\n", errcode);
    }

    exit(EXIT_FAILURE);
}

/* Sets ncurses refresh rate. Lower values make it refresh more often. */
void set_window_refresh_rate(size_t refresh_rate)
{
    timeout(refresh_rate);
}

static void get_custom_toxic_colours(const Client_Config *c_config, short *bar_bg_color, short *bar_fg_color,
                                     short *bar_accent_color, short *bar_notify_color)
{
    if (!string_is_empty(c_config->color_bar_bg)) {
        if (strcmp(c_config->color_bar_bg, "black") == 0) {
            *bar_bg_color = COLOR_BLACK;
        } else if (strcmp(c_config->color_bar_bg, "red") == 0) {
            *bar_bg_color = COLOR_RED;
        } else if (strcmp(c_config->color_bar_bg, "blue") == 0) {
            *bar_bg_color = COLOR_BLUE;
        } else if (strcmp(c_config->color_bar_bg, "cyan") == 0) {
            *bar_bg_color = COLOR_CYAN;
        } else if (strcmp(c_config->color_bar_bg, "green") == 0) {
            *bar_bg_color = COLOR_GREEN;
        } else if (strcmp(c_config->color_bar_bg, "yellow") == 0) {
            *bar_bg_color = COLOR_YELLOW;
        } else if (strcmp(c_config->color_bar_bg, "magenta") == 0) {
            *bar_bg_color = COLOR_MAGENTA;
        } else if (strcmp(c_config->color_bar_bg, "white") == 0) {
            *bar_bg_color = COLOR_WHITE;
        } else if (strcmp(c_config->color_bar_bg, "gray") == 0) {
            *bar_bg_color = CUSTOM_COLOUR_GRAY;
        } else if (strcmp(c_config->color_bar_bg, "orange") == 0) {
            *bar_bg_color = CUSTOM_COLOUR_ORANGE;
        } else if (strcmp(c_config->color_bar_bg, "pink") == 0) {
            *bar_bg_color = CUSTOM_COLOUR_PINK;
        } else if (strcmp(c_config->color_bar_bg, "brown") == 0) {
            *bar_bg_color = CUSTOM_COLOUR_BROWN;
        }
    }

    if (!string_is_empty(c_config->color_bar_fg)) {
        if (strcmp(c_config->color_bar_fg, "black") == 0) {
            *bar_fg_color = COLOR_BLACK;
        } else if (strcmp(c_config->color_bar_fg, "red") == 0) {
            *bar_fg_color = COLOR_RED;
        } else if (strcmp(c_config->color_bar_fg, "blue") == 0) {
            *bar_fg_color = COLOR_BLUE;
        } else if (strcmp(c_config->color_bar_fg, "cyan") == 0) {
            *bar_fg_color = COLOR_CYAN;
        } else if (strcmp(c_config->color_bar_fg, "green") == 0) {
            *bar_fg_color = COLOR_GREEN;
        } else if (strcmp(c_config->color_bar_fg, "yellow") == 0) {
            *bar_fg_color = COLOR_YELLOW;
        } else if (strcmp(c_config->color_bar_fg, "magenta") == 0) {
            *bar_fg_color = COLOR_MAGENTA;
        } else if (strcmp(c_config->color_bar_fg, "white") == 0) {
            *bar_fg_color = COLOR_WHITE;
        } else if (strcmp(c_config->color_bar_fg, "gray") == 0) {
            *bar_fg_color = CUSTOM_COLOUR_GRAY;
        } else if (strcmp(c_config->color_bar_fg, "orange") == 0) {
            *bar_fg_color = CUSTOM_COLOUR_ORANGE;
        } else if (strcmp(c_config->color_bar_fg, "pink") == 0) {
            *bar_fg_color = CUSTOM_COLOUR_PINK;
        } else if (strcmp(c_config->color_bar_fg, "brown") == 0) {
            *bar_fg_color = CUSTOM_COLOUR_BROWN;
        }
    }

    if (!string_is_empty(c_config->color_bar_accent)) {
        if (strcmp(c_config->color_bar_accent, "black") == 0) {
            *bar_accent_color = COLOR_BLACK;
        } else if (strcmp(c_config->color_bar_accent, "red") == 0) {
            *bar_accent_color = COLOR_RED;
        } else if (strcmp(c_config->color_bar_accent, "blue") == 0) {
            *bar_accent_color = COLOR_BLUE;
        } else if (strcmp(c_config->color_bar_accent, "cyan") == 0) {
            *bar_accent_color = COLOR_CYAN;
        } else if (strcmp(c_config->color_bar_accent, "green") == 0) {
            *bar_accent_color = COLOR_GREEN;
        } else if (strcmp(c_config->color_bar_accent, "yellow") == 0) {
            *bar_accent_color = COLOR_YELLOW;
        } else if (strcmp(c_config->color_bar_accent, "magenta") == 0) {
            *bar_accent_color = COLOR_MAGENTA;
        } else if (strcmp(c_config->color_bar_accent, "white") == 0) {
            *bar_accent_color = COLOR_WHITE;
        } else if (strcmp(c_config->color_bar_accent, "gray") == 0) {
            *bar_accent_color = CUSTOM_COLOUR_GRAY;
        } else if (strcmp(c_config->color_bar_accent, "orange") == 0) {
            *bar_accent_color = CUSTOM_COLOUR_ORANGE;
        } else if (strcmp(c_config->color_bar_accent, "pink") == 0) {
            *bar_accent_color = CUSTOM_COLOUR_PINK;
        } else if (strcmp(c_config->color_bar_accent, "brown") == 0) {
            *bar_accent_color = CUSTOM_COLOUR_BROWN;
        }
    }

    if (!string_is_empty(c_config->color_bar_notify)) {
        if (strcmp(c_config->color_bar_notify, "black") == 0) {
            *bar_notify_color = COLOR_BLACK;
        } else if (strcmp(c_config->color_bar_notify, "red") == 0) {
            *bar_notify_color = COLOR_RED;
        } else if (strcmp(c_config->color_bar_notify, "blue") == 0) {
            *bar_notify_color = COLOR_BLUE;
        } else if (strcmp(c_config->color_bar_notify, "cyan") == 0) {
            *bar_notify_color = COLOR_CYAN;
        } else if (strcmp(c_config->color_bar_notify, "green") == 0) {
            *bar_notify_color = COLOR_GREEN;
        } else if (strcmp(c_config->color_bar_notify, "yellow") == 0) {
            *bar_notify_color = COLOR_YELLOW;
        } else if (strcmp(c_config->color_bar_notify, "magenta") == 0) {
            *bar_notify_color = COLOR_MAGENTA;
        } else if (strcmp(c_config->color_bar_notify, "white") == 0) {
            *bar_notify_color = COLOR_WHITE;
        } else if (strcmp(c_config->color_bar_notify, "gray") == 0) {
            *bar_notify_color = CUSTOM_COLOUR_GRAY;
        } else if (strcmp(c_config->color_bar_notify, "orange") == 0) {
            *bar_notify_color = CUSTOM_COLOUR_ORANGE;
        } else if (strcmp(c_config->color_bar_notify, "pink") == 0) {
            *bar_notify_color = CUSTOM_COLOUR_PINK;
        } else if (strcmp(c_config->color_bar_notify, "brown") == 0) {
            *bar_notify_color = CUSTOM_COLOUR_BROWN;
        }
    }
}

void init_term(const Client_Config *c_config, Init_Queue *init_q, bool use_default_locale)
{
#if HAVE_WIDECHAR

    if (!use_default_locale) {
        if (setlocale(LC_ALL, "") == NULL)
            exit_toxic_err(FATALERR_LOCALE_NOT_SET,
                           "Could not set your locale. Please check your locale settings or "
                           "disable unicode support with the -d flag.");
    }

#endif

    initscr();
    cbreak();
    keypad(stdscr, 1);
    noecho();
    nonl();
    set_window_refresh_rate(NCURSES_DEFAULT_REFRESH_RATE);

    if (!has_colors()) {
        init_queue_add(init_q, "This terminal does not support colors.");
        refresh();
        return;
    }

    short bg_color = COLOR_BLACK;
    short bar_bg_color = COLOR_BLUE;
    short bar_fg_color = COLOR_WHITE;
    short bar_accent_color = COLOR_CYAN;
    short bar_notify_color = COLOR_YELLOW;

    if (start_color() != 0) {
        init_queue_add(init_q, "Failed to initialize ncurses colors.");
        // let's try anyways
    }

    if (c_config->native_colors) {
        if (assume_default_colors(-1, -1) == OK) {
            bg_color = -1;
        }
    }

    get_custom_toxic_colours(c_config, &bar_bg_color, &bar_fg_color, &bar_accent_color, &bar_notify_color);

    init_pair(WHITE, COLOR_WHITE, COLOR_BLACK);
    init_pair(GREEN, COLOR_GREEN, bg_color);
    init_pair(CYAN, COLOR_CYAN, bg_color);
    init_pair(RED, COLOR_RED, bg_color);
    init_pair(BLUE, COLOR_BLUE, bg_color);
    init_pair(YELLOW, COLOR_YELLOW, bg_color);
    init_pair(MAGENTA, COLOR_MAGENTA, bg_color);
    init_pair(BLACK, COLOR_BLACK, COLOR_BLACK);
    init_pair(WHITE_BLUE, COLOR_WHITE, COLOR_BLUE);
    init_pair(BLACK_WHITE, COLOR_BLACK, COLOR_WHITE);
    init_pair(WHITE_BLACK, COLOR_WHITE, COLOR_BLACK);
    init_pair(WHITE_GREEN, COLOR_WHITE, COLOR_GREEN);
    init_pair(BLACK_BG, COLOR_BLACK, bar_bg_color);
    init_pair(MAGENTA_BG, COLOR_MAGENTA, bar_bg_color);
    init_pair(BAR_TEXT, bar_fg_color, bar_bg_color);
    init_pair(BAR_SOLID, bar_bg_color, bar_bg_color);
    init_pair(BAR_ACCENT, bar_accent_color, bar_bg_color);
    init_pair(BAR_NOTIFY, bar_notify_color, bar_bg_color);
    init_pair(PEERLIST_LINE, bar_bg_color, bg_color);
    init_pair(STATUS_ONLINE, COLOR_GREEN, bar_bg_color);
    init_pair(STATUS_AWAY, COLOR_YELLOW, bar_bg_color);
    init_pair(STATUS_BUSY, COLOR_RED, bar_bg_color);
    init_pair(BLACK_BAR_FG, COLOR_BLACK, bar_bg_color);
    init_pair(WHITE_BAR_FG, COLOR_WHITE, bar_bg_color);
    init_pair(RED_BAR_FG, COLOR_RED, bar_bg_color);
    init_pair(GREEN_BAR_FG, COLOR_GREEN, bar_bg_color);
    init_pair(BLUE_BAR_FG, COLOR_BLUE, bar_bg_color);
    init_pair(CYAN_BAR_FG, COLOR_CYAN, bar_bg_color);
    init_pair(YELLOW_BAR_FG, COLOR_YELLOW, bar_bg_color);
    init_pair(MAGENTA_BAR_FG, COLOR_MAGENTA, bar_bg_color);

    if (COLORS >= 256) {
        init_color(CUSTOM_COLOUR_GRAY, 664, 664, 664);
        init_color(CUSTOM_COLOUR_ORANGE, 935, 525, 210);
        init_color(CUSTOM_COLOUR_PINK, 820, 555, 555);
        init_color(CUSTOM_COLOUR_BROWN, 485, 305, 210);

        init_pair(GRAY_BAR_FG, CUSTOM_COLOUR_GRAY, bar_bg_color);
        init_pair(ORANGE_BAR_FG, CUSTOM_COLOUR_ORANGE, bar_bg_color);
        init_pair(PINK_BAR_FG, CUSTOM_COLOUR_PINK, bar_bg_color);
        init_pair(BROWN_BAR_FG, CUSTOM_COLOUR_BROWN, bar_bg_color);
    } else {
        init_queue_add(init_q, "This terminal does not support 256-colors. Certain non-default colors may "
                       "not be displayed properly as a result.");
    }

    refresh();
}

/* Store Tox profile data to path.
 *
 * Return 0 if stored successfully.
 * Return -1 on error.
 */
#define TEMP_PROFILE_EXT ".tmp"
int store_data(const Toxic *toxic)
{
    const Run_Options *run_opts = toxic->run_opts;

    const char *path = toxic->client_data.data_path;

    if (path == NULL) {
        return -1;
    }

    const size_t temp_buf_size = strlen(path) + strlen(TEMP_PROFILE_EXT) + 1;
    char *temp_path = malloc(temp_buf_size);

    if (temp_path == NULL) {
        return -1;
    }

    snprintf(temp_path, temp_buf_size, "%s%s", path, TEMP_PROFILE_EXT);

    FILE *fp = fopen(temp_path, "wb");

    if (fp == NULL) {
        free(temp_path);
        return -1;
    }

    const size_t data_len = tox_get_savedata_size(toxic->tox);
    char *data = malloc(data_len * sizeof(char));

    if (data == NULL) {
        free(temp_path);
        fclose(fp);
        return -1;
    }

    tox_get_savedata(toxic->tox, (uint8_t *) data);

    const Client_Data *client_data = &toxic->client_data;

    if (client_data->is_encrypted && !run_opts->unencrypt_data) {
        size_t enc_len = data_len + TOX_PASS_ENCRYPTION_EXTRA_LENGTH;
        char *enc_data = malloc(enc_len * sizeof(char));

        if (enc_data == NULL) {
            fclose(fp);
            free(temp_path);
            free(data);
            return -1;
        }

        Tox_Err_Encryption err;
        tox_pass_encrypt((uint8_t *) data, data_len, (const uint8_t *) toxic->client_data.pass,
                         toxic->client_data.pass_len, (uint8_t *) enc_data, &err);

        if (err != TOX_ERR_ENCRYPTION_OK) {
            fprintf(stderr, "tox_pass_encrypt() failed with error %d\n", err);
            fclose(fp);
            free(temp_path);
            free(data);
            free(enc_data);
            return -1;
        }

        if (fwrite(enc_data, enc_len, 1, fp) != 1) {
            fprintf(stderr, "Failed to write profile data.\n");
            fclose(fp);
            free(temp_path);
            free(data);
            free(enc_data);
            return -1;
        }

        free(enc_data);
    } else {  /* data will not be encrypted */
        if (fwrite(data, data_len, 1, fp) != 1) {
            fprintf(stderr, "Failed to write profile data.\n");
            fclose(fp);
            free(temp_path);
            free(data);
            return -1;
        }
    }

    fclose(fp);
    free(data);

    if (rename(temp_path, path) != 0) {
        free(temp_path);
        return -1;
    }

    free(temp_path);

    return 0;
}

/* Set interface refresh flag. This should be called whenever the interface changes.
 *
 * This function is not thread safe.
 */
void flag_interface_refresh(void)
{
    Winthread.flag_refresh = 1;
    Winthread.last_refresh_flag = get_unix_time();
}
