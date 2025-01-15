BASE_DIR = $(shell pwd -P)
CFG_DIR = $(BASE_DIR)/cfg

-include $(CFG_DIR)/global_vars.mk

LIBS = toxcore ncursesw libconfig libcurl

CFLAGS ?= -std=c11 -pthread -Wall -Wpedantic -Wunused -fstack-protector-all -Wvla -Wno-missing-braces -Wmissing-prototypes -Wcast-align -Wcast-qual -Wmissing-declarations -Wshadow -Wunused-macros -Wformat-nonliteral -Wno-unknown-pragmas
CFLAGS += '-DTOXICVER="$(VERSION)"' -DHAVE_WIDECHAR -D_XOPEN_SOURCE_EXTENDED -D_FILE_OFFSET_BITS=64
CFLAGS += '-DPACKAGE_DATADIR="$(abspath $(DATADIR))"'
CFLAGS += ${USER_CFLAGS}
LDFLAGS ?=
LDFLAGS += ${USER_LDFLAGS}

OBJ = autocomplete.o avatars.o bootstrap.o chat.o chat_commands.o conference.o configdir.o curl_util.o execute.o
OBJ += file_transfers.o friendlist.o global_commands.o conference_commands.o groupchats.o groupchat_commands.o help.o
OBJ += init_queue.o input.o line_info.o log.o main.o message_queue.o misc_tools.o name_lookup.o netprof.o notify.o prompt.o qr_code.o
OBJ += settings.o term_mplex.o toxic.o toxic_strings.o windows.o

# Check if debug build is enabled
RELEASE := $(shell if [ -z "$(ENABLE_RELEASE)" ] || [ "$(ENABLE_RELEASE)" = "0" ] ; then echo disabled ; else echo enabled ; fi)
ifneq ($(RELEASE), enabled)
	CFLAGS += -O0 -g -DDEBUG
	LDFLAGS += -O0
else
	CFLAGS += -O2 -flto
	LDFLAGS += -O2 -flto
endif

# Check if LLVM Address Sanitizer is enabled
ASAN := $(shell if [ -z "$(ENABLE_ASAN)" ] || [ "$(ENABLE_ASAN)" = "0" ] ; then echo disabled ; else echo enabled ; fi)
ifneq ($(ASAN), disabled)
	CFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

# Check on wich system we are running
UNAME_S = $(shell uname -s)
ifeq ($(UNAME_S), Linux)
LDFLAGS += -ldl -lrt
endif
ifeq ($(UNAME_S), OpenBSD)
LIBS := $(filter-out ncursesw, $(LIBS))
LDFLAGS += -lncursesw
endif
ifeq ($(UNAME_S), NetBSD)
LIBS := $(filter-out ncursesw, $(LIBS))
LDFLAGS += -lncursesw
endif
ifeq ($(UNAME_S), Darwin)
    -include $(CFG_DIR)/systems/Darwin.mk
endif

# Check on which platform we are running
UNAME_M = $(shell uname -m)
ifeq ($(UNAME_M), x86_64)
    -include $(CFG_DIR)/platforms/x86_64.mk
endif
ifneq ($(filter %86, $(UNAME_M)),)
    -include $(CFG_DIR)/platforms/x86.mk
endif
ifneq ($(filter arm%, $(UNAME_M)),)
    -include $(CFG_DIR)/platforms/arm.mk
endif

# Include all needed checks
-include $(CFG_DIR)/checks/check_features.mk

# Fix path for object files
OBJ := $(addprefix $(BUILD_DIR)/, $(OBJ))

# Targets
all: $(BUILD_DIR)/toxic

$(BUILD_DIR)/toxic: $(OBJ)
	@echo "  LD    $(@:$(BUILD_DIR)/%=%)"
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/toxic $(OBJ) $(LDFLAGS)

$(BUILD_DIR)/osx_video.o: $(SRC_DIR)/$(OSX_VIDEO)
	@echo "  CC    $(@:$(BUILD_DIR)/)osx_video.o"
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/osx_video.o -c $(SRC_DIR)/$(OSX_VIDEO)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@if [ ! -e $(BUILD_DIR) ]; then \
		mkdir -p $(BUILD_DIR) ;\
	fi
	@echo "  CC    $(@:$(BUILD_DIR)/%=%)"
	@$(CC) $(CFLAGS) -o $(BUILD_DIR)/$*.o -c $(SRC_DIR)/$*.c
	@$(CC) -MM $(CFLAGS) $(SRC_DIR)/$*.c >$(BUILD_DIR)/$*.d

clean:
	rm -f $(BUILD_DIR)/*.d $(BUILD_DIR)/*.o $(BUILD_DIR)/toxic

-include $(BUILD_DIR)/$(OBJ:.o=.d)

-include $(CFG_DIR)/targets/*.mk

.PHONY: clean all
