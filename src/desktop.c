#define G_LOG_DOMAIN "phoc-desktop"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_gtk_primary_selection.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#ifdef PHOC_HAS_WLR_OUTPUT_POWER_MANAGEMENT
#include <wlr/types/wlr_output_power_management_v1.h>
#endif
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "layers.h"
#include "seat.h"
#include "server.h"
#include "view.h"
#include "virtual.h"
#include "xcursor.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

enum {
  PROP_0,
  PROP_CONFIG,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE(PhocDesktop, phoc_desktop, G_TYPE_OBJECT);


static void
phoc_desktop_set_property (GObject     *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

  switch (property_id) {
  case PROP_CONFIG:
    self->config = g_value_get_pointer (value);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_desktop_get_property (GObject    *object,
			   guint       property_id,
			   GValue     *value,
			   GParamSpec *pspec)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

  switch (property_id) {
  case PROP_CONFIG:
    g_value_set_pointer (value, self->config);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static bool view_at(struct roots_view *view, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (view->wlr_surface == NULL) {
		return false;
	}

	double view_sx = lx - (view->box.x * view->scale);
	double view_sy = ly - (view->box.y * view->scale);
	rotate_child_position(&view_sx, &view_sy, 0, 0,
		view->box.width, view->box.height, -view->rotation);
	view_sx /= view->scale;
	view_sy /= view->scale;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:;
		struct roots_xdg_surface_v6 *xdg_surface_v6 =
			roots_xdg_surface_v6_from_view(view);
		_surface = wlr_xdg_surface_v6_surface_at(xdg_surface_v6->xdg_surface_v6,
			view_sx, view_sy, &_sx, &_sy);
		break;
	case ROOTS_XDG_SHELL_VIEW:;
		struct roots_xdg_surface *xdg_surface =
			roots_xdg_surface_from_view(view);
		_surface = wlr_xdg_surface_surface_at(xdg_surface->xdg_surface,
			view_sx, view_sy, &_sx, &_sy);
		break;
#ifdef PHOC_XWAYLAND
	case ROOTS_XWAYLAND_VIEW:
		_surface = wlr_surface_surface_at(view->wlr_surface,
			view_sx, view_sy, &_sx, &_sy);
		break;
#endif
	default:
		g_error("Invalid view type %d", view->type);
	}
	if (_surface != NULL) {
		*sx = _sx;
		*sy = _sy;
		*surface = _surface;
		return true;
	}

	if (view_get_deco_part(view, view_sx, view_sy)) {
		*sx = view_sx;
		*sy = view_sy;
		*surface = NULL;
		return true;
	}

	return false;
}

static struct roots_view *desktop_view_at(PhocDesktop *desktop,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy) {
	struct roots_view *view;
	wl_list_for_each(view, &desktop->views, link) {
		if (view_at(view, lx, ly, surface, sx, sy)) {
			return view;
		}
	}
	return NULL;
}

static struct wlr_surface *layer_surface_at(struct roots_output *output,
		struct wl_list *layer, double ox, double oy, double *sx, double *sy) {
	struct roots_layer_surface *roots_surface;

	wl_list_for_each_reverse(roots_surface, layer, link) {
		if (roots_surface->layer_surface->current.exclusive_zone <= 0) {
			continue;
		}

		double _sx = ox - roots_surface->geo.x;
		double _sy = oy - roots_surface->geo.y;

		struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
			roots_surface->layer_surface, _sx, _sy, sx, sy);

		if (sub) {
			return sub;
		}
	}

	wl_list_for_each(roots_surface, layer, link) {
		if (roots_surface->layer_surface->current.exclusive_zone > 0) {
			continue;
		}

		double _sx = ox - roots_surface->geo.x;
		double _sy = oy - roots_surface->geo.y;

		struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
			roots_surface->layer_surface, _sx, _sy, sx, sy);

		if (sub) {
			return sub;
		}
	}

	return NULL;
}

struct wlr_surface *desktop_surface_at(PhocDesktop *desktop,
		double lx, double ly, double *sx, double *sy,
		struct roots_view **view) {
	struct wlr_surface *surface = NULL;
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(desktop->layout, lx, ly);
	struct roots_output *roots_output = NULL;
	double ox = lx, oy = ly;
	if (view) {
		*view = NULL;
	}

