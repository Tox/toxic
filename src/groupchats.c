/*  groupchats.c
 *
 *  Copyright (C) 2020-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE    /* needed for strcasestr() and wcswidth() */
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <unistd.h>
#include <inttypes.h>

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

#include "windows.h"
#include "toxic.h"
#include "execute.h"
#include "misc_tools.h"
#include "groupchats.h"
#include "toxic_strings.h"
#include "log.h"
#include "line_info.h"
#include "settings.h"
#include "input.h"
#include "help.h"
#include "notify.h"
#include "autocomplete.h"
#include "audio_device.h"

extern char *DATA_FILE;
static int max_groupchat_index = 0;

extern struct Winthread Winthread;

#define GROUP_SIDEBAR_OFFSET 3    /* Offset for the peer number box at the top of the statusbar */

static_assert(TOX_GROUP_CHAT_ID_SIZE == TOX_PUBLIC_KEY_SIZE, "TOX_GROUP_CHAT_ID_SIZE != TOX_PUBLIC_KEY_SIZE");

/* groupchat command names used for tab completion. */
static const char *const group_cmd_list[] = {
    "/accept",
    "/add",
    "/avatar",
    "/chatid",
    "/clear",
    "/close",
    "/color",
    "/conference",
    "/connect",
    "/disconnect",
    "/decline",
    "/exit",
    "/group",
    "/help",
    "/ignore",
    "/join",
    "/kick",
    "/list",
    "/locktopic",
    "/log",
    "/mod",
    "/myid",
#ifdef QRCODE
    "/myqr",
#endif /* QRCODE */
    "/nick",
    "/note",
    "/passwd",
    "/nospam",
    "/peerlimit",
    "/privacy",
    "/quit",
    "/rejoin",
    "/requests",
#ifdef PYTHON
    "/run",
#endif /* PYTHON */
    "/silence",
    "/status",
    "/topic",
    "/unignore",
    "/unmod",
    "/unsilence",
    "/voice",
    "/whisper",
    "/whois",
};

static GroupChat groupchats[MAX_GROUPCHAT_NUM];

static ToxWindow *new_group_chat(Tox *tox, uint32_t groupnumber, const char *groupname, int length);
static void groupchat_set_group_name(ToxWindow *self, Toxic *toxic, uint32_t groupnumber);
static void groupchat_update_name_list(uint32_t groupnumber);
static void groupchat_onGroupPeerJoin(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id);
static int realloc_peer_list(uint32_t groupnumber, uint32_t n);
static void groupchat_onGroupNickChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
                                        const char *new_nick, size_t len);
static void groupchat_onGroupStatusChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
        TOX_USER_STATUS status);
static void groupchat_onGroupSelfNickChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, const char *old_nick,
        size_t old_length, const char *new_nick, size_t length);
static void ignore_list_cleanup(GroupChat *chat);

/*
 * Return a GroupChat pointer associated with groupnumber.
 * Return NULL if groupnumber is invalid.
 */
GroupChat *get_groupchat(uint32_t groupnumber)
{
    for (size_t i = 0; i < max_groupchat_index; ++i) {
        if (!groupchats[i].active) {
            continue;
        }

        if (groupchats[i].groupnumber == groupnumber) {
            return &groupchats[i];
        }
    }

    return NULL;
}

/*
 * Return the groupnumber associated with `public_key`.
 * Return -1 if public_key does not designate a valid group.
 *
 * `public_key` must be a string of at least TOX_PUBLIC_KEY_SIZE * 2 chars in length.
 */
static int get_groupnumber_by_public_key_string(const char *public_key)
{
    char pk_bin[TOX_PUBLIC_KEY_SIZE];

    if (tox_pk_string_to_bytes(public_key, strlen(public_key), pk_bin, sizeof(pk_bin)) != 0) {
        return -1;
    }

    for (size_t i = 0; i < max_groupchat_index; ++i) {
        const GroupChat *chat = &groupchats[i];

        if (!chat->active) {
            continue;
        }

        if (memcmp(pk_bin, chat->chat_id, TOX_PUBLIC_KEY_SIZE) == 0) {
            return chat->groupnumber;
        }
    }

    return -1;
}

static const char *get_group_exit_string(Tox_Group_Exit_Type exit_type)
{
    switch (exit_type) {
        case TOX_GROUP_EXIT_TYPE_QUIT:
            return "Quit";

        case TOX_GROUP_EXIT_TYPE_TIMEOUT:
            return "Connection timed out";

        case TOX_GROUP_EXIT_TYPE_DISCONNECTED:
            return "Disconnected";

        case TOX_GROUP_EXIT_TYPE_KICK:
            return "Kicked";

        case TOX_GROUP_EXIT_TYPE_SYNC_ERROR:
            return "Sync error";

        default:
            return "Unknown error";
    }
}

static void clear_peer(GroupPeer *peer)
{
    *peer = (GroupPeer) {
        0
    };
}

void groupchat_rejoin(ToxWindow *self, Toxic *toxic)
{
    const Client_Config *c_config = toxic->c_config;
    GroupChat *chat = get_groupchat(self->num);

    if (chat == NULL) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,  "Failed to fetch GroupChat object.");
        return;
    }

    Tox_Err_Group_Self_Query s_err;
    const uint32_t self_peer_id = tox_group_self_get_peer_id(toxic->tox, self->num, &s_err);

    if (s_err != TOX_ERR_GROUP_SELF_QUERY_OK) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                      "Failed to fetch self peer_id in groupchat_rejoin()");
        return;
    }

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        clear_peer(&chat->peer_list[i]);
    }

    chat->num_peers = 0;
    chat->max_idx = 0;
    realloc_peer_list(self->num, 0);

    groupchat_onGroupPeerJoin(self, toxic, self->num, self_peer_id);
}

static void kill_groupchat_window(ToxWindow *self, Windows *windows, const Client_Config *c_config)
{
    if (self == NULL) {
        return;
    }

    ChatContext *ctx = self->chatwin;
    StatusBar *statusbar = self->stb;

    if (ctx != NULL) {
        log_disable(ctx->log);
        line_info_cleanup(ctx->hst);
        delwin(ctx->linewin);
        delwin(ctx->history);
        delwin(ctx->sidebar);
        free(ctx->log);
        free(ctx);
    }

    delwin(statusbar->topline);

    free(statusbar);
    free(self->help);

    kill_notifs(self->active_box);
    del_window(self, windows, c_config);
}

/* Closes groupchat window and cleans up. */
static void close_groupchat(ToxWindow *self, Toxic *toxic, uint32_t groupnumber)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    ignore_list_cleanup(chat);

    realloc_peer_list(groupnumber, 0);

    free_ptr_array((void **) chat->name_list);

    *chat = (GroupChat) {
        0
    };

    int i;

    for (i = max_groupchat_index; i > 0; --i) {
        if (groupchats[i - 1].active) {
            break;
        }
    }

    max_groupchat_index = i;
    kill_groupchat_window(self, toxic->windows, toxic->c_config);
}

void exit_groupchat(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, const char *partmessage, size_t length)
{
    if (length > TOX_GROUP_MAX_PART_LENGTH) {
        length = TOX_GROUP_MAX_PART_LENGTH;
    }

    tox_group_leave(toxic->tox, groupnumber, (const uint8_t *) partmessage, length, NULL);

    if (self != NULL) {
        close_groupchat(self, toxic, groupnumber);
    }
}

/*
 * Initializes groupchat log. This should only be called after we have the group name.
 */
