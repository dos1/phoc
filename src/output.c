#define G_LOG_DOMAIN "phoc-output"

#include "config.h"

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "settings.h"
#include "layers.h"
#include "output.h"
#include "render.h"
#include "server.h"

/**
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
void rotate_child_position(double *sx, double *sy, double sw, double sh,
		double pw, double ph, float rotation) {
	if (rotation == 0.0) {
		return;
	}

	// Coordinates relative to the center of the subsurface
	double cx = *sx - pw/2 + sw/2,
		cy = *sy - ph/2 + sh/2;
	// Rotated coordinates
	double rx = cos(rotation)*cx - sin(rotation)*cy,
		ry = cos(rotation)*cy + sin(rotation)*cx;
	*sx = rx + pw/2 - sw/2;
	*sy = ry + ph/2 - sh/2;
}

struct surface_iterator_data {
	roots_surface_iterator_func_t user_iterator;
	void *user_data;

	struct roots_output *output;
	double ox, oy;
	int width, height;
	float rotation, scale;
};

static bool get_surface_box(struct surface_iterator_data *data,
		struct wlr_surface *surface, int sx, int sy,
		struct wlr_box *surface_box) {
	struct roots_output *output = data->output;

	if (!wlr_surface_has_buffer(surface)) {
		return false;
	}

	int sw = surface->current.width;
	int sh = surface->current.height;

	double _sx = sx + surface->sx;
	double _sy = sy + surface->sy;
	rotate_child_position(&_sx, &_sy, sw, sh, data->width, data->height,
		data->rotation);

	struct wlr_box box = {
		.x = data->ox + _sx,
		.y = data->oy + _sy,
		.width = sw,
		.height = sh,
	};
	if (surface_box != NULL) {
		*surface_box = box;
	}

	struct wlr_box rotated_box;
	wlr_box_rotated_bounds(&rotated_box, &box, data->rotation);

	struct wlr_box output_box = {0};
	wlr_output_effective_resolution(output->wlr_output,
		&output_box.width, &output_box.height);
	scale_box(&output_box, 1 / data->scale);

	struct wlr_box intersection;
	return wlr_box_intersection(&intersection, &output_box, &rotated_box);
}

static void output_for_each_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *_data) {
	struct surface_iterator_data *data = _data;

	struct wlr_box box;
	bool intersects = get_surface_box(data, surface, sx, sy, &box);
	if (!intersects) {
		return;
	}

	data->user_iterator(data->output, surface, &box, data->rotation,
		data->scale, data->user_data);
}

void output_surface_for_each_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.ox = ox,
		.oy = oy,
		.width = surface->current.width,
		.height = surface->current.height,
		.rotation = 0,
		.scale = 1.0
	};

	wlr_surface_for_each_surface(surface,
		output_for_each_surface_iterator, &data);
}

void output_xdg_surface_for_each_surface(struct roots_output *output,
		struct wlr_xdg_surface *xdg_surface, double ox, double oy,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.ox = ox,
		.oy = oy,
		.width = xdg_surface->surface->current.width,
		.height = xdg_surface->surface->current.height,
		.rotation = 0,
		.scale = 1.0
	};

	wlr_xdg_surface_for_each_surface(xdg_surface,
		output_for_each_surface_iterator, &data);
}

void output_view_for_each_surface(struct roots_output *output,
		struct roots_view *view, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct wlr_box *output_box =
		wlr_output_layout_get_box(output->desktop->layout, output->wlr_output);
	if (!output_box) {
		return;
	}

	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.ox = view->box.x - output_box->x,
		.oy = view->box.y - output_box->y,
		.width = view->box.width,
		.height = view->box.height,
		.rotation = view->rotation,
		.scale = view->scale
	};

	view_for_each_surface(view, output_for_each_surface_iterator, &data);
}

#ifdef PHOC_XWAYLAND
void output_xwayland_children_for_each_surface(
		struct roots_output *output, struct wlr_xwayland_surface *surface,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_box *output_box =
		wlr_output_layout_get_box(output->desktop->layout, output->wlr_output);
	if (!output_box) {
		return;
	}

	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->mapped) {
			double ox = child->x - output_box->x;
			double oy = child->y - output_box->y;
			output_surface_for_each_surface(output, child->surface,
				ox, oy, iterator, user_data);
		}
		output_xwayland_children_for_each_surface(output, child,
			iterator, user_data);
	}
}
#endif

static void output_layer_handle_surface(struct roots_output *output,
		struct roots_layer_surface *layer_surface, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
		layer_surface->layer_surface;
	output_surface_for_each_surface(output, wlr_layer_surface_v1->surface,
		layer_surface->geo.x, layer_surface->geo.y, iterator,
		user_data);

	struct wlr_xdg_popup *state;
	wl_list_for_each(state, &wlr_layer_surface_v1->popups, link) {
		struct wlr_xdg_surface *popup = state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_sx, popup_sy;
		popup_sx = layer_surface->geo.x;
		popup_sx += popup->popup->geometry.x - popup->geometry.x;
		popup_sy = layer_surface->geo.y;
		popup_sy += popup->popup->geometry.y - popup->geometry.y;

		output_xdg_surface_for_each_surface(output, popup,
			popup_sx, popup_sy, iterator, user_data);
	}
}

void output_layer_for_each_surface(struct roots_output *output,
		struct wl_list *layer_surfaces, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct roots_layer_surface *layer_surface;
	wl_list_for_each_reverse(layer_surface, layer_surfaces, link) {
		if (layer_surface->layer_surface->current.exclusive_zone <= 0) {
			output_layer_handle_surface(output, layer_surface, iterator, user_data);
		}
	}
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		if (layer_surface->layer_surface->current.exclusive_zone > 0) {
			output_layer_handle_surface(output, layer_surface, iterator, user_data);
		}
	}
}

void output_drag_icons_for_each_surface(struct roots_output *output,
		PhocInput *input, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct wlr_box *output_box =
		wlr_output_layout_get_box(output->desktop->layout, output->wlr_output);
	if (!output_box) {
		return;
	}

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		struct roots_drag_icon *drag_icon = seat->drag_icon;
		if (!drag_icon || !drag_icon->wlr_drag_icon->mapped) {
			continue;
		}

		double ox = drag_icon->x - output_box->x;
		double oy = drag_icon->y - output_box->y;
		output_surface_for_each_surface(output,
			drag_icon->wlr_drag_icon->surface, ox, oy, iterator, user_data);
	}
}

void output_for_each_surface(struct roots_output *output,
		roots_surface_iterator_func_t iterator, void *user_data) {
	PhocDesktop *desktop = output->desktop;
	PhocServer *server = phoc_server_get_default ();

	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		output_view_for_each_surface(output, view, iterator, user_data);

#ifdef PHOC_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			struct roots_xwayland_surface *xwayland_surface =
				roots_xwayland_surface_from_view(view);
			output_xwayland_children_for_each_surface(output,
				xwayland_surface->xwayland_surface, iterator, user_data);
		}
#endif
	} else {
		struct roots_view *view;
		wl_list_for_each_reverse(view, &desktop->views, link) {
			output_view_for_each_surface(output, view, iterator, user_data);
		}
	}

	output_drag_icons_for_each_surface(output, server->input,
		iterator, user_data);

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		output_layer_for_each_surface(output, &output->layers[i],
			iterator, user_data);
	}
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

void get_decoration_box(struct roots_view *view,
		struct roots_output *output, struct wlr_box *box) {
	struct wlr_output *wlr_output = output->wlr_output;

	struct wlr_box deco_box;
	view_get_deco_box(view, &deco_box);
	double sx = deco_box.x - view->box.x;
	double sy = deco_box.y - view->box.y;
	rotate_child_position(&sx, &sy, deco_box.width, deco_box.height,
		view->wlr_surface->current.width,
		view->wlr_surface->current.height, view->rotation);
	double x = sx + view->box.x;
	double y = sy + view->box.y;

	wlr_output_layout_output_coords(output->desktop->layout, wlr_output, &x, &y);

	box->x = x * wlr_output->scale;
	box->y = y * wlr_output->scale;
	box->width = deco_box.width * wlr_output->scale;
	box->height = deco_box.height * wlr_output->scale;
}

void output_damage_whole(struct roots_output *output) {
	wlr_output_damage_add_whole(output->damage);
}

static bool view_accept_damage(struct roots_output *output,
		struct roots_view *view) {
	if (view->wlr_surface == NULL) {
		return false;
	}
	if (output->fullscreen_view == NULL) {
		return true;
	}
	if (output->fullscreen_view == view) {
		return true;
	}
#ifdef PHOC_XWAYLAND
	if (output->fullscreen_view->type == ROOTS_XWAYLAND_VIEW &&
			view->type == ROOTS_XWAYLAND_VIEW) {
		// Special case: accept damage from children
		struct wlr_xwayland_surface *xsurface =
			roots_xwayland_surface_from_view(view)->xwayland_surface;
		struct wlr_xwayland_surface *fullscreen_xsurface =
			roots_xwayland_surface_from_view(output->fullscreen_view)->xwayland_surface;
		while (xsurface != NULL) {
			if (fullscreen_xsurface == xsurface) {
				return true;
			}
			xsurface = xsurface->parent;
		}
	}
#endif
	return false;
}

static void damage_surface_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		float scale, void *data) {
	bool *whole = data;

	struct wlr_box box = *_box;
	scale_box(&box, scale);
	scale_box(&box, output->wlr_output->scale);

	int center_x = box.x + box.width/2;
	int center_y = box.y + box.height/2;

	if (pixman_region32_not_empty(&surface->buffer_damage)) {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		wlr_surface_get_effective_damage(surface, &damage);
		wlr_region_scale(&damage, &damage, scale);
		wlr_region_scale(&damage, &damage, output->wlr_output->scale);
		if (ceil(output->wlr_output->scale) > surface->current.scale) {
			// When scaling up a surface, it'll become blurry so we need to
			// expand the damage region
			wlr_region_expand(&damage, &damage,
				ceil(output->wlr_output->scale) - surface->current.scale);
		}
		pixman_region32_translate(&damage, box.x, box.y);
		wlr_region_rotated_bounds(&damage, &damage, rotation,
			center_x, center_y);
		wlr_output_damage_add(output->damage, &damage);
		pixman_region32_fini(&damage);
	}

	if (*whole) {
		wlr_box_rotated_bounds(&box, &box, rotation);
		wlr_output_damage_add_box(output->damage, &box);
	}

	wlr_output_schedule_frame(output->wlr_output);
}

void output_damage_whole_local_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy) {
	bool whole = true;
	output_surface_for_each_surface(output, surface, ox, oy,
		damage_surface_iterator, &whole);
}

static void damage_whole_decoration(struct roots_view *view,
		struct roots_output *output) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	wlr_box_rotated_bounds(&box, &box, view->rotation);

	wlr_output_damage_add_box(output->damage, &box);
}

void output_damage_whole_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	damage_whole_decoration(view, output);

	bool whole = true;
	output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
}

void output_damage_whole_drag_icon(struct roots_output *output,
		struct roots_drag_icon *icon) {
	bool whole = true;
	output_surface_for_each_surface(output, icon->wlr_drag_icon->surface,
		icon->x, icon->y, damage_surface_iterator, &whole);
}

void output_damage_from_local_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy) {
	bool whole = false;
	output_surface_for_each_surface(output, surface, ox, oy,
		damage_surface_iterator, &whole);
}

void output_damage_from_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	bool whole = false;
	output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
}

static void set_mode(struct wlr_output *output,
		struct roots_output_config *oc) {
	int mhz = (int)(oc->mode.refresh_rate * 1000);

	if (wl_list_empty(&output->modes)) {
		// Output has no mode, try setting a custom one
		wlr_output_set_custom_mode(output, oc->mode.width, oc->mode.height, mhz);
		return;
	}

	struct wlr_output_mode *mode, *best = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
			if (mode->refresh == mhz) {
				best = mode;
				break;
			}
			best = mode;
		}
	}
	if (!best) {
		wlr_log(WLR_ERROR, "Configured mode for %s not available", output->name);
	} else {
		wlr_log(WLR_DEBUG, "Assigning configured mode to %s", output->name);
		wlr_output_set_mode(output, best);
	}
}

static void update_output_manager_config(PhocDesktop *desktop) {
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();

	struct roots_output *output;
	wl_list_for_each(output, &desktop->outputs, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(config, output->wlr_output);
		struct wlr_box *output_box = wlr_output_layout_get_box(
			output->desktop->layout, output->wlr_output);
		if (output_box) {
			config_head->state.x = output_box->x;
			config_head->state.y = output_box->y;
		}
	}

	wlr_output_manager_v1_set_configuration(desktop->output_manager_v1, config);
}

void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop =
		wl_container_of(listener, desktop, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	bool ok = true;
	struct wlr_output_configuration_head_v1 *config_head;
	// First disable outputs we need to disable
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		if (config_head->state.enabled)
			continue;

		if (!wlr_output->enabled)
			continue;

		wlr_output_enable(wlr_output, false);
		wlr_output_layout_remove(desktop->layout, wlr_output);
		ok &= wlr_output_commit(wlr_output);
	}

	// Then enable outputs that need to
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;

		if (!config_head->state.enabled)
			continue;

		wlr_output_enable(wlr_output, true);
		if (config_head->state.mode != NULL) {
			wlr_output_set_mode(wlr_output, config_head->state.mode);
		} else {
			wlr_output_set_custom_mode(wlr_output,
				config_head->state.custom_mode.width,
				config_head->state.custom_mode.height,
				config_head->state.custom_mode.refresh);
		}
		wlr_output_layout_add(desktop->layout, wlr_output,
			config_head->state.x, config_head->state.y);
		wlr_output_set_transform(wlr_output, config_head->state.transform);
		wlr_output_set_scale(wlr_output, config_head->state.scale);
		struct roots_output *output = wlr_output->data;
		ok &= wlr_output_commit(wlr_output);
		if (output->fullscreen_view) {
			view_set_fullscreen(output->fullscreen_view, true, wlr_output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);

	update_output_manager_config(desktop);
}

void handle_output_manager_test(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop =
		wl_container_of(listener, desktop, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	// TODO: implement test-only mode
	wlr_output_configuration_v1_send_succeeded(config);
	wlr_output_configuration_v1_destroy(config);
}

void
phoc_output_handle_output_power_manager_set_mode(struct wl_listener *listener, void *data)
{
  struct wlr_output_power_v1_set_mode_event *event = data;
  struct roots_output *self;
  bool enable = true;

  g_return_if_fail (event && event->output && event->output->data);

  self = event->output->data;
  g_debug ("Request to set output power mode of %p to %d",
	   self, event->mode);
  switch (event->mode) {
  case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
    enable = false;
    break;
  case ZWLR_OUTPUT_POWER_V1_MODE_ON:
    enable = true;
    break;
  default:
    g_warning ("Unhandled power state %d for %p", event->mode, self);
    return;
  }

  if (enable == self->wlr_output->enabled)
    return;

  wlr_output_enable(self->wlr_output, enable);
  if (!wlr_output_commit(self->wlr_output)) {
    g_warning ("Failed to commit power mode change to %d for %p", enable, self);
    return;
  }

  if (enable)
    output_damage_whole (self);
}

static void output_destroy(struct roots_output *output) {
	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);

	wl_list_remove(&output->link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->enable.link);
	wl_list_remove(&output->mode.link);
	wl_list_remove(&output->transform.link);
	wl_list_remove(&output->damage_frame.link);
	wl_list_remove(&output->damage_destroy.link);
	free(output);
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, destroy);
	PhocDesktop *desktop = output->desktop;
	output_destroy(output);
	update_output_manager_config(desktop);
}

static void output_handle_enable(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, enable);
	update_output_manager_config(output->desktop);
}

static void output_damage_handle_frame(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_frame);
	output_render(output);
}

static void output_damage_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_destroy);
	output_destroy(output);
}

static void output_handle_mode(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, mode);
	arrange_layers(output);
	update_output_manager_config(output->desktop);
}

static void output_handle_transform(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, transform);
	arrange_layers(output);
}

void handle_new_output(struct wl_listener *listener, void *data) {
	PhocDesktop *desktop = wl_container_of(listener, desktop,
		new_output);
	struct wlr_output *wlr_output = data;
	PhocServer *server = phoc_server_get_default ();
	PhocInput *input = server->input;
	struct roots_config *config = desktop->config;

	wlr_log(WLR_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(WLR_DEBUG, "'%s %s %s' %"PRId32"mm x %"PRId32"mm", wlr_output->make,
		wlr_output->model, wlr_output->serial, wlr_output->phys_width,
		wlr_output->phys_height);

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	wl_list_insert(&desktop->outputs, &output->link);

	output->damage = wlr_output_damage_create(wlr_output);

	output->debug_touch_points = NULL;

	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->enable.notify = output_handle_enable;
	wl_signal_add(&wlr_output->events.enable, &output->enable);
	output->mode.notify = output_handle_mode;
	wl_signal_add(&wlr_output->events.mode, &output->mode);
	output->transform.notify = output_handle_transform;
	wl_signal_add(&wlr_output->events.transform, &output->transform);

	output->damage_frame.notify = output_damage_handle_frame;
	wl_signal_add(&output->damage->events.frame, &output->damage_frame);
	output->damage_destroy.notify = output_damage_handle_destroy;
	wl_signal_add(&output->damage->events.destroy, &output->damage_destroy);

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		wl_list_init(&output->layers[i]);
	}

	struct roots_output_config *output_config =
		roots_config_get_output(config, wlr_output);

	struct wlr_output_mode *preferred_mode =
		wlr_output_preferred_mode(wlr_output);
	if (output_config) {
		if (output_config->enable) {
			if (wlr_output_is_drm(wlr_output)) {
				struct roots_output_mode_config *mode_config;
				wl_list_for_each(mode_config, &output_config->modes, link) {
					wlr_drm_connector_add_mode(wlr_output, &mode_config->info);
				}
			} else if (!wl_list_empty(&output_config->modes)) {
				wlr_log(WLR_ERROR, "Can only add modes for DRM backend");
			}

			if (output_config->mode.width) {
				set_mode(wlr_output, output_config);
			} else if (preferred_mode != NULL) {
				wlr_output_set_mode(wlr_output, preferred_mode);
			}

			wlr_output_set_scale(wlr_output, output_config->scale);
			wlr_output_set_transform(wlr_output, output_config->transform);
			wlr_output_layout_add(desktop->layout, wlr_output, output_config->x,
				output_config->y);
		} else {
			wlr_output_enable(wlr_output, false);
		}
	} else {
		if (preferred_mode != NULL) {
			wlr_output_set_mode(wlr_output, preferred_mode);
		}
		wlr_output_enable(wlr_output, true);
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}
	wlr_output_commit(wlr_output);

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_configure_cursor(seat);
		roots_seat_configure_xcursor(seat);
	}

	arrange_layers(output);
	output_damage_whole(output);

	update_output_manager_config(desktop);
}

/**
 * phoc_output_is_builtin:
 *
 * Returns: %TRUE if the output a built in panel (e.g. laptop panel or
 * phone LCD), %FALSE otherwise.
 */
gboolean
phoc_output_is_builtin (struct roots_output *self)
{
  struct wlr_output *output;

  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (self->wlr_output, FALSE);
  output = self->wlr_output;
  g_return_val_if_fail (output->name, FALSE);

  if (g_str_has_prefix (output->name, "LVDS-"))
    return TRUE;
  else if (g_str_has_prefix (output->name, "eDP-"))
    return TRUE;
  else if (g_str_has_prefix (output->name, "DSI-"))
    return TRUE;

  return FALSE;
}
