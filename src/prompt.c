/*  prompt.c
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE    /* needed for wcswidth() */
#endif

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "autocomplete.h"
#include "execute.h"
#include "friendlist.h"
#include "help.h"
#include "input.h"
#include "line_info.h"
#include "log.h"
#include "misc_tools.h"
#include "netprof.h"
#include "notify.h"
#include "prompt.h"
#include "settings.h"
#include "toxic.h"
#include "toxic_strings.h"
#include "windows.h"

extern struct Winthread Winthread;

extern FriendsList Friends;
FriendRequests FrndRequests;

/* Array of global command names used for tab completion. */
static const char *const glob_cmd_list[] = {
    "/accept",
    "/add",
    "/avatar",
    "/clear",
    "/color",
    "/connect",
    "/decline",
    "/exit",
    "/group",
    "/conference",
#ifdef GAMES
    "/game",
#endif
    "/help",
    "/join",
    "/log",
    "/myid",
#ifdef QRCODE
    "/myqr",
#endif /* QRCODE */
    "/nick",
    "/note",
    "/nospam",
    "/quit",
    "/requests",
    "/status",

#ifdef AUDIO

    "/lsdev",
    "/sdev",

#endif /* AUDIO */

#ifdef VIDEO

    "/lsvdev",
    "/svdev",

#endif /* VIDEO */

#ifdef PYTHON

    "/run",

#endif /* PYTHON */

};

void kill_prompt_window(ToxWindow *self, Windows *windows, const Client_Config *c_config)
{
    ChatContext *ctx = self->chatwin;
    StatusBar *statusbar = self->stb;

    if (ctx != NULL)  {
        log_disable(ctx->log);
        line_info_cleanup(ctx->hst);

        delwin(ctx->linewin);
        delwin(ctx->history);
        free(ctx->log);
        free(ctx);
    }

    delwin(statusbar->topline);
    free(self->help);
    free(statusbar);

    del_window(self, windows, c_config);
}

/* callback: Updates own connection status in prompt statusbar */
void on_self_connection_status(Tox *tox, Tox_Connection connection_status, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;

    if (toxic == NULL) {
        return;
    }

    StatusBar *statusbar = toxic->home_window->stb;
    statusbar->connection = connection_status;

    flag_interface_refresh();
}

/* Updates own nick in prompt statusbar */
void prompt_update_nick(ToxWindow *self, const char *nick)
{
    StatusBar *statusbar = self->stb;
    snprintf(statusbar->nick, sizeof(statusbar->nick), "%s", nick);
    statusbar->nick_len = strlen(statusbar->nick);
}

/* Updates own statusmessage */
void prompt_update_statusmessage(Toxic *toxic, const char *statusmsg)
{
    ToxWindow *self = toxic->home_window;
    StatusBar *statusbar = self->stb;

    snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);

    const size_t len = strlen(statusbar->statusmsg);
    statusbar->statusmsg_len = len;

    Tox_Err_Set_Info err;
    tox_self_set_status_message(toxic->tox, (const uint8_t *) statusmsg, len, &err);

    if (err != TOX_ERR_SET_INFO_OK) {
        line_info_add(self, toxic->c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to set note (error %d)\n", err);
    }
}

/* Updates own status in prompt statusbar */
void prompt_update_status(ToxWindow *self, Tox_User_Status status)
{
    StatusBar *statusbar = self->stb;
    statusbar->status = status;
}

/* Returns our own connection status */
Tox_Connection prompt_selfConnectionStatus(Toxic *toxic)
{
    StatusBar *statusbar = toxic->home_window->stb;
    return statusbar->connection;
}

/* Adds friend request to pending friend requests.
   Returns request number on success, -1 if queue is full. */
static int add_friend_request(const char *public_key, const char *data)
{
    if (FrndRequests.max_idx >= MAX_FRIEND_REQUESTS) {
        return -1;
    }

    for (int i = 0; i <= FrndRequests.max_idx; ++i) {
        if (!FrndRequests.request[i].active) {
            FrndRequests.request[i].active = true;
            memcpy(FrndRequests.request[i].key, public_key, TOX_PUBLIC_KEY_SIZE);
            snprintf(FrndRequests.request[i].msg, sizeof(FrndRequests.request[i].msg), "%s", data);

            if (i == FrndRequests.max_idx) {
                ++FrndRequests.max_idx;
            }

            ++FrndRequests.num_requests;

            return i;
        }
    }

    return -1;
}

