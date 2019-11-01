#define G_LOG_DOMAIN "phoc-layer-shell"

#include "config.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/log.h>
#include "desktop.h"
#include "layers.h"
#include "output.h"
#include "server.h"

#define LAYER_SHELL_LAYER_COUNT 4

static void apply_exclusive(struct wlr_box *usable_area,
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	if (exclusive <= 0) {
		return;
	}
	struct {
		uint32_t anchors;
		int *positive_axis;
		int *negative_axis;
		int margin;
	} edges[] = {
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
			.margin = margin_top,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
			.margin = margin_bottom,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
			.margin = margin_left,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
			.margin = margin_right,
		},
	};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
		if ((anchor & edges[i].anchors) == edges[i].anchors && exclusive + edges[i].margin > 0) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive + edges[i].margin;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			}
		}
	}
}

static void update_cursors(struct roots_layer_surface *roots_surface,
		struct wl_list *seats /* struct roots_seat */) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_seat *seat;
	wl_list_for_each(seat, seats, link) {
		struct roots_cursor *cursor = roots_seat_get_cursor(seat);
		double sx, sy;

		struct wlr_surface *surface = desktop_surface_at(
			server->desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);

		if (surface == roots_surface->layer_surface->surface) {
			struct timespec time;
			if (clock_gettime(CLOCK_MONOTONIC, &time) == 0) {
				roots_cursor_update_position(cursor,
					time.tv_sec * 1000 + time.tv_nsec / 1000000);
			} else {
				wlr_log(WLR_ERROR, "Failed to get time, not updating"
					"position. Errno: %s\n", strerror(errno));
			}
		}
	}
}

static void arrange_layer(struct wlr_output *output,
		struct wl_list *seats /* struct *roots_seat */,
		struct wl_list *list /* struct *roots_layer_surface */,
		struct wlr_box *usable_area, bool exclusive) {
	struct roots_layer_surface *roots_surface;
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output,
			&full_area.width, &full_area.height);
	wl_list_for_each_reverse(roots_surface, list, link) {
		struct wlr_layer_surface_v1 *layer = roots_surface->layer_surface;
		struct wlr_layer_surface_v1_state *state = &layer->current;
		if (exclusive != (state->exclusive_zone > 0)) {
			continue;
		}
		struct wlr_box bounds;
		if (state->exclusive_zone == -1) {
			bounds = full_area;
		} else {
			bounds = *usable_area;
		}
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		// Horizontal axis
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = bounds.x;
			box.width = bounds.width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		// Vertical axis
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = bounds.y;
			box.height = bounds.height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		// Margin
		if ((state->anchor & both_horiz) == both_horiz) {
			box.x += state->margin.left;
			box.width -= state->margin.left + state->margin.right;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if ((state->anchor & both_vert) == both_vert) {
			box.y += state->margin.top;
			box.height -= state->margin.top + state->margin.bottom;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			// TODO: Bubble up a protocol error?
			wlr_layer_surface_v1_close(layer);
			continue;
		}

		// Apply
		struct wlr_box old_geo = roots_surface->geo;
		roots_surface->geo = box;
		if (layer->mapped) {
			apply_exclusive(usable_area, state->anchor, state->exclusive_zone,
					state->margin.top, state->margin.right,
					state->margin.bottom, state->margin.left);
		}
		wlr_layer_surface_v1_configure(layer, box.width, box.height);

		// Having a cursor newly end up over the moved layer will not
		// automatically send a motion event to the surface. The event needs to
		// be synthesized.
		// Only update layer surfaces which kept their size (and so buffers) the
		// same, because those with resized buffers will be handled separately.

		if (roots_surface->geo.x != old_geo.x
				|| roots_surface->geo.y != old_geo.y) {
			update_cursors(roots_surface, seats);
		}
	}
}

struct osk_origin {
	struct wlr_layer_surface_v1_state state;
	struct roots_layer_surface *surface;
	enum zwlr_layer_shell_v1_layer layer;
};

static struct osk_origin find_osk(struct wl_list layers[LAYER_SHELL_LAYER_COUNT]) {
	struct osk_origin origin = {0};
	for (unsigned i = 0; i < LAYER_SHELL_LAYER_COUNT; i++) {
		struct wl_list *list = &layers[i];
		struct roots_layer_surface *roots_surface;
		wl_list_for_each(roots_surface, list, link) {
			if (strcmp(roots_surface->layer_surface->namespace, "osk") == 0) {
				origin.state = roots_surface->layer_surface->current;
				origin.surface = roots_surface;
				origin.layer = i;
				return origin;
			}
		}
	}
	return origin;
}

