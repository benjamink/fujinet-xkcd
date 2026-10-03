# mekkogx/toolchains/amigagcc.mk
CC_DEFAULT ?= m68k-amigaos-gcc
AS_DEFAULT ?= m68k-amigaos-as
LD_DEFAULT ?= $(CC_DEFAULT)
AR_DEFAULT ?= m68k-amigaos-ar

include $(MWD)/tc-common.mk

# -mcrt=nix13 keeps executables Kickstart 1.3 compatible and handles Workbench startup.
CFLAGS  += -mcpu=68000 -msoft-float -mcrt=nix13 -Os -Wall -Wextra -std=gnu99 -fomit-frame-pointer
LDFLAGS += -mcpu=68000 -msoft-float -mcrt=nix13 -s

DSTRING_OPEN = '"
DSTRING_CLOSE = "'
CFLAGS += -DGIT_VERSION=$(DSTRING_OPEN)$(GIT_VERSION)$(DSTRING_CLOSE)

define include-dir-flag
  -I$1
endef

define asm-include-dir-flag
  -I$1
endef

define library-dir-flag
  -L$1
endef

define library-flag
  -l$1
endef

define link-lib
  $(AR) rcs $1 $2
endef

define link-bin
  $(LD) $(LDFLAGS) -o $1 $2 $(LIBS)
endef

define compile
  $(CC) -MMD -MP -MF $(1:.o=.d) -c $(CFLAGS) -o $1 $2
endef

define assemble
  $(AS) $(ASFLAGS) -o $1 $2
endef
