/*  conference.c
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE    /* needed for strcasestr() and wcswidth() */
#endif

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#ifdef AUDIO
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
/* compatibility with older versions of OpenAL */
#ifndef ALC_ALL_DEVICES_SPECIFIER
#include <AL/alext.h>
#endif /* ALC_ALL_DEVICES_SPECIFIER */
#endif /* __APPLE__ */
#endif /* AUDIO */

#include "audio_device.h"
#include "autocomplete.h"
#include "conference.h"
#include "execute.h"
#include "help.h"
#include "input.h"
#include "line_info.h"
#include "log.h"
#include "misc_tools.h"
#include "notify.h"
#include "settings.h"
#include "toxic.h"
#include "toxic_strings.h"
#include "windows.h"

#define MAX_CONFERENCE_NUM 100
#define CONFERENCE_EVENT_WAIT 30

static ConferenceChat conferences[MAX_CONFERENCE_NUM];
static int max_conference_index = 0;

extern struct Winthread Winthread;

static_assert(TOX_CONFERENCE_ID_SIZE == TOX_PUBLIC_KEY_SIZE, "TOX_CONFERENCE_ID_SIZE != TOX_PUBLIC_KEY_SIZE");

/* Array of conference command names used for tab completion. */
static const char *const conference_cmd_list[] = {
    "/accept",
    "/add",
#ifdef AUDIO
    "/audio",
#endif
    "/avatar",
    "/chatid",
    "/clear",
    "/close",
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
#ifdef AUDIO
    "/mute",
#endif
    "/myid",
#ifdef QRCODE
    "/myqr",
#endif /* QRCODE */
    "/nick",
    "/note",
    "/nospam",
    "/quit",
    "/requests",
#ifdef AUDIO
    "/ptt",
    "/sense",
#endif
    "/status",
    "/title",

#ifdef PYTHON

    "/run",

#endif /* PYTHON */
};

static ToxWindow *new_conference_chat(uint32_t conferencenum);

void conference_set_title(ToxWindow *self, uint32_t conferencesnum, const char *title, size_t length)
{
    ConferenceChat *chat = &conferences[conferencesnum];

    if (!chat->active) {
        return;
    }

    if (length > CONFERENCE_MAX_TITLE_LENGTH) {
        length = CONFERENCE_MAX_TITLE_LENGTH;
    }

    memcpy(chat->title, title, length);
    chat->title[length] = 0;
    chat->title_length = length;

    set_window_title(self, title, length);
}

static void kill_conference_window(ToxWindow *self, Windows *windows, const Client_Config *c_config)
{
    if (self == NULL) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    if (ctx != NULL) {
        log_disable(ctx->log);
        line_info_cleanup(ctx->hst);
        delwin(ctx->linewin);
        delwin(ctx->history);
        delwin(ctx->sidebar);
        free(ctx->log);
        free(ctx);
    }

    free(self->help);
    kill_notifs(self->active_box);
    del_window(self, windows, c_config);
}

static void init_conference_logging(ToxWindow *self, Toxic *toxic, uint32_t conferencenum)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    char my_id[TOX_ADDRESS_SIZE];
    tox_self_get_address(tox, (uint8_t *) my_id);

    char conference_id[TOX_CONFERENCE_ID_SIZE];
    tox_conference_get_id(tox, conferencenum, (uint8_t *) conference_id);

    if (log_init(ctx->log, c_config, conferences[self->num].title, my_id, conference_id, LOG_TYPE_CHAT) != 0) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Warning: Log failed to initialize.");
        return;
    }

    if (load_chat_history(ctx->log, self, c_config) != 0) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to load chat history.");
    }

    if (c_config->autolog) {
        if (log_enable(ctx->log) != 0) {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to enable chat log.");
        }
    }
}

static void delete_conference(ToxWindow *self, Toxic *toxic, uint32_t conferencenum);

int init_conference_win(Toxic *toxic, uint32_t conferencenum, uint8_t type, const char *title, size_t length)
{
    if (toxic == NULL) {
        return -1;
    }

    if (conferencenum > MAX_CONFERENCE_NUM) {
        return -1;
    }

    ToxWindow *self = new_conference_chat(conferencenum);

    for (int i = 0; i <= max_conference_index; ++i) {
        if (!conferences[i].active) {
            // FIXME: it is assumed at various points in the code that
            // toxcore's conferencenums agree with toxic's indices to conferences;
            // probably it so happens that this will (at least typically) be
            // the case, because toxic and tox maintain the indices in
            // parallel ways. But it isn't guaranteed by the API.
            if (i == max_conference_index) {
                ++max_conference_index;
            }

            conferences[i].active = true;
            conferences[i].conferencenum = conferencenum;
            conferences[i].num_peers = 0;
            conferences[i].type = type;
            conferences[i].start_time = get_unix_time();
            conferences[i].audio_enabled = false;
            conferences[i].last_sent_audio = 0;

            const int window_id = add_window(toxic, self);

            if (window_id < 0) {
                fprintf(stderr, "Failed to create new conference window\n");
                delete_conference(self, toxic, conferencenum);
                return -1;
            }

            conferences[i].window_id = window_id;

            if (!tox_conference_get_id(toxic->tox, conferencenum, (uint8_t *) conferences[i].id)) {
                fprintf(stderr, "Failed to fetch conference ID for conferencenum: %u\n", conferencenum);
                delete_conference(self, toxic, conferencenum);
                return -1;
            }

#ifdef AUDIO
            conferences[i].push_to_talk_enabled = toxic->c_config->push_to_talk;
#endif

            set_active_window_by_id(toxic->windows, conferences[i].window_id);

            conference_set_title(self, conferencenum, title, length);

            init_conference_logging(self, toxic, conferencenum);

            return conferences[i].window_id;
        }
    }

    kill_conference_window(self, toxic->windows, toxic->c_config);

    return -1;
}

