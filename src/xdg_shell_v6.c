#define G_LOG_DOMAIN "phoc-xdg-shell-v6"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "config.h"
#include "desktop.h"
#include "input.h"
#include "server.h"

static const struct roots_view_child_interface popup_impl;

static void popup_destroy(struct roots_view_child *child) {
	assert(child->impl == &popup_impl);
	struct roots_xdg_popup_v6 *popup = (struct roots_xdg_popup_v6 *)child;
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);
	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	free(popup);
}

static const struct roots_view_child_interface popup_impl = {
	.destroy = popup_destroy,
};

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xdg_popup_v6 *popup =
		wl_container_of(listener, popup, destroy);
	view_child_destroy(&popup->view_child);
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	struct roots_xdg_popup_v6 *popup =
		wl_container_of(listener, popup, map);
	view_damage_whole(popup->view_child.view);
	input_update_cursor_focus(popup->view_child.view->desktop->server->input);
}

static void popup_handle_unmap(struct wl_listener *listener, void *data) {
	struct roots_xdg_popup_v6 *popup =
		wl_container_of(listener, popup, unmap);
	view_damage_whole(popup->view_child.view);
	input_update_cursor_focus(popup->view_child.view->desktop->server->input);
}

static struct roots_xdg_popup_v6 *popup_create(struct roots_view *view,
	struct wlr_xdg_popup_v6 *wlr_popup);

static void popup_handle_new_popup(struct wl_listener *listener, void *data) {
	struct roots_xdg_popup_v6 *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup_v6 *wlr_popup = data;
	popup_create(popup->view_child.view, wlr_popup);
}

static void popup_unconstrain(struct roots_xdg_popup_v6 *popup) {
	// get the output of the popup's positioner anchor point and convert it to
	// the toplevel parent's coordinate system and then pass it to
	// wlr_xdg_popup_v6_unconstrain_from_box

	// TODO: unconstrain popups for rotated windows
	if (popup->view_child.view->rotation != 0.0) {
		return;
	}

	struct roots_view *view = popup->view_child.view;
	struct wlr_output_layout *layout = view->desktop->layout;
	struct wlr_xdg_popup_v6 *wlr_popup = popup->wlr_popup;

	int anchor_lx, anchor_ly;
	wlr_xdg_popup_v6_get_anchor_point(wlr_popup, &anchor_lx, &anchor_ly);

	int popup_lx, popup_ly;
	wlr_xdg_popup_v6_get_toplevel_coords(wlr_popup, wlr_popup->geometry.x,
		wlr_popup->geometry.y, &popup_lx, &popup_ly);
	popup_lx += view->box.x;
	popup_ly += view->box.y;

	anchor_lx += popup_lx;
	anchor_ly += popup_ly;

	double dest_x = 0, dest_y = 0;
	wlr_output_layout_closest_point(layout, NULL, anchor_lx, anchor_ly,
		&dest_x, &dest_y);

	struct wlr_output *output =
		wlr_output_layout_output_at(layout, dest_x, dest_y);
	if (output == NULL) {
		return;
	}

	struct wlr_box *output_box =
		wlr_output_layout_get_box(view->desktop->layout, output);

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_toplevel_sx_box = {
		.x = output_box->x - view->box.x,
		.y = output_box->y - view->box.y,
		.width = output_box->width,
		.height = output_box->height,
	};

	wlr_xdg_popup_v6_unconstrain_from_box(popup->wlr_popup, &output_toplevel_sx_box);
}

static struct roots_xdg_popup_v6 *popup_create(struct roots_view *view,
		struct wlr_xdg_popup_v6 *wlr_popup) {
	struct roots_xdg_popup_v6 *popup =
		calloc(1, sizeof(struct roots_xdg_popup_v6));
	if (popup == NULL) {
		return NULL;
	}
	popup->wlr_popup = wlr_popup;
	view_child_init(&popup->view_child, &popup_impl,
		view, wlr_popup->base->surface);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
	popup->map.notify = popup_handle_map;
	wl_signal_add(&wlr_popup->base->events.map, &popup->map);
	popup->unmap.notify = popup_handle_unmap;
	wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

	popup_unconstrain(popup);

	return popup;
}


static void get_size(struct roots_view *view, struct wlr_box *box) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;

	struct wlr_box geo_box;
	wlr_xdg_surface_v6_get_geometry(surface, &geo_box);
	box->width = geo_box.width;
	box->height = geo_box.height;
}

static void activate(struct roots_view *view, bool active) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_set_activated(surface, active);
	}
}

static void apply_size_constraints(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height, uint32_t *dest_width,
		uint32_t *dest_height) {
	*dest_width = width;
	*dest_height = height;

	struct wlr_xdg_toplevel_v6_state *state = &surface->toplevel->current;
	if (width < state->min_width) {
		*dest_width = state->min_width;
	} else if (state->max_width > 0 &&
			width > state->max_width) {
		*dest_width = state->max_width;
	}
	if (height < state->min_height) {
		*dest_height = state->min_height;
	} else if (state->max_height > 0 &&
			height > state->max_height) {
		*dest_height = state->max_height;
	}
}

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(surface, width, height, &constrained_width,
		&constrained_height);

	wlr_xdg_toplevel_v6_set_size(surface, constrained_width,
		constrained_height);
}