/*
 * Return true if input is recognized by handler
 */
static bool prompt_onKey(ToxWindow *self, Toxic *toxic, wint_t key, bool ltr)
{
    if (toxic == NULL || self == NULL) {
        return false;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);

    UNUSED_VAR(y);

    if (x2 <= 0 || y2 <= 0) {
        return false;
    }

    if (ctx->pastemode && key == '\r') {
        key = '\n';
    }

    /* ignore non-menu related input if active */
    if (self->help->active) {
        help_onKey(self, key);
        return true;
    }

    if (ltr || key == '\n') {    /* char is printable */
        input_new_char(self, toxic, key, x, x2);
        return true;
    }

    if (line_info_onKey(self, c_config, key)) {
        return true;
    }

    if (input_handle(self, toxic, key, x, x2)) {
        return true;
    }

    int input_ret = false;

    if (key == '\t') {    /* TAB key: auto-completes command */
        input_ret = true;

        if (ctx->len > 1 && ctx->line[0] == '/') {
            int diff;

            if (wcsncmp(ctx->line, L"/avatar ", wcslen(L"/avatar ")) == 0) {
                diff = dir_match(self, toxic, ctx->line, L"/avatar");
            }

#ifdef PYTHON
            else if (wcsncmp(ctx->line, L"/run ", wcslen(L"/run ")) == 0) {
                diff = dir_match(self, toxic, ctx->line, L"/run");
            }

#endif

            else {
                diff = complete_line(self, toxic, glob_cmd_list, sizeof(glob_cmd_list) / sizeof(char *));
            }

            if (diff != -1) {
                if (x + diff > x2 - 1) {
                    int wlen = MAX(0, wcswidth(ctx->line, sizeof(ctx->line) / sizeof(wchar_t)));
                    ctx->start = wlen < x2 ? 0 : wlen - x2 + 1;
                }
            } else {
                sound_notify(self, toxic, notif_error, 0, NULL);
            }
        } else {
            sound_notify(self, toxic, notif_error, 0, NULL);
        }
    } else if (key == '\r') {
        input_ret = true;

        rm_trailing_spaces_buf(ctx);

        wstrsubst(ctx->line, L'¶', L'\n');

        char line[MAX_STR_SIZE];

        if (wcs_to_mbs_buf(line, ctx->line, MAX_STR_SIZE) == -1) {
            memset(line, 0, sizeof(line));
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to parse message.");
        }

        const bool contains_blocked_word = string_contains_blocked_word(line, &toxic->client_data);

        if (line[0] != '\0' && !contains_blocked_word) {
            add_line_to_hist(ctx);

            if (strcmp(line, "/clear") != 0) {
                line_info_add(self, c_config, false, NULL, NULL, PROMPT, 0, 0, "%s", line);
            }

            execute(ctx->history, self, toxic, line, GLOBAL_COMMAND_MODE);
        }

        if (!contains_blocked_word) {
            wclear(ctx->linewin);
            wmove(self->window, y2, 0);
            reset_buf(ctx);
        } else {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, "* Message contains blocked word");
        }
    }

    return input_ret;
}