static void init_groupchat_log(ToxWindow *self, Toxic *toxic, uint32_t groupnumber)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    char my_id[TOX_ADDRESS_SIZE];
    tox_self_get_address(tox, (uint8_t *) my_id);

    char chat_id[TOX_GROUP_CHAT_ID_SIZE];

    Tox_Err_Group_State_Query err;

    if (!tox_group_get_chat_id(tox, groupnumber, (uint8_t *)chat_id, &err)) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                      "Failed to fetch chat id. Logging disabled. (error: %d)", err);
        return;
    }

    if (log_init(ctx->log, c_config, chat->group_name, my_id, chat_id, LOG_TYPE_CHAT) != 0) {
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

/* Creates a new toxic groupchat window associated with groupnumber.
 *
 * Returns 0 on success.
 * Returns -1 on general failure.
 * Returns -2 if the groupnumber is already in use. This usually means that the client has
 *     been kicked and needs to close the chat window before opening a new one.
 */
int init_groupchat_win(Toxic *toxic, uint32_t groupnumber, const char *groupname, size_t length,
                       Group_Join_Type join_type)
{
    if (toxic == NULL) {
        return -1;
    }

    Tox *tox = toxic->tox;

    ToxWindow *self = new_group_chat(tox, groupnumber, groupname, length);

    for (int i = 0; i <= max_groupchat_index; ++i) {
        if (!groupchats[i].active) {
            if (i == max_groupchat_index) {
                ++max_groupchat_index;
            }

            groupchats[i].active = true;
            groupchats[i].groupnumber = groupnumber;
            groupchats[i].num_peers = 0;
            groupchats[i].time_connected = get_unix_time();

            const int window_id = add_window(toxic, self);

            if (window_id < 0) {
                fprintf(stderr, "Failed to create new groupchat window\n");
                close_groupchat(self, toxic, groupnumber);
                return -1;
            }

            groupchats[i].window_id = window_id;

            if (!tox_group_get_chat_id(tox, groupnumber, (uint8_t *) groupchats[i].chat_id, NULL)) {
                fprintf(stderr, "Failed to fetch new groupchat ID\n");
                close_groupchat(self, toxic, groupnumber);
                return -1;
            }

            set_active_window_by_id(toxic->windows, groupchats[i].window_id);
            store_data(toxic);

            Tox_Err_Group_Self_Query err;
            const uint32_t peer_id = tox_group_self_get_peer_id(tox, groupnumber, &err);

            if (err != TOX_ERR_GROUP_SELF_QUERY_OK) {
                close_groupchat(self, toxic, groupnumber);
                return -1;
            }

            if (join_type == Group_Join_Type_Create || join_type == Group_Join_Type_Load) {
                groupchat_set_group_name(self, toxic, groupnumber);
            }

            groupchat_onGroupPeerJoin(self, toxic, groupnumber, peer_id);

            return 0;
        }
    }

    kill_groupchat_window(self, toxic->windows, toxic->c_config);

    return -1;
}

void groupchat_update_statusbar_topic(ToxWindow *self, const Tox *tox)
{
    StatusBar *statusbar = self->stb;

    char topic[TOX_GROUP_MAX_TOPIC_LENGTH + 1] = {0};
    const size_t t_len = tox_group_get_topic_size(tox, self->num, NULL);

    if (t_len > TOX_GROUP_MAX_TOPIC_LENGTH) {
        return;
    }

    tox_group_get_topic(tox, self->num, (uint8_t *)topic, NULL);
    topic[t_len] = '\0';
    filter_string(topic, t_len, false);
    snprintf(statusbar->topic, sizeof(statusbar->topic), "%s", topic);
    statusbar->topic_len = strlen(statusbar->topic);
}

void set_nick_this_group(ToxWindow *self, Toxic *toxic, const char *new_nick, size_t length)
{
    if (self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    char old_nick[TOX_MAX_NAME_LENGTH + 1];
    size_t old_length = get_group_self_nick_truncate(tox, old_nick, self->num);

    Tox_Err_Group_Self_Name_Set err;
    tox_group_self_set_name(tox, self->num, (const uint8_t *) new_nick, length, &err);

    GroupChat *chat = get_groupchat(self->num);

    if (chat == NULL) {
        line_info_add(self, c_config, false, NULL, 0, SYS_MSG, 0, RED, "-!- Failed to set nick: invalid groupnumber");
        return;
    }

    switch (err) {
        case TOX_ERR_GROUP_SELF_NAME_SET_OK: {
            groupchat_onGroupSelfNickChange(self, toxic, self->num, old_nick, old_length, new_nick, length);
            break;
        }

        default: {
            if (chat->time_connected > 0) {
                line_info_add(self, c_config, false, NULL, 0, SYS_MSG, 0, RED, "-!- Failed to set nick (error %d).", err);
            }

            break;
        }
    }
}

/* void set_nick_all_groups(Tox *tox, const char *new_nick, size_t length) */
/* { */
/*     for (int i = 0; i < max_groupchat_index; ++i) { */
/*         if (groupchats[i].active) { */
/*             ToxWindow *self = get_window_pointer_by_id(groupchats[i].window_id); */

/*             if (!self) { */
/*                 continue; */
/*             } */

/*             char old_nick[TOX_MAX_NAME_LENGTH + 1]; */
/*             size_t old_length = get_group_self_nick_truncate(tox, old_nick, self->num); */

/*             Tox_Err_Group_Self_Name_Set err; */
/*             tox_group_self_set_name(tox, groupchats[i].groupnumber, (uint8_t *) new_nick, length, &err); */

/*             switch (err) { */
/*                 case TOX_ERR_GROUP_SELF_NAME_SET_OK: { */
/*                     groupchat_onGroupSelfNickChange(self, tox, self->num, old_nick, old_length, new_nick, length); */
/*                     break; */
/*                 } */

/*                 default: { */
/*                     if (groupchats[i].time_connected > 0) { */
/*                         line_info_add(self, c_config, false, NULL, 0, SYS_MSG, 0, RED, "-!- Failed to set nick (error %d).", err); */
/*                     } */

/*                     break; */
/*                 } */
/*             } */
/*         } */
/*     } */
/* } */

void set_status_all_groups(Toxic *toxic, uint8_t status)
{
    for (int i = 0; i < max_groupchat_index; ++i) {
        if (groupchats[i].active) {
            ToxWindow *self = get_window_pointer_by_id(toxic->windows, groupchats[i].window_id);

            if (self == NULL) {
                continue;
            }

            Tox_Err_Group_Self_Query s_err;
            const uint32_t self_peer_id = tox_group_self_get_peer_id(toxic->tox, self->num, &s_err);

            if (s_err != TOX_ERR_GROUP_SELF_QUERY_OK) {
                line_info_add(self, toxic->c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                              "Failed to fetch self peer_id.");
                continue;
            }

            if (tox_group_self_set_status(toxic->tox, self->num, (TOX_USER_STATUS) status, NULL)) {
                groupchat_onGroupStatusChange(self, toxic, self->num, self_peer_id, (TOX_USER_STATUS) status);
            }
        }
    }
}

/* Returns a weight for peer_sort_cmp based on the peer's role. */
#define PEER_CMP_BASE_WEIGHT 100000
static int peer_sort_cmp_weight(const struct GroupPeer *peer)
{
    int w = PEER_CMP_BASE_WEIGHT;

    if (peer->role == TOX_GROUP_ROLE_FOUNDER) {
        w <<= 2;
    } else if (peer->role == TOX_GROUP_ROLE_MODERATOR) {
        w <<= 1;
    } else if (peer->role == TOX_GROUP_ROLE_OBSERVER) {
        w >>= 1;
    }

    return w;
}

static int peer_sort_cmp(const void *n1, const void *n2)
{
    const struct GroupPeer *peer1 = (const struct GroupPeer *) n1;
    const struct GroupPeer *peer2 = (const struct GroupPeer *) n2;

    int res = qsort_strcasecmp_hlpr(peer1->name, peer2->name);
    return res - peer_sort_cmp_weight(peer1) + peer_sort_cmp_weight(peer2);

}

/* Sorts the peer list, first by role, then by name. */
static void sort_peerlist(uint32_t groupnumber)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return;
    }

    qsort(chat->peer_list, chat->max_idx, sizeof(struct GroupPeer), peer_sort_cmp);
}

