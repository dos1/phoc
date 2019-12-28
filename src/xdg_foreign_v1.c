/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-xdg-foreign-v1"

#include "config.h"

#include <xdg-foreign-unstable-v1-protocol.h>
#include "desktop.h"
#include "server.h"
#include "util.h"
#include "view.h"

#define PHOC_XDG_FOREIGN_HANDLE_LENGTH 32

typedef struct _PhocXdgExportedV1 {
  struct wl_resource *resource;
  struct roots_view  *view;
  PhocXdgForeignV1   *xdg_foreign;
  char *handle;
} PhocXdgExportedV1;

static PhocXdgExportedV1 *phoc_xdg_exported_from_resource(struct wl_resource *resource);

static void
handle_exported_destroy(struct wl_client *client,
			struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}


static const struct zxdg_exported_v1_interface zxdg_exported_v1_impl = {
  handle_exported_destroy,
};

static void
xdg_exported_destroy (PhocXdgExportedV1 *xdg_exported)
{
  PhocXdgForeignV1 *xdg_foreign = xdg_exported->xdg_foreign;

#if 0
  while (exported->imported) {
    MetaWaylandXdgImported *imported = exported->imported->data;

      zxdg_imported_v1_send_destroyed (imported->resource);
      meta_wayland_xdg_imported_destroy (imported);
  }


  g_clear_signal_handler (&exported->surface_unmapped_handler_id,
                          exported->surface);
#endif

  wl_resource_set_user_data (xdg_exported->resource, NULL);

  g_hash_table_remove (xdg_foreign->exported_surfaces, xdg_exported->handle);

  g_free (xdg_exported->handle);
  g_free (xdg_exported);
}

static void
handle_exported_resource_destroy(struct wl_resource *resource)
{
  PhocXdgExportedV1 *xdg_exported = phoc_xdg_exported_from_resource (resource);

  g_debug ("Destroying exported resource %p (res %p)", xdg_exported, resource);
  if (xdg_exported)
    xdg_exported_destroy (xdg_exported);
}

static PhocXdgExportedV1 *
phoc_xdg_exported_from_resource(struct wl_resource *resource)
{
  g_assert(wl_resource_instance_of(resource, &zxdg_exported_v1_interface,
				   &zxdg_exported_v1_impl));
  return wl_resource_get_user_data(resource);
}

static void
hafndle_imported_destroy(struct wl_client *client,
			 struct wl_resource *resource)
{
  g_warning("%s: %d", __func__, __LINE__);
}

static void
handle_imported_set_parent_of(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *surface)
{
  g_warning("%s: %d", __func__, __LINE__);
}

static const struct zxdg_imported_v1_interface zxdg_imported_v1_impl = {
  hafndle_imported_destroy,
  handle_imported_set_parent_of,
};

static void
handle_exporter_destroy(struct wl_client *client,
			struct wl_resource *resource)
{
  g_warning("%s: %d", __func__, __LINE__);

}
static void
handle_exporter_export(struct wl_client *client,
		       struct wl_resource *resource,
		       uint32_t id,
		       struct wl_resource *surface_resource)
{

  PhocXdgForeignV1 *xdg_foreign = phoc_xdg_foreign_v1_from_resource (resource);
  PhocXdgExportedV1 *exported;
  struct wlr_surface *surface = wlr_surface_from_resource (surface_resource);
  struct roots_view *view = surface->data;
  struct wl_resource *xdg_exported_resource;
  gchar *handle;

  g_debug("%s: Shall export surface %p (res %p)", __func__, surface, resource);
  /* FIXME: typecheck when g_object */
  g_return_if_fail (view);

  if (view->type != ROOTS_XDG_SHELL_VIEW &&
      view->type != ROOTS_XDG_SHELL_V6_VIEW) {

    wl_resource_post_error (resource,
			    WL_DISPLAY_ERROR_INVALID_OBJECT,
			    "exported surface has an invalid type");
    return;
  }

  /* FIXME: use virtual funcs (or better clean up views) */
  switch (view->type) {
  case ROOTS_XDG_SHELL_VIEW: {
    struct roots_xdg_surface *xdg_surface =
      roots_xdg_surface_from_view(view);
    if (xdg_surface->xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
      wl_resource_post_error (resource,
			      WL_DISPLAY_ERROR_INVALID_OBJECT,
			      "exported surface has an invalid role");
      return;
    }
    break;
  }
  case ROOTS_XDG_SHELL_V6_VIEW: {
    struct roots_xdg_surface_v6 *xdg_v6_surface =
      roots_xdg_surface_v6_from_view(view);
    if (xdg_v6_surface->xdg_surface_v6->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
      wl_resource_post_error (resource,
			      WL_DISPLAY_ERROR_INVALID_OBJECT,
			      "exported surface has an invalid role");
      return;
    }
    break;
  }
  case ROOTS_XWAYLAND_VIEW:
  default:
    g_assert_not_reached ();
  }