static void free_peer(ConferencePeer *peer)
{
#ifdef AUDIO

    if (peer->sending_audio) {
        close_device(output, peer->audio_out_idx);
    }

#endif
}

void free_conference(ToxWindow *self, Windows *windows, const Client_Config *c_config, uint32_t conferencenum)
{
    ConferenceChat *chat = &conferences[conferencenum];

    for (uint32_t i = 0; i < chat->num_peers; ++i) {
        ConferencePeer *peer = &chat->peer_list[i];

        if (peer->active) {
            free_peer(peer);
        }
    }

#ifdef AUDIO

    if (chat->audio_enabled) {
        close_device(input, chat->audio_in_idx);
    }

#endif

    free(chat->name_list);
    free(chat->peer_list);
    conferences[conferencenum] = (ConferenceChat) {
        0
    };

    int i;

    for (i = max_conference_index; i > 0; --i) {
        if (conferences[i - 1].active) {
            break;
        }
    }

    max_conference_index = i;
    kill_conference_window(self, windows, c_config);
}

static void delete_conference(ToxWindow *self, Toxic *toxic, uint32_t conferencenum)
{
    tox_conference_delete(toxic->tox, conferencenum, NULL);
    free_conference(self, toxic->windows, toxic->c_config, conferencenum);
}

void conference_rename_log_path(Toxic *toxic, uint32_t conferencenum, const char *new_title)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return;
    }

    char myid[TOX_ADDRESS_SIZE];
    tox_self_get_address(toxic->tox, (uint8_t *) myid);

    char conference_id[TOX_CONFERENCE_ID_SIZE];
    tox_conference_get_id(toxic->tox, conferencenum, (uint8_t *) conference_id);

    if (rename_logfile(toxic->windows, toxic->c_config, chat->title, new_title, myid, conference_id,
                       chat->window_id) != 0) {
        fprintf(stderr, "Failed to rename conference log to '%s'\n", new_title);
    }
}

/* destroys and re-creates conference window with or without the peerlist */
void redraw_conference_win(ToxWindow *self)
{
    ChatContext *ctx = self->chatwin;

    endwin();
    refresh();
    clear();

    int x2;
    int y2;
    getmaxyx(self->window, y2, x2);

    if (y2 <= 0 || x2 <= 0) {
        return;
    }

    if (ctx->sidebar) {
        delwin(ctx->sidebar);
        ctx->sidebar = NULL;
    }

    delwin(ctx->linewin);
    delwin(ctx->history);
    delwin(self->window_bar);
    delwin(self->window);

    self->window = newwin(y2, x2, 0, 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);
    self->window_bar = subwin(self->window, WINDOW_BAR_HEIGHT, x2, y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);

    if (self->show_peerlist) {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2 - SIDEBAR_WIDTH - 1, 0, 0);
        ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);
    } else {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2, 0, 0);
    }

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);

    if (!self->scroll_pause) {
        line_info_reset_start(self, ctx->hst);
    }
}

static void conference_onConferenceMessage(ToxWindow *self, Toxic *toxic, uint32_t conferencenum, uint32_t peernum,
        Tox_Message_Type type, const char *msg, size_t len)
{
    UNUSED_VAR(len);

    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    if (self->num != conferencenum) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH];
    get_conference_nick_truncate(tox, nick, peernum, conferencenum);

    char selfnick[TOX_MAX_NAME_LENGTH];
    tox_self_get_name(tox, (uint8_t *) selfnick);

    size_t sn_len = tox_self_get_name_size(tox);
    selfnick[sn_len] = '\0';

    int nick_clr = strcmp(nick, selfnick) == 0 ? GREEN : CYAN;

    /* Only play sound if mentioned by someone else */
    if (strcasestr(msg, selfnick) && strcmp(selfnick, nick)) {
        if (self->active_box != -1) {
            box_notify2(self, toxic, generic_message, NT_WNDALERT_0 | NT_NOFOCUS | c_config->bell_on_message,
                        self->active_box, "%s %s", nick, msg);
        } else {
            box_notify(self, toxic, generic_message, NT_WNDALERT_0 | NT_NOFOCUS | c_config->bell_on_message,
                       &self->active_box, self->name, "%s %s", nick, msg);
        }

        nick_clr = RED;
    } else {
        sound_notify(self, toxic, silent, NT_WNDALERT_1, NULL);
    }

    line_info_add(self, c_config, true, nick, NULL, type == TOX_MESSAGE_TYPE_NORMAL ? IN_MSG : IN_ACTION, 0,
                  nick_clr, "%s", msg);

    write_to_log(ctx->log, c_config, msg, nick,
                 type == TOX_MESSAGE_TYPE_NORMAL ? LOG_HINT_NORMAL_I : LOG_HINT_ACTION);
}