/* Puts the peer_id associated with nick in `peer_id`.
 *
 * Returns 0 on success.
 * Returns -1 if peer is not found.
 * Returns -2 if there are more than one peers with nick.
 */
static int group_get_nick_peer_id(uint32_t groupnumber, const char *nick, uint32_t *peer_id)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return -1;
    }

    if (nick == NULL) {
        return -1;
    }

    size_t count = 0;

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        GroupPeer *peer = &chat->peer_list[i];

        if (!peer->active) {
            continue;
        }

        if (strcmp(nick, peer->name) == 0) {
            if (++count > 1) {
                return -2;
            }

            *peer_id = peer->peer_id;
        }
    }

    return count > 0 ? 0 : -1;
}

/* Gets the peer_id associated with `public_key`.
 *
 * Returns 0 on success.
 * Returns -1 on failure or if `public_key` is invalid.
 */
int group_get_public_key_peer_id(uint32_t groupnumber, const char *public_key, uint32_t *peer_id)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return -1;
    }

    if (public_key == NULL) {
        return -1;
    }

    if (strlen(public_key) < TOX_GROUP_PEER_PUBLIC_KEY_SIZE * 2) {
        return -1;
    }

    char key_bin[TOX_GROUP_PEER_PUBLIC_KEY_SIZE];

    if (tox_pk_string_to_bytes(public_key, TOX_GROUP_PEER_PUBLIC_KEY_SIZE * 2, key_bin, sizeof(key_bin)) == -1) {
        return -1;
    }

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        GroupPeer *peer = &chat->peer_list[i];

        if (!peer->active) {
            continue;
        }

        if (memcmp(key_bin, peer->public_key, TOX_GROUP_PEER_PUBLIC_KEY_SIZE) == 0) {
            *peer_id = peer->peer_id;
            return 0;
        }
    }

    return -1;
}

/* Puts the peer_id associated with `identifier` in `peer_id`. The string may be
 * either a nick or a public key.
 *
 * On failure, `peer_id` is set to (uint32_t)-1.
 *
 * This function is intended to be a helper for groupchat_commands.c and will print
 * error messages to `self`.
 * Return 0 on success.
 * Return -1 if the identifier does not correspond with a peer in the group, or if
 *   the identifier is a nick which is being used by multiple peers.
 */
int group_get_peer_id_of_identifier(ToxWindow *self, const Client_Config *c_config,
                                    const char *identifier, uint32_t *peer_id)
{
    *peer_id = (uint32_t) -1;

    if (group_get_public_key_peer_id(self->num, identifier, peer_id) == 0) {
        return 0;
    }

    int ret = group_get_nick_peer_id(self->num, identifier, peer_id);

    switch (ret) {
        case 0: {
            return 0;
        }

        case -1: {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,  "Invalid peer name or public key.");
            return -1;
        }

        case -2: {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                          "More than one peer is using this name; specify the target's public key.");
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                          "Use the /whois or /list command to determine the key.");
            *peer_id = (uint32_t) -1;
            return -1;
        }

        default:
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,  "Unspecified error.");
            return -1;
    }
}

static void groupchat_update_last_seen(uint32_t groupnumber, uint32_t peer_id)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return;
    }

    const int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index >= 0) {
        chat->peer_list[peer_index].last_active = get_unix_time();
    }
}

/* Returns the peerlist index of peer_id for groupnumber's group chat.
 * Returns -1 on failure.
 */
int get_peer_index(uint32_t groupnumber, uint32_t peer_id)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return -1;
    }

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        if (!chat->peer_list[i].active) {
            continue;
        }

        if (chat->peer_list[i].peer_id == peer_id) {
            return i;
        }
    }

    return -1;
}

/**
 * Return true if `key` is in the ignored list.
 */
static bool peer_is_ignored(const GroupChat *chat, const uint8_t *key)
{
    for (uint16_t i = 0; i < chat->num_ignored; ++i) {
        if (memcmp(chat->ignored_list[i], key, TOX_GROUP_PEER_PUBLIC_KEY_SIZE) == 0) {
            return true;
        }
    }

    return false;
}

static bool ignore_list_add_key(GroupChat *chat, const uint8_t *key)
{
    uint8_t **tmp_list = (uint8_t **)realloc(chat->ignored_list, (chat->num_ignored + 1) * sizeof(uint8_t *));

    if (tmp_list == NULL) {
        return false;
    }

    chat->ignored_list = tmp_list;

    tmp_list[chat->num_ignored] = (uint8_t *)malloc(sizeof(uint8_t) * TOX_GROUP_PEER_PUBLIC_KEY_SIZE);

    if (tmp_list[chat->num_ignored] == NULL) {
        return false;
    }

    memcpy(tmp_list[chat->num_ignored], key, TOX_GROUP_PEER_PUBLIC_KEY_SIZE);
    ++chat->num_ignored;

    return true;
}

static void ignore_list_cleanup(GroupChat *chat)
{
    for (uint16_t i = 0; i < chat->num_ignored; ++i) {
        if (chat->ignored_list[i] != NULL) {
            free(chat->ignored_list[i]);
        }
    }

    free(chat->ignored_list);
    chat->ignored_list = NULL;
    chat->num_ignored = 0;
}

static bool ignore_list_rm_key(GroupChat *chat, const uint8_t *key)
{
    if (chat->num_ignored == 0) {
        return false;
    }

    int32_t idx = -1;

    for (uint16_t i = 0; i < chat->num_ignored; ++i) {
        if (memcmp(chat->ignored_list[i], key, TOX_GROUP_PEER_PUBLIC_KEY_SIZE) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        fprintf(stderr, "Key not found in ignore list\n");
        return false;
    }

    if ((chat->num_ignored - 1) == 0) {
        ignore_list_cleanup(chat);
        return true;
    }

    --chat->num_ignored;

    if (idx != chat->num_ignored) {
        memcpy(chat->ignored_list[idx], chat->ignored_list[chat->num_ignored], TOX_GROUP_PEER_PUBLIC_KEY_SIZE);
    }

    free(chat->ignored_list[chat->num_ignored]);

    uint8_t **tmp_list = realloc(chat->ignored_list, chat->num_ignored * sizeof(uint8_t *));

    if (tmp_list == NULL) {
        return false;
    }

    chat->ignored_list = tmp_list;

    return true;
}

void group_toggle_peer_ignore(uint32_t groupnumber, int peer_id, bool ignore)
{
    int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index < 0) {
        fprintf(stderr, "Failed to find peer index (group_toggle_peer_ignore())\n");
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return;
    }

    GroupPeer *peer = &chat->peer_list[peer_index];

    peer->is_ignored = ignore;

    bool ret;

    if (ignore) {
        ret = ignore_list_add_key(chat, peer->public_key);
    } else {
        ret = ignore_list_rm_key(chat, peer->public_key);
    }

    if (!ret) {
        fprintf(stderr, "Client failed to modify ignore list\n");
    }
}

static void groupchat_update_name_list(uint32_t groupnumber)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (!chat) {
        return;
    }

    free_ptr_array((void **) chat->name_list);

    chat->name_list = (char **) malloc_ptr_array(chat->num_peers, TOX_MAX_NAME_LENGTH + 1);

    if (!chat->name_list) {
        fprintf(stderr, "WARNING: Out of memory in groupchat_update_name_list()\n");
        return;
    }

    uint32_t count = 0;

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        if (chat->peer_list[i].active) {
            size_t length = chat->peer_list[i].name_length;
            memcpy(chat->name_list[count], chat->peer_list[i].name, length);
            chat->name_list[count][length] = 0;
            ++count;
        }
    }

    sort_peerlist(groupnumber);
}

