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

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(dir $(mkfile_path))
INCLUDE_DIR=$(DESTDIR)/include
LIB_DIR=$(DESTDIR)/lib
OBJ_DIR ?= $(CURDIR)/build/C++

PPRZLINK_HEADERS=$(wildcard pprzlink/*.h)
PPRZLINK_SRC=$(wildcard pprzlink/*.cpp)
PPRZLINK_OBJS=$(patsubst %.cpp,$(OBJ_DIR)/%.o,$(PPRZLINK_SRC))

IVY_CFLAGS = $(shell $(PKG_CONFIG) --cflags 'ivy-cpp >= 3.18' tinyxml2)
IVY_LIBS = $(shell $(PKG_CONFIG) --libs 'ivy-cpp >= 3.18' tinyxml2)

.PHONY: all libpprzlink++ install check-deps
all: libpprzlink++

check-deps:
	$(PKG_CONFIG) --print-errors --exists 'ivy-cpp >= 3.18' tinyxml2

$(OBJ_DIR)/%.o: %.cpp | check-deps
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -I$(current_dir) $(IVY_CFLAGS) $(CXXFLAGS) -std=c++23 -Wall -Wextra -fPIC -pthread -MMD -MP -c $< -o $@

libpprzlink++: $(OBJ_DIR)/libpprzlink++.so $(OBJ_DIR)/libpprzlink++.a

$(OBJ_DIR)/libpprzlink++.so: $(PPRZLINK_OBJS)
	$(CXX) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS) $(IVY_LIBS) -pthread

$(OBJ_DIR)/libpprzlink++.a: $(PPRZLINK_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

install: libpprzlink++
	mkdir -p $(INCLUDE_DIR)/pprzlink $(LIB_DIR)
	cp $(PPRZLINK_HEADERS) $(INCLUDE_DIR)/pprzlink
	cp -R pprzlink/exceptions $(INCLUDE_DIR)/pprzlink
	cp $(OBJ_DIR)/libpprzlink++.* $(LIB_DIR)

-include $(PPRZLINK_OBJS:.o=.d)
