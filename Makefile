CC = gcc
CFLAGS := -ggdb3 -O2 -Wall -std=c11
CFLAGS += -Wno-unused-function -Wvla

# Flags for FUSE
LDLIBS := $(shell pkg-config fuse --cflags --libs)

# Name for the filesystem!
FS_NAME := fisopfs

all: build
	
build: $(FS_NAME)

format: .clang-files .clang-format
	xargs -r clang-format -i <$<

clean:
	rm -rf $(EXEC) *.o core vgcore.* $(FS_NAME)

.PHONY: all build clean format