static void conference_onConferenceTitleChange(ToxWindow *self, Toxic *toxic, uint32_t conferencenum, uint32_t peernum,
        const char *title,
        size_t length)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    if (self->num != conferencenum) {
        return;
    }

    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return;
    }

    conference_rename_log_path(toxic, conferencenum, title);  // must be called first

    conference_set_title(self, conferencenum, title, length);

    /* don't announce title when we join the room */
    if (!timed_out(conferences[conferencenum].start_time, CONFERENCE_EVENT_WAIT)) {
        return;
    }

    char nick[TOX_MAX_NAME_LENGTH];
    get_conference_nick_truncate(tox, nick, peernum, conferencenum);

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), "-!- %s set the conference title to: %s", nick, title);

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, true, MAGENTA, "%s", tmp_event);
    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_TOPIC);
}

/* Puts `(NameListEntry *)`s in `entries` for each matched peer, up to a
 * maximum of `maxpeers`.
 * Maches each peer whose name or pubkey begins with `prefix`.
 * If `prefix` is exactly the pubkey of a peer, matches only that peer.
 * return number of entries placed in `entries`.
 */
uint32_t get_name_list_entries_by_prefix(uint32_t conferencenum, const char *prefix, NameListEntry **entries,
        uint32_t maxpeers)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return 0;
    }

    const size_t len = strlen(prefix);

    if (len == 2 * TOX_PUBLIC_KEY_SIZE) {
        for (uint32_t i = 0; i < chat->num_peers; ++i) {
            NameListEntry *entry = &chat->name_list[i];

            if (strcasecmp(prefix, entry->pubkey_str) == 0) {
                entries[0] = entry;
                return 1;
            }
        }
    }

    uint32_t n = 0;

    for (uint32_t i = 0; i < chat->num_peers; ++i) {
        NameListEntry *entry = &chat->name_list[i];

        if (strncmp(prefix, entry->name, len) == 0
                || strncasecmp(prefix, entry->pubkey_str, len) == 0) {
            entries[n] = entry;
            ++n;

            if (n == maxpeers) {
                return n;
            }
        }
    }

    return n;
}


static int compare_name_list_entries(const void *a, const void *b)
{
    const int cmp1 = qsort_strcasecmp_hlpr(
                         ((const NameListEntry *)a)->name,
                         ((const NameListEntry *)b)->name);

    if (cmp1 == 0) {
        return qsort_strcasecmp_hlpr(
                   ((const NameListEntry *)a)->pubkey_str,
                   ((const NameListEntry *)b)->pubkey_str);
    }

    return cmp1;
}

static void conference_update_name_list(uint32_t conferencenum)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return;
    }

    if (chat->name_list) {
        free(chat->name_list);
    }

    chat->name_list = malloc(chat->num_peers * sizeof(NameListEntry));

    if (chat->name_list == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in conference_update_name_list");
    }

    uint32_t count = 0;

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        const ConferencePeer *peer = &chat->peer_list[i];
        NameListEntry *entry = &chat->name_list[count];

        if (peer->active) {
            memcpy(entry->name, peer->name, peer->name_length + 1);
            tox_pk_bytes_to_str(peer->pubkey, sizeof(peer->pubkey), entry->pubkey_str, sizeof(entry->pubkey_str));
            entry->peernum = i;
            ++count;
        }
    }

    if (count != chat->num_peers) {
        fprintf(stderr, "WARNING: count != chat->num_peers\n");
    }

    qsort(chat->name_list, count, sizeof(NameListEntry), compare_name_list_entries);
}

/* Reallocates conferencenum's peer list.
 *
 * Returns 0 on success.
 * Returns -1 on failure.
 */
static int realloc_peer_list(ConferenceChat *chat, uint32_t num_peers)
{
    if (!chat) {
        return -1;
    }

    if (num_peers == 0) {
        free(chat->peer_list);
        chat->peer_list = NULL;
        return 0;
    }

    ConferencePeer *tmp_list = realloc(chat->peer_list, num_peers * sizeof(ConferencePeer));

    if (!tmp_list) {
        return -1;
    }

    chat->peer_list = tmp_list;

    return 0;
}

/* return NULL if peer or conference doesn't exist */
static ConferencePeer *peer_in_conference(uint32_t conferencenum, uint32_t peernum)
{
    if (conferencenum >= MAX_CONFERENCE_NUM) {
        return NULL;
    }

    const ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || peernum > chat->max_idx) {
        return NULL;
    }

    ConferencePeer *peer = &chat->peer_list[peernum];

    if (!peer->active) {
        return NULL;
    }

    return peer;
}

#ifdef AUDIO

/* Return true if ptt is disabled or enabled and active. */
static bool conference_check_push_to_talk(ConferenceChat *chat)
{
    if (!chat->push_to_talk_enabled) {
        return true;
    }

    return !timed_out(chat->ptt_last_pushed, 1);
}

static void conference_enable_push_to_talk(ConferenceChat *chat)
{
    chat->ptt_last_pushed = get_unix_time();
}

static void set_peer_audio_position(Tox *tox, uint32_t conferencenum, uint32_t peernum)
{
    ConferenceChat *chat = &conferences[conferencenum];
    ConferencePeer *peer = &chat->peer_list[peernum];

    if (peer == NULL || !peer->sending_audio) {
        return;
    }

    // Position peers at distance 1 in front of listener,
    // ordered left to right by order in peerlist excluding self.
    uint32_t num_posns = chat->num_peers;
    uint32_t peer_posn = peernum;

    for (uint32_t i = 0; i < chat->num_peers; ++i) {
        if (tox_conference_peer_number_is_ours(tox, conferencenum, peernum, NULL)) {
            if (i == peernum) {
                return;
            }

            --num_posns;

            if (i < peernum) {
                --peer_posn;
            }
        }
    }

    const float angle = asinf(peer_posn - (float)(num_posns - 1) / 2);
    set_source_position(peer->audio_out_idx, sinf(angle), cosf(angle), 0);
}
#endif // AUDIO