/* destroys and re-creates groupchat window */
void redraw_groupchat_win(ToxWindow *self)
{
    ChatContext *ctx = self->chatwin;
    StatusBar *statusbar = self->stb;

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
    delwin(statusbar->topline);

    self->window = newwin(y2, x2, 0, 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);
    self->window_bar = subwin(self->window, WINDOW_BAR_HEIGHT, x2, y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);
    statusbar->topline = subwin(self->window, TOP_BAR_HEIGHT, x2, 0, 0);

    if (self->show_peerlist) {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2 - SIDEBAR_WIDTH - 1, 0, 0);
        ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);
    } else {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2, 0, 0);
    }

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);

    self->x = 0;  // trigger the statusbar to be re-sized

    if (!self->scroll_pause) {
        line_info_reset_start(self, ctx->hst);
    }
}

static void group_onAction(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id, const char *action,
                           size_t len)
{
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_nick_truncate(toxic->tox, nick, peer_id, groupnumber);

    char self_nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_self_nick_truncate(toxic->tox, self_nick, groupnumber);

    if (strcasestr(action, self_nick)) {
        if (self->active_box != -1) {
            box_notify2(self, toxic, generic_message, NT_WNDALERT_0 | NT_NOFOCUS |
                        c_config->bell_on_message, self->active_box, "* %s %s", nick, action);
        } else {
            box_notify(self, toxic, generic_message, NT_WNDALERT_0 | NT_NOFOCUS |
                       c_config->bell_on_message, &self->active_box, self->name, "* %s %s", nick, action);
        }
    } else {
        sound_notify(self, toxic, silent, NT_WNDALERT_1, NULL);
    }

    line_info_add(self, c_config, true, nick, NULL, IN_ACTION, 0, 0, "%s", action);
    write_to_log(ctx->log, c_config, action, nick, LOG_HINT_ACTION);
}

static void groupchat_onGroupMessage(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
                                     TOX_MESSAGE_TYPE type, const char *msg, size_t len)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    groupchat_update_last_seen(groupnumber, peer_id);

    if (type == TOX_MESSAGE_TYPE_ACTION) {
        group_onAction(self, toxic, groupnumber, peer_id, msg, len);
        return;
    }

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_nick_truncate(tox, nick, peer_id, groupnumber);

    char self_nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_self_nick_truncate(tox, self_nick, groupnumber);

    int nick_clr = CYAN;

    /* Only play sound if mentioned by someone else */
    if (strcasestr(msg, self_nick) && strcmp(self_nick, nick)) {
        sound_notify(self, toxic, generic_message, NT_WNDALERT_0 | c_config->bell_on_message, NULL);

        if (self->active_box != -1) {
            box_silent_notify2(self, toxic, NT_NOFOCUS, self->active_box, "%s %s", nick, msg);
        } else {
            box_silent_notify(self, toxic, NT_NOFOCUS, &self->active_box, self->name, "%s %s", nick, msg);
        }

        nick_clr = RED;
    } else {
        sound_notify(self, toxic, silent, NT_WNDALERT_1, NULL);
    }

    line_info_add(self, c_config, true, nick, NULL, IN_MSG, 0, nick_clr, "%s", msg);
    write_to_log(ctx->log, c_config, msg, nick, LOG_HINT_NORMAL_I);
}

static void groupchat_onGroupPrivateMessage(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
        const char *msg, size_t len)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    groupchat_update_last_seen(groupnumber, peer_id);

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_nick_truncate(tox, nick, peer_id, groupnumber);

    line_info_add(self, c_config, true, nick, NULL, IN_PRVT_MSG, 0, MAGENTA, "%s", msg);
    write_to_log(ctx->log, c_config, msg, nick, LOG_HINT_PRIVATE_I);

    if (self->active_box != -1) {
        box_notify2(self, toxic, generic_message, NT_WNDALERT_0 | NT_NOFOCUS | c_config->bell_on_message,
                    self->active_box, "%s %s", nick, msg);
    } else {
        box_notify(self, toxic,  generic_message, NT_WNDALERT_0 | NT_NOFOCUS | c_config->bell_on_message,
                   &self->active_box, self->name, "%s %s", nick, msg);
    }
}

static void groupchat_onGroupTopicChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
        const char *topic, size_t length)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    groupchat_update_last_seen(groupnumber, peer_id);
    groupchat_update_statusbar_topic(self, tox);

    char nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_nick_truncate(tox, nick, peer_id, groupnumber);

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), "-!- %s set the topic to: %s", nick, topic);

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, MAGENTA, "%s", tmp_event);
    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_TOPIC);
}

static void groupchat_onGroupPeerLimit(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_limit)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), "-!- The founder has set the peer limit to %u", peer_limit);

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
}

static void groupchat_onGroupPrivacyState(ToxWindow *self, Toxic *toxic, uint32_t groupnumber,
        Tox_Group_Privacy_State state)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    const char *state_str = state == TOX_GROUP_PRIVACY_STATE_PUBLIC ? "public" : "private";

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), "-!- The founder has set the group to %s.", state_str);

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
}

static void groupchat_onGroupVoiceState(ToxWindow *self, Toxic *toxic, uint32_t groupnumber,
                                        Tox_Group_Voice_State voice_state)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    char tmp_event[MAX_STR_SIZE];

    switch (voice_state) {
        case TOX_GROUP_VOICE_STATE_ALL: {
            snprintf(tmp_event, sizeof(tmp_event), "-!- The founder set the voice state to ALL.");
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
            break;
        }

        case TOX_GROUP_VOICE_STATE_MODERATOR: {
            snprintf(tmp_event, sizeof(tmp_event), "-!- The founder set the voice state to MODERATOR.");
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
            break;
        }

        case TOX_GROUP_VOICE_STATE_FOUNDER: {
            snprintf(tmp_event, sizeof(tmp_event), "-!- The founder set the voice state to FOUNDER.");
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
            break;
        }

        default:
            return;
    }

    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
}

static void groupchat_onGroupTopicLock(ToxWindow *self, Toxic *toxic, uint32_t groupnumber,
                                       Tox_Group_Topic_Lock topic_lock)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    const char *tlock_str = topic_lock == TOX_GROUP_TOPIC_LOCK_ENABLED ? "locked" : "unlocked";

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), "-!- The founder has %s the topic.", tlock_str);

    line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
    write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
}

static void groupchat_onGroupPassword(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, const char *password,
                                      size_t length)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    char tmp_event[MAX_STR_SIZE];

    if (length > 0) {
        snprintf(tmp_event, sizeof(tmp_event), "-!- The founder has password protected the group.");
        line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
        write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
    } else {
        snprintf(tmp_event, sizeof(tmp_event), "-!- The founder has removed password protection from the group.");
        line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", tmp_event);
        write_to_log(ctx->log, c_config, tmp_event, NULL, LOG_HINT_FOUNDER);
    }
}

/* Reallocates groupnumber's peer list to size n.
 *
 * Returns 0 on success.
 * Returns -1 on failure.
 */
static int realloc_peer_list(uint32_t groupnumber, uint32_t n)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return -1;
    }

    if (n == 0) {
        free(chat->peer_list);
        chat->peer_list = NULL;
        return 0;
    }

    struct GroupPeer *tmp_list = realloc(chat->peer_list, n * sizeof(struct GroupPeer));

    if (!tmp_list) {
        return -1;
    }

    chat->peer_list = tmp_list;

    return 0;
}

