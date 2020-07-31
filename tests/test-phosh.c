/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#include "testlib.h"

typedef struct _PhocTestXdgToplevelSurface
{
  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  PhocTestForeignToplevel *foreign_toplevel;
  char* title;
  PhocTestBuffer buffer;
  guint32 width, height;
  gboolean configured;
  gboolean toplevel_configured;
} PhocTestXdgToplevelSurface;

typedef struct _PhocTestThumbnail
{
  char* title;
  PhocTestBuffer buffer;
  guint32 width, height;
} PhocTestThumbnail;

typedef struct _PhocTestKeyboardEvent
{
  char *title;
  struct phosh_private_keyboard_event *kbevent;
  gboolean test_grab_successful;
  gboolean test_grab_unsuccesful;
} PhocTestKeyboardEvent;

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
			     uint32_t serial)
{
  PhocTestXdgToplevelSurface *xs = data;

  g_debug ("Configured %p serial %d", xdg_surface, serial);
  xdg_surface_ack_configure(xs->xdg_surface, serial);
  xs->configured = TRUE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  xdg_surface_handle_configure,
};

#define WIDTH 100
#define HEIGHT 200

static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
  PhocTestXdgToplevelSurface *xs = data;

  g_debug ("Configured %p, size: %dx%d", xdg_toplevel, width, height);
  g_assert_nonnull (xdg_toplevel);

  xs->width = width ?: WIDTH;
  xs->height = height ?: HEIGHT;
  xs->toplevel_configured = TRUE;
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_surface)
{
  /* TBD */
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  xdg_toplevel_handle_configure,
  xdg_toplevel_handle_close,
};

static void
phoc_test_xdg_surface_free (PhocTestXdgToplevelSurface *xs)
{
  xdg_toplevel_destroy (xs->xdg_toplevel);
  xdg_surface_destroy (xs->xdg_surface);
  wl_surface_destroy (xs->wl_surface);
  phoc_test_buffer_free (&xs->buffer);
  g_free (xs);
}

static PhocTestXdgToplevelSurface *
phoc_test_xdg_surface_new (PhocTestClientGlobals *globals,
			   guint32 width, guint32 height, char* title, guint32 color)
{
  PhocTestXdgToplevelSurface *xs = g_malloc0 (sizeof(PhocTestXdgToplevelSurface));

  xs->wl_surface = wl_compositor_create_surface (globals->compositor);
  g_assert_nonnull (xs->wl_surface);

  xs->xdg_surface = xdg_wm_base_get_xdg_surface (globals->xdg_shell,
						 xs->wl_surface);
  g_assert_nonnull (xs->wl_surface);
  xdg_surface_add_listener (xs->xdg_surface,
			    &xdg_surface_listener, xs);
  xs->xdg_toplevel = xdg_surface_get_toplevel (xs->xdg_surface);
  g_assert_nonnull (xs->xdg_toplevel);
  xdg_toplevel_add_listener (xs->xdg_toplevel,
			     &xdg_toplevel_listener, xs);
  xdg_toplevel_set_min_size (xs->xdg_toplevel, WIDTH, HEIGHT);
  xs->title = title;
  xdg_toplevel_set_title (xs->xdg_toplevel, xs->title);

  wl_surface_commit (xs->wl_surface);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_true (xs->configured);
  g_assert_true (xs->toplevel_configured);

  phoc_test_client_create_shm_buffer (globals, &xs->buffer, xs->width, xs->height,
				      WL_SHM_FORMAT_XRGB8888);

  for (int i = 0; i < xs->width * xs->height * 4; i+=4)
    *(guint32*)(xs->buffer.shm_data + i) = color;

  wl_surface_attach (xs->wl_surface, xs->buffer.wl_buffer, 0, 0);
  wl_surface_damage (xs->wl_surface, 0, 0, xs->width, xs->height);
  wl_surface_commit (xs->wl_surface);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  xs->foreign_toplevel = phoc_test_client_get_foreign_toplevel_handle (globals, title);
  g_assert_true (xs->foreign_toplevel);

  return xs;
}

static PhocTestScreencopyFrame *
phoc_test_get_thumbnail (PhocTestClientGlobals *globals,
			   guint32 max_width, guint32 max_height, PhocTestForeignToplevel *toplevel)
{
  PhocTestScreencopyFrame *thumbnail = g_malloc0 (sizeof(PhocTestScreencopyFrame));

  struct zwlr_screencopy_frame_v1 *handle = phosh_private_get_thumbnail (globals->phosh, toplevel->handle, max_width, max_height);
  phoc_test_client_capture_frame (globals, thumbnail, handle);

  return thumbnail;
}

static void
phoc_test_thumbnail_free (PhocTestScreencopyFrame *frame)
{
  phoc_test_buffer_free (&frame->buffer);
  zwlr_screencopy_frame_v1_destroy (frame->handle);
  g_free (frame);
}