#define NET_INFO_REFRESH_INTERVAL 1
static void draw_network_info(const Tox *tox, StatusBar *stb)
{
    WINDOW *win = stb->topline;

    wattron(win, COLOR_PAIR(BAR_TEXT));
    wprintw(win, "%s", stb->network_info);
    wattroff(win, COLOR_PAIR(BAR_TEXT));

    if (!timed_out(stb->time_last_refreshed, NET_INFO_REFRESH_INTERVAL)) {
        return;
    }

    stb->time_last_refreshed = get_unix_time();

    const uint64_t up_bytes = netprof_get_bytes_up(tox);
    const uint64_t down_bytes = netprof_get_bytes_down(tox);

    const uint64_t up_delta = (up_bytes - stb->up_bytes) / NET_INFO_REFRESH_INTERVAL;
    const uint64_t down_delta = (down_bytes - stb->down_bytes) / NET_INFO_REFRESH_INTERVAL;

    stb->up_bytes = up_bytes;
    stb->down_bytes = down_bytes;

    float up = up_bytes;
    float down = down_bytes;
    const char *up_unit = "bytes";
    const char *down_unit = "bytes";

    if (up_bytes > MiB) {
        up /= (float)MiB;
        up_unit = "MiB";
    } else if (up_bytes > KiB) {
        up /= (float)KiB;
        up_unit = "KiB";
    }

    if (down_bytes > MiB) {
        down /= (float)MiB;
        down_unit = "MiB";
    } else if (down_bytes > KiB) {
        down /= (float)KiB;
        down_unit = "KiB";
    }

    float up_bps = up_delta;
    float down_bps = down_delta;
    const char *up_bps_unit = "b/s";
    const char *down_bps_unit = "b/s";

    if (up_bps > MiB) {
        up_bps /= (float)MiB;
        up_bps_unit = "MiB/s";
    } else if (up_bps > KiB) {
        up_bps /= (float)KiB;
        up_bps_unit = "KiB/s";
    }

    if (down_bps > MiB) {
        down_bps /= (float)MiB;
        down_bps_unit = "MiB/s";
    } else if (down_bps > KiB) {
        down_bps /= (float)KiB;
        down_bps_unit = "KiB/s";
    }

    snprintf(stb->network_info, sizeof(stb->network_info),
             " | [Up: %.1f%s (%.1f%s) | Down: %.1f%s (%.1f%s)]", up, up_unit, up_bps, up_bps_unit,
             down, down_unit, down_bps, down_bps_unit);
}