static bool find_peer_by_pubkey(const ConferencePeer *list, uint32_t num_peers, uint8_t *pubkey, uint32_t *idx)
{
    for (uint32_t i = 0; i < num_peers; ++i) {
        const ConferencePeer *peer = &list[i];

        if (peer->active && memcmp(peer->pubkey, pubkey, TOX_PUBLIC_KEY_SIZE) == 0) {
            if (idx) {
                *idx = i;
            }

            return true;
        }
    }

    return false;
}

static void update_peer_list(ToxWindow *self, Toxic *toxic, uint32_t conferencenum, uint32_t num_peers,
                             uint32_t old_num_peers)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    ConferencePeer *old_peer_list = malloc(old_num_peers * sizeof(ConferencePeer));

    if (!old_peer_list) {
        exit_toxic_err(FATALERR_MEMORY, "failed in update_peer_list");
    }

    if (chat->peer_list != NULL) {
        memcpy(old_peer_list, chat->peer_list, old_num_peers * sizeof(ConferencePeer));
    }

    if (realloc_peer_list(chat, num_peers) != 0) {
        free(old_peer_list);
        fprintf(stderr, "Warning: realloc_peer_list() failed in update_peer_list()\n");
        return;
    }

    for (uint32_t i = 0; i < num_peers; ++i) {
        ConferencePeer *peer = &chat->peer_list[i];

        *peer = (struct ConferencePeer) {
            0
        };

        Tox_Err_Conference_Peer_Query err;
        tox_conference_peer_get_public_key(tox, conferencenum, i, peer->pubkey, &err);

        if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
            continue;
        }

        bool new_peer = true;
        uint32_t j;

        if (find_peer_by_pubkey(old_peer_list, old_num_peers, peer->pubkey, &j)) {
            ConferencePeer *old_peer = &old_peer_list[j];
            memcpy(peer, old_peer, sizeof(ConferencePeer));
            old_peer->active = false;
            new_peer = false;
        }

        size_t length = tox_conference_peer_get_name_size(tox, conferencenum, i, &err);

        if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
            continue;
        }

        if (length >= TOX_MAX_NAME_LENGTH) {
            length = TOX_MAX_NAME_LENGTH - 1;
        }

        tox_conference_peer_get_name(tox, conferencenum, i, (uint8_t *) peer->name, &err);
        peer->name[length] = '\0';

        if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
            continue;
        }

        peer->active = true;
        peer->name_length = length;
        peer->peernum = i;

        if (new_peer && peer->name_length > 0 && timed_out(chat->start_time, CONFERENCE_EVENT_WAIT)) {
            const char *msg = "has joined the conference";
            line_info_add(self, c_config, true, peer->name, NULL, CONNECTION, 0, GREEN, "%s", msg);
            write_to_log(ctx->log, c_config, msg, peer->name, LOG_HINT_CONNECT);
        }

#ifdef AUDIO
        set_peer_audio_position(tox, conferencenum, i);
#endif
    }

    conference_update_name_list(conferencenum);

    for (uint32_t i = 0; i < old_num_peers; ++i) {
        ConferencePeer *old_peer = &old_peer_list[i];

        if (old_peer->active) {
            if (old_peer->name_length > 0 && !find_peer_by_pubkey(chat->peer_list, chat->num_peers, old_peer->pubkey, NULL)) {
                const char *msg = "has left the conference";
                line_info_add(self, c_config, true, old_peer->name, NULL, DISCONNECTION, 0, RED, "%s", msg);
                write_to_log(ctx->log, c_config, msg, old_peer->name, LOG_HINT_DISCONNECT);
            }

            free_peer(old_peer);
        }
    }

    free(old_peer_list);
}

static void conference_onConferenceNameListChange(ToxWindow *self, Toxic *toxic, uint32_t conferencenum)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;

    if (self->num != conferencenum) {
        return;
    }

    if (conferencenum > max_conference_index) {
        return;
    }

    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return;
    }

    Tox_Err_Conference_Peer_Query err;

    const uint32_t num_peers = tox_conference_peer_count(tox, conferencenum, &err);

    if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
        fprintf(stderr, "conference_onConferenceNameListChange() failed with error: %d\n", err);
        return;
    }

    const uint32_t old_num = chat->num_peers;

    chat->num_peers = num_peers;
    chat->max_idx = num_peers;
    update_peer_list(self, toxic, conferencenum, num_peers, old_num);
}

static void conference_onConferencePeerNameChange(ToxWindow *self, Toxic *toxic, uint32_t conferencenum,
        uint32_t peernum,
        const char *name, size_t length)
{
    UNUSED_VAR(length);

    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;

    if (self->num != conferencenum) {
        return;
    }

    const ConferencePeer *peer = peer_in_conference(conferencenum, peernum);

    if (peer != NULL) {
        ChatContext *ctx = self->chatwin;

        if (peer->name_length > 0) {
            char log_event[TOX_MAX_NAME_LENGTH * 2 + 32];
            line_info_add(self, c_config, true, peer->name, (const char *) name, NAME_CHANGE, 0, 0,
                          " is now known as ");

            snprintf(log_event, sizeof(log_event), "is now known as %s", (const char *) name);
            write_to_log(ctx->log, c_config, log_event, peer->name, LOG_HINT_NAME);

            // this is kind of a hack; peers always join a group with no name set and then set it after
        } else if (timed_out(conferences[conferencenum].start_time, CONFERENCE_EVENT_WAIT)) {
            const char *msg = "has joined the conference";
            line_info_add(self, c_config, true, name, NULL, CONNECTION, 0, GREEN, "%s", msg);
            write_to_log(ctx->log, c_config, msg, name, LOG_HINT_CONNECT);
        }
    }

    conference_onConferenceNameListChange(self, toxic, conferencenum);
}