static gboolean
test_client_phosh_thumbnail_simple (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestXdgToplevelSurface *toplevel_green;
  PhocTestScreencopyFrame *green_thumbnail;

  toplevel_green = phoc_test_xdg_surface_new (globals, WIDTH, HEIGHT, "green", 0xFF00FF00);
  g_assert_nonnull (toplevel_green);
  phoc_assert_screenshot (globals, "test-phosh-thumbnail-simple-1.png");

  green_thumbnail = phoc_test_get_thumbnail (globals, toplevel_green->width, toplevel_green->height, toplevel_green->foreign_toplevel);
  phoc_assert_buffer_equal (&toplevel_green->buffer, &green_thumbnail->buffer);
  phoc_test_thumbnail_free (green_thumbnail);

  phoc_test_xdg_surface_free (toplevel_green);
  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}

static void
test_phosh_thumbnail_simple (void)
{
  PhocTestClientIface iface = {
   .client_run = test_client_phosh_thumbnail_simple,
  };

  phoc_test_client_run (3, &iface, GINT_TO_POINTER (FALSE));
}

static void
keyboard_event_handle_grab_failed (void *data,
                                   struct phosh_private_keyboard_event *kbevent,
                                   const char *accelerator,
                                   uint32_t error)
{
  PhocTestKeyboardEvent *kbe = data;

  g_assert_nonnull (kbevent);
  g_assert (kbe->kbevent == kbevent);

  kbe->test_grab_unsuccesful = TRUE;
}

static void
keyboard_event_handle_grab_success (void *data,
                                   struct phosh_private_keyboard_event *kbevent,
                                   const char *accelerator,
                                   uint32_t action_id)
{
  PhocTestKeyboardEvent *kbe = data;

  g_assert_nonnull (kbevent);
  g_assert (kbe->kbevent == kbevent);

  if (action_id > 0)
    kbe->test_grab_successful = TRUE;
}

static const struct phosh_private_keyboard_event_listener keyboard_event_listener = {
   .grab_failed_event = keyboard_event_handle_grab_failed,
   .grab_success_event = keyboard_event_handle_grab_success,
};

static PhocTestKeyboardEvent *
phoc_test_keyboard_event_new (PhocTestClientGlobals *globals,
                              char* title)
{
  PhocTestKeyboardEvent *kbe = g_malloc0 (sizeof (PhocTestKeyboardEvent));

  g_assert (phosh_private_get_version (globals->phosh) >= 5);

  kbe->kbevent = phosh_private_get_keyboard_event (globals->phosh);
  kbe->title = title;

  phosh_private_keyboard_event_add_listener (kbe->kbevent, &keyboard_event_listener, kbe);

  return kbe;
}

static gboolean
test_client_phosh_kbevent_simple (PhocTestClientGlobals *globals, gpointer unused)
{
  PhocTestKeyboardEvent *test1;
  PhocTestKeyboardEvent *test2;
  gchar *test_accelerators[] = {
    "XF86AudioLowerVolume",
    "XF86AudioRaiseVolume",
    "<SHIFT>XF86AudioLowerVolume",
    "F9",
  };

  test1 = phoc_test_keyboard_event_new (globals, "test-mediakey-grabbing");
  test2 = phoc_test_keyboard_event_new (globals, "test-invalid-grabbing");

  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent, test_accelerators[0]);
  phosh_private_keyboard_event_grab_accelerator_request (test2->kbevent, test_accelerators[3]);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_true (test1->test_grab_successful);
  g_assert_true (test2->test_grab_unsuccesful);

  test1->test_grab_successful = FALSE;
  test2->test_grab_unsuccesful = FALSE;

  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent, test_accelerators[1]);
  phosh_private_keyboard_event_grab_accelerator_request (test2->kbevent, test_accelerators[1]);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_true (test1->test_grab_successful);
  g_assert_true (test2->test_grab_unsuccesful);

  test1->test_grab_successful = FALSE;

  phosh_private_keyboard_event_grab_accelerator_request (test1->kbevent, test_accelerators[2]);
  wl_display_dispatch (globals->display);
  wl_display_roundtrip (globals->display);

  g_assert_true (test1->test_grab_successful);

  phosh_private_keyboard_event_destroy (test1->kbevent);
  phosh_private_keyboard_event_destroy (test2->kbevent);
  return TRUE;
}

static void
test_phosh_kbevents_simple (void)
{
  PhocTestClientIface iface = {
   .client_run = test_client_phosh_kbevent_simple,
  };

  phoc_test_client_run (3, &iface, NULL);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phoc/phosh/thumbnail/simple", test_phosh_thumbnail_simple);
  g_test_add_func ("/phoc/phosh/kbevents/simple", test_phosh_kbevents_simple);
  return g_test_run ();
}