static void move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	struct roots_xdg_surface_v6 *roots_surface =
		roots_xdg_surface_v6_from_view(view);
	struct wlr_xdg_surface_v6 *surface = roots_surface->xdg_surface_v6;
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;

	uint32_t constrained_width, constrained_height;
	apply_size_constraints(surface, width, height, &constrained_width,
		&constrained_height);

	if (update_x) {
		x = x + width - constrained_width;
	}
	if (update_y) {
		y = y + height - constrained_height;
	}

	view->pending_move_resize.update_x = update_x;
	view->pending_move_resize.update_y = update_y;
	view->pending_move_resize.x = x;
	view->pending_move_resize.y = y;
	view->pending_move_resize.width = constrained_width;
	view->pending_move_resize.height = constrained_height;

	uint32_t serial = wlr_xdg_toplevel_v6_set_size(surface, constrained_width,
		constrained_height);
	if (serial > 0) {
		roots_surface->pending_move_resize_configure_serial = serial;
	} else if (roots_surface->pending_move_resize_configure_serial == 0) {
		view_update_position(view, x, y);
	}
}

static bool want_auto_maximize(struct roots_view *view) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;

	return surface->toplevel && !surface->toplevel->parent;
}

static void maximize(struct roots_view *view, bool maximized) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	wlr_xdg_toplevel_v6_set_maximized(surface, maximized);
}

static void set_fullscreen(struct roots_view *view, bool fullscreen) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	wlr_xdg_toplevel_v6_set_fullscreen(surface, fullscreen);
}

static void _close(struct roots_view *view) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	struct wlr_xdg_popup_v6 *popup = NULL;
	wl_list_for_each(popup, &surface->popups, link) {
		wlr_xdg_surface_v6_send_close(popup->base);
	}
	wlr_xdg_surface_v6_send_close(surface);
}

static void for_each_surface(struct roots_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xdg_surface_v6 *surface =
		roots_xdg_surface_v6_from_view(view)->xdg_surface_v6;
	wlr_xdg_surface_v6_for_each_surface(surface, iterator, user_data);
}

static void destroy(struct roots_view *view) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		roots_xdg_surface_v6_from_view(view);
	wl_list_remove(&roots_xdg_surface->surface_commit.link);
	wl_list_remove(&roots_xdg_surface->destroy.link);
	wl_list_remove(&roots_xdg_surface->new_popup.link);
	wl_list_remove(&roots_xdg_surface->map.link);
	wl_list_remove(&roots_xdg_surface->unmap.link);
	wl_list_remove(&roots_xdg_surface->request_move.link);
	wl_list_remove(&roots_xdg_surface->request_resize.link);
	wl_list_remove(&roots_xdg_surface->request_maximize.link);
	wl_list_remove(&roots_xdg_surface->request_fullscreen.link);
	wl_list_remove(&roots_xdg_surface->set_title.link);
	wl_list_remove(&roots_xdg_surface->set_app_id.link);
	wl_list_remove(&roots_xdg_surface->set_parent.link);
	free(roots_xdg_surface);
}

static const struct roots_view_interface view_impl = {
	.activate = activate,
	.resize = resize,
	.move_resize = move_resize,
	.want_auto_maximize = want_auto_maximize,
	.maximize = maximize,
	.set_fullscreen = set_fullscreen,
	.close = _close,
	.for_each_surface = for_each_surface,
	.destroy = destroy,
};

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_move);
	struct roots_view *view = &roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_move_event *e = data;
	struct roots_seat *seat = input_seat_from_wlr_seat(input, e->seat->seat);

	if (view->desktop->maximize) {
		return;
	}

	// TODO verify event serial
	if (!seat || roots_seat_get_cursor(seat)->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_move(seat, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_resize);
	struct roots_view *view = &roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_resize_event *e = data;
	struct roots_seat *seat = input_seat_from_wlr_seat(input, e->seat->seat);

	if (view->desktop->maximize) {
		return;
	}

	// TODO verify event serial
	assert(seat);
	if (!seat || roots_seat_get_cursor(seat)->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_maximize);
	struct roots_view *view = &roots_xdg_surface->view;
	struct wlr_xdg_surface_v6 *surface = roots_xdg_surface->xdg_surface_v6;

	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	view_maximize(view, surface->toplevel->client_pending.maximized);
}

static void handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_fullscreen);
	struct roots_view *view = &roots_xdg_surface->view;
	struct wlr_xdg_surface_v6 *surface = roots_xdg_surface->xdg_surface_v6;
	struct wlr_xdg_toplevel_v6_set_fullscreen_event *e = data;

	if (surface->role != WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		return;
	}

	view_set_fullscreen(view, e->fullscreen, e->output);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, set_title);

	view_set_title(&roots_xdg_surface->view,
		roots_xdg_surface->xdg_surface_v6->toplevel->title);
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, set_app_id);

	view_set_app_id(&roots_xdg_surface->view,
		roots_xdg_surface->xdg_surface_v6->toplevel->app_id);
}