static void groupchat_onGroupPeerJoin(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    if (realloc_peer_list(groupnumber, chat->max_idx + 1) == -1) {
        return;
    }

    clear_peer(&chat->peer_list[chat->max_idx]);

    for (uint32_t i = 0; i <= chat->max_idx; ++i) {
        GroupPeer *peer = &chat->peer_list[i];

        if (peer->active) {
            continue;
        }

        ++chat->num_peers;

        peer->active = true;
        peer->peer_id = peer_id;
        get_group_nick_truncate(tox, peer->name, peer_id, groupnumber);
        peer->name_length = strlen(peer->name);
        snprintf(peer->prev_name, sizeof(peer->prev_name), "%s", peer->name);
        peer->status = tox_group_peer_get_status(tox, groupnumber, peer_id, NULL);
        peer->role = tox_group_peer_get_role(tox, groupnumber, peer_id, NULL);
        peer->last_active = get_unix_time();
        tox_group_peer_get_public_key(tox, groupnumber, peer_id, (uint8_t *)peer->public_key, NULL);
        peer->is_ignored = peer_is_ignored(chat, peer->public_key);

        if (peer->is_ignored) {
            tox_group_set_ignore(tox, groupnumber, peer_id, true, NULL);
        }

        if (i == chat->max_idx) {
            ++chat->max_idx;
        }

        /* ignore join messages when we first connect to the group */
        if (timed_out(chat->time_connected, 60) && c_config->show_group_connection_msg) {
            line_info_add(self, c_config, true, peer->name, NULL, CONNECTION, 0, GREEN, "has joined the room");

            write_to_log(ctx->log, c_config, "has joined the room", peer->name, LOG_HINT_CONNECT);
            sound_notify(self, toxic, silent, NT_WNDALERT_2, NULL);
        }

        groupchat_update_name_list(groupnumber);

        return;
    }
}

void groupchat_onGroupPeerExit(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
                               Tox_Group_Exit_Type exit_type,
                               const char *name, size_t name_len, const char *part_message, size_t length)
{
    UNUSED_VAR(name_len);
    UNUSED_VAR(length);

    if (toxic == NULL || self == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    if (exit_type != TOX_GROUP_EXIT_TYPE_SELF_DISCONNECTED && c_config->show_group_connection_msg) {
        char log_str[TOX_MAX_NAME_LENGTH + MAX_STR_SIZE];

        if (length > 0) {
            line_info_add(self, c_config, true, name, NULL, DISCONNECTION, 0, RED, "[Quit]: %s", part_message);
            snprintf(log_str, sizeof(log_str), "has left the room (%s)", part_message);
            write_to_log(ctx->log, c_config, log_str, name, LOG_HINT_DISCONNECT);
            sound_notify(self, toxic, silent, NT_WNDALERT_2, NULL);
        } else {
            const char *exit_string = get_group_exit_string(exit_type);
            line_info_add(self, c_config, true, name, NULL, DISCONNECTION, 0, RED, "[%s]", exit_string);
            snprintf(log_str, sizeof(log_str), "[%s]", exit_string);
            write_to_log(ctx->log, c_config, log_str, name, LOG_HINT_DISCONNECT);
            sound_notify(self, toxic, silent, NT_WNDALERT_2, NULL);
        }

    }

    int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index < 0) {
        return;
    }

    clear_peer(&chat->peer_list[peer_index]);

    uint32_t i;

    for (i = chat->max_idx; i > 0; --i) {
        if (chat->peer_list[i - 1].active) {
            break;
        }
    }

    if (realloc_peer_list(groupnumber, i) == -1) {
        return;
    }

    --chat->num_peers;
    chat->max_idx = i;

    groupchat_update_name_list(groupnumber);
}

static void groupchat_set_group_name(ToxWindow *self, Toxic *toxic, uint32_t groupnumber)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    Tox_Err_Group_State_Query err;
    size_t len = tox_group_get_name_size(tox, groupnumber, &err);

    if (err != TOX_ERR_GROUP_STATE_QUERY_OK) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                      "Failed to retrieve group name length (error %d)", err);
        return;
    }

    char tmp_groupname[TOX_GROUP_MAX_GROUP_NAME_LENGTH + 1];

    if (!tox_group_get_name(tox, groupnumber, (uint8_t *) tmp_groupname, &err)) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to retrieve group name (error %d)", err);
        return;
    }

    len = copy_tox_str(chat->group_name, sizeof(chat->group_name), tmp_groupname, len);
    chat->group_name_length = len;

    if (len > 0) {
        set_window_title(self, chat->group_name, len);
        init_groupchat_log(self, toxic, groupnumber);
    }
}

static void groupchat_onGroupSelfJoin(ToxWindow *self, Toxic *toxic, uint32_t groupnumber)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    chat->time_connected = get_unix_time();

    char topic[TOX_GROUP_MAX_TOPIC_LENGTH + 1];

    Tox_Err_Group_State_Query err;
    const size_t topic_length = tox_group_get_topic_size(tox, groupnumber, &err);

    if (err != TOX_ERR_GROUP_STATE_QUERY_OK) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0,
                      "Failed to retrieve group topic length (error %d)", err);
        return;
    }

    tox_group_get_topic(tox, groupnumber, (uint8_t *) topic, &err);
    topic[topic_length] = 0;

    if (err != TOX_ERR_GROUP_STATE_QUERY_OK) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Failed to retrieve group topic (error %d)",
                      err);
        return;
    }

    if (chat->group_name_length == 0) {
        groupchat_set_group_name(self, toxic, groupnumber);
    }

    /* Update own role since it may have changed while we were offline */
    Tox_Err_Group_Self_Query s_err;
    Tox_Group_Role role = tox_group_self_get_role(tox, groupnumber, &s_err);

    if (s_err != TOX_ERR_GROUP_SELF_QUERY_OK) {
        return;
    }

    const uint32_t self_peer_id = tox_group_self_get_peer_id(tox, groupnumber, &s_err);

    if (s_err != TOX_ERR_GROUP_SELF_QUERY_OK) {
        return;
    }

    const int idx = get_peer_index(groupnumber, self_peer_id);

    if (idx < 0) {
        return;
    }

    chat->peer_list[idx].role = role;

    sort_peerlist(groupnumber);
}

static void groupchat_onGroupRejected(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, Tox_Group_Join_Fail type)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    if (self->num != groupnumber || !get_groupchat(groupnumber)) {
        return;
    }

    const char *msg = NULL;

    switch (type) {
        case TOX_GROUP_JOIN_FAIL_PEER_LIMIT:
            msg = "Group is full. Try again with the '/rejoin' command.";
            break;

        case TOX_GROUP_JOIN_FAIL_INVALID_PASSWORD:
            msg = "Invalid password.";
            break;

        case TOX_GROUP_JOIN_FAIL_UNKNOWN:
            msg = "Failed to join group. Try again with the '/rejoin' command.";
            break;
    }

    line_info_add(self, toxic->c_config, true, NULL, NULL, SYS_MSG, 0, RED, "-!- %s", msg);
}

static void groupchat_update_roles(Tox *tox, uint32_t groupnumber)
{
    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        GroupPeer *peer = &chat->peer_list[i];

        if (!peer->active) {
            continue;
        }

        Tox_Err_Group_Peer_Query err;
        Tox_Group_Role role = tox_group_peer_get_role(tox, groupnumber, peer->peer_id, &err);

        if (err == TOX_ERR_GROUP_PEER_QUERY_OK) {
            peer->role = role;
        }
    }
}