static void prompt_onDraw(ToxWindow *self, Toxic *toxic)
{
    if (toxic == NULL || self == NULL) {
        fprintf(stderr, "prompt_onDraw null param\n");
        return;
    }

    int x2;
    int y2;
    getmaxyx(self->window, y2, x2);

    if (y2 <= 0 || x2 <= 0) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    pthread_mutex_lock(&Winthread.lock);
    line_info_print(self, toxic->c_config);
    pthread_mutex_unlock(&Winthread.lock);

    wclear(ctx->linewin);

    if (ctx->len > 0) {
        mvwprintw(ctx->linewin, 0, 0, "%ls", &ctx->line[ctx->start]);
    }

    curs_set(1);

    StatusBar *statusbar = self->stb;

    wmove(statusbar->topline, 0, 0);

    pthread_mutex_lock(&Winthread.lock);
    Tox_Connection connection = statusbar->connection;
    Tox_User_Status status = statusbar->status;
    pthread_mutex_unlock(&Winthread.lock);

    wattron(statusbar->topline, COLOR_PAIR(BAR_ACCENT));
    wprintw(statusbar->topline, " [");
    wattroff(statusbar->topline, COLOR_PAIR(BAR_ACCENT));

    switch (connection) {
        case TOX_CONNECTION_TCP:
            wattron(statusbar->topline, A_BOLD | COLOR_PAIR(STATUS_ONLINE));
            wprintw(statusbar->topline, "TCP");
            wattroff(statusbar->topline, A_BOLD | COLOR_PAIR(STATUS_ONLINE));
            break;

        case TOX_CONNECTION_UDP:
            wattron(statusbar->topline, A_BOLD | COLOR_PAIR(STATUS_ONLINE));
            wprintw(statusbar->topline, "UDP");
            wattroff(statusbar->topline, A_BOLD | COLOR_PAIR(STATUS_ONLINE));
            break;

        default:
            wattron(statusbar->topline, COLOR_PAIR(BAR_TEXT));
            wprintw(statusbar->topline, "Offline");
            wattroff(statusbar->topline, COLOR_PAIR(BAR_TEXT));
            break;
    }

    wattron(statusbar->topline, COLOR_PAIR(BAR_ACCENT));
    wprintw(statusbar->topline, "]");
    wattroff(statusbar->topline, COLOR_PAIR(BAR_ACCENT));

    if (status != TOX_USER_STATUS_NONE) {
        int colour = MAGENTA;
        const char *status_text = "ERROR";

        switch (status) {
            case TOX_USER_STATUS_AWAY:
                colour = STATUS_AWAY;
                status_text = "Away";
                break;

            case TOX_USER_STATUS_BUSY:
                colour = STATUS_BUSY;
                status_text = "Busy";
                break;

            default:
                break;
        }

        wattron(statusbar->topline, COLOR_PAIR(BAR_ACCENT));
        wprintw(statusbar->topline, " [");
        wattroff(statusbar->topline, COLOR_PAIR(BAR_ACCENT));

        wattron(statusbar->topline, A_BOLD | COLOR_PAIR(colour));
        wprintw(statusbar->topline, "%s", status_text);
        wattroff(statusbar->topline, A_BOLD | COLOR_PAIR(colour));

        wattron(statusbar->topline, COLOR_PAIR(BAR_ACCENT));
        wprintw(statusbar->topline, "]");
        wattroff(statusbar->topline, COLOR_PAIR(BAR_ACCENT));

        wattron(statusbar->topline, COLOR_PAIR(BAR_TEXT));

        pthread_mutex_lock(&Winthread.lock);
        wprintw(statusbar->topline, " %s", statusbar->nick);
        pthread_mutex_unlock(&Winthread.lock);
    } else {
        wattron(statusbar->topline, COLOR_PAIR(BAR_TEXT));

        pthread_mutex_lock(&Winthread.lock);
        wprintw(statusbar->topline, " %s", statusbar->nick);
        pthread_mutex_unlock(&Winthread.lock);
    }

    int s_y;
    int s_x;
    getyx(statusbar->topline, s_y, s_x);

    mvwhline(statusbar->topline, s_y, s_x, ' ', x2 - s_x);
    wattroff(statusbar->topline, COLOR_PAIR(BAR_TEXT));

    /* Reset statusbar->statusmsg on window resize */
    if (x2 != self->x) {
        char statusmsg[TOX_MAX_STATUS_MESSAGE_LENGTH];

        pthread_mutex_lock(&Winthread.lock);

        const size_t slen = tox_self_get_status_message_size(toxic->tox);
        tox_self_get_status_message(toxic->tox, (uint8_t *) statusmsg);

        statusmsg[slen] = '\0';
        snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);
        statusbar->statusmsg_len = strlen(statusbar->statusmsg);

        pthread_mutex_unlock(&Winthread.lock);
    }

    self->x = x2;

    /* Truncate note if it doesn't fit in statusbar */
    const int maxlen = x2 - getcurx(statusbar->topline) - 3;

    pthread_mutex_lock(&Winthread.lock);
    const size_t statusmsg_len = statusbar->statusmsg_len;
    pthread_mutex_unlock(&Winthread.lock);

    if (statusmsg_len > maxlen && maxlen >= 3) {
        pthread_mutex_lock(&Winthread.lock);
        statusbar->statusmsg[maxlen - 3] = 0;
        strcat(statusbar->statusmsg, "...");
        statusbar->statusmsg_len = maxlen;
        pthread_mutex_unlock(&Winthread.lock);
    }

    if (statusmsg_len) {
        wattron(statusbar->topline, COLOR_PAIR(BAR_ACCENT));
        wprintw(statusbar->topline, " | ");
        wattroff(statusbar->topline, COLOR_PAIR(BAR_ACCENT));

        wattron(statusbar->topline, COLOR_PAIR(BAR_TEXT));
        pthread_mutex_lock(&Winthread.lock);
        wprintw(statusbar->topline, "%s", statusbar->statusmsg);
        pthread_mutex_unlock(&Winthread.lock);
        wattroff(statusbar->topline, COLOR_PAIR(BAR_TEXT));
    }

    if (toxic->c_config->show_network_info) {
        draw_network_info(toxic->tox, statusbar);
    }

    int y;
    int x;
    getyx(self->window, y, x);

    UNUSED_VAR(x);

    const int new_x = ctx->start ? x2 - 1 : MAX(0, wcswidth(ctx->line, ctx->pos));
    wmove(self->window, y, new_x);

    draw_window_bar(self, toxic->windows);

    wnoutrefresh(self->window);

    if (self->help->active) {
        help_draw_main(self);
    }
}

