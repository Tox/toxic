/*
 * Toxic -- Tox Curses Client
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "toxic_windows.h"
#include "prompt.h"
#include "execute.h"
#include "misc_tools.h"

uint8_t pending_frnd_requests[MAX_FRIENDS_NUM][TOX_CLIENT_ID_SIZE] = {0};
uint8_t num_frnd_requests = 0;

/* Updates own nick in prompt statusbar */
void prompt_update_nick(ToxWindow *prompt, uint8_t *nick, uint16_t len)
{
    StatusBar *statusbar = prompt->stb;
    snprintf(statusbar->nick, sizeof(statusbar->nick), "%s", nick);
    statusbar->nick_len = len;
}

/* Updates own statusmessage in prompt statusbar */
void prompt_update_statusmessage(ToxWindow *prompt, uint8_t *statusmsg, uint16_t len)
{
    StatusBar *statusbar = prompt->stb;
    snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);
    statusbar->statusmsg_len = len;
}

/* Updates own status in prompt statusbar */
void prompt_update_status(ToxWindow *prompt, TOX_USERSTATUS status)
{
    StatusBar *statusbar = prompt->stb;
    statusbar->status = status;
}

/* Updates own connection status in prompt statusbar */
void prompt_update_connectionstatus(ToxWindow *prompt, bool is_connected)
{
    StatusBar *statusbar = prompt->stb;
    statusbar->is_online = is_connected;
}

/* Adds friend request to pending friend requests. 
   Returns request number on success, -1 if queue is full or other error. */
static int add_friend_request(uint8_t *public_key)
{
    if (num_frnd_requests >= MAX_FRIENDS_NUM)
        return -1;

    int i;

    for (i = 0; i <= num_frnd_requests; ++i) {
        if (!strlen(pending_frnd_requests[i])) {
            memcpy(pending_frnd_requests[i], public_key, TOX_CLIENT_ID_SIZE);

            if (i == num_frnd_requests)
                ++num_frnd_requests;

            return i;
        }
    }

    return -1;
}

static void prompt_onKey(ToxWindow *self, Tox *m, wint_t key)
{
    PromptBuf *prt = self->promptbuf;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);

    /* BACKSPACE key: Remove one character from line */
    if (key == 0x107 || key == 0x8 || key == 0x7f) {
        if (prt->pos > 0)
            del_char_buf_bck(prt->line, &prt->pos, &prt->len);
            wmove(self->window, y, x-1);    /* not necessary but fixes a display glitch */
            prt->scroll = false;
    }

    else if (key == KEY_DC) {      /* DEL key: Remove character at pos */
        if (prt->pos != prt->len) {
            del_char_buf_frnt(prt->line, &prt->pos, &prt->len);
            prt->scroll = false;
        }
    }

    else if (key == KEY_HOME) {    /* HOME key: Move cursor to beginning of line */
        if (prt->pos != 0)
            prt->pos = 0;
    }

    else if (key == KEY_END) {     /* END key: move cursor to end of line */
        if (prt->pos != prt->len)
            prt->pos = prt->len;
    } 

    else if (key == KEY_LEFT) {
        if (prt->pos > 0)
            --prt->pos;
    } 

    else if (key == KEY_RIGHT) {
        if (prt->pos < prt->len)
            ++prt->pos;
    } else
#if HAVE_WIDECHAR
    if (iswprint(key))
#else
    if (isprint(key))
#endif
    {
        if (prt->len < (MAX_STR_SIZE-1)) {
            add_char_to_buf(prt->line, &prt->pos, &prt->len, key);
            prt->scroll = true;
        }
    }
    /* RETURN key: execute command */
    else if (key == '\n') {
        wprintw(self->window, "\n"); 
        uint8_t *line = wcs_to_char(prt->line);
        execute(self->window, self, m, line, GLOBAL_COMMAND_MODE);
        reset_buf(prt->line, &prt->pos, &prt->len);
    }
}

