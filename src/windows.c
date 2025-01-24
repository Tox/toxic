/*  windows.c
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "avatars.h"
#include "chat.h"
#include "conference.h"
#include "file_transfers.h"
#include "friendlist.h"
#include "groupchats.h"
#include "line_info.h"
#include "log.h"
#include "misc_tools.h"
#include "prompt.h"
#include "settings.h"
#include "toxic.h"
#include "windows.h"

#ifdef GAMES
#include "game_base.h"
#endif

/* CALLBACKS START */
void on_friend_request(Tox *tox, const uint8_t *public_key, const uint8_t *data, size_t length, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[MAX_STR_SIZE + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) data, length);
    filter_string(msg, length, false);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFriendRequest != NULL) {
            w->onFriendRequest(w, toxic, (const char *) public_key, msg, length);
        }
    }
}

void on_friend_connection_status(Tox *tox, uint32_t friendnumber, Tox_Connection connection_status, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    on_avatar_friend_connection_status(toxic, friendnumber, connection_status);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConnectionChange != NULL) {
            w->onConnectionChange(w, toxic, friendnumber, connection_status);
        }
    }

    flag_interface_refresh();
}

void on_friend_typing(Tox *tox, uint32_t friendnumber, bool is_typing, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    if (!toxic->c_config->show_typing_other) {
        return;
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onTypingChange != NULL) {
            w->onTypingChange(w, toxic, friendnumber, is_typing);
        }
    }

    flag_interface_refresh();
}

void on_friend_message(Tox *tox, uint32_t friendnumber, Tox_Message_Type type, const uint8_t *string, size_t length,
                       void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[MAX_STR_SIZE + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) string, length);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onMessage != NULL) {
            w->onMessage(w, toxic, friendnumber, type, msg, length);
        }
    }
}

void on_friend_name(Tox *tox, uint32_t friendnumber, const uint8_t *string, size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    if (friend_config_alias_is_set(friendnumber)) {
        return;
    }

    char nick[TOXIC_MAX_NAME_LENGTH + 1];
    length = copy_tox_str(nick, sizeof(nick), (const char *) string, length);
    filter_string(nick, length, true);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onNickChange != NULL) {
            w->onNickChange(w, toxic, friendnumber, nick, length);
        }
    }

    flag_interface_refresh();

    store_data(toxic);
}

void on_friend_status_message(Tox *tox, uint32_t friendnumber, const uint8_t *string, size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[TOX_MAX_STATUS_MESSAGE_LENGTH + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) string, length);
    filter_string(msg, length, false);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onStatusMessageChange != NULL) {
            w->onStatusMessageChange(w, friendnumber, msg, length);
        }
    }

    flag_interface_refresh();
}

void on_friend_status(Tox *tox, uint32_t friendnumber, Tox_User_Status status, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onStatusChange != NULL) {
            w->onStatusChange(w, toxic, friendnumber, status);
        }
    }

    flag_interface_refresh();
}

// TODO: This isn't a proper tox callback. Refactor with friendlist.
void on_friend_added(Toxic *toxic, uint32_t friendnumber, bool sort)
{
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFriendAdded != NULL) {
            w->onFriendAdded(w, toxic, friendnumber, sort);
        }
    }

    store_data(toxic);
}

void on_conference_message(Tox *tox, uint32_t conferencenumber, uint32_t peernumber, Tox_Message_Type type,
                           const uint8_t *message, size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[MAX_STR_SIZE + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) message, length);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConferenceMessage != NULL) {
            w->onConferenceMessage(w, toxic, conferencenumber, peernumber, type, msg, length);
        }
    }
}

void on_conference_invite(Tox *tox, uint32_t friendnumber, Tox_Conference_Type type, const uint8_t *conference_pub_key,
                          size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConferenceInvite != NULL) {
            w->onConferenceInvite(w, toxic, friendnumber, type, (const char *) conference_pub_key, length);
        }
    }
}

void on_conference_peer_list_changed(Tox *tox, uint32_t conferencenumber, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConferenceNameListChange != NULL) {
            w->onConferenceNameListChange(w, toxic, conferencenumber);
        }
    }

    flag_interface_refresh();
}

void on_conference_peer_name(Tox *tox, uint32_t conferencenumber, uint32_t peernumber, const uint8_t *name,
                             size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char nick[TOXIC_MAX_NAME_LENGTH + 1];
    length = copy_tox_str(nick, sizeof(nick), (const char *) name, length);
    filter_string(nick, length, true);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConferencePeerNameChange != NULL) {
            w->onConferencePeerNameChange(w, toxic, conferencenumber, peernumber, nick, length);
        }
    }
}

