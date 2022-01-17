HEADERS = $(wildcard *.h)
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
DEPFILES = $(SOURCES:.c=.d)

EXEPROGNAME = exe32-linux
EXEPROGVER = 1b
BASE_PATH = kmc/gcc/mipse/bin

CFLAGS = -m32 -Wall -Wextra -DEXEPROGNAME=\"$(EXEPROGNAME)\" -DEXEPROGVER="\"$(EXEPROGVER)\""
LDFLAGS = -m32

ifneq ($(NOBASEPATH), 1)
CFLAGS += -DDEFAULT_BASE_PATH=\"$(BASE_PATH)/\"
endif

ifeq ($(DEBUG), 1)
CFLAGS += -g

ifeq ($(WITH_LOG_FILE), 1)
CFLAGS += -DLOG_FILE=\"log.txt\"
endif

else
CFLAGS += -DNDEBUG -O2
endif

all: $(EXEPROGNAME)

clean: clean-symlinks
	rm -f $(OBJECTS) $(DEPFILES) $(EXEPROGNAME)

%.o: %.c
	@$(CC) -MM -MMD -MP -MF"$*.d" -c $(CFLAGS) -o $@ $<
	$(CC) -c $(CFLAGS) -o $@ $<

$(EXEPROGNAME): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

wp_progs = $(wildcard $(BASE_PATH)/*.out)

symlinks: $(EXEPROGNAME)
	for f in $(basename $(notdir $(wp_progs))); do ln -s $< $$f; done

clean-symlinks:
	rm -f $(basename $(notdir $(wp_progs)))

.PHONY: all clean

-include $(DEPFILES)