  xdg_exported_resource =
    wl_resource_create (client,
                        &zxdg_exported_v1_interface,
                        wl_resource_get_version (resource),
                        id);
  if (!xdg_exported_resource) {
    wl_client_post_no_memory (client);
    return;
  }

  exported = g_new0 (PhocXdgExportedV1, 1);
  exported->xdg_foreign = xdg_foreign;
  exported->view = view;
  exported->resource = xdg_exported_resource;

#if 0
  exported->surface_unmapped_handler_id =
    g_signal_connect (surface, "unmapped",
                      G_CALLBACK (exported_surface_unmapped),
                      exported);
#endif

  wl_resource_set_implementation (xdg_exported_resource,
                                  &zxdg_exported_v1_impl,
                                  exported,
                                  handle_exported_resource_destroy);

  while (TRUE) {
    handle = phoc_generate_random_id (xdg_foreign->rand,
				      PHOC_XDG_FOREIGN_HANDLE_LENGTH);

    if (!g_hash_table_contains (xdg_foreign->exported_surfaces, handle)) {
      g_hash_table_insert (xdg_foreign->exported_surfaces, handle, exported);
      break;
    }

    g_free (handle);
  }

  g_debug("%s: Exported %p (res %p)", __func__, exported, xdg_exported_resource);
  exported->handle = handle;
  zxdg_exported_v1_send_handle (xdg_exported_resource, handle);
}

static const struct zxdg_exporter_v1_interface zxdg_exporter_v1_impl = {
  handle_exporter_destroy,
  handle_exporter_export,
};

static void
xdg_exporter_bind (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  PhocXdgForeignV1 *xdg_foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_exporter_v1_interface,
                                 1,
                                 id);

  if (resource == NULL) {
    wl_client_post_no_memory (client);
    return;
  }

  wl_resource_set_implementation (resource,
                                  &zxdg_exporter_v1_impl,
                                  xdg_foreign, NULL);
}

static void
handle_importer_destroy(struct wl_client *client,
			struct wl_resource *resource)
{
  g_warning("%s: %d", __func__, __LINE__);
}

static void
handle_importer_import(struct wl_client *client,
		       struct wl_resource *resource,
		       uint32_t id,
		       const char *handle)
{
  g_warning("%s: %d", __func__, __LINE__);
};

static const struct zxdg_importer_v1_interface zxdg_importer_v1_impl = {
  handle_importer_destroy,
  handle_importer_import,
};

static void
xdg_importer_bind (struct wl_client *client,
                   void             *data,
                   uint32_t          version,
                   uint32_t          id)
{
  PhocXdgForeignV1 *xdg_foreign = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &zxdg_importer_v1_interface,
                                 1,
                                 id);

  if (resource == NULL) {
      wl_client_post_no_memory (client);
      return;
  }

  wl_resource_set_implementation (resource,
                                  &zxdg_importer_v1_impl,
                                  xdg_foreign, NULL);
}

PhocXdgForeignV1*
phoc_xdg_foreign_v1_create(struct wl_display *display)
{
  PhocXdgForeignV1 *xdg_foreign = g_new0(PhocXdgForeignV1, 1);
  if (!xdg_foreign)
    return NULL;

  xdg_foreign->rand = g_rand_new ();
  xdg_foreign->exported_surfaces = g_hash_table_new ((GHashFunc) g_str_hash,
						     (GEqualFunc) g_str_equal);

  xdg_foreign->exporter = wl_global_create(display,
					   &zxdg_exporter_v1_interface, 1,
					   xdg_foreign,
					   xdg_exporter_bind);
  if (!xdg_foreign->exporter)
    return NULL;

  xdg_foreign->importer = wl_global_create(display,
					   &zxdg_importer_v1_interface, 1,
					   xdg_foreign,
					   xdg_importer_bind);
  if (!xdg_foreign->importer) {
    wl_global_destroy(xdg_foreign->exporter);
    return NULL;
  }

  g_info ("Initialized xdg-foreign interface");
  return xdg_foreign;
}

void
phoc_xdg_foreign_v1_destroy(PhocXdgForeignV1 *xdg_foreign)
{
  wl_global_destroy(xdg_foreign->exporter);
  wl_global_destroy(xdg_foreign->importer);
}

PhocXdgForeignV1 *
phoc_xdg_foreign_v1_from_resource(struct wl_resource *resource)
{
  g_assert(wl_resource_instance_of(resource, &zxdg_exporter_v1_interface,
				   &zxdg_exporter_v1_impl) ||
	   wl_resource_instance_of(resource, &zxdg_importer_v1_interface,
				   &zxdg_importer_v1_impl));
  return wl_resource_get_user_data(resource);
}