	if (wlr_output) {
		roots_output = wlr_output->data;
		wlr_output_layout_output_coords(desktop->layout, wlr_output, &ox, &oy);

		if ((surface = layer_surface_at(roots_output,
					&roots_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
					ox, oy, sx, sy))) {
			return surface;
		}

		struct roots_output *output = wlr_output->data;
		if (output != NULL && output->fullscreen_view != NULL) {

			if (output->force_shell_reveal) {
				if ((surface = layer_surface_at(roots_output,
						&roots_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
						ox, oy, sx, sy))) {
					return surface;
				}
			}

			if (view_at(output->fullscreen_view, lx, ly, &surface, sx, sy)) {
				if (view) {
					*view = output->fullscreen_view;
				}
				return surface;
			} else {
				return NULL;
			}
		}

		if ((surface = layer_surface_at(roots_output,
					&roots_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
					ox, oy, sx, sy))) {
			return surface;
		}
	}

	struct roots_view *_view;
	if ((_view = desktop_view_at(desktop, lx, ly, &surface, sx, sy))) {
		if (view) {
			*view = _view;
		}
		return surface;
	}

	if (wlr_output) {
		if ((surface = layer_surface_at(roots_output,
					&roots_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
					ox, oy, sx, sy))) {
			return surface;
		}
		if ((surface = layer_surface_at(roots_output,
					&roots_output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
					ox, oy, sx, sy))) {
			return surface;
		}
	}
	return NULL;
}

static void handle_layout_change(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop =
		wl_container_of(listener, desktop, layout_change);

	struct wlr_output *center_output =
		wlr_output_layout_get_center_output(desktop->layout);
	if (center_output == NULL) {
		return;
	}

	struct wlr_box *center_output_box =
		wlr_output_layout_get_box(desktop->layout, center_output);
	double center_x = center_output_box->x + center_output_box->width/2;
	double center_y = center_output_box->y + center_output_box->height/2;

	struct roots_view *view;
	wl_list_for_each(view, &desktop->views, link) {
		struct wlr_box box;
		view_get_box(view, &box);

		if (wlr_output_layout_intersects(desktop->layout, NULL, &box)) {
			continue;
		}

		view_move(view, center_x - box.width/2, center_y - box.height/2);
	}
}

static void input_inhibit_activate(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop = wl_container_of(
			listener, desktop, input_inhibit_activate);
	struct roots_seat *seat;
	PhocServer *server = phoc_server_get_default ();

	wl_list_for_each(seat, &server->input->seats, link) {
		roots_seat_set_exclusive_client(seat,
				desktop->input_inhibit->active_client);
	}
}

static void input_inhibit_deactivate(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop = wl_container_of(
			listener, desktop, input_inhibit_deactivate);
	struct roots_seat *seat;
	PhocServer *server = phoc_server_get_default ();

	wl_list_for_each(seat, &server->input->seats, link) {
		roots_seat_set_exclusive_client(seat, NULL);
	}
}

static void handle_constraint_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_pointer_constraint *constraint =
		wl_container_of(listener, constraint, destroy);
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	struct roots_seat *seat = wlr_constraint->seat->data;
	struct roots_cursor *cursor = roots_seat_get_cursor (seat);

	wl_list_remove(&constraint->destroy.link);

	if (cursor->active_constraint == wlr_constraint) {
		wl_list_remove(&cursor->constraint_commit.link);
		wl_list_init(&cursor->constraint_commit.link);
		cursor->active_constraint = NULL;

		if (wlr_constraint->current.committed &
				WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT &&
				cursor->pointer_view) {
			double sx = wlr_constraint->current.cursor_hint.x;
			double sy = wlr_constraint->current.cursor_hint.y;

			struct roots_view *view = cursor->pointer_view->view;
			rotate_child_position(&sx, &sy, 0, 0, view->box.width, view->box.height,
				view->rotation);
			double lx = view->box.x + sx;
			double ly = view->box.y + sy;

			wlr_cursor_warp(cursor->cursor, NULL, lx, ly);
		}
	}