static void send_conference_action(ToxWindow *self, ChatContext *ctx, Toxic *toxic, char *action)
{
    if (action == NULL) {
        wprintw(ctx->history, "Invalid syntax.\n");
        return;
    }

    Tox_Err_Conference_Send_Message err;

    if (!tox_conference_send_message(toxic->tox, self->num, TOX_MESSAGE_TYPE_ACTION, (uint8_t *) action,
                                     strlen(action), &err)) {
        line_info_add(self, toxic->c_config, false, NULL, NULL, SYS_MSG, 0, RED,
                      " * Failed to send action (error %d)", err);
    }
}

/* Offset for the peer number box at the top of the statusbar */
static int sidebar_offset(uint32_t conferencenum)
{
    return 2 + conferences[conferencenum].audio_enabled;
}

/*
 * Return true if input is recognized by handler
 */
static bool conference_onKey(ToxWindow *self, Toxic *toxic, wint_t key, bool ltr)
{
    if (toxic == NULL || self == NULL) {
        return false;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);

    UNUSED_VAR(y);

    if (x2 <= 0 || y2 <= 0) {
        return false;
    }

    if (self->help->active) {
        help_onKey(self, key);
        return true;
    }

    if (ctx->pastemode && key == L'\r') {
        key = L'\n';
    }

    if (ltr || key == L'\n') {    /* char is printable */
        input_new_char(self, toxic, key, x, x2);
        return true;
    }

    if (line_info_onKey(self, c_config, key)) {
        return true;
    }

    if (input_handle(self, toxic, key, x, x2)) {
        return true;
    }

    bool input_ret = false;
    ConferenceChat *chat = &conferences[self->num];

#ifdef AUDIO

    if (chat->audio_enabled && chat->push_to_talk_enabled && key == KEY_F(2)) {
        input_ret = true;
        conference_enable_push_to_talk(chat);
    }

#endif // AUDIO

    if (key == L'\t') {  /* TAB key: auto-completes peer name or command */
        input_ret = true;

        if (ctx->len > 0) {
            int diff = -1;

            /* TODO: make this not suck */
            if (ctx->line[0] != L'/' || wcscmp(ctx->line, L"/me") == 0) {
                const char **complete_strs = calloc(chat->num_peers, sizeof(const char *));

                if (complete_strs) {
                    for (uint32_t i = 0; i < chat->num_peers; ++i) {
                        complete_strs[i] = (const char *) chat->name_list[i].name;
                    }

                    diff = complete_line(self, toxic, complete_strs, chat->num_peers);
                    free(complete_strs);
                }
            } else if (wcsncmp(ctx->line, L"/avatar ", wcslen(L"/avatar ")) == 0) {
                diff = dir_match(self, toxic, ctx->line, L"/avatar");
            }

#ifdef PYTHON
            else if (wcsncmp(ctx->line, L"/run ", wcslen(L"/run ")) == 0) {
                diff = dir_match(self, toxic, ctx->line, L"/run");
            }

#endif
            else if (wcsncmp(ctx->line, L"/mute ", wcslen(L"/mute ")) == 0) {
                const char **complete_strs = calloc(chat->num_peers, sizeof(const char *));

                if (complete_strs) {
                    for (uint32_t i = 0; i < chat->num_peers; ++i) {
                        complete_strs[i] = (const char *) chat->name_list[i].name;
                    }

                    diff = complete_line(self, toxic, complete_strs, chat->num_peers);

                    if (diff == -1) {
                        for (uint32_t i = 0; i < chat->num_peers; ++i) {
                            complete_strs[i] = (const char *) chat->name_list[i].pubkey_str;
                        }

                        diff = complete_line(self, toxic, complete_strs, chat->num_peers);
                    }

                    free(complete_strs);
                }

            } else {
                diff = complete_line(self, toxic, conference_cmd_list, sizeof(conference_cmd_list) / sizeof(char *));
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
    } else if (key == T_KEY_C_DOWN) {    /* Scroll peerlist up and down one position */
        input_ret = true;
        const int L = y2 - CHATBOX_HEIGHT - sidebar_offset(self->num);

        if (chat->side_pos < (int64_t) chat->num_peers - L) {
            ++chat->side_pos;
        }
    } else if (key == T_KEY_C_UP) {
        input_ret = true;

        if (chat->side_pos > 0) {
            --chat->side_pos;
        }
    } else if (key == L'\r') {
        input_ret = true;
        rm_trailing_spaces_buf(ctx);

        if (!wstring_is_empty(ctx->line)) {
            add_line_to_hist(ctx);

            wstrsubst(ctx->line, L'¶', L'\n');

            char line[MAX_STR_SIZE];

            if (wcs_to_mbs_buf(line, ctx->line, MAX_STR_SIZE) == -1) {
                memset(line, 0, sizeof(line));
            }

            if (line[0] == '/') {
                if (strcmp(line, "/close") == 0) {
                    delete_conference(self, toxic, self->num);
                    return true;
                } else if (strncmp(line, "/me ", strlen("/me ")) == 0) {
                    send_conference_action(self, ctx, toxic, line + strlen("/me "));
                } else {
                    execute(ctx->history, self, toxic, line, CONFERENCE_COMMAND_MODE);
                }
            } else if (line[0]) {
                Tox_Err_Conference_Send_Message err;

                if (!tox_conference_send_message(tox, self->num, TOX_MESSAGE_TYPE_NORMAL, (uint8_t *) line, strlen(line), &err)) {
                    line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to send message (error %d)", err);
                }
            } else {
                line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to parse message.");
            }
        }

        wclear(ctx->linewin);
        wmove(self->window, y2, 0);
        reset_buf(ctx);
    }

    return input_ret;
}

static void draw_peer(ToxWindow *self, Toxic *toxic, ChatContext *ctx, uint32_t i)
{
    pthread_mutex_lock(&Winthread.lock);
    const uint32_t peer_idx = i + conferences[self->num].side_pos;
    const uint32_t peernum = conferences[self->num].name_list[peer_idx].peernum;
    const bool is_self = tox_conference_peer_number_is_ours(toxic->tox, self->num, peernum, NULL);
    const bool audio = conferences[self->num].audio_enabled;

    if (audio) {
#ifdef AUDIO
        const ConferencePeer *peer = peer_in_conference(self->num, peernum);
        const bool audio_active = is_self
                                  ? !timed_out(conferences[self->num].last_sent_audio, 2)
                                  : peer != NULL && peer->sending_audio && !timed_out(peer->last_audio_time, 2);
        const bool mute = audio_active &&
                          (is_self
                           ? device_is_muted(input, conferences[self->num].audio_in_idx)
                           : peer != NULL && device_is_muted(output, peer->audio_out_idx));
        pthread_mutex_unlock(&Winthread.lock);

        const int aud_attr = A_BOLD | COLOR_PAIR(audio_active && !mute ? GREEN : RED);
        wattron(ctx->sidebar, aud_attr);
        waddch(ctx->sidebar, audio_active ? (mute ? 'M' : '*') : '-');
        wattroff(ctx->sidebar, aud_attr);
        waddch(ctx->sidebar, ' ');
#endif
    } else {
        pthread_mutex_unlock(&Winthread.lock);
    }

    /* truncate nick to fit in side panel without modifying list */
    char tmpnick[TOX_MAX_NAME_LENGTH];
    const int maxlen = SIDEBAR_WIDTH - 2 - 2 * audio;

    pthread_mutex_lock(&Winthread.lock);
    memcpy(tmpnick, &conferences[self->num].name_list[peer_idx].name, maxlen);
    pthread_mutex_unlock(&Winthread.lock);

    tmpnick[maxlen] = '\0';

    if (is_self) {
        wattron(ctx->sidebar, COLOR_PAIR(GREEN));
    }

    wprintw(ctx->sidebar, "%s\n", tmpnick);

    if (is_self) {
        wattroff(ctx->sidebar, COLOR_PAIR(GREEN));
    }
}

static void conference_onDraw(ToxWindow *self, Toxic *toxic)
{
    if (toxic == NULL || self == NULL) {
        fprintf(stderr, "conference_onDraw null param\n");
        return;
    }

    int x2, y2;
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0 || y2 <= 0) {
        return;
    }

    ConferenceChat *chat = &conferences[self->num];

    if (!chat->active) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    pthread_mutex_lock(&Winthread.lock);
    line_info_print(self, toxic->c_config);
    pthread_mutex_unlock(&Winthread.lock);

    wclear(ctx->linewin);

    curs_set(1);

    if (ctx->len > 0) {
        mvwprintw(ctx->linewin, 0, 0, "%ls", &ctx->line[ctx->start]);
    }

    wclear(ctx->sidebar);

    if (self->show_peerlist) {
        wattron(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));
        mvwvline(ctx->sidebar, 0, 0, ACS_VLINE, y2 - CHATBOX_HEIGHT);
        mvwaddch(ctx->sidebar, y2 - CHATBOX_HEIGHT, 0, ACS_BTEE);
        wattroff(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));

        pthread_mutex_lock(&Winthread.lock);
        const uint32_t num_peers = chat->num_peers;
        const bool audio = chat->audio_enabled;
        const int header_lines = sidebar_offset(self->num);
        pthread_mutex_unlock(&Winthread.lock);

        int line = 0;

        if (audio) {
#ifdef AUDIO
            pthread_mutex_lock(&Winthread.lock);
            const bool ptt_idle = !conference_check_push_to_talk(chat) && chat->push_to_talk_enabled;
            const bool mic_on = !device_is_muted(input, chat->audio_in_idx);
            const float volume = get_input_volume();
            const float threshold = device_get_VAD_threshold(chat->audio_in_idx);
            pthread_mutex_unlock(&Winthread.lock);

            wmove(ctx->sidebar, line, 1);
            wattron(ctx->sidebar, A_BOLD);
            wprintw(ctx->sidebar, "Mic: ");

            if (!mic_on) {
                wattron(ctx->sidebar, COLOR_PAIR(RED));
                wprintw(ctx->sidebar, "MUTED");
                wattroff(ctx->sidebar, COLOR_PAIR(RED));
            } else if (ptt_idle)  {
                wattron(ctx->sidebar, COLOR_PAIR(GREEN));
                wprintw(ctx->sidebar, "PTT");
                wattroff(ctx->sidebar, COLOR_PAIR(GREEN));
            }  else {
                const int color = volume > threshold ? GREEN : RED;
                wattron(ctx->sidebar, COLOR_PAIR(color));

                float v = volume;

                if (v <= 0.0f) {
                    wprintw(ctx->sidebar, ".");
                }

                while (v > 0.0f) {
                    wprintw(ctx->sidebar, v > 10.0f ? (v > 15.0f ? "*" : "+") : (v > 5.0f ? "-" : "."));
                    v -= 20.0f;
                }

                wattroff(ctx->sidebar, COLOR_PAIR(color));
            }

            wattroff(ctx->sidebar, A_BOLD);
            ++line;
#endif  // AUDIO
        }

        wmove(ctx->sidebar, line, 1);
        wattron(ctx->sidebar, A_BOLD);
        wprintw(ctx->sidebar, "Peers: %"PRIu32"\n", num_peers);
        wattroff(ctx->sidebar, A_BOLD);
        ++line;

        wattron(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));
        mvwaddch(ctx->sidebar, line, 0, ACS_LTEE);
        mvwhline(ctx->sidebar, line, 1, ACS_HLINE, SIDEBAR_WIDTH - 1);
        wattroff(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));

        for (uint32_t i = 0;
                i < num_peers && i < y2 - header_lines - CHATBOX_HEIGHT;
                ++i) {
            wmove(ctx->sidebar, i + header_lines, 1);
            draw_peer(self, toxic, ctx, i);
        }
    }

    int y, x;
    getyx(self->window, y, x);

    UNUSED_VAR(x);

    int new_x = ctx->start ? x2 - 1 : MAX(0, wcswidth(ctx->line, ctx->pos));
    wmove(self->window, y, new_x);

    draw_window_bar(self, toxic->windows);

    wnoutrefresh(self->window);

    if (self->help->active) {
        help_draw_main(self);
    }
}

