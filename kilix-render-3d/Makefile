PROJECT := kilix-render-3d
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=
CC ?= cc
AR ?= ar
INSTALL ?= install
CPPFLAGS += -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -fPIC $(WARNINGS) -MMD -MP
LDLIBS += -lm
ENABLE_OPENGL ?= 0
GL_PKG_CONFIG ?= pkg-config
GL_PACKAGES := egl gl
GL_AVAILABLE := $(shell $(GL_PKG_CONFIG) --exists $(GL_PACKAGES) >/dev/null 2>&1 && echo 1 || echo 0)
SOURCES := src/math.c src/renderer.c src/mesh_file.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST := $(BUILD_DIR)/test-render-3d
MESH_TEST := $(BUILD_DIR)/test-mesh-file
PC := $(BUILD_DIR)/$(PROJECT).pc
GL_PC := $(BUILD_DIR)/$(PROJECT)-gl.pc
GL_SOURCES := src/renderer_gl.c
GL_OBJECTS := $(GL_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
GL_LIB := $(BUILD_DIR)/lib$(PROJECT)-gl.a
GL_TEST := $(BUILD_DIR)/test-render-3d-gl
GL_CPPFLAGS := $(shell $(GL_PKG_CONFIG) --cflags $(GL_PACKAGES) 2>/dev/null)
GL_LDLIBS := $(shell $(GL_PKG_CONFIG) --libs $(GL_PACKAGES) 2>/dev/null)
ifeq ($(ENABLE_OPENGL),1)
ifeq ($(GL_AVAILABLE),0)
$(error ENABLE_OPENGL=1 requires pkg-config packages: $(GL_PACKAGES))
endif
ALL_GL_TARGET := $(GL_LIB)
INSTALL_GL_DEPS := $(GL_LIB) $(GL_PC)
endif
.PHONY: all test test-opengl sanitize sanitize-opengl opengl check-opengl clean install
all: $(LIB) $(ALL_GL_TARGET)
$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/%.o: src/%.c include/kr3d.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD_DIR)/renderer_gl.o: src/renderer_gl.c include/kr3d.h include/kr3d_gl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GL_CPPFLAGS) $(CFLAGS) -c $< -o $@
$(GL_LIB): $(GL_OBJECTS)
	$(AR) rcs $@ $^
$(TEST): tests/test_render3d.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@
$(MESH_TEST): tests/test_mesh_file.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@
test: $(TEST) $(MESH_TEST)
	$(TEST)
	$(MESH_TEST)
check-opengl:
	@$(GL_PKG_CONFIG) --exists $(GL_PACKAGES) || { \
		echo "OpenGL backend unavailable: missing pkg-config packages: $(GL_PACKAGES)" >&2; exit 1; }
opengl: check-opengl $(GL_LIB)
$(GL_TEST): tests/test_render3d_gl.c $(LIB) $(GL_LIB)
	$(CC) $(CPPFLAGS) $(GL_CPPFLAGS) $(CFLAGS) $< $(GL_LIB) $(LIB) \
		$(GL_LDLIBS) $(LDLIBS) -o $@
test-opengl: check-opengl $(GL_TEST)
	LIBGL_ALWAYS_SOFTWARE=1 $(GL_TEST)
sanitize:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(SOURCES) tests/test_render3d.c \
		-fsanitize=address,undefined $(LDLIBS) -o $(BUILD_DIR)/test-render-3d-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test-render-3d-sanitize
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(SOURCES) tests/test_mesh_file.c \
		-fsanitize=address,undefined $(LDLIBS) -o $(BUILD_DIR)/test-mesh-file-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test-mesh-file-sanitize
sanitize-opengl: check-opengl
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(GL_CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) \
		$(GL_SOURCES) tests/test_render3d_gl.c -fsanitize=address,undefined \
		$(GL_LDLIBS) $(LDLIBS) -o $(BUILD_DIR)/test-render-3d-gl-sanitize
	LIBGL_ALWAYS_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test-render-3d-gl-sanitize
$(PC): kilix-render-3d.pc.in Makefile | $(BUILD_DIR)
	sed 's|@PREFIX@|$(PREFIX)|g' $< > $@
$(GL_PC): kilix-render-3d-gl.pc.in Makefile | $(BUILD_DIR)
	sed 's|@PREFIX@|$(PREFIX)|g' $< > $@
install: $(LIB) $(PC) $(INSTALL_GL_DEPS)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib/pkgconfig
	$(INSTALL) -m 0644 include/kr3d.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL) -m 0644 $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
ifeq ($(ENABLE_OPENGL),1)
	$(INSTALL) -m 0644 include/kr3d_gl.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(GL_LIB) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL) -m 0644 $(GL_PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
endif
clean:
	rm -rf $(BUILD_DIR)
-include $(OBJECTS:.o=.d) $(GL_OBJECTS:.o=.d)