static void prompt_onConnectionChange(ToxWindow *self, Toxic *toxic, uint32_t friendnum,
                                      Tox_Connection connection_status)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (!friend_config_get_show_connection_msg(friendnum)) {
        return;
    }

    char nick[TOXIC_MAX_NAME_LENGTH + 1];
    get_friend_name(nick, sizeof(nick), friendnum);

    const char *msg;

    if (connection_status != TOX_CONNECTION_NONE && Friends.list[friendnum].connection_status == TOX_CONNECTION_NONE) {
        msg = "has come online";
        line_info_add(self, c_config, true, nick, NULL, CONNECTION, 0, GREEN, "%s", msg);
        write_to_log(ctx->log, c_config, msg, nick, LOG_HINT_CONNECT);

        if (self->active_box != -1) {
            box_notify2(self, toxic, user_log_in, NT_WNDALERT_2 | NT_NOTIFWND | NT_RESTOL, self->active_box,
                        "%s has come online", nick);
        } else {
            box_notify(self, toxic, user_log_in, NT_WNDALERT_2 | NT_NOTIFWND | NT_RESTOL, &self->active_box,
                       "Toxic", "%s has come online", nick);
        }
    } else if (connection_status == TOX_CONNECTION_NONE) {
        msg = "has gone offline";
        line_info_add(self, c_config, true, nick, NULL, DISCONNECTION, 0, RED, "%s", msg);
        write_to_log(ctx->log, c_config, msg, nick, LOG_HINT_DISCONNECT);

        if (self->active_box != -1) {
            box_notify2(self, toxic, user_log_out, NT_WNDALERT_2 | NT_NOTIFWND | NT_RESTOL, self->active_box,
                        "%s has gone offline", nick);
        } else {
            box_notify(self, toxic, user_log_out, NT_WNDALERT_2 | NT_NOTIFWND | NT_RESTOL, &self->active_box,
                       "Toxic", "%s has gone offline", nick);
        }
    }
}

/**
 * Return true is the first 3 bytes of `key` are identical to any other contact in the contact list.
 */
static bool key_is_similar(const char *key)
{
    for (size_t i = 0; i < Friends.max_idx; ++i) {
        const ToxicFriend *friend = &Friends.list[i];

        if (!friend->active) {
            continue;
        }

        if (memcmp(friend->pub_key, key, KEY_IDENT_BYTES / 2) == 0) {
            return true;
        }
    }

    return false;
}

static void prompt_onFriendRequest(ToxWindow *self, Toxic *toxic, const char *key, const char *data, size_t length)
{
    UNUSED_VAR(length);

    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 0, 0, "Friend request with the message '%s'", data);

    if (key_is_similar(key)) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED,
                      "WARNING: This contact's public key is suspiciously similar to that of another contact ");
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED,
                      "in your list. This may be an impersonation attempt, or it may have occurred by chance.");
    }

    const int n = add_friend_request(key, data);

    if (n == -1) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Friend request queue is full. Discarding request.");
        return;
    }

    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Type \"/accept %d\" or \"/decline %d\"", n, n);
    sound_notify(self, toxic, generic_message, NT_WNDALERT_1 | NT_NOTIFWND, NULL);
}

void prompt_init_statusbar(Toxic *toxic, bool first_time_run)
{
    ToxWindow *self = toxic->home_window;

    if (self == NULL) {
        exit_toxic_err(FATALERR_WININIT, "failed in prompt_init_statusbar");
    }

    int x2, y2;
    getmaxyx(self->window, y2, x2);

    if (y2 <= 0 || x2 <= 0) {
        exit_toxic_err(FATALERR_CURSES, "failed in prompt_init_statusbar");
    }

    UNUSED_VAR(y2);

    Tox *tox = toxic->tox;

    /* Init statusbar info */
    StatusBar *statusbar = self->stb;
    statusbar->status = TOX_USER_STATUS_NONE;
    statusbar->connection = TOX_CONNECTION_NONE;

    char nick[TOX_MAX_NAME_LENGTH + 1];
    char statusmsg[TOX_MAX_STATUS_MESSAGE_LENGTH + 1];

    const size_t n_len = tox_self_get_name_size(tox);
    tox_self_get_name(tox, (uint8_t *) nick);

    size_t s_len = tox_self_get_status_message_size(tox);
    tox_self_get_status_message(tox, (uint8_t *) statusmsg);

    Tox_User_Status status = tox_self_get_status(tox);

    nick[n_len] = '\0';
    statusmsg[s_len] = '\0';

    if (first_time_run) {
        snprintf(statusmsg, sizeof(statusmsg), "Toxing on Toxic");
        s_len = strlen(statusmsg);
        statusmsg[s_len] = '\0';
    }

    prompt_update_statusmessage(toxic, statusmsg);
    prompt_update_status(toxic->home_window, status);
    prompt_update_nick(toxic->home_window, nick);

    /* Init statusbar subwindow */
    statusbar->topline = subwin(self->window, TOP_BAR_HEIGHT, x2, 0, 0);
}

