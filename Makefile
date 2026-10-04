PRODUCT = xkcd
PLATFORMS += amiga

# Portable logic lives in src/, platform code in src/<platform>/.
SRC_DIRS = src src/%PLATFORM%
INCLUDE_DIRS = src

# fujinet-nio-lib is not fujinet-lib, so FUJINET_LIB stays undefined and the
# NIO library is wired in explicitly for amiga. The workspace env.sh exports
# FUJINET_LIB (the 6502 library), so force it back to undefined for common.mk.
override FUJINET_LIB = __UNDEFINED__
ifndef FUJINET_NIO_LIB
  ifdef NIO_WORKSPACE
    FUJINET_NIO_LIB := $(NIO_WORKSPACE)/repos/fujinet-nio-lib
  endif
endif
FUJINET_NIO_DRIVER ?= $(FUJINET_NIO_LIB)/../fujinet-nio-driver
NIO_LIB_A = $(FUJINET_NIO_LIB)/build/fujinet-nio-amiga.a

EXTRA_INCLUDE_AMIGA = $(FUJINET_NIO_LIB)/include $(FUJINET_NIO_DRIVER)/amiga/include
LIBS_EXTRA_AMIGA = $(NIO_LIB_A)
EXECUTABLE_EXTRA_DEPS_AMIGA = $(NIO_LIB_A)
DISK_EXTRA_FILES_AMIGA = amiga/icons/xkcd.info:xkcd.info amiga/icons/Disk.info:Disk.info \
                         amiga/ReadMe.txt:ReadMe amiga/icons/ReadMe.info:ReadMe.info

# Version in the $VER string: the newest v* tag without its "v" (v0.2 -> 0.2),
# or 0.1 before the first tag. The release workflow passes XKCD_VERSION from the
# tag it is publishing, which may not exist yet when it builds.
XKCD_VERSION ?= $(patsubst v%,%,$(shell git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null))
ifeq ($(strip $(XKCD_VERSION)),)
  override XKCD_VERSION := 0.1
endif
CFLAGS_EXTRA_AMIGA += -DXKCD_VERSION='"$(XKCD_VERSION)"'

include mekkogx/toplevel-rules.mk

# Ask fujinet-nio-lib's own build whether its archive is current.
$(NIO_LIB_A):
	@test -f "$(FUJINET_NIO_LIB)/include/fujinet-nio.h" || { echo "Set FUJINET_NIO_LIB or NIO_WORKSPACE"; exit 1; }
	$(MAKE) -C $(FUJINET_NIO_LIB) amiga

# Host-side unit tests for the portable modules in src/.
.PHONY: test
test:
	$(MAKE) -f tests/Makefile.host