void on_conference_title(Tox *tox, uint32_t conferencenumber, uint32_t peernumber, const uint8_t *title, size_t length,
                         void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char data[MAX_STR_SIZE + 1];
    length = copy_tox_str(data, sizeof(data), (const char *) title, length);
    filter_string(data, length, false);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onConferenceTitleChange != NULL) {
            w->onConferenceTitleChange(w, toxic, conferencenumber, peernumber, data, length);
        }
    }
}

void on_file_chunk_request(Tox *tox, uint32_t friendnumber, uint32_t filenumber, uint64_t position,
                           size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    FileTransfer *ft = get_file_transfer_struct(friendnumber, filenumber);

    if (ft == NULL) {
        return;
    }

    if (ft->file_type == TOX_FILE_KIND_AVATAR) {
        on_avatar_chunk_request(toxic, ft, position, length);
        return;
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFileChunkRequest != NULL) {
            w->onFileChunkRequest(w, toxic, friendnumber, filenumber, position, length);
        }
    }
}

void on_file_recv_chunk(Tox *tox, uint32_t friendnumber, uint32_t filenumber, uint64_t position,
                        const uint8_t *data, size_t length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    const FileTransfer *ft = get_file_transfer_struct(friendnumber, filenumber);

    if (ft == NULL) {
        return;
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFileRecvChunk != NULL) {
            w->onFileRecvChunk(w, toxic, friendnumber, filenumber, position, (const char *) data, length);
        }
    }
}

void on_file_recv_control(Tox *tox, uint32_t friendnumber, uint32_t filenumber, Tox_File_Control control,
                          void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    FileTransfer *ft = get_file_transfer_struct(friendnumber, filenumber);

    if (ft == NULL) {
        return;
    }

    if (ft->file_type == TOX_FILE_KIND_AVATAR) {
        on_avatar_file_control(toxic, ft, control);
        return;
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFileControl != NULL) {
            w->onFileControl(w, toxic, friendnumber, filenumber, control);
        }
    }
}

void on_file_recv(Tox *tox, uint32_t friendnumber, uint32_t filenumber, uint32_t kind, uint64_t file_size,
                  const uint8_t *filename, size_t filename_length, void *userdata)
{
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    /* We don't care about receiving avatars */
    if (kind != TOX_FILE_KIND_DATA) {
        tox_file_control(tox, friendnumber, filenumber, TOX_FILE_CONTROL_CANCEL, NULL);
        return;
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onFileRecv != NULL) {
            w->onFileRecv(w, toxic, friendnumber, filenumber, file_size, (const char *) filename,
                          filename_length);
        }
    }
}

void on_friend_read_receipt(Tox *tox, uint32_t friendnumber, uint32_t receipt, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onReadReceipt != NULL) {
            w->onReadReceipt(w, toxic, friendnumber, receipt);
        }
    }
}

void on_lossless_custom_packet(Tox *tox, uint32_t friendnumber, const uint8_t *data, size_t length, void *userdata)
{
    UNUSED_VAR(tox);

    if (length == 0 || data == NULL) {
        return;
    }

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    const uint8_t type = data[0];

    switch (type) {
#ifdef GAMES

        case CUSTOM_PACKET_GAME_INVITE: {
            for (uint16_t i = 0; i < windows->count; ++i) {
                ToxWindow *w = windows->list[i];

                if (w->onGameInvite != NULL) {
                    w->onGameInvite(w, toxic, friendnumber, data + 1, length - 1);
                }
            }

            break;
        }

        case CUSTOM_PACKET_GAME_DATA: {
            for (uint16_t i = 0; i < windows->count; ++i) {
                ToxWindow *w = windows->list[i];

                if (w->onGameData != NULL) {
                    w->onGameData(w, toxic, friendnumber, data + 1, length - 1);
                }
            }

            break;
        }

#else
        UNUSED_VAR(toxic);
#endif // GAMES

        default: {
            fprintf(stderr, "Got unknown custom packet of type: %u\n", type);
            return;
        }
    }
}

void on_group_invite(Tox *tox, uint32_t friendnumber, const uint8_t *invite_data, size_t length,
                     const uint8_t *group_name,
                     size_t group_name_length, void *userdata)
{
    UNUSED_VAR(tox);
    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char gname[MAX_STR_SIZE + 1];
    group_name_length = copy_tox_str(gname, sizeof(gname), (const char *) group_name, group_name_length);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupInvite != NULL) {
            w->onGroupInvite(w, toxic, friendnumber, (const char *) invite_data, length, gname,
                             group_name_length);
        }
    }
}

void on_group_message(Tox *tox, uint32_t groupnumber, uint32_t peer_id, Tox_Message_Type type,
                      const uint8_t *message, size_t length, Tox_Group_Message_Id message_id, void *userdata)
{
    UNUSED_VAR(message_id);
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[MAX_STR_SIZE + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) message, length);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupMessage != NULL) {
            w->onGroupMessage(w, toxic, groupnumber, peer_id, type, msg, length);
        }
    }
}