static void conference_onInit(ToxWindow *self, Toxic *toxic)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    int x2, y2;
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0 || y2 <= 0) {
        exit_toxic_err(FATALERR_CURSES, "failed in conference_onInit");
    }

    ChatContext *ctx = self->chatwin;

    ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2 - SIDEBAR_WIDTH - 1, 0, 0);
    self->window_bar = subwin(self->window, WINDOW_BAR_HEIGHT, x2, y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);
    ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);

    ctx->hst = calloc(1, sizeof(struct history));
    ctx->log = calloc(1, sizeof(struct chatlog));

    if (ctx->log == NULL || ctx->hst == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in conference_onInit");
    }

    line_info_init(ctx->hst);

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);
}

/*
 * Return the conference number associated with `public_key`.
 * Return -1 if public_key does not designate a valid conference.
 *
 * `public_key` must be a string of at least TOX_PUBLIC_KEY_SIZE * 2 chars in length.
 */
static int get_conferencenum_by_public_key_string(const char *public_key)
{
    char pk_bin[TOX_PUBLIC_KEY_SIZE];

    if (tox_pk_string_to_bytes(public_key, strlen(public_key), pk_bin, sizeof(pk_bin)) != 0) {
        return -1;
    }

    for (size_t i = 0; i < max_conference_index; ++i) {
        const ConferenceChat *chat = &conferences[i];

        if (!chat->active) {
            continue;
        }

        if (memcmp(pk_bin, chat->id, TOX_PUBLIC_KEY_SIZE) == 0) {
            return chat->conferencenum;
        }
    }

    return -1;
}

