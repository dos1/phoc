/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once
#include <wlr/types/wlr_layer_shell_v1.h>

#define PHOSH_PRIVATE_XDG_SWITCHER_SINCE_VERSION 2

struct phosh_private {
  struct wl_resource* resource;
  struct wl_global *global;
  struct wl_list xdg_switchers; // phosh_private_xdg_switchers::link

  PhocDesktop *desktop;
  struct {
    struct wl_listener layer_shell_new_surface;
    struct wl_listener panel_surface_destroy;
  } listeners;
  struct wlr_layer_surface_v1 *panel;
  struct wl_list apps;
};


struct phosh_private_xdg_switcher {
  struct wl_list link;
  struct wl_resource *resource;
  struct phosh_private *phosh;

  struct {
    struct wl_signal destroy;
  } events;
};


struct phosh_private_screencopy_frame {
  struct wl_resource *resource, *toplevel;
  struct phosh_private *phosh;
  struct wl_listener view_destroy;

  enum wl_shm_format format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;

  struct wl_shm_buffer *buffer;
  struct roots_view *view;
};

struct phosh_private* phosh_create(PhocDesktop *desktop,
				   struct wl_display *display);
void phosh_destroy(struct phosh_private *shell);
struct phosh_private *phosh_private_from_resource(struct wl_resource *resource);
struct phosh_private_xdg_switcher *phosh_private_xdg_switcher_from_resource(struct wl_resource *resource);
struct phosh_private_screencopy_frame *phosh_private_screencopy_frame_from_resource(struct wl_resource *resource);
bool   phosh_forward_keysym (xkb_keysym_t keysym);