static void print_welcome_msg(ToxWindow *self, const Client_Config *c_config)
{
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, BLUE, "    _____ _____  _____ ____ ");
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, BLUE, "   |_   _/ _ \\ \\/ /_ _/ ___|");
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, BLUE, "     | || | | \\  / | | |    ");
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, BLUE, "     | || |_| /  \\ | | |___ ");
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, BLUE,
                  "     |_| \\___/_/\\_\\___\\____| v.%s\n", TOXICVER);
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, CYAN,
                  "Welcome to Toxic, a free, open source Tox-based instant messaging client.");
    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 1, CYAN,
                  "Type \"/help\" for assistance. Further help may be found via the man page.\n");
}

static void prompt_init_log(Toxic *toxic)
{
    const Client_Config *c_config = toxic->c_config;
    ToxWindow *self = toxic->home_window;
    ChatContext *ctx = self->chatwin;

    char myid[TOX_ADDRESS_SIZE];
    tox_self_get_address(toxic->tox, (uint8_t *) myid);

    if (log_init(ctx->log, toxic->c_config, self->name, myid, NULL, LOG_TYPE_PROMPT) != 0) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Warning: Log failed to initialize.");
        return;
    }

    if (toxic->c_config->autolog) {
        if (log_enable(ctx->log) == -1) {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Warning: Failed to enable log.");
        }
    }
}

static void prompt_onInit(ToxWindow *self, Toxic *toxic)
{
    curs_set(1);

    if (toxic == NULL || self == NULL) {
        return;
    }

    int y2;
    int x2;
    getmaxyx(self->window, y2, x2);

    if (y2 <= 0 || x2 <= 0) {
        exit_toxic_err(FATALERR_CURSES, "failed in prompt_onInit");
    }

    ChatContext *ctx = self->chatwin;

    ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2, 0, 0);
    self->window_bar = subwin(self->window, WINDOW_BAR_HEIGHT, x2, y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - WINDOW_BAR_HEIGHT, 0);

    ctx->log = calloc(1, sizeof(struct chatlog));
    ctx->hst = calloc(1, sizeof(struct history));

    if (ctx->log == NULL || ctx->hst == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in prompt_onInit");
    }

    line_info_init(ctx->hst);

    prompt_init_log(toxic);

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);

    if (toxic->c_config->show_welcome_msg) {
        print_welcome_msg(toxic->home_window, toxic->c_config);
    }
}

ToxWindow *new_prompt(void)
{
    ToxWindow *ret = calloc(1, sizeof(ToxWindow));

    if (ret == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_prompt");
    }

    ret->num = -1;
    ret->type = WINDOW_TYPE_PROMPT;

    ret->onKey = &prompt_onKey;
    ret->onDraw = &prompt_onDraw;
    ret->onInit = &prompt_onInit;
    ret->onConnectionChange = &prompt_onConnectionChange;
    ret->onFriendRequest = &prompt_onFriendRequest;

    strcpy(ret->name, "Home");

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    StatusBar *stb = calloc(1, sizeof(StatusBar));
    Help *help = calloc(1, sizeof(Help));

    if (stb == NULL || chatwin == NULL || help == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_prompt");
    }

    ret->chatwin = chatwin;
    ret->stb = stb;
    ret->help = help;

    ret->active_box = -1;

    return ret;
}