/*
 * Sets the tab name colour of the ToxWindow associated with `public_key` to `colour`.
 *
 * Return false if conference does not exist.
 */
static bool conference_window_set_tab_name_colour(Windows *windows, const char *public_key, int colour)
{
    const int conferencenum = get_conferencenum_by_public_key_string(public_key);

    if (conferencenum < 0) {
        return false;
    }

    ToxWindow *self = get_window_by_number_type(windows, conferencenum, WINDOW_TYPE_CONFERENCE);

    if (self == NULL) {
        return false;
    }

    self->colour = colour;

    return true;
}

bool conference_config_set_tab_name_colour(Windows *windows, const char *public_key, const char *colour)
{
    const int colour_val = colour_string_to_int(colour);

    if (colour_val < 0) {
        return false;
    }

    return conference_window_set_tab_name_colour(windows, public_key, colour_val);
}

bool conference_config_set_autolog(Windows *windows, const char *public_key, bool autolog_enabled)
{
    const int conferencenum = get_conferencenum_by_public_key_string(public_key);

    if (conferencenum < 0) {
        return false;
    }

    return autolog_enabled
           ? enable_window_log_by_number_type(windows, conferencenum, WINDOW_TYPE_CONFERENCE)
           : disable_window_log_by_number_type(windows, conferencenum, WINDOW_TYPE_CONFERENCE);
}

static ToxWindow *new_conference_chat(uint32_t conferencenum)
{
    ToxWindow *ret = calloc(1, sizeof(ToxWindow));

    if (ret == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_conference_chat");
    }

    ret->type = WINDOW_TYPE_CONFERENCE;

    ret->onKey = &conference_onKey;
    ret->onDraw = &conference_onDraw;
    ret->onInit = &conference_onInit;
    ret->onConferenceMessage = &conference_onConferenceMessage;
    ret->onConferenceNameListChange = &conference_onConferenceNameListChange;
    ret->onConferencePeerNameChange = &conference_onConferencePeerNameChange;
    ret->onConferenceTitleChange = &conference_onConferenceTitleChange;

    snprintf(ret->name, sizeof(ret->name), "Conference %u", conferencenum);

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    Help *help = calloc(1, sizeof(Help));

    if (chatwin == NULL || help == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_conference_chat");
    }

    ret->chatwin = chatwin;
    ret->help = help;

    ret->num = conferencenum;
    ret->show_peerlist = true;
    ret->active_box = -1;

    return ret;
}