/// Adjusts keyboard properties
static void change_osk(const struct osk_origin *osk, struct wl_list layers[LAYER_SHELL_LAYER_COUNT], bool force_overlay) {
	if (force_overlay && osk->layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		wl_list_remove(&osk->surface->link);
		wl_list_insert(&layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &osk->surface->link);
	}

	if (!force_overlay && osk->layer != osk->surface->layer_surface->client_pending.layer) {
		wl_list_remove(&osk->surface->link);
		wl_list_insert(&layers[osk->surface->layer_surface->client_pending.layer], &osk->surface->link);
	}
}

void arrange_layers(struct roots_output *output) {
	struct wlr_box usable_area = { 0 };
	PhocServer *server = phoc_server_get_default ();

	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);

	struct osk_origin osk_place = find_osk(output->layers);
	if (osk_place.surface) {
		bool osk_force_overlay = false;
		struct roots_seat *seat;
		wl_list_for_each(seat, &server->input->seats, link) {
			if (seat->focused_layer && seat->focused_layer->client_pending.layer >=
					osk_place.surface->layer_surface->client_pending.layer) {
				osk_force_overlay = true;
				break;
			}
		}
		change_osk(&osk_place, output->layers, osk_force_overlay);
	}

	// Arrange exclusive surfaces from top->bottom
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, true);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, true);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, true);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, true);
	memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));

	struct roots_view *view;
	wl_list_for_each(view, &output->desktop->views, link) {
		if (view->maximized) {
			view_arrange_maximized(view);
		}
	}

	// Arrange non-exlusive surfaces from top->bottom
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, false);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, false);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, false);
	arrange_layer(output->wlr_output, &server->input->seats,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, false);

	// Find topmost keyboard interactive layer, if such a layer exists
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	size_t nlayers = sizeof(layers_above_shell) / sizeof(layers_above_shell[0]);
	struct roots_layer_surface *layer, *topmost = NULL;
	for (size_t i = 0; i < nlayers; ++i) {
		wl_list_for_each(layer,
				&output->layers[layers_above_shell[i]], link) {
			if (layer->layer_surface->current.keyboard_interactive
					&& layer->layer_surface->mapped) {
				topmost = layer;
				break;
			}
		}
		if (topmost != NULL) {
			break;
		}
	}

	struct roots_input *input = server->input;
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_set_focus_layer(seat,
				topmost ? topmost->layer_surface : NULL);
	}
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wl_list_remove(&layer->output_destroy.link);
	wlr_layer_surface_v1_close(layer->layer_surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		struct roots_output *output = wlr_output->data;
		struct wlr_box old_geo = layer->geo;
		arrange_layers(output);

		// Cursor changes which happen as a consequence of resizing a layer
		// surface are applied in arrange_layers. Because the resize happens
		// before the underlying surface changes, it will only receive a cursor
		// update if the new cursor position crosses the *old* sized surface in
		// the *new* layer surface.
		// Another cursor move event is needed when the surface actually
		// changes.
		struct wlr_surface *surface = layer_surface->surface;
		if (surface->previous.width != surface->current.width ||
				surface->previous.height != surface->current.height) {
			update_cursors(layer, &server->input->seats);
		}

		if (memcmp(&old_geo, &layer->geo, sizeof(struct wlr_box)) != 0) {
			output_damage_whole_local_surface(output, layer_surface->surface,
					old_geo.x, old_geo.y);
			output_damage_whole_local_surface(output, layer_surface->surface,
					layer->geo.x, layer->geo.y);
		} else {
			output_damage_from_local_surface(output, layer_surface->surface,
					layer->geo.x, layer->geo.y);
		}
	}
}

static void unmap(struct wlr_layer_surface_v1 *layer_surface) {
	struct roots_layer_surface *layer = layer_surface->data;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		struct roots_output *output = wlr_output->data;
		output_damage_whole_local_surface(output, layer_surface->surface,
			layer->geo.x, layer->geo.y);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer = wl_container_of(
			listener, layer, destroy);
	if (layer->layer_surface->mapped) {
		unmap(layer->layer_surface);
	}
	wl_list_remove(&layer->link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->unmap.link);
	wl_list_remove(&layer->surface_commit.link);
	if (layer->layer_surface->output) {
		wl_list_remove(&layer->output_destroy.link);
		arrange_layers((struct roots_output *)layer->layer_surface->output->data);
	}
	free(layer);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface_v1 *layer_surface = data;
	struct roots_layer_surface *layer = layer_surface->data;
	struct wlr_output *wlr_output = layer_surface->output;
	if (!wlr_output) {
		return;
	}
	struct roots_output *output = wlr_output->data;
	output_damage_whole_local_surface(output, layer_surface->surface,
		layer->geo.x, layer->geo.y);
	wlr_surface_send_enter(layer_surface->surface, wlr_output);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_surface *layer = wl_container_of(
			listener, layer, unmap);
	unmap(layer->layer_surface);
	input_update_cursor_focus(server->input);
}

