/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _PhocXdgForeignV1 {
  struct wl_global *exporter;
  struct wl_global *importer;

  GRand *rand;
  GHashTable *exported_surfaces;
} PhocXdgForeignV1;

PhocXdgForeignV1 *phoc_xdg_foreign_v1_create (struct wl_display *display);
void phoc_xdg_foreign_v1_destroy (PhocXdgForeignV1 *xdg_foreign);
PhocXdgForeignV1 *phoc_xdg_foreign_v1_from_resource(struct wl_resource *resource);

G_END_DECLS