void on_group_private_message(Tox *tox, uint32_t groupnumber, uint32_t peer_id, Tox_Message_Type type,
                              const uint8_t *message, size_t length, Tox_Group_Message_Id message_id,
                              void *userdata)
{
    UNUSED_VAR(tox);
    UNUSED_VAR(type);
    UNUSED_VAR(message_id);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char msg[MAX_STR_SIZE + 1];
    length = copy_tox_str(msg, sizeof(msg), (const char *) message, length);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPrivateMessage != NULL) {
            w->onGroupPrivateMessage(w, toxic, groupnumber, peer_id, msg, length);
        }
    }
}

void on_group_status_change(Tox *tox, uint32_t groupnumber, uint32_t peer_id, Tox_User_Status status, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupStatusChange != NULL) {
            w->onGroupStatusChange(w, toxic, groupnumber, peer_id, status);
        }
    }

    flag_interface_refresh();
}

void on_group_peer_join(Tox *tox, uint32_t groupnumber, uint32_t peer_id, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPeerJoin != NULL) {
            w->onGroupPeerJoin(w, toxic, groupnumber, peer_id);
        }
    }

    flag_interface_refresh();
}

void on_group_peer_exit(Tox *tox, uint32_t groupnumber, uint32_t peer_id, Tox_Group_Exit_Type exit_type,
                        const uint8_t *nick,
                        size_t nick_len, const uint8_t *part_message, size_t length, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char toxic_nick[TOXIC_MAX_NAME_LENGTH + 1];
    nick_len = copy_tox_str(toxic_nick, sizeof(toxic_nick), (const char *) nick, nick_len);
    filter_string(toxic_nick, nick_len, true);

    char buf[MAX_STR_SIZE + 1] = {0};
    size_t buf_len = 0;

    if (part_message) {
        buf_len = copy_tox_str(buf, sizeof(buf), (const char *) part_message, length);
        filter_string(buf, buf_len, false);
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPeerExit != NULL) {
            w->onGroupPeerExit(w, toxic, groupnumber, peer_id, exit_type, toxic_nick, nick_len, buf, buf_len);
        }
    }
}

void on_group_topic_change(Tox *tox, uint32_t groupnumber, uint32_t peer_id, const uint8_t *topic, size_t length,
                           void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char data[MAX_STR_SIZE + 1];
    length = copy_tox_str(data, sizeof(data), (const char *) topic, length);
    filter_string(data, length, false);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupTopicChange != NULL) {
            w->onGroupTopicChange(w, toxic, groupnumber, peer_id, data, length);
        }
    }
}

void on_group_peer_limit(Tox *tox, uint32_t groupnumber, uint32_t peer_limit, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPeerLimit != NULL) {
            w->onGroupPeerLimit(w, toxic, groupnumber, peer_limit);
        }
    }
}

void on_group_privacy_state(Tox *tox, uint32_t groupnumber, Tox_Group_Privacy_State privacy_state, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPrivacyState != NULL) {
            w->onGroupPrivacyState(w, toxic, groupnumber, privacy_state);
        }
    }
}

void on_group_topic_lock(Tox *tox, uint32_t groupnumber, Tox_Group_Topic_Lock topic_lock, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupTopicLock != NULL) {
            w->onGroupTopicLock(w, toxic, groupnumber, topic_lock);
        }
    }
}

void on_group_password(Tox *tox, uint32_t groupnumber, const uint8_t *password, size_t length, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupPassword != NULL) {
            w->onGroupPassword(w, toxic, groupnumber, (const char *) password, length);
        }
    }
}

void on_group_nick_change(Tox *tox, uint32_t groupnumber, uint32_t peer_id, const uint8_t *newname, size_t length,
                          void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    char name[TOXIC_MAX_NAME_LENGTH + 1];
    length = copy_tox_str(name, sizeof(name), (const char *) newname, length);
    filter_string(name, length, true);

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupNickChange != NULL) {
            w->onGroupNickChange(w, toxic, groupnumber, peer_id, name, length);
        }
    }
}

void on_group_self_join(Tox *tox, uint32_t groupnumber, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupSelfJoin != NULL) {
            w->onGroupSelfJoin(w, toxic, groupnumber);
        }
    }
}

void on_group_rejected(Tox *tox, uint32_t groupnumber, Tox_Group_Join_Fail type, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupRejected != NULL) {
            w->onGroupRejected(w, toxic, groupnumber, type);
        }
    }
}