static struct roots_layer_surface *popup_get_root_layer(struct roots_layer_popup *popup) {
	while (popup->parent_type == LAYER_PARENT_POPUP) {
		popup = popup->parent_popup;
	}
	return popup->parent_layer;
}

static void popup_unconstrain(struct roots_layer_popup *popup) {
	struct roots_layer_surface *layer = popup_get_root_layer(popup);
	struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

	struct roots_output *output = layer->layer_surface->output->data;

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_toplevel_sx_box = {
		.x = -layer->geo.x,
		.y = -layer->geo.y,
		.width = output->usable_area.width,
		.height = output->usable_area.height,
	};

	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void popup_damage(struct roots_layer_popup *layer_popup, bool whole) {
	struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
	struct wlr_surface *surface = popup->base->surface;
	int popup_sx = popup->geometry.x - popup->base->geometry.x;
	int popup_sy = popup->geometry.y - popup->base->geometry.y;
	int ox = popup_sx, oy = popup_sy;
	struct roots_layer_surface *layer;
	while (layer_popup->parent_type == LAYER_PARENT_POPUP) {
		layer_popup = layer_popup->parent_popup;
		ox += layer_popup->wlr_popup->geometry.x;
		oy += layer_popup->wlr_popup->geometry.y;
	}
	layer = layer_popup->parent_layer;
	ox += layer->geo.x;
	oy += layer->geo.y;

	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}
	struct roots_output *output = wlr_output->data;

	if (whole) {
		output_damage_whole_local_surface(output, surface, ox, oy);
	} else {
		output_damage_from_local_surface(output, surface, ox, oy);
	}
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_popup *popup = wl_container_of(listener, popup, map);
	struct roots_layer_surface *layer = popup_get_root_layer(popup);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}
	wlr_surface_send_enter(popup->wlr_popup->base->surface, wlr_output);
	popup_damage(popup, true);
	input_update_cursor_focus(server->input);
}

static void popup_handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_popup *popup = wl_container_of(listener, popup, unmap);
	popup_damage(popup, true);
	input_update_cursor_focus(server->input);
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup = wl_container_of(listener, popup, commit);
	popup_damage(popup, false);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->new_popup.link);
	wl_list_remove(&popup->new_subsurface.link);
	free(popup);
}

static struct roots_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup);
static struct roots_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface);

static void popup_new_popup(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct roots_layer_popup *new_popup = popup_create(wlr_popup);
	new_popup->parent_type = LAYER_PARENT_POPUP;
	new_popup->parent_popup = popup;
	popup_unconstrain(new_popup);
}

static void popup_new_subsurface(struct wl_listener *listener, void *data) {
	struct roots_layer_popup *popup =
		wl_container_of(listener, popup, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	struct roots_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_POPUP;
	subsurface->parent_popup = popup;
}

static struct roots_layer_popup *popup_create(struct wlr_xdg_popup *wlr_popup) {
	struct roots_layer_popup *popup =
		calloc(1, sizeof(struct roots_layer_popup));
	if (popup == NULL) {
		return NULL;
	}
	popup->wlr_popup = wlr_popup;
	popup->map.notify = popup_handle_map;
	wl_signal_add(&wlr_popup->base->events.map, &popup->map);
	popup->unmap.notify = popup_handle_unmap;
	wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
	popup->commit.notify = popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
	popup->new_popup.notify = popup_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);
	popup->new_subsurface.notify = popup_new_subsurface;
	wl_signal_add(&wlr_popup->base->surface->events.new_subsurface, &popup->new_subsurface);

	return popup;
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *roots_layer_surface =
		wl_container_of(listener, roots_layer_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	struct roots_layer_popup *popup = popup_create(wlr_popup);
	popup->parent_type = LAYER_PARENT_LAYER;
	popup->parent_layer = roots_layer_surface;
	popup_unconstrain(popup);
}

static struct roots_layer_surface *subsurface_get_root_layer(struct roots_layer_subsurface *subsurface) {
	while (subsurface->parent_type == LAYER_PARENT_SUBSURFACE) {
		subsurface = subsurface->parent_subsurface;
	}
	if (subsurface->parent_type == LAYER_PARENT_POPUP) {
		return popup_get_root_layer(subsurface->parent_popup);
	}
	return subsurface->parent_layer;
}

static void subsurface_damage(struct roots_layer_subsurface *subsurface, bool whole) {
	struct roots_layer_surface *layer = subsurface_get_root_layer(subsurface);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}
	struct roots_output *output = wlr_output->data;
	int ox = subsurface->wlr_subsurface->current.x + layer->geo.x;
	int oy = subsurface->wlr_subsurface->current.y + layer->geo.y;
	if (whole) {
		output_damage_whole_local_surface(output, subsurface->wlr_subsurface->surface,
			ox, oy);
	} else {
		output_damage_from_local_surface(output, subsurface->wlr_subsurface->surface,
			ox, oy);
	}
}