	free(constraint);
}

static void handle_pointer_constraint(struct wl_listener *listener,
		void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;
	struct roots_seat *seat = wlr_constraint->seat->data;
	struct roots_cursor *cursor = roots_seat_get_cursor (seat);

	struct roots_pointer_constraint *constraint =
		calloc(1, sizeof(struct roots_pointer_constraint));
	constraint->constraint = wlr_constraint;

	constraint->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);

	double sx, sy;
	struct wlr_surface *surface = desktop_surface_at(
		server->desktop,
		cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);

	if (surface == wlr_constraint->surface) {
		assert(!cursor->active_constraint);
		roots_cursor_constrain(cursor, wlr_constraint, sx, sy);
	}
}

static void
auto_maximize_changed_cb (PhocDesktop *self,
			  const gchar *key,
			  GSettings   *settings)
{
  gboolean max = g_settings_get_boolean (settings, key);

  g_return_if_fail (PHOC_IS_DESKTOP (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  phoc_desktop_set_auto_maximize (self, max);
}

static void
scale_to_fit_changed_cb (PhocDesktop *self,
			  const gchar *key,
			  GSettings   *settings)
{
    gboolean max = g_settings_get_boolean (settings, key);

    g_return_if_fail (PHOC_IS_DESKTOP (self));
    g_return_if_fail (G_IS_SETTINGS (settings));

    phoc_desktop_set_scale_to_fit (self, max);
}


static void
phoc_desktop_constructed (GObject *object)
{
  PhocDesktop *self = PHOC_DESKTOP (object);
  PhocServer *server = phoc_server_get_default ();
  struct roots_config *config = self->config;

  G_OBJECT_CLASS (phoc_desktop_parent_class)->constructed (object);

  wl_list_init(&self->views);
  wl_list_init(&self->outputs);

  self->new_output.notify = handle_new_output;
  wl_signal_add(&server->backend->events.new_output, &self->new_output);

  self->layout = wlr_output_layout_create();
  wlr_xdg_output_manager_v1_create(server->wl_display, self->layout);
  self->layout_change.notify = handle_layout_change;
  wl_signal_add(&self->layout->events.change, &self->layout_change);

  self->compositor = wlr_compositor_create(server->wl_display,
					   server->renderer);

  self->xdg_shell_v6 = wlr_xdg_shell_v6_create(server->wl_display);
  wl_signal_add(&self->xdg_shell_v6->events.new_surface,
		&self->xdg_shell_v6_surface);
  self->xdg_shell_v6_surface.notify = handle_xdg_shell_v6_surface;

  self->xdg_shell = wlr_xdg_shell_create(server->wl_display);
  wl_signal_add(&self->xdg_shell->events.new_surface,
		&self->xdg_shell_surface);
  self->xdg_shell_surface.notify = handle_xdg_shell_surface;

  self->layer_shell = wlr_layer_shell_v1_create(server->wl_display);
  wl_signal_add(&self->layer_shell->events.new_surface,
		&self->layer_shell_surface);
  self->layer_shell_surface.notify = handle_layer_shell_surface;

  self->tablet_v2 = wlr_tablet_v2_create(server->wl_display);

  const char *cursor_theme = NULL;
#ifdef PHOC_XWAYLAND
  const char *cursor_default = ROOTS_XCURSOR_DEFAULT;
#endif
  struct roots_cursor_config *cc =
    roots_config_get_cursor(config, ROOTS_CONFIG_DEFAULT_SEAT_NAME);
  if (cc != NULL) {
    cursor_theme = cc->theme;
#ifdef PHOC_XWAYLAND
    if (cc->default_image != NULL) {
      cursor_default = cc->default_image;
    }
#endif
  }

  char cursor_size_fmt[16];
  snprintf(cursor_size_fmt, sizeof(cursor_size_fmt),
	   "%d", ROOTS_XCURSOR_SIZE);
  setenv("XCURSOR_SIZE", cursor_size_fmt, 1);
  if (cursor_theme != NULL) {
    setenv("XCURSOR_THEME", cursor_theme, 1);
  }

#ifdef PHOC_XWAYLAND
  self->xcursor_manager = wlr_xcursor_manager_create(cursor_theme,
						     ROOTS_XCURSOR_SIZE);
  g_return_if_fail (self->xcursor_manager);

  if (config->xwayland) {
    self->xwayland = wlr_xwayland_create(server->wl_display,
					 self->compositor, config->xwayland_lazy);
    wl_signal_add(&self->xwayland->events.new_surface,
		  &self->xwayland_surface);
    self->xwayland_surface.notify = handle_xwayland_surface;

    setenv("DISPLAY", self->xwayland->display_name, true);

    if (wlr_xcursor_manager_load(self->xcursor_manager, 1)) {
      wlr_log(WLR_ERROR, "Cannot load XWayland XCursor theme");
    }
    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
								  self->xcursor_manager, cursor_default, 1);
    if (xcursor != NULL) {
      struct wlr_xcursor_image *image = xcursor->images[0];
      wlr_xwayland_set_cursor(self->xwayland, image->buffer,
			      image->width * 4, image->width, image->height, image->hotspot_x,
			      image->hotspot_y);
    }
  }
#endif

  self->gamma_control_manager_v1 = wlr_gamma_control_manager_v1_create(server->wl_display);
  self->export_dmabuf_manager_v1 =
    wlr_export_dmabuf_manager_v1_create(server->wl_display);
  self->server_decoration_manager =
    wlr_server_decoration_manager_create(server->wl_display);
  wlr_server_decoration_manager_set_default_mode(self->server_decoration_manager,
						 WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
  self->idle = wlr_idle_create(server->wl_display);
  self->idle_inhibit = wlr_idle_inhibit_v1_create(server->wl_display);
  self->primary_selection_device_manager =
    wlr_gtk_primary_selection_device_manager_create(server->wl_display);
  self->input_inhibit =
    wlr_input_inhibit_manager_create(server->wl_display);
  self->input_inhibit_activate.notify = input_inhibit_activate;
  wl_signal_add(&self->input_inhibit->events.activate,
		&self->input_inhibit_activate);
  self->input_inhibit_deactivate.notify = input_inhibit_deactivate;
  wl_signal_add(&self->input_inhibit->events.deactivate,
		&self->input_inhibit_deactivate);

  self->input_method =
    wlr_input_method_manager_v2_create(server->wl_display);

  self->text_input = wlr_text_input_manager_v3_create(server->wl_display);

  self->gtk_shell = phoc_gtk_shell_create(self, server->wl_display);
  self->phosh = phosh_create(self, server->wl_display);
  self->virtual_keyboard = wlr_virtual_keyboard_manager_v1_create(
								  server->wl_display);
  wl_signal_add(&self->virtual_keyboard->events.new_virtual_keyboard,
		&self->virtual_keyboard_new);
  self->virtual_keyboard_new.notify = phoc_handle_virtual_keyboard;

  self->virtual_pointer = wlr_virtual_pointer_manager_v1_create(server->wl_display);
  wl_signal_add(&self->virtual_pointer->events.new_virtual_pointer,
		&self->virtual_pointer_new);
  self->virtual_pointer_new.notify = phoc_handle_virtual_pointer;

  self->screencopy = wlr_screencopy_manager_v1_create(server->wl_display);

  self->xdg_decoration_manager =
    wlr_xdg_decoration_manager_v1_create(server->wl_display);
  wl_signal_add(&self->xdg_decoration_manager->events.new_toplevel_decoration,
		&self->xdg_toplevel_decoration);
  self->xdg_toplevel_decoration.notify = handle_xdg_toplevel_decoration;

  self->pointer_constraints =
    wlr_pointer_constraints_v1_create(server->wl_display);
  self->pointer_constraint.notify = handle_pointer_constraint;
  wl_signal_add(&self->pointer_constraints->events.new_constraint,
		&self->pointer_constraint);

  self->presentation =
    wlr_presentation_create(server->wl_display, server->backend);
  self->foreign_toplevel_manager_v1 =
    wlr_foreign_toplevel_manager_v1_create(server->wl_display);
  self->relative_pointer_manager =
    wlr_relative_pointer_manager_v1_create(server->wl_display);
  self->pointer_gestures =
    wlr_pointer_gestures_v1_create(server->wl_display);

  self->output_manager_v1 =
    wlr_output_manager_v1_create(server->wl_display);
  self->output_manager_apply.notify = handle_output_manager_apply;
  wl_signal_add(&self->output_manager_v1->events.apply,
		&self->output_manager_apply);
  self->output_manager_test.notify = handle_output_manager_test;
  wl_signal_add(&self->output_manager_v1->events.test,
		&self->output_manager_test);

#ifdef PHOC_HAS_WLR_OUTPUT_POWER_MANAGEMENT
  self->output_power_manager_v1 =
    wlr_output_power_manager_v1_create(server->wl_display);
  self->output_power_manager_set_mode.notify =
    phoc_output_handle_output_power_manager_set_mode;
  wl_signal_add(&self->output_power_manager_v1->events.set_mode,
		&self->output_power_manager_set_mode);
#endif

  wlr_data_control_manager_v1_create(server->wl_display);

  self->settings = g_settings_new ("sm.puri.phoc");
  g_signal_connect_swapped(self->settings, "changed::auto-maximize",
			   G_CALLBACK (auto_maximize_changed_cb), self);
  auto_maximize_changed_cb(self, "auto-maximize", self->settings);
  g_signal_connect_swapped(self->settings, "changed::scale-to-fit",
			   G_CALLBACK (scale_to_fit_changed_cb), self);
  scale_to_fit_changed_cb(self, "scale-to-fit", self->settings);
}


static void
phoc_desktop_finalize (GObject *object)
{
  PhocDesktop *self = PHOC_DESKTOP (object);

#ifdef PHOC_XWAYLAND
  // We need to shutdown Xwayland before disconnecting all clients, otherwise
  // wlroots will restart it automatically.
  g_clear_pointer (&self->xwayland, wlr_xwayland_destroy);
#endif

  g_clear_pointer (&self->phosh, phosh_destroy);
  g_clear_pointer (&self->gtk_shell, phoc_gtk_shell_destroy);

  G_OBJECT_CLASS (phoc_desktop_parent_class)->finalize (object);
}


static void
phoc_desktop_class_init (PhocDesktopClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->set_property = phoc_desktop_set_property;
  object_class->get_property = phoc_desktop_get_property;

  object_class->constructed = phoc_desktop_constructed;
  object_class->finalize = phoc_desktop_finalize;

  props[PROP_CONFIG] =
    g_param_spec_pointer (
      "config",
      "Config",
      "The config object",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_desktop_init (PhocDesktop *self)
{
}


PhocDesktop *
phoc_desktop_new (struct roots_config *config)
{
  return g_object_new (PHOC_TYPE_DESKTOP, "config", config, NULL);
}


/**
 * phoc_desktop_toggle_output_blank:
 *
 * Blank or unblank all outputs depending on the current state
 */
void
phoc_desktop_toggle_output_blank (PhocDesktop *self)
{
  struct roots_output *output;

  wl_list_for_each(output, &self->outputs, link) {
    gboolean enable = !output->wlr_output->enabled;

    wlr_output_enable (output->wlr_output, enable);
    wlr_output_commit (output->wlr_output);
    if (enable)
      output_damage_whole(output);
  }
}

/**
 * phoc_desktop_set_auto_maximize:
 *
 * Turn auto maximization of toplevels on (%TRUE) or off (%FALSE)
 */
void
phoc_desktop_set_auto_maximize (PhocDesktop *self, gboolean enable)
{
  g_debug ("auto-maximize: %d", enable);
  self->maximize = enable;
}

gboolean
phoc_desktop_get_auto_maximize (PhocDesktop *self)
{
  return self->maximize;
}

/**
 * phoc_desktop_set_scale_to_fit:
 *
 * Turn auto scaling of all oversized toplevels on (%TRUE) or off (%FALSE)
 */
void
phoc_desktop_set_scale_to_fit (PhocDesktop *self, gboolean enable)
{
    g_debug ("scale to fit: %d", enable);
    self->scale_to_fit = enable;
}

gboolean
phoc_desktop_get_scale_to_fit (PhocDesktop *self)
{
    return self->scale_to_fit;
}
