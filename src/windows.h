/*  windows.h
 *
 *  Copyright (C) 2014-2024 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#ifndef WINDOWS_H
#define WINDOWS_H

#include <pthread.h>
#include <signal.h>
#include <wchar.h>
#include <wctype.h>

#include <tox/tox.h>

#ifdef AUDIO
#include <tox/toxav.h>
#endif /* AUDIO */

#include "settings.h"
#include "toxic.h"

#define MAX_WINDOW_NAME_LENGTH 22
#define CURS_Y_OFFSET 1    /* y-axis cursor offset for chat contexts */
#define CHATBOX_HEIGHT 1
#define TOP_BAR_HEIGHT 1
#define WINDOW_BAR_HEIGHT 1


typedef enum CustomPacket {
    CUSTOM_PACKET_GAME_INVITE = 160,
    CUSTOM_PACKET_GAME_DATA   = 161,
} CustomPacket;

/* Our own custom colours */
typedef enum CUSTOM_COLOUR {
    CUSTOM_COLOUR_GRAY    = 255,  // values descend from top to prevent conflicts with colour pairs
    CUSTOM_COLOUR_ORANGE  = 254,
    CUSTOM_COLOUR_PINK    = 253,
    CUSTOM_COLOUR_BROWN   = 252,
} CUSTOM_COLOUR;

/* ncurses colour pairs as FOREGROUND_BACKGROUND. No background defaults to black. */
typedef enum COLOUR_PAIR {
    WHITE,
    GREEN,
    CYAN,
    RED,
    BLUE,
    YELLOW,
    MAGENTA,
    BLACK,
    BLACK_WHITE,
    WHITE_BLACK,
    WHITE_BLUE,
    WHITE_GREEN,
    BAR_TEXT,
    STATUS_ONLINE,
    BAR_ACCENT,
    MAGENTA_BG,
    BLACK_BG,
    STATUS_BUSY,
    STATUS_AWAY,
    BAR_NOTIFY,
    PEERLIST_LINE,
    BAR_SOLID,
    BLACK_BAR_FG,
    WHITE_BAR_FG,
    RED_BAR_FG,
    GREEN_BAR_FG,
    BLUE_BAR_FG,
    CYAN_BAR_FG,
    YELLOW_BAR_FG,
    MAGENTA_BAR_FG,
    GRAY_BAR_FG,
    ORANGE_BAR_FG,
    PINK_BAR_FG,
    BROWN_BAR_FG,
} COLOUR_PAIR;

/* tab alert types: lower types take priority (this relies on the order of COLOUR_PAIR) */
typedef enum {
    WINDOW_ALERT_NONE = 0,
    WINDOW_ALERT_0 = STATUS_ONLINE,
    WINDOW_ALERT_1 = BAR_ACCENT,
    WINDOW_ALERT_2 = MAGENTA_BG,
} WINDOW_ALERTS;

typedef enum Window_Type {
    WINDOW_TYPE_PROMPT,
    WINDOW_TYPE_CHAT,
    WINDOW_TYPE_CONFERENCE,
    WINDOW_TYPE_GROUPCHAT,
    WINDOW_TYPE_FRIEND_LIST,

#ifdef GAMES
    WINDOW_TYPE_GAME,
#endif
} Window_Type;

/* Fixes text color problem on some terminals.
   Uncomment if necessary */
/* #define URXVT_FIX */

/*
 * Used to control access to global variables via a mutex, as well as to handle signals.
 * Any file, variable or data structure that is used by the UI/Window thread and any other thread
 * must be guarded by `lock`.
 *
 * There should only ever be one instance of this struct.
 */
struct Winthread {
    pthread_t tid;
    pthread_mutex_t lock;
    volatile sig_atomic_t sig_exit_toxic;
    volatile sig_atomic_t flag_resize;
    volatile sig_atomic_t flag_refresh;
    volatile sig_atomic_t last_refresh_flag;
};

extern struct Winthread Winthread;

struct cqueue_thread {
    pthread_t tid;
};

struct av_thread {
    pthread_t tid;
};

typedef struct ToxWindow ToxWindow;
typedef struct StatusBar StatusBar;
typedef struct PromptBuf PromptBuf;
typedef struct ChatContext ChatContext;
typedef struct Help Help;

#ifdef GAMES
typedef struct GameData GameData;
#endif

struct ToxWindow {
    bool(*onKey)(ToxWindow *, Toxic *, wint_t, bool);
    void(*onDraw)(ToxWindow *, Toxic *);
    void(*onInit)(ToxWindow *, Toxic *);
    void(*onNickRefresh)(ToxWindow *, Toxic *);

