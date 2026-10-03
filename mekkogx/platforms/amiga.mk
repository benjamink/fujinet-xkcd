# mekkogx/platforms/amiga.mk
EXEC_SUFFIX =
DISK = $(R2R_PD)/$(PRODUCT_BASE).adf
LIBRARY = $(R2R_PD)/lib$(PRODUCT_BASE).$(PLATFORM).a
DISK_LABEL ?= $(PRODUCT_BASE)

MWD := $(realpath $(dir $(lastword $(MAKEFILE_LIST)))..)
include $(MWD)/common.mk
include $(MWD)/toolchains/amigagcc.mk

CFLAGS += -D__AMIGA__

XDFTOOL ?= $(shell command -v xdftool 2>/dev/null || \
             (command -v uvx >/dev/null 2>&1 && echo "uvx --from amitools xdftool"))

r2r:: $(BUILD_DISK) $(BUILD_EXEC) $(BUILD_LIB) $(R2R_EXTRA_DEPS)
	make -f $(PLATFORM_MK) $(PLATFORM)/r2r-post

# Non-bootable OFS disk readable by Kickstart 1.3. DISK_EXTRA_FILES entries are
# "src[:dest]"; dest defaults to the basename.
$(BUILD_DISK): $(DISK_EXECUTABLES) $(DISK_EXTRA_DEPS) $(foreach f,$(DISK_EXTRA_FILES),$(firstword $(subst :, ,$(f)))) | $(R2R_PD)
	@if [ -z "$(XDFTOOL)" ]; then echo "xdftool (amitools) or uvx is required"; exit 1; fi
	$(RM) $@
	$(XDFTOOL) $@ create + format $(DISK_LABEL) ofs \
	  $(foreach e,$(DISK_EXECUTABLES),+ write $(e) $(notdir $(e)))
	$(foreach f,$(DISK_EXTRA_FILES),$(call copy-to-disk,,$(firstword $(subst :, ,$(f))),$(or $(word 2,$(subst :, ,$(f))),$(notdir $(f))),$@);)
	@make -f $(PLATFORM_MK) $(PLATFORM)/disk-post

# $1 flags (unused) $2 source $3 destination name $4 disk image
define copy-to-disk
    $(XDFTOOL) $4 write $2 $3
endef