void on_group_moderation(Tox *tox, uint32_t groupnumber, uint32_t source_peer_id, uint32_t target_peer_id,
                         Tox_Group_Mod_Event type, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupModeration != NULL) {
            w->onGroupModeration(w, toxic, groupnumber, source_peer_id, target_peer_id, type);
        }
    }
}

void on_group_voice_state(Tox *tox, uint32_t groupnumber, Tox_Group_Voice_State voice_state, void *userdata)
{
    UNUSED_VAR(tox);

    Toxic *toxic = (Toxic *) userdata;
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onGroupVoiceState != NULL) {
            w->onGroupVoiceState(w, toxic, groupnumber, voice_state);
        }
    }
}

/*
 * Returns the windows list index of the window associated with `id`.
 * Returns -1 if `id` is not found.
 */
static int get_window_index(Windows *windows, uint16_t id)
{
    for (uint16_t i = 0; i < windows->count; ++i) {
        if (windows->list[i]->id == id) {
            return i;
        }
    }

    return -1;
}

static uint16_t get_new_window_id(Windows *windows)
{
    uint16_t new_id = 0;

    for (uint16_t i = 0; i < UINT16_MAX; ++i) {
        if (get_window_pointer_by_id(windows, i) == NULL) {
            new_id = i;
            break;
        }
    }

    return new_id;
}

/* CALLBACKS END */

int add_window(Toxic *toxic, ToxWindow *w)
{
    if (w == NULL || LINES < 2) {
        fprintf(stderr, "Failed to add window.\n");
        return -1;
    }

    Windows *windows = toxic->windows;
    const uint16_t new_index = windows->count;

    w->id = get_new_window_id(windows);
    w->window = newwin(LINES, COLS, 0, 0);

    if (w->window == NULL) {
        fprintf(stderr, "newwin() failed in add_window()\n");
        return -1;
    }

    w->colour = BAR_TEXT;

#ifdef URXVT_FIX
    /* Fixes text color problem on some terminals. */
    wbkgd(w->window, COLOR_PAIR(6));
#endif

    if (w->onInit) {
        w->onInit(w, toxic);
    }

    ToxWindow **tmp_list = (ToxWindow **)realloc(windows->list, (windows->count + 1) * sizeof(ToxWindow *));

    if (tmp_list == NULL) {
        exit_toxic_err(FATALERR_MEMORY, "realloc(_, %d * sizeof(ToxWindow *)) failed in add_window()", windows->count + 1);
    }

    tmp_list[new_index] = w;
    windows->list = tmp_list;
    ++windows->count;

    return w->id;
}

void set_active_window_by_type(Windows *windows, Window_Type type)
{
    for (uint16_t i = 0; i < windows->count; ++i) {
        if (windows->list[i]->type == type) {
            windows->active_index = i;
            return;
        }
    }

    fprintf(stderr, "Warning: attemping to set active window with no active type: %d\n", type);
}

void set_active_window_by_id(Windows *windows, uint16_t id)
{
    const int idx = get_window_index(windows, id);

    if (idx < 0) {
        fprintf(stderr, "Warning: attemping to set active window with invalid id: %u\n", id);
        return;
    }

    windows->active_index = idx;
}

/* Displays the next window if `ch` is equal to the next window key binding.
 * Otherwise displays the previous window.
 */
static void set_next_window(Windows *windows, const Client_Config *c_config, int ch)
{
    if (windows->count == 0) {
        return;
    }

    if (ch == c_config->key_next_tab) {
        windows->active_index = (windows->active_index + 1) % windows->count;
        return;
    }

    if (ch == c_config->key_prev_tab) {
        windows->active_index = windows->active_index > 0 ? windows->active_index - 1 : windows->count - 1;
        return;
    }

    fprintf(stderr, "Warning: set_next_window() got invalid key: %c\n", ch);
}

/* Deletes window w and cleans up */
void del_window(ToxWindow *w, Windows *windows, const Client_Config *c_config)
{
    const int idx = get_window_index(windows, w->id);

    if (idx < 0) {
        return;
    }

    delwin(w->window_bar);
    delwin(w->window);
    free(w);

    assert(windows->count > 0);

    --windows->count;

    if (windows->count != idx) {
        windows->list[idx] = windows->list[windows->count];
    }

    windows->list[windows->count] = NULL;

    if (windows->count == 0) {
        free(windows->list);
        windows->list = NULL;
        return;
    }

    ToxWindow **tmp_list = (ToxWindow **)realloc(windows->list, windows->count * sizeof(ToxWindow *));

    if (tmp_list == NULL) {
        exit_toxic_err(FATALERR_MEMORY,
                       "realloc(_, %d * sizeof(ToxWindow *)) failed in del_window()", windows->count);
    }

    windows->list = tmp_list;

    clear();
    refresh();

    if (windows->active_index == 2) {  // skip back to home window instead of friendlist
        set_next_window(windows, c_config, c_config->key_prev_tab);
    }

    set_next_window(windows, c_config, c_config->key_prev_tab);
}