#ifdef AUDIO

#define CONFAV_SAMPLE_RATE 48000
#define CONFAV_FRAME_DURATION 20
#define CONFAV_SAMPLES_PER_FRAME (CONFAV_SAMPLE_RATE * CONFAV_FRAME_DURATION / 1000)

void audio_conference_callback(void *tox, uint32_t conferencenum, uint32_t peernum, const int16_t *pcm,
                               unsigned int samples, uint8_t channels, uint32_t sample_rate, void *userdata)
{
    const Client_Config *c_config = (Client_Config *) userdata;

    if (c_config == NULL) {
        return;
    }

    ConferencePeer *peer = peer_in_conference(conferencenum, peernum);

    if (peer == NULL) {
        return;
    }

    if (!peer->sending_audio) {
        if (open_output_device(&peer->audio_out_idx,
                               sample_rate, CONFAV_FRAME_DURATION, channels, c_config->VAD_threshold) != de_None) {
            // TODO: error message?
            return;
        }

        peer->sending_audio = true;

        set_peer_audio_position(tox, conferencenum, peernum);
    }

    write_out(peer->audio_out_idx, pcm, samples, channels, sample_rate);

    peer->last_audio_time = get_unix_time();

    return;
}

static void conference_read_device_callback(const int16_t *captured, uint32_t size, void *data)
{
    UNUSED_VAR(size);

    AudioInputCallbackData *audio_input_callback_data = (AudioInputCallbackData *)data;

    ConferenceChat *chat = &conferences[audio_input_callback_data->conferencenum];

    if (!conference_check_push_to_talk(chat)) {
        return;
    }

    chat->last_sent_audio = get_unix_time();

    toxav_group_send_audio(audio_input_callback_data->tox,
                           audio_input_callback_data->conferencenum,
                           captured, CONFAV_SAMPLES_PER_FRAME,
                           audio_input_callback_data->audio_channels,
                           CONFAV_SAMPLE_RATE);
}

bool init_conference_audio_input(Toxic *toxic, uint32_t conferencenum)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || chat->audio_enabled) {
        return false;
    }

    const Client_Config *c_config = toxic->c_config;

    const int channels = c_config->conference_audio_channels;

    const AudioInputCallbackData audio_input_callback_data = {
        toxic->tox,
        conferencenum,
        channels,
    };

    chat->audio_input_callback_data = audio_input_callback_data;


    const bool success = (open_input_device(&chat->audio_in_idx,
                                            conference_read_device_callback, &chat->audio_input_callback_data,
                                            CONFAV_SAMPLE_RATE, CONFAV_FRAME_DURATION, channels,
                                            c_config->VAD_threshold)
                          == de_None);

    chat->audio_enabled = success;

    return success;
}

bool toggle_conference_push_to_talk(uint32_t conferencenum, bool enabled)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return false;
    }

    chat->push_to_talk_enabled = enabled;

    return true;
}

bool enable_conference_audio(ToxWindow *self, Toxic *toxic, uint32_t conferencenum)
{
    if (!toxav_groupchat_av_enabled(toxic->tox, conferencenum)) {
        if (toxav_groupchat_enable_av(toxic->tox, conferencenum, audio_conference_callback,
                                      (void *) toxic->c_config) != 0) {
            return false;
        }
    }

    const ConferenceChat *chat = &conferences[conferencenum];

    if (chat->audio_enabled) {
        return true;
    }

    const bool success = init_conference_audio_input(toxic, conferencenum);

    if (success) {
        self->is_call = true;
    }

    return success;
}

bool disable_conference_audio(ToxWindow *self, Toxic *toxic, uint32_t conferencenum)
{
    ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active) {
        return false;
    }

    if (chat->audio_enabled) {
        close_device(input, chat->audio_in_idx);
        chat->audio_enabled = false;
    } else {
        return true;
    }

    const bool success = toxav_groupchat_disable_av(toxic->tox, conferencenum) == 0;

    if (success) {
        self->is_call = false;
    }

    return success;
}

bool conference_mute_self(uint32_t conferencenum)
{
    const ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || !chat->audio_enabled) {
        return false;
    }

    device_mute(input, chat->audio_in_idx);

    return true;
}

bool conference_mute_peer(const Tox *tox, uint32_t conferencenum, uint32_t peernum)
{
    if (tox_conference_peer_number_is_ours(tox, conferencenum, peernum, NULL)) {
        return conference_mute_self(conferencenum);
    }

    const ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || !chat->audio_enabled
            || peernum > chat->max_idx) {
        return false;
    }

    const ConferencePeer *peer = peer_in_conference(conferencenum, peernum);

    if (peer == NULL || !peer->sending_audio) {
        return false;
    }

    device_mute(output, peer->audio_out_idx);
    return true;
}

bool conference_set_VAD_threshold(uint32_t conferencenum, float threshold)
{
    const ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || !chat->audio_enabled) {
        return false;
    }

    return (device_set_VAD_threshold(chat->audio_in_idx, threshold) == de_None);
}

float conference_get_VAD_threshold(uint32_t conferencenum)
{
    const ConferenceChat *chat = &conferences[conferencenum];

    if (!chat->active || !chat->audio_enabled) {
        return 0.0f;
    }

    return device_get_VAD_threshold(chat->audio_in_idx);
}
#endif  // AUDIO