void groupchat_onGroupModeration(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t src_peer_id,
                                 uint32_t tgt_peer_id, Tox_Group_Mod_Event type)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    char src_name[TOX_MAX_NAME_LENGTH + 1];
    char tgt_name[TOX_MAX_NAME_LENGTH + 1];

    get_group_nick_truncate(tox, src_name, src_peer_id, groupnumber);
    get_group_nick_truncate(tox, tgt_name, tgt_peer_id, groupnumber);

    const int tgt_index = get_peer_index(groupnumber, tgt_peer_id);

    if (tgt_index < 0) {
        groupchat_update_roles(tox, groupnumber);
        return;
    }

    groupchat_update_last_seen(groupnumber, src_peer_id);

    char msg[MAX_STR_SIZE];

    switch (type) {
        case TOX_GROUP_MOD_EVENT_KICK:
            snprintf(msg, sizeof(msg), "-!- %s has been kicked by %s", tgt_name, src_name);
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, RED, "%s", msg);
            break;

        case TOX_GROUP_MOD_EVENT_OBSERVER:
            chat->peer_list[tgt_index].role = TOX_GROUP_ROLE_OBSERVER;
            snprintf(msg, sizeof(msg), "-!- %s has set %s's role to observer", src_name, tgt_name);
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", msg);
            sort_peerlist(groupnumber);
            break;

        case TOX_GROUP_MOD_EVENT_USER:
            chat->peer_list[tgt_index].role = TOX_GROUP_ROLE_USER;
            snprintf(msg, sizeof(msg), "-!- %s has set %s's role to user", src_name, tgt_name);
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", msg);
            sort_peerlist(groupnumber);
            break;

        case TOX_GROUP_MOD_EVENT_MODERATOR:
            chat->peer_list[tgt_index].role = TOX_GROUP_ROLE_MODERATOR;
            snprintf(msg, sizeof(msg), "-!- %s has set %s's role to moderator", src_name, tgt_name);
            line_info_add(self, c_config, true, NULL, NULL, SYS_MSG, 1, BLUE, "%s", msg);
            sort_peerlist(groupnumber);
            break;

        default:
            return;
    }

    ChatContext *ctx = self->chatwin;
    write_to_log(ctx->log, c_config, msg, NULL, LOG_HINT_MOD_EVENT);
}

static void groupchat_onGroupSelfNickChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber,
        const char *old_nick, size_t old_length, const char *new_nick, size_t length)
{
    UNUSED_VAR(old_length);

    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    Tox_Err_Group_Self_Query s_err;
    const uint32_t peer_id = tox_group_self_get_peer_id(tox, self->num, &s_err);

    if (s_err != TOX_ERR_GROUP_SELF_QUERY_OK) {
        return;
    }

    const int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index < 0) {
        return;
    }

    length = MIN(length, TOX_MAX_NAME_LENGTH - 1);
    memcpy(chat->peer_list[peer_index].name, new_nick, length);
    chat->peer_list[peer_index].name[length] = '\0';
    chat->peer_list[peer_index].name_length = length;

    line_info_add(self, toxic->c_config, true, old_nick, chat->peer_list[peer_index].name, NAME_CHANGE, 0,
                  MAGENTA, " is now known as ");

    groupchat_update_last_seen(groupnumber, peer_id);
    groupchat_update_name_list(groupnumber);

    ChatContext *ctx = self->chatwin;

    char log_event[TOX_MAX_NAME_LENGTH + 32];
    snprintf(log_event, sizeof(log_event), "is now known as %s", chat->peer_list[peer_index].name);
    write_to_log(ctx->log, toxic->c_config, log_event, old_nick, LOG_HINT_NAME);
}

static void groupchat_onGroupNickChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
                                        const char *new_nick, size_t length)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    const int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index < 0) {
        return;
    }

    groupchat_update_last_seen(groupnumber, peer_id);

    GroupPeer *peer = &chat->peer_list[peer_index];

    length = MIN(length, TOX_MAX_NAME_LENGTH - 1);
    memcpy(peer->name, new_nick, length);
    peer->name[length] = '\0';
    peer->name_length = length;

    line_info_add(self, toxic->c_config, true, peer->prev_name, peer->name, NAME_CHANGE, 0, MAGENTA,
                  " is now known as ");

    ChatContext *ctx = self->chatwin;

    char log_event[TOX_MAX_NAME_LENGTH + 32];
    snprintf(log_event, sizeof(log_event), "is now known as %s", peer->name);
    write_to_log(ctx->log, toxic->c_config, log_event, peer->prev_name, LOG_HINT_NAME);

    snprintf(peer->prev_name, sizeof(peer->prev_name), "%s", peer->name);

    groupchat_update_name_list(groupnumber);
}

static void groupchat_onGroupStatusChange(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, uint32_t peer_id,
        TOX_USER_STATUS status)
{
    UNUSED_VAR(toxic);

    if (self == NULL) {
        return;
    }

    if (self->num != groupnumber) {
        return;
    }

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        return;
    }

    const int peer_index = get_peer_index(groupnumber, peer_id);

    if (peer_index < 0) {
        return;
    }

    groupchat_update_last_seen(groupnumber, peer_id);
    chat->peer_list[peer_index].status = status;
}

static void send_group_message(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, const char *msg,
                               TOX_MESSAGE_TYPE type)
{
    if (toxic == NULL || self == NULL) {
        return;
    }

    Tox *tox = toxic->tox;
    const Client_Config *c_config = toxic->c_config;

    ChatContext *ctx = self->chatwin;

    if (msg == NULL) {
        wprintw(ctx->history, "Message is empty.\n");
        return;
    }

    Tox_Err_Group_Send_Message err;
    tox_group_send_message(tox, groupnumber, type, (const uint8_t *) msg, strlen(msg), &err);

    if (err != TOX_ERR_GROUP_SEND_MESSAGE_OK) {
        if (err == TOX_ERR_GROUP_SEND_MESSAGE_PERMISSIONS) {
            const Tox_Group_Role role = tox_group_self_get_role(tox, groupnumber, NULL);

            if (role == TOX_GROUP_ROLE_OBSERVER) {
                line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * You are silenced.");
            } else {
                line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * You do not have voice.");
            }
        } else {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to send message (Error %d).", err);
        }

        return;
    }

    char self_nick[TOX_MAX_NAME_LENGTH + 1];
    get_group_self_nick_truncate(tox, self_nick, groupnumber);

    if (type == TOX_MESSAGE_TYPE_NORMAL) {
        line_info_add(self, c_config, true, self_nick, NULL, OUT_MSG_READ, 0, 0, "%s", msg);
        write_to_log(ctx->log, c_config, msg, self_nick, LOG_HINT_NORMAL_O);
    } else if (type == TOX_MESSAGE_TYPE_ACTION) {
        line_info_add(self, c_config, true, self_nick, NULL, OUT_ACTION_READ, 0, 0, "%s", msg);
        write_to_log(ctx->log, c_config, msg, self_nick, LOG_HINT_ACTION);
    }
}

