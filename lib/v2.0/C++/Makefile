# Hey Emacs, this is a -*- makefile -*-
#
# Copyright (C) 2019 Fabien Garcia <fabien.garcia@enac.fr>
#
# This file is part of paparazzi.
#
# paparazzi is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# paparazzi is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with paparazzi; see the file COPYING.  If not, see
# <http://www.gnu.org/licenses/>.
#

PKG_CONFIG ?= pkg-config
WITH_IVY ?= 1
CMAKE ?= cmake

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(dir $(mkfile_path))
INCLUDE_DIR=$(DESTDIR)/include
LIB_DIR=$(DESTDIR)/lib
OBJ_DIR ?= $(CURDIR)/build/C++
CONFIG_STAMP = $(OBJ_DIR)/.with-ivy
LLNL_UNITS_DIR ?= $(abspath $(current_dir)/../../../third_party/llnl_units)
LLNL_UNITS_BUILD_DIR ?= $(abspath $(OBJ_DIR))/llnl_units
LLNL_UNITS_ARCHIVE = $(LLNL_UNITS_BUILD_DIR)/lib/libunits.a
LLNL_UNITS_CFLAGS ?= -I"$(LLNL_UNITS_DIR)"
LLNL_UNITS_LIBS ?= "$(LLNL_UNITS_ARCHIVE)"
LLNL_UNITS_INPUTS = $(wildcard $(LLNL_UNITS_DIR)/CMakeLists.txt $(LLNL_UNITS_DIR)/units/*.cpp \
    $(LLNL_UNITS_DIR)/units/*.hpp $(LLNL_UNITS_DIR)/units/CMakeLists.txt $(LLNL_UNITS_DIR)/config/*.cmake*)

PPRZLINK_HEADERS=$(wildcard pprzlink/*.h)
PPRZLINK_SRC=$(wildcard pprzlink/*.cpp)
ifeq ($(WITH_IVY),0)
PPRZLINK_HEADERS:=$(filter-out pprzlink/IvyLink.h,$(PPRZLINK_HEADERS))
PPRZLINK_SRC:=$(filter-out pprzlink/IvyLink.cpp,$(PPRZLINK_SRC))
DEPENDENCIES=tinyxml2
else
DEPENDENCIES='ivy-cpp >= 3.18' tinyxml2
endif
PPRZLINK_OBJS=$(patsubst %.cpp,$(OBJ_DIR)/%.o,$(PPRZLINK_SRC))

DEP_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(DEPENDENCIES))
DEP_LIBS = $(shell $(PKG_CONFIG) --libs $(DEPENDENCIES))

.PHONY: all libpprzlink++ install check-deps FORCE
all: libpprzlink++

FORCE:

$(CONFIG_STAMP): FORCE
	@mkdir -p $(OBJ_DIR)
	@if ! test -f "$@" || ! test "$$(cat "$@")" = "$(WITH_IVY)"; then printf '%s\n' '$(WITH_IVY)' > "$@"; fi

check-deps:
	$(PKG_CONFIG) --print-errors --exists $(DEPENDENCIES)
	@if ! test -f "$(LLNL_UNITS_DIR)/units/units.cpp"; then \
	  printf '%s\n' 'Initialize LLNL/units from the pprzlink root: git submodule update --init third_party/llnl_units'; \
	  exit 1; \
	fi

$(LLNL_UNITS_ARCHIVE): $(LLNL_UNITS_INPUTS) | check-deps
	$(CMAKE) -S "$(LLNL_UNITS_DIR)" -B "$(LLNL_UNITS_BUILD_DIR)" \
	  -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_CXX_STANDARD=14 -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$(LLNL_UNITS_BUILD_DIR)/lib" \
	  -DUNITS_ENABLE_TESTS=OFF -DUNITS_INSTALL=ON -DUNITS_HEADER_ONLY=OFF \
	  -DUNITS_BUILD_STATIC_LIBRARY=ON -DUNITS_BUILD_SHARED_LIBRARY=OFF \
	  -DUNITS_BUILD_CONVERTER_APP=OFF -DUNITS_BUILD_WEBSERVER=OFF
	+$(CMAKE) --build "$(LLNL_UNITS_BUILD_DIR)" --target units

$(OBJ_DIR)/%.o: %.cpp | check-deps
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -I$(current_dir) $(DEP_CFLAGS) $(LLNL_UNITS_CFLAGS) $(CXXFLAGS) -std=c++23 -Wall -Wextra -fPIC -pthread -MMD -MP -c $< -o $@

libpprzlink++: $(OBJ_DIR)/libpprzlink++.so $(OBJ_DIR)/libpprzlink++.a

$(OBJ_DIR)/libpprzlink++.so: $(PPRZLINK_OBJS) $(CONFIG_STAMP) $(LLNL_UNITS_ARCHIVE)
	$(CXX) $(LDFLAGS) -shared -o $@ $(PPRZLINK_OBJS) $(LDLIBS) $(DEP_LIBS) $(LLNL_UNITS_LIBS) -pthread

$(OBJ_DIR)/libpprzlink++.a: $(PPRZLINK_OBJS) $(CONFIG_STAMP) $(LLNL_UNITS_ARCHIVE)
	rm -f $@
	$(AR) rcs $@ $(PPRZLINK_OBJS)

install: libpprzlink++
	env -u DESTDIR $(CMAKE) --install "$(LLNL_UNITS_BUILD_DIR)" --prefix "$(abspath $(DESTDIR)/)"
	mkdir -p $(INCLUDE_DIR)/pprzlink $(LIB_DIR)
	cp $(PPRZLINK_HEADERS) $(INCLUDE_DIR)/pprzlink
	cp -R pprzlink/exceptions $(INCLUDE_DIR)/pprzlink
	mkdir -p $(INCLUDE_DIR)/pprzlink/detail
	cp pprzlink/detail/CheckedNumber.h $(INCLUDE_DIR)/pprzlink/detail
	cp $(OBJ_DIR)/libpprzlink++.* $(LIB_DIR)
	mkdir -p "$(DESTDIR)/share/licenses/pprzlink++/LLNL-units"
	cp "$(LLNL_UNITS_DIR)/LICENSE" "$(LLNL_UNITS_DIR)/NOTICE" "$(DESTDIR)/share/licenses/pprzlink++/LLNL-units"

-include $(PPRZLINK_OBJS:.o=.d)