    void(*onFriendRequest)(ToxWindow *, Toxic *, const char *, const char *, size_t);
    void(*onFriendAdded)(ToxWindow *, Toxic *, uint32_t, bool);
    void(*onConnectionChange)(ToxWindow *, Toxic *, uint32_t, Tox_Connection);
    void(*onMessage)(ToxWindow *, Toxic *, uint32_t, Tox_Message_Type, const char *, size_t);
    void(*onNickChange)(ToxWindow *, Toxic *, uint32_t, const char *, size_t);
    void(*onStatusChange)(ToxWindow *, Toxic *, uint32_t, Tox_User_Status);
    void(*onStatusMessageChange)(ToxWindow *, uint32_t, const char *, size_t);
    void(*onConferenceMessage)(ToxWindow *, Toxic *, uint32_t, uint32_t, Tox_Message_Type, const char *, size_t);
    void(*onConferenceInvite)(ToxWindow *, Toxic *, int32_t, uint8_t, const char *, uint16_t);
    void(*onConferenceNameListChange)(ToxWindow *, Toxic *, uint32_t);
    void(*onConferencePeerNameChange)(ToxWindow *, Toxic *, uint32_t, uint32_t, const char *, size_t);
    void(*onConferenceTitleChange)(ToxWindow *, Toxic *, uint32_t, uint32_t, const char *, size_t);
    void(*onFileChunkRequest)(ToxWindow *, Toxic *, uint32_t, uint32_t, uint64_t, size_t);
    void(*onFileRecvChunk)(ToxWindow *, Toxic *, uint32_t, uint32_t, uint64_t, const char *, size_t);
    void(*onFileControl)(ToxWindow *, Toxic *, uint32_t, uint32_t, Tox_File_Control);
    void(*onFileRecv)(ToxWindow *, Toxic *, uint32_t, uint32_t, uint64_t, const char *, size_t);
    void(*onTypingChange)(ToxWindow *, Toxic *, uint32_t, bool);
    void(*onReadReceipt)(ToxWindow *, Toxic *, uint32_t, uint32_t);

#ifdef GAMES
    void(*onGameInvite)(ToxWindow *, Toxic *, uint32_t, const uint8_t *, size_t);
    void(*onGameData)(ToxWindow *, Toxic *, uint32_t, const uint8_t *, size_t);
#endif // GAMES

    void(*onGroupInvite)(ToxWindow *, Toxic *, uint32_t, const char *, size_t, const char *, size_t);
    void(*onGroupMessage)(ToxWindow *, Toxic *, uint32_t, uint32_t, Tox_Message_Type, const char *, size_t);
    void(*onGroupPrivateMessage)(ToxWindow *, Toxic *, uint32_t, uint32_t, const char *, size_t);
    void(*onGroupPeerJoin)(ToxWindow *, Toxic *, uint32_t, uint32_t);
    void(*onGroupPeerExit)(ToxWindow *, Toxic *, uint32_t, uint32_t, Tox_Group_Exit_Type, const char *, size_t,
                           const char *, size_t);
    void(*onGroupNickChange)(ToxWindow *, Toxic *, uint32_t, uint32_t, const char *, size_t);
    void(*onGroupStatusChange)(ToxWindow *, Toxic *, uint32_t, uint32_t, Tox_User_Status);
    void(*onGroupTopicChange)(ToxWindow *, Toxic *, uint32_t, uint32_t, const char *, size_t);
    void(*onGroupPeerLimit)(ToxWindow *, Toxic *, uint32_t, uint32_t);
    void(*onGroupPrivacyState)(ToxWindow *, Toxic *, uint32_t, Tox_Group_Privacy_State);
    void(*onGroupTopicLock)(ToxWindow *, Toxic *, uint32_t, Tox_Group_Topic_Lock);
    void(*onGroupPassword)(ToxWindow *, Toxic *, uint32_t, const char *, size_t);
    void(*onGroupSelfJoin)(ToxWindow *, Toxic *, uint32_t);
    void(*onGroupRejected)(ToxWindow *, Toxic *, uint32_t, Tox_Group_Join_Fail);
    void(*onGroupModeration)(ToxWindow *, Toxic *, uint32_t, uint32_t, uint32_t, Tox_Group_Mod_Event);
    void(*onGroupVoiceState)(ToxWindow *, Toxic *, uint32_t, Tox_Group_Voice_State);

#ifdef AUDIO

    void(*onInvite)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onRinging)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onStarting)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onError)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onStart)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onCancel)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onReject)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onEnd)(ToxWindow *, Toxic *, uint32_t, int);
    void(*onWriteDevice)(ToxWindow *, Toxic *, uint32_t, int, const int16_t *, unsigned int, uint8_t, unsigned int);

    bool is_call;
    int ringing_sound;