void init_windows(Toxic *toxic)
{
    if (COLS <= CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT) {
        exit_toxic_err(FATALERR_WININIT, "add_window() for prompt failed in init_windows");
    }

    toxic->home_window = new_prompt();

    const int win_num = add_window(toxic, toxic->home_window);

    if (win_num != 0) {  // prompt window is always index 0
        exit_toxic_err(FATALERR_WININIT, "add_window() for prompt failed in init_windows");
    }

    if (add_window(toxic, new_friendlist()) != 1) {  // friendlist is always index 1
        exit_toxic_err(FATALERR_WININIT, "add_window() for friendlist failed in init_windows");
    }

    set_active_window_by_id(toxic->windows, win_num);
}

void on_window_resize(Windows *windows)
{
    endwin();
    refresh();
    clear();

    int x2;
    int y2;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w == NULL) {
            continue;
        }

        if (w->type == WINDOW_TYPE_FRIEND_LIST)  {
            delwin(w->window_bar);
            delwin(w->window);
            w->window = newwin(LINES, COLS, 0, 0);
            w->window_bar = subwin(w->window, WINDOW_BAR_HEIGHT, COLS, LINES - 2, 0);
            continue;
        }

#ifdef GAMES

        if (w->type == WINDOW_TYPE_GAME) {
            delwin(w->window_bar);
            delwin(w->window);
            delwin(w->game->window);
            w->window = newwin(LINES, COLS, 0, 0);

            getmaxyx(w->window, y2, x2);

            if (y2 <= 0 || x2 <= 0) {
                fprintf(stderr, "Failed to resize game window: max_x: %d, max_y: %d\n", x2, y2);
                delwin(w->window);
                continue;
            }

            w->window_bar = subwin(w->window, WINDOW_BAR_HEIGHT, COLS, LINES - 2, 0);
            w->game->window = subwin(w->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2, 0, 0);
            continue;
        }

#endif // GAMES

        if (w->help->active) {
            wclear(w->help->win);
        }

        if (w->type == WINDOW_TYPE_CONFERENCE || w->type == WINDOW_TYPE_GROUPCHAT) {
            delwin(w->chatwin->sidebar);
            w->chatwin->sidebar = NULL;
        }

        if (w->type != WINDOW_TYPE_CONFERENCE) {
            delwin(w->stb->topline);
        }

        delwin(w->chatwin->linewin);
        delwin(w->chatwin->history);
        delwin(w->window_bar);
        delwin(w->window);

        w->window = newwin(LINES, COLS, 0, 0);

        getmaxyx(w->window, y2, x2);

        if (y2 <= 0 || x2 <= 0) {
            fprintf(stderr, "Failed to resize window: max_x: %d, max_y: %d\n", x2, y2);
            delwin(w->window);
            continue;
        }

        if (w->show_peerlist) {
            w->chatwin->history = subwin(w->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT,
                                         x2 - SIDEBAR_WIDTH - 1, 0, 0);
            w->chatwin->sidebar = subwin(w->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT,
                                         SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);
        } else {
            w->chatwin->history =  subwin(w->window, y2 - CHATBOX_HEIGHT - WINDOW_BAR_HEIGHT, x2, 0, 0);
        }

        if (w->type != WINDOW_TYPE_CONFERENCE) {
            w->stb->topline = subwin(w->window, TOP_BAR_HEIGHT, x2, 0, 0);
        }

        w->window_bar = subwin(w->window, WINDOW_BAR_HEIGHT, x2,
                               y2 - (CHATBOX_HEIGHT + WINDOW_BAR_HEIGHT), 0);
        w->chatwin->linewin = subwin(w->window, CHATBOX_HEIGHT, x2, y2 - WINDOW_BAR_HEIGHT, 0);

#ifdef AUDIO

        if (w->chatwin->infobox.active) {
            delwin(w->chatwin->infobox.win);
            w->chatwin->infobox.win = newwin(INFOBOX_HEIGHT, INFOBOX_WIDTH + 1, 1, x2 - INFOBOX_WIDTH);
        }

#endif /* AUDIO */

        scrollok(w->chatwin->history, 0);
        wmove(w->window, y2 - CURS_Y_OFFSET, 0);

        if (!w->scroll_pause) {
            ChatContext *ctx = w->chatwin;
            line_info_reset_start(w, ctx->hst);
        }
    }
}