static void send_group_prvt_message(ToxWindow *self, Toxic *toxic, uint32_t groupnumber, const char *data,
                                    size_t data_len)
{
    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    GroupChat *chat = get_groupchat(groupnumber);

    if (chat == NULL) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, "Failed to fetch GroupChat object.");
        return;
    }

    if (data == NULL) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, "Invalid comand.");
        return;
    }

    uint32_t name_length = 0;
    const char *nick = NULL;

    /* need to match the longest nick in case of nicks that are smaller sub-strings */
    for (uint32_t i = 0; i < chat->max_idx; ++i) {
        if (!chat->peer_list[i].active) {
            continue;
        }

        if (data_len < chat->peer_list[i].name_length) {
            continue;
        }

        if (memcmp(chat->peer_list[i].name, data, chat->peer_list[i].name_length) == 0) {
            if (chat->peer_list[i].name_length > name_length) {
                name_length = chat->peer_list[i].name_length;
                nick = chat->peer_list[i].name;
            }
        }
    }

    if (nick == NULL) {
        if (data_len < TOX_GROUP_PEER_PUBLIC_KEY_SIZE * 2) {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Invalid nick.");
            return;
        }

        nick = data;
        name_length = TOX_GROUP_PEER_PUBLIC_KEY_SIZE * 2;
    }

    uint32_t peer_id;

    if (group_get_peer_id_of_identifier(self, c_config, nick, &peer_id) != 0) {
        return;
    }

    const int msg_len = ((int) data_len) - ((int) name_length) - 1;

    if (msg_len <= 0) {
        line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, 0, "Message is empty.");
        return;
    }

    const char *msg = data + name_length + 1;

    Tox_Err_Group_Send_Private_Message err;

    if (!tox_group_send_private_message(toxic->tox, groupnumber, peer_id, TOX_MESSAGE_TYPE_NORMAL,
                                        (const uint8_t *) msg, msg_len, &err)) {
        if (err == TOX_ERR_GROUP_SEND_PRIVATE_MESSAGE_PERMISSIONS) {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * You are silenced.");
        } else {
            line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to send private message (%d)", err);
        }

        return;
    }

    char pm_nick[TOX_MAX_NAME_LENGTH + 3];
    snprintf(pm_nick, sizeof(pm_nick), ">%s<", nick);

    line_info_add(self, c_config, true, pm_nick, NULL, OUT_PRVT_MSG, 0, 0, "%s", msg);
    write_to_log(ctx->log, c_config, msg, pm_nick, LOG_HINT_PRIVATE_O);
}

/*
 * Return true if input is recognized by handler
 */
static bool groupchat_onKey(ToxWindow *self, Toxic *toxic, wint_t key, bool ltr)
{
    if (self == NULL || toxic == NULL) {
        return false;
    }

    const Client_Config *c_config = toxic->c_config;
    ChatContext *ctx = self->chatwin;

    GroupChat *chat = get_groupchat(self->num);

    if (chat == NULL) {
        return false;
    }

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

    if (key == L'\t') {  /* TAB key: auto-completes peer name or command */
        input_ret = true;

        if (ctx->len > 0) {
            int diff;

            /* TODO: make this not suck */
            if (ctx->line[0] != L'/' || wcschr(ctx->line, L' ') != NULL) {
                diff = complete_line(self, toxic, (const char *const *) chat->name_list, chat->num_peers);
            } else if (wcsncmp(ctx->line, L"/avatar \"", wcslen(L"/avatar \"")) == 0) {
                diff = dir_match(self, toxic, ctx->line, L"/avatar");
            } else {
                diff = complete_line(self, toxic, group_cmd_list, sizeof(group_cmd_list) / sizeof(char *));
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
        int L = y2 - CHATBOX_HEIGHT - GROUP_SIDEBAR_OFFSET;

        if (chat->side_pos < (int) chat->num_peers - L) {
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
                if (strncmp(line, "/close", strlen("/close")) == 0) {
                    int offset = 6;

                    if (line[offset] != '\0') {
                        ++offset;
                    }

                    const char *part_message = line + offset;
                    size_t part_length = ctx->len - offset;

                    if (part_length > 0) {
                        exit_groupchat(self, toxic, self->num, part_message, part_length);
                    } else {
                        exit_groupchat(self, toxic, self->num, c_config->group_part_message,
                                       strlen(c_config->group_part_message));
                    }

                    return true;
                } else if (strncmp(line, "/me ", strlen("/me ")) == 0) {
                    send_group_message(self, toxic, self->num, line + 4, TOX_MESSAGE_TYPE_ACTION);
                } else if (strncmp(line, "/whisper ", strlen("/whisper ")) == 0) {
                    send_group_prvt_message(self, toxic, self->num, line + 9, ctx->len - 9);
                } else {
                    execute(ctx->history, self, toxic, line, GROUPCHAT_COMMAND_MODE);
                }
            } else if (line[0]) {
                send_group_message(self, toxic, self->num, line, TOX_MESSAGE_TYPE_NORMAL);
            } else {
                line_info_add(self, c_config, false, NULL, NULL, SYS_MSG, 0, RED, " * Failed to parse message.");
            }

            wclear(ctx->linewin);
            wmove(self->window, y2, 0);
            reset_buf(ctx);
        }
    }

    return input_ret;
}

static void draw_groupchat_top_bar(ToxWindow *self, const Toxic *toxic, int x2)
{
    StatusBar *statusbar = self->stb;

    wmove(statusbar->topline, 0, 0);

    const size_t sidebar_width = self->show_peerlist ? SIDEBAR_WIDTH + 1 : 1;
    const int maxlen = x2 - getcurx(statusbar->topline) - sidebar_width - 1;

    pthread_mutex_lock(&Winthread.lock);

    const size_t topic_len = statusbar->topic_len;

    wattron(statusbar->topline, COLOR_PAIR(BAR_TEXT));

    if (topic_len > maxlen && maxlen >= 3) {
        statusbar->topic[maxlen - 3] = '\0';
        strcat(statusbar->topic, "...");
        statusbar->topic_len = strlen(statusbar->topic);
    }

    if (topic_len > 0) {
        wprintw(statusbar->topline, " %s", statusbar->topic);
    }

    pthread_mutex_unlock(&Winthread.lock);

    int s_y;
    int s_x;
    getyx(statusbar->topline, s_y, s_x);

    mvwhline(statusbar->topline, s_y, s_x, ' ', x2 - s_x);

    wattroff(statusbar->topline, COLOR_PAIR(BAR_TEXT));
}

static void groupchat_onDraw(ToxWindow *self, Toxic *toxic)
{
    if (toxic == NULL || self == NULL) {
        fprintf(stderr, "groupchat_onDraw null param\n");
        return;
    }

    int x2, y2;
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0 || y2 <= 0) {
        return;
    }

    ChatContext *ctx = self->chatwin;

    pthread_mutex_lock(&Winthread.lock);

    GroupChat *chat = get_groupchat(self->num);

    if (chat == NULL) {
        pthread_mutex_unlock(&Winthread.lock);
        return;
    }

    line_info_print(self, toxic->c_config);

    pthread_mutex_unlock(&Winthread.lock);

    wclear(ctx->linewin);

    if (ctx->len > 0) {
        mvwprintw(ctx->linewin, 0, 0, "%ls", &ctx->line[ctx->start]);
    }

    curs_set(1);

    if (self->x != x2) {
        groupchat_update_statusbar_topic(self, toxic->tox);
    }

    draw_groupchat_top_bar(self, toxic, x2);

    self->x = x2;

    wclear(ctx->sidebar);

    if (self->show_peerlist) {
        wattron(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));
        mvwvline(ctx->sidebar, 0, 0, ACS_VLINE, y2 - CHATBOX_HEIGHT);
        mvwaddch(ctx->sidebar, y2 - CHATBOX_HEIGHT, 0, ACS_BTEE);
        wattroff(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));

        wmove(ctx->sidebar, 0, 1);
        wattron(ctx->sidebar, A_BOLD);

        pthread_mutex_lock(&Winthread.lock);

        if (chat->num_peers > 1) {
            wprintw(ctx->sidebar, "Peers: %d\n", chat->num_peers);
        } else if (tox_group_is_connected(toxic->tox, self->num, NULL)) {
            wprintw(ctx->sidebar, "Connecting...\n");
        } else {
            wprintw(ctx->sidebar, "Disconnected\n");
        }

        pthread_mutex_unlock(&Winthread.lock);

        wattroff(ctx->sidebar, A_BOLD);

        wattron(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));
        mvwaddch(ctx->sidebar, 1, 0, ACS_LTEE);
        mvwhline(ctx->sidebar, 1, 1, ACS_HLINE, SIDEBAR_WIDTH - 1);
        wattroff(ctx->sidebar, COLOR_PAIR(PEERLIST_LINE));

        const int maxlines = y2 - GROUP_SIDEBAR_OFFSET - CHATBOX_HEIGHT;
        uint32_t offset = 0;

        pthread_mutex_lock(&Winthread.lock);
        const uint32_t max_idx = chat->max_idx;
        const uint32_t start = chat->side_pos;
        pthread_mutex_unlock(&Winthread.lock);

        for (uint32_t i = start; i < max_idx && offset < maxlines; ++i) {
            pthread_mutex_lock(&Winthread.lock);

            if (!chat->peer_list[i].active) {
                pthread_mutex_unlock(&Winthread.lock);
                continue;
            }

            wmove(ctx->sidebar, offset + 2, 1);

            const bool is_ignored = chat->peer_list[i].is_ignored;
            uint16_t maxlen_offset = chat->peer_list[i].role == TOX_GROUP_ROLE_USER ? 2 : 3;

            if (is_ignored) {
                ++maxlen_offset;
            }

            /* truncate nick to fit in side panel without modifying list */
            char tmpnck[TOX_MAX_NAME_LENGTH];
            const size_t maxlen = SIDEBAR_WIDTH - maxlen_offset;
            snprintf(tmpnck, maxlen + 1, "%s", chat->peer_list[i].name);

            int namecolour = WHITE;

            if (chat->peer_list[i].status == TOX_USER_STATUS_AWAY) {
                namecolour = YELLOW;
            } else if (chat->peer_list[i].status == TOX_USER_STATUS_BUSY) {
                namecolour = RED;
            }

            /* Signify roles (e.g. founder, moderator) */
            const char *rolesig = "";
            int rolecolour = WHITE;

            if (chat->peer_list[i].role == TOX_GROUP_ROLE_FOUNDER) {
                rolesig = "&";
                rolecolour = BLUE;
            } else if (chat->peer_list[i].role == TOX_GROUP_ROLE_MODERATOR) {
                rolesig = "+";
                rolecolour = GREEN;
            } else if (chat->peer_list[i].role == TOX_GROUP_ROLE_OBSERVER) {
                rolesig = "-";
                rolecolour = MAGENTA;
            }

            pthread_mutex_unlock(&Winthread.lock);

            if (is_ignored) {
                wattron(ctx->sidebar, COLOR_PAIR(RED) | A_BOLD);
                wprintw(ctx->sidebar, "#");
                wattroff(ctx->sidebar, COLOR_PAIR(RED) | A_BOLD);
            }

            wattron(ctx->sidebar, COLOR_PAIR(rolecolour) | A_BOLD);
            wprintw(ctx->sidebar, "%s", rolesig);
            wattroff(ctx->sidebar, COLOR_PAIR(rolecolour) | A_BOLD);

            wattron(ctx->sidebar, COLOR_PAIR(namecolour));
            wprintw(ctx->sidebar, "%s\n", tmpnck);
            wattroff(ctx->sidebar, COLOR_PAIR(namecolour));

            ++offset;
        }
    }

    int y;
    int x;
    getyx(self->window, y, x);
    UNUSED_VAR(x);

    const int new_x = ctx->start ? x2 - 1 : MAX(0, wcswidth(ctx->line, ctx->pos));
    wmove(self->window, y, new_x);

    draw_window_bar(self, toxic->windows);

    wrefresh(self->window);

    if (self->help->active) {
        help_draw_main(self);
    }
}