#endif /* AUDIO */

    int active_box; /* For box notify */

    char name[TOXIC_MAX_NAME_LENGTH + 1];
    int colour;  /* The ncurses colour pair of the window name */
    uint32_t num;    /* corresponds to friendnumber in chat windows */
    uint16_t id; /* a unique and permanent identifier for this window */
    bool scroll_pause; /* true if this window is not scrolled to the bottom */
    unsigned int pending_messages;  /* # of new messages in this window since the last time it was focused */
    int x;

    Window_Type type;

    int show_peerlist;    /* used to toggle conference peerlist */

    WINDOW_ALERTS alert;

    ChatContext *chatwin;
    StatusBar *stb;
    Help *help;

#ifdef GAMES
    GameData *game;
#endif

    WINDOW *window;
    WINDOW *window_bar;
};

/* statusbar info holder */
struct StatusBar {
    WINDOW *topline;
    char statusmsg[TOX_MAX_STATUS_MESSAGE_LENGTH + 1];
    size_t statusmsg_len;
    char nick[TOXIC_MAX_NAME_LENGTH + 1];
    size_t nick_len;
    Tox_User_Status status;
    Tox_Connection connection;
    char topic[TOX_GROUP_MAX_TOPIC_LENGTH + 1];
    size_t topic_len;

    /* network info */
    uint64_t up_bytes;
    uint64_t down_bytes;
    uint64_t time_last_refreshed;
    char network_info[MAX_STR_SIZE];
};

#ifdef AUDIO

#define INFOBOX_HEIGHT 7
#define INFOBOX_WIDTH 21

/* holds display info for audio calls */
struct infobox {
    float vad_lvl;
    bool in_is_muted;
    bool out_is_muted;
    bool hide;
    bool active;

    time_t lastupdate;
    time_t starttime;
    char timestr[TIME_STR_SIZE];

    WINDOW *win;
};

#endif /* AUDIO */

#define MAX_LINE_HIST 128

/* chat and conference window/buffer holder */
struct ChatContext {
    wchar_t line[MAX_STR_SIZE];
    int pos;
    int len;
    int start;    /* the position to start printing line at */

    wchar_t ln_history[MAX_LINE_HIST][MAX_STR_SIZE];  /* history for input lines/commands */
    int hst_pos;
    int hst_tot;

    wchar_t yank[MAX_STR_SIZE];    /* contains last killed/discarded line */
    int yank_len;

    struct history *hst;
    struct chatlog *log;
    struct chat_queue *cqueue;

#ifdef AUDIO
    struct infobox infobox;
#endif

    uint8_t self_is_typing;
    uint8_t pastemode; /* whether to translate \r to \n */

    WINDOW *history;
    WINDOW *linewin;
    WINDOW *sidebar;
};

struct Help {
    WINDOW *win;
    int type;
    bool active;
};

void init_windows(Toxic *toxic);
void draw_active_window(Toxic *toxic);
void del_window(ToxWindow *w, Windows *windows, const Client_Config *c_config);
void kill_all_windows(Toxic *toxic);    /* should only be called on shutdown */
void on_window_resize(Windows *windows);
void force_refresh(WINDOW *w);
ToxWindow *get_window_pointer_by_id(Windows *windows, uint16_t id);
ToxWindow *get_active_window(const Windows *windows);
void draw_window_bar(ToxWindow *self, Windows *windows);

/*
 * Initializes window `w` and adds it to the windows list.
 *
 * Returns the window's unique ID on success.
 * Returns -1 on failure.
 */
int add_window(Toxic *toxic, ToxWindow *w);

/*
 * Sets the active window to the window associated with `id`.
 */
void set_active_window_by_id(Windows *windows, uint16_t id);

/*
 * Sets the active window to the first found window of window type `type`.
 */
void set_active_window_by_type(Windows *windows, Window_Type type);

/*
 * Enables and disables the log associated the ToxWindow of type `type` and with
 * num `number`.
 *
 * Returns true on success.
 */
bool enable_window_log_by_number_type(Windows *windows, uint32_t number, Window_Type type);
bool disable_window_log_by_number_type(Windows *windows, uint32_t number, Window_Type type);

/*
 * Returns a pointer to the ToxWindow of type `type` and with num `number`.
 * Returns NULL if a window with those parameters doesn't exist.
 */
ToxWindow *get_window_by_number_type(Windows *windows, uint32_t number, Window_Type type);

/* Returns the number of active windows of given type. */
uint16_t get_num_active_windows_type(const Windows *windows, Window_Type type);

/* refresh inactive windows to prevent scrolling bugs.
   call at least once per second */
void refresh_inactive_windows(Windows *windows, const Client_Config *c_config);

/*
 * Updates the friend name associated with the given window. Called
 * after config changes.
 */
void refresh_window_names(Toxic *toxic);

#endif // WINDOWS_H