static void draw_window_tab(WINDOW *win, ToxWindow *toxwin, bool active_window)
{
    pthread_mutex_lock(&Winthread.lock);

    const bool has_alert = toxwin->alert != WINDOW_ALERT_NONE;
    const unsigned int pending_messages = toxwin->pending_messages;

    pthread_mutex_unlock(&Winthread.lock);

    Window_Type type = toxwin->type;

    if (active_window) {
        wattron(win, A_BOLD | COLOR_PAIR(BAR_ACCENT));
        wprintw(win, " [");
        wattroff(win, COLOR_PAIR(BAR_ACCENT));
        wattron(win, COLOR_PAIR(BAR_TEXT));
    } else {
        if (has_alert) {
            wattron(win, COLOR_PAIR(BAR_ACCENT));
            wprintw(win, " [");
            wattroff(win, COLOR_PAIR(BAR_ACCENT));
            wattron(win, A_BOLD | COLOR_PAIR(toxwin->alert));
        } else {
            wattron(win, COLOR_PAIR(BAR_ACCENT));
            wprintw(win, " [");
            wattroff(win, COLOR_PAIR(BAR_ACCENT));
            wattron(win, COLOR_PAIR(BAR_TEXT));
        }
    }

    if (type == WINDOW_TYPE_PROMPT || type == WINDOW_TYPE_FRIEND_LIST) {
        if (!has_alert) {
            wattron(win, COLOR_PAIR(toxwin->colour));
            wprintw(win, "%s", toxwin->name);
            wattroff(win, COLOR_PAIR(toxwin->colour));
        } else {
            wprintw(win, "%s", toxwin->name);
        }
    } else if (active_window) {
        wattron(win, COLOR_PAIR(toxwin->colour));
        wprintw(win, "%s", toxwin->name);
        wattroff(win, COLOR_PAIR(toxwin->colour));
    } else {
        if (pending_messages > 0) {
            wprintw(win, "%u", pending_messages);
        } else {
            wprintw(win, "-");
        }
    }

    if (active_window) {
        wattroff(win, COLOR_PAIR(BAR_TEXT));
        wattron(win, COLOR_PAIR(BAR_ACCENT));
        wprintw(win, "]");
        wattroff(win, A_BOLD | COLOR_PAIR(BAR_ACCENT));
    } else {
        if (has_alert) {
            wattroff(win, A_BOLD | COLOR_PAIR(toxwin->alert));
            wattron(win, COLOR_PAIR(BAR_ACCENT));
            wprintw(win, "]");
            wattroff(win, COLOR_PAIR(BAR_ACCENT));
        } else {
            wattroff(win, COLOR_PAIR(BAR_TEXT));
            wattron(win, COLOR_PAIR(BAR_ACCENT));
            wprintw(win, "]");
            wattroff(win, COLOR_PAIR(BAR_ACCENT));
        }
    }
}

void draw_window_bar(ToxWindow *self, Windows *windows)
{
    WINDOW *win = self->window_bar;
    wclear(win);

    if (self->scroll_pause) {
        wattron(win, A_BLINK | A_BOLD | COLOR_PAIR(BAR_NOTIFY));
        wprintw(win, "^");
        wattroff(win, A_BLINK | A_BOLD | COLOR_PAIR(BAR_NOTIFY));
    } else {
        wattron(win, COLOR_PAIR(BAR_TEXT));
        wprintw(win, " ");
        wattroff(win, COLOR_PAIR(BAR_TEXT));
    }

    for (uint16_t i = 0; i < windows->count; ++i) {
        const bool active_window = i == windows->active_index;
        draw_window_tab(win, windows->list[i], active_window);
    }

    int cur_x;
    int cur_y;
    getyx(win, cur_y, cur_x);

    UNUSED_VAR(cur_y);

    wattron(win, COLOR_PAIR(BAR_TEXT));
    mvwhline(win, 0, cur_x, ' ', COLS - cur_x);
    wattroff(win, COLOR_PAIR(BAR_TEXT));
}

/*
 * Gets current char from stdscr and puts it in ch.
 *
 * Return 1 if char is printable.
 * Return 0 if char is not printable.
 * Return -1 on error.
 */
static int get_current_char(wint_t *ch)
{
    wint_t tmpchar = 0;
    bool is_printable = false;

#ifdef HAVE_WIDECHAR
    int status = wget_wch(stdscr, &tmpchar);

    if (status == ERR) {
        return -1;
    }

    if (status == OK) {
        is_printable = iswprint(tmpchar);
    }

#else
    tmpchar = getch();

    if (tmpchar == ERR) {
        return -1;
    }

    is_printable = isprint(tmpchar);
#endif /* HAVE_WIDECHAR */

    *ch = tmpchar;

    return (int) is_printable;
}