static void handle_set_parent(struct wl_listener* listener, void* data) {
	struct roots_xdg_surface_v6* roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, set_parent);

	if (roots_xdg_surface->xdg_surface_v6->toplevel->parent) {
		struct roots_xdg_surface *parent = roots_xdg_surface->xdg_surface_v6->toplevel->parent->data;
		view_set_parent(&roots_xdg_surface->view, &parent->view);
	} else {
		view_set_parent(&roots_xdg_surface->view, NULL);
	}
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_surface =
		wl_container_of(listener, roots_surface, surface_commit);
	struct roots_view *view = &roots_surface->view;
	struct wlr_xdg_surface_v6 *surface = roots_surface->xdg_surface_v6;

	if (!surface->mapped) {
		return;
	}

	view_apply_damage(view);

	struct wlr_box size;
	get_size(view, &size);
	view_update_size(view, size.width, size.height);

	uint32_t pending_serial =
		roots_surface->pending_move_resize_configure_serial;
	if (pending_serial > 0 && pending_serial >= surface->configure_serial) {
		double x = view->box.x;
		double y = view->box.y;
		if (view->pending_move_resize.update_x) {
			x = view->pending_move_resize.x + view->pending_move_resize.width -
				size.width;
		}
		if (view->pending_move_resize.update_y) {
			y = view->pending_move_resize.y + view->pending_move_resize.height -
				size.height;
		}
		view_update_position(view, x, y);

		if (pending_serial == surface->configure_serial) {
			roots_surface->pending_move_resize_configure_serial = 0;
		}
	}
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, new_popup);
	struct wlr_xdg_popup_v6 *wlr_popup = data;
	popup_create(&roots_xdg_surface->view, wlr_popup);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, map);
	struct roots_view *view = &roots_xdg_surface->view;

	struct wlr_box box;
	get_size(view, &box);
	view->box.width = box.width;
	view->box.height = box.height;

	view_map(view, roots_xdg_surface->xdg_surface_v6->surface);
	view_setup(view);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, unmap);
	view_unmap(&roots_xdg_surface->view);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, destroy);
	view_destroy(&roots_xdg_surface->view);
}

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data) {
	struct wlr_xdg_surface_v6 *surface = data;
	assert(surface->role != WLR_XDG_SURFACE_V6_ROLE_NONE);

	if (surface->role == WLR_XDG_SURFACE_V6_ROLE_POPUP) {
		wlr_log(WLR_DEBUG, "new xdg popup");
		return;
	}

	PhocDesktop *desktop =
		wl_container_of(listener, desktop, xdg_shell_v6_surface);

	wlr_log(WLR_DEBUG, "new xdg toplevel: title=%s, app_id=%s",
		surface->toplevel->title, surface->toplevel->app_id);
	wlr_xdg_surface_v6_ping(surface);

	struct roots_xdg_surface_v6 *roots_surface =
		calloc(1, sizeof(struct roots_xdg_surface_v6));
	if (!roots_surface) {
		return;
	}

	view_init(&roots_surface->view, &view_impl, ROOTS_XDG_SHELL_V6_VIEW, desktop);
	roots_surface->xdg_surface_v6 = surface;
	surface->data = roots_surface;

	// catch up with state accumulated before commiting
	if (surface->toplevel->parent) {
		struct roots_xdg_surface* parent = surface->toplevel->parent->data;
		view_set_parent(&roots_surface->view, &parent->view);
	}
	view_maximize(&roots_surface->view, surface->toplevel->client_pending.maximized);
	view_set_fullscreen(&roots_surface->view, surface->toplevel->client_pending.fullscreen,
		surface->toplevel->client_pending.fullscreen_output);
	view_auto_maximize(&roots_surface->view);
	view_set_title(&roots_surface->view, surface->toplevel->title);
	view_set_app_id(&roots_surface->view, surface->toplevel->app_id);

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->surface->events.commit,
		&roots_surface->surface_commit);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&surface->events.unmap, &roots_surface->unmap);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->toplevel->events.request_move,
		&roots_surface->request_move);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->toplevel->events.request_resize,
		&roots_surface->request_resize);
	roots_surface->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&surface->toplevel->events.request_maximize,
		&roots_surface->request_maximize);
	roots_surface->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&surface->toplevel->events.request_fullscreen,
		&roots_surface->request_fullscreen);
	roots_surface->set_title.notify = handle_set_title;
	wl_signal_add(&surface->toplevel->events.set_title,
		&roots_surface->set_title);
	roots_surface->set_app_id.notify = handle_set_app_id;
	wl_signal_add(&surface->toplevel->events.set_app_id,
		&roots_surface->set_app_id);
	roots_surface->set_parent.notify = handle_set_parent;
	wl_signal_add(&surface->toplevel->events.set_parent,
		&roots_surface->set_parent);
	roots_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&surface->events.new_popup, &roots_surface->new_popup);
}

struct roots_xdg_surface_v6 *roots_xdg_surface_v6_from_view(
		struct roots_view *view) {
	assert(view->impl == &view_impl);
	return (struct roots_xdg_surface_v6 *)view;
}