static void groupchat_onInit(ToxWindow *self, Toxic *toxic)
{
    UNUSED_VAR(toxic);

    if (self == NULL) {
        return;
    }

    int x2;
    int y2;
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0 || y2 <= 0) {
        exit_toxic_err(FATALERR_CURSES, "failed in groupchat_onInit");
    }

    self->x = x2;

    groupchat_update_statusbar_topic(self, toxic->tox);

    ChatContext *ctx = self->chatwin;

    ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2 - SIDEBAR_WIDTH - 1, 0, 0);
    self->window_bar = subwin(self->window, WINDOW_BAR_HEIGHT, x2, y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);
    ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);
    self->stb->topline = subwin(self->window, TOP_BAR_HEIGHT, x2, 0, 0);

    ctx->hst = calloc(1, sizeof(struct history));
    ctx->log = calloc(1, sizeof(struct chatlog));

    if (ctx->log == NULL || ctx->hst == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in groupchat_onInit");
    }

    line_info_init(ctx->hst);

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);
}

/*
 * Sets the tab name colour of the ToxWindow associated with `public_key` to `colour`.
 *
 * Return false if group does not exist.
 */
static bool groupchat_window_set_tab_name_colour(Windows *windows, const char *public_key, int colour)
{
    const int groupnumber = get_groupnumber_by_public_key_string(public_key);

    if (groupnumber < 0) {
        return false;
    }

    ToxWindow *self = get_window_by_number_type(windows, groupnumber, WINDOW_TYPE_GROUPCHAT);

    if (self == NULL) {
        return false;
    }

    self->colour = colour;

    return true;
}

bool groupchat_config_set_tab_name_colour(Windows *windows, const char *public_key, const char *colour)
{
    const int colour_val = colour_string_to_int(colour);

    if (colour_val < 0) {
        return false;
    }

    return groupchat_window_set_tab_name_colour(windows, public_key, colour_val);
}

bool groupchat_config_set_autolog(Windows *windows, const char *public_key, bool autolog_enabled)
{
    const int groupnumber = get_groupnumber_by_public_key_string(public_key);

    if (groupnumber < 0) {
        return false;
    }

    return autolog_enabled
           ? enable_window_log_by_number_type(windows, groupnumber, WINDOW_TYPE_GROUPCHAT)
           : disable_window_log_by_number_type(windows, groupnumber, WINDOW_TYPE_GROUPCHAT);
}

static ToxWindow *new_group_chat(Tox *tox, uint32_t groupnumber, const char *groupname, int length)
{
    ToxWindow *ret = calloc(1, sizeof(ToxWindow));

    if (ret == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_group_chat");
    }

    ret->type = WINDOW_TYPE_GROUPCHAT;

    ret->onKey = &groupchat_onKey;
    ret->onDraw = &groupchat_onDraw;
    ret->onInit = &groupchat_onInit;
    ret->onGroupMessage = &groupchat_onGroupMessage;
    ret->onGroupPrivateMessage = &groupchat_onGroupPrivateMessage;
    ret->onGroupPeerJoin = &groupchat_onGroupPeerJoin;
    ret->onGroupPeerExit = &groupchat_onGroupPeerExit;
    ret->onGroupTopicChange = &groupchat_onGroupTopicChange;
    ret->onGroupPeerLimit = &groupchat_onGroupPeerLimit;
    ret->onGroupPrivacyState = &groupchat_onGroupPrivacyState;
    ret->onGroupTopicLock = &groupchat_onGroupTopicLock;
    ret->onGroupPassword = &groupchat_onGroupPassword;
    ret->onGroupNickChange = &groupchat_onGroupNickChange;
    ret->onGroupStatusChange = &groupchat_onGroupStatusChange;
    ret->onGroupSelfJoin = &groupchat_onGroupSelfJoin;
    ret->onGroupRejected = &groupchat_onGroupRejected;
    ret->onGroupModeration = &groupchat_onGroupModeration;
    ret->onGroupVoiceState = &groupchat_onGroupVoiceState;

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    StatusBar *stb = calloc(1, sizeof(StatusBar));
    Help *help = calloc(1, sizeof(Help));

    if (stb == NULL || chatwin == NULL || help == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "failed in new_group_chat");
    }

    ret->chatwin = chatwin;
    ret->stb = stb;
    ret->help = help;

    ret->num = groupnumber;
    ret->show_peerlist = true;
    ret->active_box = -1;

    if (groupname && length > 0) {
        set_window_title(ret, groupname, length);
    } else {
        snprintf(ret->name, sizeof(ret->name), "Group %u", groupnumber);
    }

    return ret;
}