static struct key_sequence_codes {
    wchar_t *code;
    wint_t   key;
} Keys[] = {

    { L"[1;5A", T_KEY_C_UP    },
    { L"[1;5B", T_KEY_C_DOWN  },
    { L"[1;5C", T_KEY_C_RIGHT },
    { L"[1;5D", T_KEY_C_LEFT  },
    { L"[A",    KEY_UP        },
    { L"[B",    KEY_DOWN      },
    { L"[C",    KEY_RIGHT     },
    { L"[D",    KEY_LEFT      },
    { L"a",     T_KEY_A_A     },
    { L"b",     T_KEY_A_B     },
    { L"c",     T_KEY_A_C     },
    { L"d",     T_KEY_A_D     },
    { L"e",     T_KEY_A_E     },
    { L"f",     T_KEY_A_F     },
    { L"g",     T_KEY_A_G     },
    { L"h",     T_KEY_A_H     },
    { L"i",     T_KEY_A_I     },
    { L"j",     T_KEY_A_J     },
    { L"k",     T_KEY_A_K     },
    { L"l",     T_KEY_A_L     },
    { L"m",     T_KEY_A_M     },
    { L"n",     T_KEY_A_N     },
    { L"o",     T_KEY_A_O     },
    { L"p",     T_KEY_A_P     },
    { L"q",     T_KEY_A_Q     },
    { L"r",     T_KEY_A_R     },
    { L"s",     T_KEY_A_S     },
    { L"t",     T_KEY_A_T     },
    { L"u",     T_KEY_A_U     },
    { L"v",     T_KEY_A_V     },
    { L"w",     T_KEY_A_W     },
    { L"x",     T_KEY_A_X     },
    { L"y",     T_KEY_A_Y     },
    { L"z",     T_KEY_A_Z     },
    { NULL, 0 }
};

/*
 * Return key code corresponding to character sequence queued in stdscr.
 * Return -1 if sequence is unknown.
 */
#define MAX_SEQUENCE_SIZE 5
static wint_t get_input_sequence_code(void)
{
    wchar_t code[MAX_SEQUENCE_SIZE + 1];

    size_t length = 0;
    wint_t ch = 0;

    for (size_t i = 0; i < MAX_SEQUENCE_SIZE; ++i) {
        int res = get_current_char(&ch);

        if (res < 0) {
            break;
        }

        ++length;
        code[i] = (wchar_t) ch;
    }

    if (length == 0) {
        return -1;
    }

    code[length] = L'\0';

    for (size_t i = 0; Keys[i].key != 0; ++i) {
        if (wcscmp(code, Keys[i].code) == 0) {
            return Keys[i].key;
        }
    }

    return -1;
}

void draw_active_window(Toxic *toxic)
{
    if (toxic == NULL) {
        return;
    }

    const Client_Config *c_config = toxic->c_config;
    Windows *windows = toxic->windows;

    ToxWindow *a = windows->list[windows->active_index];

    if (a == NULL) {
        return;
    }

    pthread_mutex_lock(&Winthread.lock);
    a->alert = WINDOW_ALERT_NONE;
    a->pending_messages = 0;
    const bool flag_refresh = Winthread.flag_refresh;
    pthread_mutex_unlock(&Winthread.lock);

    if (flag_refresh) {
        touchwin(a->window);
        a->onDraw(a, toxic);
        wrefresh(a->window);
    }

#ifdef AUDIO
    else if (a->is_call && timed_out(a->chatwin->infobox.lastupdate, 1)) {
        touchwin(a->window);
        a->onDraw(a, toxic);
        wrefresh(a->window);
    }

#endif  // AUDIO

#ifdef GAMES

    if (a->type == WINDOW_TYPE_GAME) {
        if (!flag_refresh) {  // we always want to be continously refreshing game windows
            touchwin(a->window);
            a->onDraw(a, toxic);
            wrefresh(a->window);
        }

        int ch = getch();

        if (ch == ERR) {
            return;
        }

        pthread_mutex_lock(&Winthread.lock);
        flag_interface_refresh();
        pthread_mutex_unlock(&Winthread.lock);

        if (ch == c_config->key_next_tab || ch == c_config->key_prev_tab) {
            set_next_window(windows, c_config, ch);
        }

        a->onKey(a, toxic, ch, false);  // we lock only when necessary in the onKey callback

        return;
    }

#endif // GAMES

    wint_t ch = 0;
    int printable = get_current_char(&ch);

    if (printable < 0) {
        return;
    }

    pthread_mutex_lock(&Winthread.lock);
    flag_interface_refresh();
    pthread_mutex_unlock(&Winthread.lock);

    if (printable == 0 && (ch == c_config->key_next_tab || ch == c_config->key_prev_tab)) {
        set_next_window(windows, c_config, (int) ch);
        return;
    } else if ((printable == 0) && (a->type != WINDOW_TYPE_FRIEND_LIST)) {
        pthread_mutex_lock(&Winthread.lock);
        const bool input_ret = a->onKey(a, toxic, ch, (bool) printable);
        pthread_mutex_unlock(&Winthread.lock);

        if (input_ret) {
            return;
        }

        // if an unprintable key code is unrecognized by input handler we attempt to
        // manually decode char sequence
        wint_t tmp = get_input_sequence_code();

        if (tmp != (wint_t) -1) {
            ch = tmp;
        }
    }

    pthread_mutex_lock(&Winthread.lock);
    a->onKey(a, toxic, ch, (bool) printable);
    pthread_mutex_unlock(&Winthread.lock);
}