static void subsurface_handle_map(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, map);
	subsurface_damage(subsurface, true);
	input_update_cursor_focus(server->input);
}

static void subsurface_handle_unmap(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, unmap);
	subsurface_damage(subsurface, true);
	input_update_cursor_focus(server->input);
}

static void subsurface_handle_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface = wl_container_of(listener, subsurface, commit);
	subsurface_damage(subsurface, false);
}

static void subsurface_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);

	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->commit.link);
	wl_list_remove(&subsurface->new_subsurface.link);
	free(subsurface);
}

static void subsurface_new_subsurface(struct wl_listener *listener, void *data) {
	struct roots_layer_subsurface *subsurface =
		wl_container_of(listener, subsurface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct roots_layer_subsurface *new_subsurface = layer_subsurface_create(wlr_subsurface);
	new_subsurface->parent_type = LAYER_PARENT_SUBSURFACE;
	new_subsurface->parent_subsurface = subsurface;
}

static struct roots_layer_subsurface *layer_subsurface_create(struct wlr_subsurface *wlr_subsurface) {
	struct roots_layer_subsurface *subsurface =
		calloc(1, sizeof(struct roots_layer_subsurface));
	if (subsurface == NULL) {
		return NULL;
	}
	subsurface->wlr_subsurface = wlr_subsurface;

	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->commit.notify = subsurface_handle_commit;
	wl_signal_add(&wlr_subsurface->surface->events.commit, &subsurface->commit);
	subsurface->new_subsurface.notify = subsurface_new_subsurface;
	wl_signal_add(&wlr_subsurface->surface->events.new_subsurface, &subsurface->new_subsurface);

	return subsurface;
}

static void handle_new_subsurface(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *roots_layer_surface =
		wl_container_of(listener, roots_layer_surface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct roots_layer_subsurface *subsurface = layer_subsurface_create(wlr_subsurface);
	subsurface->parent_type = LAYER_PARENT_LAYER;
	subsurface->parent_layer = roots_layer_surface;
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct wlr_layer_surface_v1 *layer_surface = data;
	PhocDesktop *desktop =
		wl_container_of(listener, desktop, layer_shell_surface);
	wlr_log(WLR_DEBUG, "new layer surface: namespace %s layer %d anchor %d "
			"size %dx%d margin %d,%d,%d,%d",
		layer_surface->namespace, layer_surface->client_pending.layer,
			layer_surface->client_pending.layer,
		layer_surface->client_pending.desired_width,
		layer_surface->client_pending.desired_height,
		layer_surface->client_pending.margin.top,
		layer_surface->client_pending.margin.right,
		layer_surface->client_pending.margin.bottom,
		layer_surface->client_pending.margin.left);

	if (!layer_surface->output) {
		struct roots_input *input = server->input;
		struct roots_seat *seat = input_last_active_seat(input);
		assert(seat); // Technically speaking we should handle this case
		struct roots_cursor *cursor = roots_seat_get_cursor(seat);
		struct wlr_output *output =
			wlr_output_layout_output_at(desktop->layout,
					cursor->cursor->x,
					cursor->cursor->y);
		if (!output) {
			wlr_log(WLR_ERROR, "Couldn't find output at (%.0f,%.0f)",
				cursor->cursor->x,
				cursor->cursor->y);
			output = wlr_output_layout_get_center_output(desktop->layout);
		}
		if (output) {
			layer_surface->output = output;
		} else {
			wlr_layer_surface_v1_close(layer_surface);
			return;
		}
	}

	struct roots_layer_surface *roots_surface =
		calloc(1, sizeof(struct roots_layer_surface));
	if (!roots_surface) {
		return;
	}

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit,
		&roots_surface->surface_commit);

	roots_surface->output_destroy.notify = handle_output_destroy;
	wl_signal_add(&layer_surface->output->events.destroy,
		&roots_surface->output_destroy);

	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&layer_surface->events.destroy, &roots_surface->destroy);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&layer_surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&layer_surface->events.unmap, &roots_surface->unmap);
	roots_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&layer_surface->events.new_popup, &roots_surface->new_popup);
	roots_surface->new_subsurface.notify = handle_new_subsurface;
	wl_signal_add(&layer_surface->surface->events.new_subsurface, &roots_surface->new_subsurface);

	roots_surface->layer_surface = layer_surface;
	layer_surface->data = roots_surface;

	struct roots_output *output = layer_surface->output->data;
	wl_list_insert(&output->layers[layer_surface->client_pending.layer], &roots_surface->link);

	// Temporarily set the layer's current state to client_pending
	// So that we can easily arrange it
	struct wlr_layer_surface_v1_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;

	arrange_layers(output);

	layer_surface->current = old_state;
}
