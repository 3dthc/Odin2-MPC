# The plugin for armhf, compiled natively with a cross compiler (build.sh runs this in the odin2-mpc-cross Docker
# image: Debian bullseye's arm-linux-gnueabihf gcc and glibc 2.31, the same libraries the kit's arm32v7/gcc:11-bullseye
# build uses, so the plugin loads on MPC OS 2.x and 3.x). Incremental: an object is rebuilt only when its source or
# a header it includes changed (-MMD). Flags are the kit's tools/build_port.sh ones.
#
#   make -f tools/arm.mk -j$(nproc) SOURCES="..." CFLAGS_PORT="..." CFLAGS_ARM="..." PORT=vst SO=odin2.so MV=/mv
CROSS    ?= arm-linux-gnueabihf-
CXX      := $(CROSS)g++
CC       := $(CROSS)gcc
STRIP    := $(CROSS)strip
READELF  := $(CROSS)readelf
OBJDIR   := $(PORT)/build/obj-arm
INC      := -I$(PORT)/build -I$(MV)/wrapper
WARN     := -Wall -Wextra -Wno-unused-parameter
CXXF     := -O2 $(WARN) -fPIC -fvisibility=hidden -std=gnu++11 $(CFLAGS_PORT) $(CFLAGS_ARM) $(INC) -MMD -MP
CF       := -O2 $(WARN) -fPIC -fvisibility=hidden -std=gnu11 $(INC) -MMD -MP
OBJS     := $(patsubst %,$(OBJDIR)/%.o,$(subst /,_,$(SOURCES)))
WRAP     := $(OBJDIR)/vst2_wrap.o
OUT      := $(PORT)/build/$(SO)

all: $(OUT)

# one rule per source (object names flatten the path, as the kit's build does)
define compile
$(OBJDIR)/$(subst /,_,$(1)).o: $(1) | $(OBJDIR)
	@echo "  compile $(1)"
	@$(CXX) $(CXXF) -c $(1) -o $$@
endef
$(foreach s,$(SOURCES),$(eval $(call compile,$(s))))

$(WRAP): $(MV)/wrapper/vst2_wrap.c $(PORT)/build/params.h | $(OBJDIR)
	@echo "  compile vst2_wrap.c"
	@$(CC) $(CF) -c $< -o $@

$(OBJDIR):
	@mkdir -p $@

$(OUT): $(OBJS) $(WRAP)
	@echo "  link $(SO)"
	@$(CXX) -O2 -shared -fPIC -fvisibility=hidden $(OBJS) $(WRAP) -lm -lpthread -Wl,--no-undefined -o $@
	@$(STRIP) $@
	@echo "exported: $$($(READELF) --dyn-syms -W $@ | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{print $$8}' | tr '\n' ' ')"
	@echo "highest glibc: $$($(READELF) -V $@ | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (MPC OS 2.x has 2.32, 3.x 2.39)"

-include $(OBJS:.o=.d) $(WRAP:.o=.d)