/* Refresh inactive windows to prevent scrolling bugs.
 * Call at least once per second.
 */
void refresh_inactive_windows(Windows *windows, const Client_Config *c_config)
{
    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *toxwin = windows->list[i];

        if (toxwin == NULL) {
            continue;
        }

        if ((i != windows->active_index) && (toxwin->type != WINDOW_TYPE_FRIEND_LIST)) {
            pthread_mutex_lock(&Winthread.lock);
            line_info_print(toxwin, c_config);
            pthread_mutex_unlock(&Winthread.lock);
        }
    }
}

/* Returns a pointer to the ToxWindow associated with `id`.
 * Returns NULL if no ToxWindow exists.
 */
ToxWindow *get_window_pointer_by_id(Windows *windows, uint16_t id)
{
    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->id == id) {
            return w;
        }
    }

    return NULL;
}

ToxWindow *get_active_window(const Windows *windows)
{
    return windows->active_index < windows->count ? windows->list[windows->active_index] : NULL;
}

void force_refresh(WINDOW *w)
{
    wclear(w);
    endwin();
    refresh();
}

/* Returns the number of active windows of given type. */
uint16_t get_num_active_windows_type(const Windows *windows, Window_Type type)
{
    uint16_t count = 0;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w == NULL) {
            continue;
        }

        if (w->type == type) {
            ++count;
        }
    }

    return count;
}

ToxWindow *get_window_by_number_type(Windows *windows, uint32_t number, Window_Type type)
{
    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *win = windows->list[i];

        if (win->type == type && win->num == number) {
            return win;
        }
    }

    return NULL;
}

bool disable_window_log_by_number_type(Windows *windows, uint32_t number, Window_Type type)
{
    ToxWindow *win = get_window_by_number_type(windows, number, type);

    if (win == NULL) {
        return false;
    }

    ChatContext *ctx = win->chatwin;

    if (ctx == NULL) {
        return false;
    }

    log_disable(ctx->log);

    return true;
}

bool enable_window_log_by_number_type(Windows *windows, uint32_t number, Window_Type type)
{
    ToxWindow *win = get_window_by_number_type(windows, number, type);

    if (win == NULL) {
        return false;
    }

    ChatContext *ctx = win->chatwin;

    if (ctx == NULL) {
        return false;
    }

    return log_enable(ctx->log) == 0;
}

void refresh_window_names(Toxic *toxic)
{
    Windows *windows = toxic->windows;

    for (uint16_t i = 0; i < windows->count; ++i) {
        ToxWindow *w = windows->list[i];

        if (w->onNickRefresh != NULL) {
            w->onNickRefresh(w, toxic);
        }
    }
}

/* destroys all chat and conference windows (should only be called on shutdown) */
void kill_all_windows(Toxic *toxic)
{
    const Client_Config *c_config = toxic->c_config;
    Windows *windows = toxic->windows;

    uint16_t deleted = 0;
    const uint16_t windows_count = windows->count;

    while (windows->list != NULL && deleted++ < windows_count) {
        ToxWindow *w = windows->list[0];

        switch (w->type) {
            case WINDOW_TYPE_CHAT: {
                kill_chat_window(w, toxic);
                break;
            }

            case WINDOW_TYPE_CONFERENCE: {
                free_conference(w, windows, c_config, w->num);
                break;
            }

#ifdef GAMES

            case WINDOW_TYPE_GAME: {
                game_kill(w, windows, c_config);
                break;
            }

#endif // GAMES

            case WINDOW_TYPE_GROUPCHAT: {
                exit_groupchat(w, toxic, w->num, c_config->group_part_message,
                               strlen(c_config->group_part_message));
                break;
            }

            case WINDOW_TYPE_PROMPT: {
                kill_prompt_window(w, windows, c_config);
                break;
            }

            case WINDOW_TYPE_FRIEND_LIST: {
                kill_friendlist(w, windows, c_config);
                break;
            }

            default: {
                fprintf(stderr, "attempting to kill unknown window type: %d\n", w->type);
                break;
            }
        }
    }
}