static void prompt_onDraw(ToxWindow *self, Tox *m)
{
    PromptBuf *prt = self->promptbuf;

    curs_set(1);
    int x, y, x2, y2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);
    wclrtobot(self->window);

    /* if len is >= screen width offset max x by X_OFST to account for prompt char */
    int px2 = prt->len >= x2 ? x2 : x2 - X_OFST;

    if (px2 <= 0)
        return;

    /* len offset to account for prompt char (0 if len is < width of screen) */
    int p_ofst = px2 != x2 ? 0 : X_OFST;

    if (prt->len > 0) {
        mvwprintw(self->window, prt->orig_y, X_OFST, wcs_to_char(prt->line));

        /* y distance between pos and len */
        int d = prt->pos < (prt->len - px2) ? (y2 - y  - 1) : 0;

        /* 1 if end of line is touching bottom of window, 0 otherwise */
        int bot = prt->orig_y + ((prt->len + p_ofst) / px2) == y2;

        /* move point of line origin up when input scrolls screen down */
        if (prt->scroll && (y + d == y2 - bot) && ((prt->len + p_ofst) % px2 == 0) ) {
            --prt->orig_y;
            prt->scroll = false;
        }

    } else {
        prt->orig_y = y;    /* Mark point of origin for new line */

        wattron(self->window, COLOR_PAIR(GREEN));
        mvwprintw(self->window, y, 0, "# ");
        wattroff(self->window, COLOR_PAIR(GREEN));
    }

    StatusBar *statusbar = self->stb;
    werase(statusbar->topline);
    mvwhline(statusbar->topline, 1, 0, ACS_HLINE, x2);
    wmove(statusbar->topline, 0, 0);

    if (statusbar->is_online) {
        int colour = WHITE;
        char *status_text = "Unknown";

        switch (statusbar->status) {
        case TOX_USERSTATUS_NONE:
            status_text = "Online";
            colour = GREEN;
            break;
        case TOX_USERSTATUS_AWAY:
            status_text = "Away";
            colour = YELLOW;
            break;
        case TOX_USERSTATUS_BUSY:
            status_text = "Busy";
            colour = RED;
            break;
        }

        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s ", statusbar->nick);
        wattron(statusbar->topline, A_BOLD);
        wattron(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);
        wprintw(statusbar->topline, "[%s]", status_text);
        wattroff(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);
    } else {
        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s ", statusbar->nick);
        wattroff(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, "[Offline]");
    }

    wattron(statusbar->topline, A_BOLD);
    wprintw(statusbar->topline, " - %s", statusbar->statusmsg);
    wattroff(statusbar->topline, A_BOLD);

    /* put cursor back in correct spot */
    int y_m = prt->pos <= 0 ? prt->orig_y : prt->orig_y + ((prt->pos + p_ofst) / px2);
    int x_m = prt->pos > 0 ? (prt->pos + X_OFST) % x2 : X_OFST;
    wmove(self->window, y_m, x_m);
}

static void prompt_onInit(ToxWindow *self, Tox *m)
{
    scrollok(self->window, true);
    execute(self->window, self, m, "/help", GLOBAL_COMMAND_MODE);
    wclrtoeol(self->window);
}

static void prompt_onConnectionChange(ToxWindow *self, Tox *m, int friendnum , uint8_t status)
{
    if (friendnum < 0)
        return;

    uint8_t nick[TOX_MAX_NAME_LENGTH] = {'\0'};

    if (tox_get_name(m, friendnum, nick) == -1)
        return;

    if (!nick[0])
        snprintf(nick, sizeof(nick), "%s", UNKNOWN_NAME);

    if (status == 1) {
        wattron(self->window, COLOR_PAIR(GREEN));
        wattron(self->window, A_BOLD);
        wprintw(self->window, "\n%s ", nick);
        wattroff(self->window, A_BOLD);
        wprintw(self->window, "has come online\n");
        wattroff(self->window, COLOR_PAIR(GREEN));
    } else {
        wattron(self->window, COLOR_PAIR(RED));
        wattron(self->window, A_BOLD);
        wprintw(self->window, "\n%s ", nick);
        wattroff(self->window, A_BOLD);
        wprintw(self->window, "has gone offline\n");
        wattroff(self->window, COLOR_PAIR(RED));
    }

}

static void prompt_onFriendRequest(ToxWindow *self, uint8_t *key, uint8_t *data, uint16_t length)
{
    // make sure message data is null-terminated
    data[length - 1] = 0;

    wprintw(self->window, "\nFriend request with the message: %s\n", data);

    int n = add_friend_request(key);

    if (n == -1) {
        wprintw(self->window, "Friend request queue is full. Discarding request.\n");
        return;
    }

    wprintw(self->window, "Type \"/accept %d\" to accept it.\n", n);
    alert_window(self, WINDOW_ALERT_2, true);
}

void prompt_init_statusbar(ToxWindow *self, Tox *m)
{
    int x, y;
    getmaxyx(self->window, y, x);

    /* Init statusbar info */
    StatusBar *statusbar = self->stb;
    statusbar->status = TOX_USERSTATUS_NONE;
    statusbar->is_online = false;

    uint8_t nick[TOX_MAX_NAME_LENGTH] = {'\0'};
    tox_get_self_name(m, nick, TOX_MAX_NAME_LENGTH);
    snprintf(statusbar->nick, sizeof(statusbar->nick), "%s", nick);

    /* temporary until statusmessage saving works */
    uint8_t *statusmsg = "Toxing on Toxic v.0.2.4";
    m_set_statusmessage(m, statusmsg, strlen(statusmsg) + 1);
    snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);

    /* Init statusbar subwindow */
    statusbar->topline = subwin(self->window, 2, x, 0, 0);
}

ToxWindow new_prompt(void)
{
    ToxWindow ret;
    memset(&ret, 0, sizeof(ret));

    ret.active = true;

    ret.onKey = &prompt_onKey;
    ret.onDraw = &prompt_onDraw;
    ret.onInit = &prompt_onInit;
    ret.onConnectionChange = &prompt_onConnectionChange;
    ret.onFriendRequest = &prompt_onFriendRequest;

    strcpy(ret.name, "prompt");

    PromptBuf *promptbuf = calloc(1, sizeof(PromptBuf));
    StatusBar *stb = calloc(1, sizeof(StatusBar));

    if (stb != NULL && promptbuf != NULL) {
        ret.promptbuf = promptbuf;
        ret.stb = stb;
    } else {
        endwin();
        fprintf(stderr, "calloc() failed. Aborting...\n");
        exit(EXIT_FAILURE);
    }

    return ret;
}
