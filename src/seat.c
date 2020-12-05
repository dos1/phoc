#define G_LOG_DOMAIN "phoc-seat"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/config.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include "cursor.h"
#include "keyboard.h"
#include "seat.h"
#include "text_input.h"
#include "touch.h"
#include "xcursor.h"

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	PhocKeyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_key);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, keyboard->seat->seat);
	struct wlr_event_keyboard_key *event = data;
	phoc_keyboard_handle_key(keyboard, event);
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
        PhocServer *server = phoc_server_get_default ();
	PhocKeyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_modifiers);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, keyboard->seat->seat);
	phoc_keyboard_handle_modifiers(keyboard);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_motion *event = data;
	roots_cursor_handle_motion(cursor, event);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion_absolute);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_motion_absolute *event = data;
	roots_cursor_handle_motion_absolute(cursor, event);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, button);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_button *event = data;
	roots_cursor_handle_button(cursor, event);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, axis);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_axis *event = data;
	roots_cursor_handle_axis(cursor, event);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, frame);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	roots_cursor_handle_frame(cursor);
}

static void handle_swipe_begin(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, swipe_begin);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_swipe_begin *event = data;
	wlr_pointer_gestures_v1_send_swipe_begin(gestures, cursor->seat->seat,
			event->time_msec, event->fingers);
}

static void handle_swipe_update(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, swipe_update);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_swipe_update *event = data;
	wlr_pointer_gestures_v1_send_swipe_update(gestures, cursor->seat->seat,
			event->time_msec, event->dx, event->dy);
}

static void handle_swipe_end(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, swipe_end);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_swipe_end *event = data;
	wlr_pointer_gestures_v1_send_swipe_end(gestures, cursor->seat->seat,
			event->time_msec, event->cancelled);
}

static void handle_pinch_begin(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, pinch_begin);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_pinch_begin *event = data;
	wlr_pointer_gestures_v1_send_pinch_begin(gestures, cursor->seat->seat,
			event->time_msec, event->fingers);
}

static void handle_pinch_update(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, pinch_update);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_pinch_update *event = data;
	wlr_pointer_gestures_v1_send_pinch_update(gestures, cursor->seat->seat,
			event->time_msec, event->dx, event->dy,
			event->scale, event->rotation);
}

static void handle_pinch_end(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, pinch_end);
	struct wlr_pointer_gestures_v1 *gestures =
		server->desktop->pointer_gestures;
	struct wlr_event_pointer_pinch_end *event = data;
	wlr_pointer_gestures_v1_send_pinch_end(gestures, cursor->seat->seat,
			event->time_msec, event->cancelled);
}

static void handle_switch_toggle(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_switch *switch_device =
		wl_container_of(listener, switch_device, toggle);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, switch_device->seat->seat);
	struct wlr_event_switch_toggle *event = data;
	roots_switch_handle_toggle(switch_device, event);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_down);
	struct wlr_event_touch_down *event = data;
	PhocDesktop *desktop = server->desktop;
	PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
							   event->device->name);
	if (output && !output->wlr_output->enabled) {
		g_debug("Touch event ignored since output '%s' is disabled.",
			output->wlr_output->name);
		return;
	}
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	roots_cursor_handle_touch_down(cursor, event);
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_up);
	struct wlr_event_touch_up *event = data;
	PhocDesktop *desktop = server->desktop;
	PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
							   event->device->name);
	/* handle touch up regardless of output status so events don't become stuck */
	roots_cursor_handle_touch_up(cursor, event);
	if (output && !output->wlr_output->enabled) {
		g_debug("Touch event ignored since output '%s' is disabled.",
			output->wlr_output->name);
		return;
	}
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_motion);
	struct wlr_event_touch_motion *event = data;
	PhocDesktop *desktop = server->desktop;
	PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
							   event->device->name);
	/* handle touch motion regardless of output status so events don't become
	   stuck */
	roots_cursor_handle_touch_motion(cursor, event);
	if (output && !output->wlr_output->enabled) {
		g_debug("Touch event ignored since output '%s' is disabled.",
			output->wlr_output->name);
		return;
	}
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
}

static void handle_tablet_tool_position(struct roots_cursor *cursor,
		struct roots_tablet *tablet,
		struct wlr_tablet_tool *tool,
		bool change_x, bool change_y,
		double x, double y, double dx, double dy) {
        PhocServer *server = phoc_server_get_default ();
	if (!change_x && !change_y) {
		return;
	}

	switch (tool->type) {
	case WLR_TABLET_TOOL_TYPE_MOUSE:
		// They are 0 either way when they weren't modified
		wlr_cursor_move(cursor->cursor, tablet->device, dx, dy);
		break;
	default:
		wlr_cursor_warp_absolute(cursor->cursor, tablet->device,
			change_x ? x : NAN, change_y ? y : NAN);
	}

	double sx, sy;
	PhocDesktop *desktop = server->desktop;
	struct wlr_surface *surface = desktop_surface_at(desktop,
			cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);
	struct roots_tablet_tool *roots_tool = tool->data;

	if (!surface) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(roots_tool->tablet_v2_tool);
		/* XXX: TODO: Fallback pointer semantics */
		return;
	}

	if (!wlr_surface_accepts_tablet_v2(tablet->tablet_v2, surface)) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(roots_tool->tablet_v2_tool);
		/* XXX: TODO: Fallback pointer semantics */
		return;
	}

	wlr_tablet_v2_tablet_tool_notify_proximity_in(roots_tool->tablet_v2_tool,
		tablet->tablet_v2, surface);

	wlr_tablet_v2_tablet_tool_notify_motion(roots_tool->tablet_v2_tool, sx, sy);
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
        PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_axis);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_axis *event = data;
	struct roots_tablet_tool *roots_tool = event->tool->data;

	if (!roots_tool) { // Should this be an assert?
		wlr_log(WLR_DEBUG, "Tool Axis, before proximity");
		return;
	}

	/**
	 * We need to handle them ourselves, not pass it into the cursor
	 * without any consideration
	 */
	handle_tablet_tool_position(cursor, event->device->data, event->tool,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_X,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_Y,
		event->x, event->y, event->dx, event->dy);

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
		wlr_tablet_v2_tablet_tool_notify_pressure(
			roots_tool->tablet_v2_tool, event->pressure);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
		wlr_tablet_v2_tablet_tool_notify_distance(
			roots_tool->tablet_v2_tool, event->distance);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X) {
		roots_tool->tilt_x = event->tilt_x;
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y) {
		roots_tool->tilt_y = event->tilt_y;
	}

	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
		wlr_tablet_v2_tablet_tool_notify_tilt(
			roots_tool->tablet_v2_tool,
			roots_tool->tilt_x, roots_tool->tilt_y);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
		wlr_tablet_v2_tablet_tool_notify_rotation(
			roots_tool->tablet_v2_tool, event->rotation);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
		wlr_tablet_v2_tablet_tool_notify_slider(
			roots_tool->tablet_v2_tool, event->slider);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
		wlr_tablet_v2_tablet_tool_notify_wheel(
			roots_tool->tablet_v2_tool, event->wheel_delta, 0);
	}
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_tip);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_tip *event = data;
	struct roots_tablet_tool *roots_tool = event->tool->data;

	if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
		wlr_tablet_v2_tablet_tool_notify_down(roots_tool->tablet_v2_tool);
		wlr_tablet_tool_v2_start_implicit_grab(roots_tool->tablet_v2_tool);
	} else {
		wlr_tablet_v2_tablet_tool_notify_up(roots_tool->tablet_v2_tool);
	}
}

static void handle_tablet_tool_destroy(struct wl_listener *listener, void *data) {
	struct roots_tablet_tool *tool =
		wl_container_of(listener, tool, tool_destroy);

	wl_list_remove(&tool->link);
	wl_list_remove(&tool->tool_link);

	wl_list_remove(&tool->tool_destroy.link);
	wl_list_remove(&tool->set_cursor.link);

	free(tool);
}

static void handle_tool_button(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_button);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_button *event = data;
	struct roots_tablet_tool *roots_tool = event->tool->data;

	wlr_tablet_v2_tablet_tool_notify_button(roots_tool->tablet_v2_tool,
		(enum zwp_tablet_pad_v2_button_state)event->button,
		(enum zwp_tablet_pad_v2_button_state)event->state);
}

static void handle_tablet_tool_set_cursor(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_tablet_tool *tool =
		wl_container_of(listener, tool, set_cursor);
	struct wlr_tablet_v2_event_cursor *evt = data;
	PhocDesktop *desktop = server->desktop;

	struct wlr_seat_pointer_request_set_cursor_event event = {
		.surface = evt->surface,
		.hotspot_x = evt->hotspot_x,
		.hotspot_y = evt->hotspot_y,
		.serial = evt->serial,
		.seat_client = evt->seat_client,
	};

	wlr_idle_notify_activity(desktop->idle, tool->seat->seat);
	roots_cursor_handle_request_set_cursor(tool->seat->cursor, &event);
}

static void handle_tool_proximity(struct wl_listener *listener, void *data) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_proximity);
	PhocDesktop *desktop = server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_proximity *event = data;

	struct wlr_tablet_tool *tool = event->tool;
	if (!tool->data) {
		struct roots_tablet_tool *roots_tool =
			calloc(1, sizeof(struct roots_tablet_tool));
		roots_tool->seat = cursor->seat;
		tool->data = roots_tool;
		roots_tool->tablet_v2_tool =
			wlr_tablet_tool_create(desktop->tablet_v2,
				cursor->seat->seat, tool);
		roots_tool->tool_destroy.notify = handle_tablet_tool_destroy;
		wl_signal_add(&tool->events.destroy, &roots_tool->tool_destroy);

		roots_tool->set_cursor.notify = handle_tablet_tool_set_cursor;
		wl_signal_add(&roots_tool->tablet_v2_tool->events.set_cursor,
			&roots_tool->set_cursor);

		wl_list_init(&roots_tool->link);
		wl_list_init(&roots_tool->tool_link);
	}

	if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
		struct roots_tablet_tool *roots_tool = tool->data;
		wlr_tablet_v2_tablet_tool_notify_proximity_out(roots_tool->tablet_v2_tool);
		return;
	}

	handle_tablet_tool_position(cursor, event->device->data, event->tool,
		true, true, event->x, event->y, 0, 0);
}

static void handle_request_set_cursor(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	roots_cursor_handle_request_set_cursor(cursor, event);
}

static void handle_pointer_focus_change(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	roots_cursor_handle_focus_change(cursor, event);
}

static void seat_reset_device_mappings(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct wlr_cursor *cursor = seat->cursor->cursor;
	wlr_cursor_map_input_to_output(cursor, device, NULL);
}

static void seat_set_device_output_mappings(struct roots_seat *seat,
		struct wlr_input_device *device, PhocOutput *output) {
	struct wlr_cursor *cursor = seat->cursor->cursor;

	switch (device->type) {
	  /* only map devices with absolute posistions */
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET_TOOL:
	case WLR_INPUT_DEVICE_TABLET_PAD:
	  break;
	default:
	  return;
	}

	if (!phoc_output_is_builtin (output))
	  return;

	g_debug ("Mapping %s to %s", device->name, output->wlr_output->name);
	wlr_cursor_map_input_to_output(cursor, device, output->wlr_output);
}

void roots_seat_configure_cursor(struct roots_seat *seat) {
	PhocServer *server = phoc_server_get_default ();
	PhocDesktop *desktop = server->desktop;
	struct wlr_cursor *cursor = seat->cursor->cursor;

	struct roots_pointer *pointer;
	PhocTouch *touch;
	struct roots_tablet *tablet;
	PhocOutput *output;

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wl_list_for_each(pointer, &seat->pointers, link) {
		seat_reset_device_mappings(seat, pointer->device);
	}
	wl_list_for_each(touch, &seat->touch, link) {
		seat_reset_device_mappings(seat, touch->device);
	}
	wl_list_for_each(tablet, &seat->tablets, link) {
		seat_reset_device_mappings(seat, tablet->device);
	}

	// configure device to output mappings
	wl_list_for_each(output, &desktop->outputs, link) {
		wl_list_for_each(pointer, &seat->pointers, link) {
			seat_set_device_output_mappings(seat, pointer->device,
				output);
		}
		wl_list_for_each(tablet, &seat->tablets, link) {
			seat_set_device_output_mappings(seat, tablet->device,
				output);
		}
		wl_list_for_each(touch, &seat->touch, link) {
			seat_set_device_output_mappings(seat, touch->device,
				output);
			g_debug("Added mapping for touch device '%s' to output '%s'",
				touch->device->name,
				output->wlr_output->name);
			g_hash_table_insert (desktop->input_output_map,
					     g_strdup (touch->device->name),
					     output);
		}
	}
}

static void roots_seat_init_cursor(struct roots_seat *seat) {
	PhocServer *server = phoc_server_get_default ();
	seat->cursor = roots_cursor_create(seat);
	if (!seat->cursor) {
		return;
	}
	seat->cursor->seat = seat;
	struct wlr_cursor *wlr_cursor = seat->cursor->cursor;
	PhocDesktop *desktop = server->desktop;
	wlr_cursor_attach_output_layout(wlr_cursor, desktop->layout);

	roots_seat_configure_cursor(seat);
	roots_seat_configure_xcursor(seat);

	// add input signals
	wl_signal_add(&wlr_cursor->events.motion, &seat->cursor->motion);
	seat->cursor->motion.notify = handle_cursor_motion;

	wl_signal_add(&wlr_cursor->events.motion_absolute,
		&seat->cursor->motion_absolute);
	seat->cursor->motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&wlr_cursor->events.button, &seat->cursor->button);
	seat->cursor->button.notify = handle_cursor_button;

	wl_signal_add(&wlr_cursor->events.axis, &seat->cursor->axis);
	seat->cursor->axis.notify = handle_cursor_axis;

	wl_signal_add(&wlr_cursor->events.frame, &seat->cursor->frame);
	seat->cursor->frame.notify = handle_cursor_frame;

	wl_signal_add(&wlr_cursor->events.swipe_begin, &seat->cursor->swipe_begin);
	seat->cursor->swipe_begin.notify = handle_swipe_begin;

	wl_signal_add(&wlr_cursor->events.swipe_update, &seat->cursor->swipe_update);
	seat->cursor->swipe_update.notify = handle_swipe_update;

	wl_signal_add(&wlr_cursor->events.swipe_end, &seat->cursor->swipe_end);
	seat->cursor->swipe_end.notify = handle_swipe_end;

	wl_signal_add(&wlr_cursor->events.pinch_begin, &seat->cursor->pinch_begin);
	seat->cursor->pinch_begin.notify = handle_pinch_begin;

	wl_signal_add(&wlr_cursor->events.pinch_update, &seat->cursor->pinch_update);
	seat->cursor->pinch_update.notify = handle_pinch_update;

	wl_signal_add(&wlr_cursor->events.pinch_end, &seat->cursor->pinch_end);
	seat->cursor->pinch_end.notify = handle_pinch_end;

	wl_signal_add(&wlr_cursor->events.touch_down, &seat->cursor->touch_down);
	seat->cursor->touch_down.notify = handle_touch_down;

	wl_signal_add(&wlr_cursor->events.touch_up, &seat->cursor->touch_up);
	seat->cursor->touch_up.notify = handle_touch_up;

	wl_signal_add(&wlr_cursor->events.touch_motion,
		&seat->cursor->touch_motion);
	seat->cursor->touch_motion.notify = handle_touch_motion;

	wl_signal_add(&wlr_cursor->events.tablet_tool_axis,
		&seat->cursor->tool_axis);
	seat->cursor->tool_axis.notify = handle_tool_axis;

	wl_signal_add(&wlr_cursor->events.tablet_tool_tip, &seat->cursor->tool_tip);
	seat->cursor->tool_tip.notify = handle_tool_tip;

	wl_signal_add(&wlr_cursor->events.tablet_tool_proximity, &seat->cursor->tool_proximity);
	seat->cursor->tool_proximity.notify = handle_tool_proximity;

	wl_signal_add(&wlr_cursor->events.tablet_tool_button, &seat->cursor->tool_button);
	seat->cursor->tool_button.notify = handle_tool_button;

	wl_signal_add(&seat->seat->events.request_set_cursor,
		&seat->cursor->request_set_cursor);
	seat->cursor->request_set_cursor.notify = handle_request_set_cursor;

	wl_signal_add(&seat->seat->pointer_state.events.focus_change,
		&seat->cursor->focus_change);
	seat->cursor->focus_change.notify = handle_pointer_focus_change;

	wl_list_init(&seat->cursor->constraint_commit.link);
}

static void roots_drag_icon_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct roots_drag_icon *icon =
		wl_container_of(listener, icon, surface_commit);
	roots_drag_icon_update_position(icon);
}

static void roots_drag_icon_handle_map(struct wl_listener *listener,
		void *data) {
	struct roots_drag_icon *icon =
		wl_container_of(listener, icon, map);
	roots_drag_icon_damage_whole(icon);
}

static void roots_drag_icon_handle_unmap(struct wl_listener *listener,
		void *data) {
	struct roots_drag_icon *icon =
		wl_container_of(listener, icon, unmap);
	roots_drag_icon_damage_whole(icon);
}

static void roots_drag_icon_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_drag_icon *icon =
		wl_container_of(listener, icon, destroy);
	roots_drag_icon_damage_whole(icon);

	assert(icon->seat->drag_icon == icon);
	icon->seat->drag_icon = NULL;

	wl_list_remove(&icon->surface_commit.link);
	wl_list_remove(&icon->unmap.link);
	wl_list_remove(&icon->destroy.link);
	free(icon);
}

static void roots_seat_handle_request_start_drag(struct wl_listener *listener,
		void *data) {
	struct roots_seat *seat =
		wl_container_of(listener, seat, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat->seat,
			event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(seat->seat, event->drag, event->serial);
		return;
	}

	struct wlr_touch_point *point;
	if (wlr_seat_validate_touch_grab_serial(seat->seat,
			event->origin, event->serial, &point)) {
		wlr_seat_start_touch_drag(seat->seat, event->drag, event->serial, point);
		return;
	}

	wlr_log(WLR_DEBUG, "Ignoring start_drag request: "
		"could not validate pointer or touch serial %" PRIu32, event->serial);
	wlr_data_source_destroy(event->drag->source);
}

static void roots_seat_handle_start_drag(struct wl_listener *listener,
		void *data) {
	struct roots_seat *seat = wl_container_of(listener, seat, start_drag);
	struct wlr_drag *wlr_drag = data;
	struct wlr_drag_icon *wlr_drag_icon = wlr_drag->icon;
	if (wlr_drag_icon == NULL) {
		return;
	}

	struct roots_drag_icon *icon = calloc(1, sizeof(struct roots_drag_icon));
	if (icon == NULL) {
		return;
	}
	icon->seat = seat;
	icon->wlr_drag_icon = wlr_drag_icon;

	icon->surface_commit.notify = roots_drag_icon_handle_surface_commit;
	wl_signal_add(&wlr_drag_icon->surface->events.commit, &icon->surface_commit);
	icon->unmap.notify = roots_drag_icon_handle_unmap;
	wl_signal_add(&wlr_drag_icon->events.unmap, &icon->unmap);
	icon->map.notify = roots_drag_icon_handle_map;
	wl_signal_add(&wlr_drag_icon->events.map, &icon->map);
	icon->destroy.notify = roots_drag_icon_handle_destroy;
	wl_signal_add(&wlr_drag_icon->events.destroy, &icon->destroy);

	assert(seat->drag_icon == NULL);
	seat->drag_icon = icon;

	roots_drag_icon_update_position(icon);
}

static void roots_seat_handle_request_set_selection(
		struct wl_listener *listener, void *data) {
	struct roots_seat *seat =
		wl_container_of(listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

static void roots_seat_handle_request_set_primary_selection(
		struct wl_listener *listener, void *data) {
	struct roots_seat *seat =
		wl_container_of(listener, seat, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat->seat, event->source, event->serial);
}

void roots_drag_icon_update_position(struct roots_drag_icon *icon) {
	roots_drag_icon_damage_whole(icon);

	struct roots_seat *seat = icon->seat;
	struct wlr_drag *wlr_drag = icon->wlr_drag_icon->drag;
	assert(wlr_drag != NULL);

	switch (seat->seat->drag->grab_type) {
	case WLR_DRAG_GRAB_KEYBOARD:
		assert(false);
	case WLR_DRAG_GRAB_KEYBOARD_POINTER:;
		struct wlr_cursor *cursor = seat->cursor->cursor;
		icon->x = cursor->x;
		icon->y = cursor->y;
		break;
	case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
		struct wlr_touch_point *point =
			wlr_seat_touch_get_point(seat->seat, wlr_drag->touch_id);
		if (point == NULL) {
			return;
		}
		icon->x = seat->touch_x;
		icon->y = seat->touch_y;
		break;
	default:
		g_error("Invalid drag grab type %d", seat->seat->drag->grab_type);
	}

	roots_drag_icon_damage_whole(icon);
}

void roots_drag_icon_damage_whole(struct roots_drag_icon *icon) {
	PhocServer *server = phoc_server_get_default ();
	PhocOutput *output;
	wl_list_for_each(output, &server->desktop->outputs,
			link) {
		phoc_output_damage_whole_drag_icon(output, icon);
	}
}

static void seat_view_destroy(struct roots_seat_view *seat_view);

static void roots_seat_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_seat *seat = wl_container_of(listener, seat, destroy);

	// TODO: probably more to be freed here
	wl_list_remove(&seat->destroy.link);

	struct roots_seat_view *view, *nview;
	wl_list_for_each_safe(view, nview, &seat->views, link) {
		seat_view_destroy(view);
	}
}

void roots_seat_destroy(struct roots_seat *seat) {
	roots_seat_handle_destroy(&seat->destroy, seat->seat);
	wlr_seat_destroy(seat->seat);
}

struct roots_seat *roots_seat_create(PhocInput *input, char *name) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_seat *seat = calloc(1, sizeof(struct roots_seat));
	if (!seat) {
		return NULL;
	}

	wl_list_init(&seat->keyboards);
	wl_list_init(&seat->pointers);
	wl_list_init(&seat->touch);
	wl_list_init(&seat->tablets);
	wl_list_init(&seat->tablet_pads);
	wl_list_init(&seat->switches);
	wl_list_init(&seat->views);

	seat->input = input;

	seat->seat = wlr_seat_create(server->wl_display, name);
	if (!seat->seat) {
		free(seat);
		return NULL;
	}
	seat->seat->data = seat;

	roots_seat_init_cursor(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->seat);
		free(seat);
		return NULL;
	}

	roots_input_method_relay_init(seat, &seat->im_relay);

	wl_list_insert(&input->seats, &seat->link);

	seat->request_set_selection.notify =
		roots_seat_handle_request_set_selection;
	wl_signal_add(&seat->seat->events.request_set_selection,
		&seat->request_set_selection);
	seat->request_set_primary_selection.notify =
		roots_seat_handle_request_set_primary_selection;
	wl_signal_add(&seat->seat->events.request_set_primary_selection,
		&seat->request_set_primary_selection);
	seat->request_start_drag.notify = roots_seat_handle_request_start_drag;
	wl_signal_add(&seat->seat->events.request_start_drag,
		&seat->request_start_drag);
	seat->start_drag.notify = roots_seat_handle_start_drag;
	wl_signal_add(&seat->seat->events.start_drag, &seat->start_drag);
	seat->destroy.notify = roots_seat_handle_destroy;
	wl_signal_add(&seat->seat->events.destroy, &seat->destroy);

	return seat;
}

static void seat_update_capabilities(struct roots_seat *seat) {
	uint32_t caps = 0;
	if (!wl_list_empty(&seat->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&seat->pointers) || !wl_list_empty(&seat->tablets)) {
		caps |= WL_SEAT_CAPABILITY_POINTER;
	}
	if (!wl_list_empty(&seat->touch)) {
		caps |= WL_SEAT_CAPABILITY_TOUCH;
	}
	wlr_seat_set_capabilities(seat->seat, caps);

	roots_seat_maybe_set_cursor (seat, seat->cursor->default_xcursor);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
	PhocKeyboard *keyboard =
		wl_container_of(listener, keyboard, device_destroy);
	struct roots_seat *seat = keyboard->seat;
	wl_list_remove(&keyboard->device_destroy.link);
	wl_list_remove(&keyboard->keyboard_key.link);
	wl_list_remove(&keyboard->keyboard_modifiers.link);
	g_object_unref (keyboard);
	seat_update_capabilities(seat);
}

static void seat_add_keyboard(struct roots_seat *seat,
		struct wlr_input_device *device) {
	assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
	PhocKeyboard *keyboard = phoc_keyboard_new (device, seat);

	wl_list_insert(&seat->keyboards, &keyboard->link);

	keyboard->device_destroy.notify = handle_keyboard_destroy;
	wl_signal_add(&keyboard->device->events.destroy, &keyboard->device_destroy);
	keyboard->keyboard_key.notify = handle_keyboard_key;
	wl_signal_add(&keyboard->device->keyboard->events.key,
		&keyboard->keyboard_key);
	keyboard->keyboard_modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&keyboard->device->keyboard->events.modifiers,
		&keyboard->keyboard_modifiers);

	wlr_seat_set_keyboard(seat->seat, device);
}

static void handle_pointer_destroy(struct wl_listener *listener, void *data) {
	struct roots_pointer *pointer =
		wl_container_of(listener, pointer, device_destroy);
	struct roots_seat *seat = pointer->seat;

	wl_list_remove(&pointer->link);
	wlr_cursor_detach_input_device(seat->cursor->cursor, pointer->device);
	wl_list_remove(&pointer->device_destroy.link);
	free(pointer);

	seat_update_capabilities(seat);
}

static void seat_add_pointer(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_pointer *pointer = calloc(1, sizeof(struct roots_pointer));
	if (!pointer) {
		wlr_log(WLR_ERROR, "could not allocate pointer for seat");
		return;
	}

	device->data = pointer;
	pointer->device = device;
	pointer->seat = seat;
	wl_list_insert(&seat->pointers, &pointer->link);

	pointer->device_destroy.notify = handle_pointer_destroy;
	wl_signal_add(&pointer->device->events.destroy, &pointer->device_destroy);

	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void handle_switch_destroy(struct wl_listener *listener, void *data) {
	struct roots_switch *switch_device =
		wl_container_of(listener, switch_device, device_destroy);
	struct roots_seat *seat = switch_device->seat;

	wl_list_remove(&switch_device->link);
	wl_list_remove(&switch_device->device_destroy.link);
	free(switch_device);

	seat_update_capabilities(seat);
}

static void seat_add_switch(struct roots_seat *seat,
		struct wlr_input_device *device) {
	assert(device->type == WLR_INPUT_DEVICE_SWITCH);
	struct roots_switch *switch_device = calloc(1, sizeof(struct roots_switch));
	if (!switch_device) {
		wlr_log(WLR_ERROR, "could not allocate switch for seat");
		return;
	}
	device->data = switch_device;
	switch_device->device = device;
	switch_device->seat = seat;
	wl_list_insert(&seat->switches, &switch_device->link);
	switch_device->device_destroy.notify = handle_switch_destroy;

	switch_device->toggle.notify = handle_switch_toggle;
	wl_signal_add(&switch_device->device->switch_device->events.toggle, &switch_device->toggle);
}

static void
handle_touch_destroy(PhocTouch *touch)
{
	struct roots_seat *seat = touch->seat;
	PhocServer *server = phoc_server_get_default ();
	PhocDesktop *desktop = server->desktop;

	g_debug("Removing touch device: %s", touch->device->name);
	g_hash_table_remove (desktop->input_output_map, touch->device->name);
	wl_list_remove(&touch->link);
	wlr_cursor_detach_input_device(seat->cursor->cursor, touch->device);
	g_object_unref (touch);

	seat_update_capabilities(seat);
}

static void seat_add_touch(struct roots_seat *seat,
		struct wlr_input_device *device) {
	PhocTouch *touch = phoc_touch_new (device, seat);

	wl_list_insert(&seat->touch, &touch->link);

	g_signal_connect(touch, "touch-destroyed",
			 G_CALLBACK (handle_touch_destroy),
			 NULL);

	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void handle_tablet_pad_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_tablet_pad *tablet_pad =
		wl_container_of(listener, tablet_pad, device_destroy);
	struct roots_seat *seat = tablet_pad->seat;

	wl_list_remove(&tablet_pad->device_destroy.link);
	wl_list_remove(&tablet_pad->tablet_destroy.link);
	wl_list_remove(&tablet_pad->attach.link);
	wl_list_remove(&tablet_pad->link);

	wl_list_remove(&tablet_pad->button.link);
	wl_list_remove(&tablet_pad->strip.link);
	wl_list_remove(&tablet_pad->ring.link);
	free(tablet_pad);

	seat_update_capabilities(seat);
}

static void handle_pad_tool_destroy(struct wl_listener *listener, void *data) {
	struct roots_tablet_pad *pad =
		wl_container_of(listener, pad, tablet_destroy);

	pad->tablet = NULL;

	wl_list_remove(&pad->tablet_destroy.link);
	wl_list_init(&pad->tablet_destroy.link);
}

static void attach_tablet_pad(struct roots_tablet_pad *pad,
		struct roots_tablet *tool) {
	wlr_log(WLR_DEBUG, "Attaching tablet pad \"%s\" to tablet tool \"%s\"",
		pad->device->name, tool->device->name);

	pad->tablet = tool;

	wl_list_remove(&pad->tablet_destroy.link);
	pad->tablet_destroy.notify = handle_pad_tool_destroy;
	wl_signal_add(&tool->device->events.destroy, &pad->tablet_destroy);
}

static void handle_tablet_pad_attach(struct wl_listener *listener, void *data) {
	struct roots_tablet_pad *pad =
		wl_container_of(listener, pad, attach);
	struct wlr_tablet_tool *wlr_tool = data;
	struct roots_tablet *tool = wlr_tool->data;

	attach_tablet_pad(pad, tool);
}

static void handle_tablet_pad_ring(struct wl_listener *listener, void *data) {
	struct roots_tablet_pad *pad =
		wl_container_of(listener, pad, ring);
	struct wlr_event_tablet_pad_ring *event = data;

	wlr_tablet_v2_tablet_pad_notify_ring(pad->tablet_v2_pad,
		event->ring, event->position,
		event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
		event->time_msec);
}

static void handle_tablet_pad_strip(struct wl_listener *listener, void *data) {
	struct roots_tablet_pad *pad =
		wl_container_of(listener, pad, strip);
	struct wlr_event_tablet_pad_strip *event = data;

	wlr_tablet_v2_tablet_pad_notify_strip(pad->tablet_v2_pad,
		event->strip, event->position,
		event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
		event->time_msec);
}

static void handle_tablet_pad_button(struct wl_listener *listener, void *data) {
	struct roots_tablet_pad *pad =
		wl_container_of(listener, pad, button);
	struct wlr_event_tablet_pad_button *event = data;

	wlr_tablet_v2_tablet_pad_notify_mode(pad->tablet_v2_pad,
		event->group, event->mode, event->time_msec);

	wlr_tablet_v2_tablet_pad_notify_button(pad->tablet_v2_pad,
		event->button, event->time_msec,
		(enum zwp_tablet_pad_v2_button_state)event->state);
}

static void seat_add_tablet_pad(struct roots_seat *seat,
		struct wlr_input_device *device) {
	PhocServer *server = phoc_server_get_default ();
	struct roots_tablet_pad *tablet_pad =
		calloc(1, sizeof(struct roots_tablet_pad));
	if (!tablet_pad) {
		wlr_log(WLR_ERROR, "could not allocate tablet_pad for seat");
		return;
	}

	device->data = tablet_pad;
	tablet_pad->device = device;
	tablet_pad->seat = seat;
	wl_list_insert(&seat->tablet_pads, &tablet_pad->link);

	tablet_pad->device_destroy.notify = handle_tablet_pad_destroy;
	wl_signal_add(&tablet_pad->device->events.destroy,
		&tablet_pad->device_destroy);

	tablet_pad->attach.notify = handle_tablet_pad_attach;
	wl_signal_add(&tablet_pad->device->tablet_pad->events.attach_tablet,
		&tablet_pad->attach);

	tablet_pad->button.notify = handle_tablet_pad_button;
	wl_signal_add(&tablet_pad->device->tablet_pad->events.button, &tablet_pad->button);

	tablet_pad->strip.notify = handle_tablet_pad_strip;
	wl_signal_add(&tablet_pad->device->tablet_pad->events.strip, &tablet_pad->strip);

	tablet_pad->ring.notify = handle_tablet_pad_ring;
	wl_signal_add(&tablet_pad->device->tablet_pad->events.ring, &tablet_pad->ring);

	wl_list_init(&tablet_pad->tablet_destroy.link);

	PhocDesktop *desktop = server->desktop;
	tablet_pad->tablet_v2_pad =
		wlr_tablet_pad_create(desktop->tablet_v2, seat->seat, device);

	/* Search for a sibling tablet */
	if (!wlr_input_device_is_libinput(device)) {
		/* We can only do this on libinput devices */
		return;
	}

	struct libinput_device_group *group =
		libinput_device_get_device_group(wlr_libinput_get_device_handle(device));
	struct roots_tablet *tool;
	wl_list_for_each(tool, &seat->tablets, link) {
		if (!wlr_input_device_is_libinput(tool->device)) {
			continue;
		}

		struct libinput_device *li_dev =
			wlr_libinput_get_device_handle(tool->device);
		if (libinput_device_get_device_group(li_dev) == group) {
			attach_tablet_pad(tablet_pad, tool);
			break;
		}
	}
}

static void handle_tablet_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_tablet *tablet =
		wl_container_of(listener, tablet, device_destroy);
	struct roots_seat *seat = tablet->seat;

	wlr_cursor_detach_input_device(seat->cursor->cursor, tablet->device);
	wl_list_remove(&tablet->device_destroy.link);
	wl_list_remove(&tablet->link);
	free(tablet);

	seat_update_capabilities(seat);
}

static void seat_add_tablet_tool(struct roots_seat *seat,
		struct wlr_input_device *device) {
	PhocServer *server = phoc_server_get_default ();

	if (!wlr_input_device_is_libinput (device))
		return;

	struct roots_tablet *tablet =
		calloc(1, sizeof(struct roots_tablet));
	if (!tablet) {
		wlr_log(WLR_ERROR, "could not allocate tablet for seat");
		return;
	}

	device->data = tablet;
	tablet->device = device;
	tablet->seat = seat;
	wl_list_insert(&seat->tablets, &tablet->link);

	tablet->device_destroy.notify = handle_tablet_destroy;
	wl_signal_add(&tablet->device->events.destroy,
		&tablet->device_destroy);

	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);

	PhocDesktop *desktop = server->desktop;

	tablet->tablet_v2 =
		wlr_tablet_create(desktop->tablet_v2, seat->seat, device);

	struct libinput_device_group *group =
		libinput_device_get_device_group(wlr_libinput_get_device_handle(device));
	struct roots_tablet_pad *pad;
	wl_list_for_each(pad, &seat->tablet_pads, link) {
		if (!wlr_input_device_is_libinput(pad->device)) {
			continue;
		}

		struct libinput_device *li_dev =
			wlr_libinput_get_device_handle(pad->device);
		if (libinput_device_get_device_group(li_dev) == group) {
			attach_tablet_pad(pad, tablet);
		}
	}
}

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device) {

	g_debug ("Adding device %s %d", device->name, device->type);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		seat_add_keyboard(seat, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		seat_add_pointer(seat, device);
		break;
	case WLR_INPUT_DEVICE_SWITCH:
		seat_add_switch(seat, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		seat_add_touch(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		seat_add_tablet_pad(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		seat_add_tablet_tool(seat, device);
		break;
	default:
		g_error("Invalid device type %d", device->type);
	}

	seat_update_capabilities(seat);
}

void roots_seat_configure_xcursor(struct roots_seat *seat) {
	PhocServer *server = phoc_server_get_default ();
	const char *cursor_theme = NULL;
	struct roots_cursor_config *cc =
		roots_config_get_cursor(seat->input->config, seat->seat->name);
	if (cc != NULL) {
		cursor_theme = cc->theme;
		if (cc->default_image != NULL) {
			seat->cursor->default_xcursor = cc->default_image;
		}
	}

	if (!seat->cursor->xcursor_manager) {
		seat->cursor->xcursor_manager =
			wlr_xcursor_manager_create(cursor_theme, ROOTS_XCURSOR_SIZE);
		if (seat->cursor->xcursor_manager == NULL) {
			wlr_log(WLR_ERROR, "Cannot create XCursor manager for theme %s",
					cursor_theme);
			return;
		}
	}

	PhocOutput *output;
	wl_list_for_each(output, &server->desktop->outputs, link) {
		float scale = output->wlr_output->scale;
#if (WLR_VERSION_MAJOR > 0 || WLR_VERSION_MINOR > 10)
		if (!wlr_xcursor_manager_load(seat->cursor->xcursor_manager, scale)) {
#else
		if (wlr_xcursor_manager_load(seat->cursor->xcursor_manager, scale)) {
#endif
			wlr_log(WLR_ERROR, "Cannot load xcursor theme for output '%s' "
				"with scale %f", output->wlr_output->name, scale);
		}
	}

	roots_seat_maybe_set_cursor (seat, seat->cursor->default_xcursor);
	wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
		seat->cursor->cursor->y);
}

bool roots_seat_has_meta_pressed(struct roots_seat *seat) {
	PhocKeyboard *keyboard;
	wl_list_for_each(keyboard, &seat->keyboards, link) {
		uint32_t modifiers =
			wlr_keyboard_get_modifiers(keyboard->device->keyboard);
		if ((modifiers ^ keyboard->meta_key) == 0) {
			return true;
		}
	}

	return false;
}

struct roots_view *roots_seat_get_focus(struct roots_seat *seat) {
	if (!seat->has_focus || wl_list_empty(&seat->views)) {
		return NULL;
	}
	struct roots_seat_view *seat_view =
		wl_container_of(seat->views.next, seat_view, link);
	return seat_view->view;
}

static void seat_view_destroy(struct roots_seat_view *seat_view) {
	struct roots_seat *seat = seat_view->seat;
	struct roots_view *view = seat_view->view;

	if (view == roots_seat_get_focus(seat)) {
		seat->has_focus = false;
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
	}

	if (seat_view == seat->cursor->pointer_view) {
		seat->cursor->pointer_view = NULL;
	}

	wl_list_remove(&seat_view->view_unmap.link);
	wl_list_remove(&seat_view->view_destroy.link);
	wl_list_remove(&seat_view->link);
	free(seat_view);

	if (view && view->parent) {
		roots_seat_set_focus(seat, view->parent);
	} else if (!wl_list_empty(&seat->views)) {
		// Focus first view
		struct roots_seat_view *first_seat_view = wl_container_of(
			seat->views.next, first_seat_view, link);
		roots_seat_set_focus(seat, first_seat_view->view);
	}
}

static void seat_view_handle_unmap(struct wl_listener *listener, void *data) {
	struct roots_seat_view *seat_view =
		wl_container_of(listener, seat_view, view_unmap);
	seat_view_destroy(seat_view);
}

static void seat_view_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_seat_view *seat_view =
		wl_container_of(listener, seat_view, view_destroy);
	seat_view_destroy(seat_view);
}

static struct roots_seat_view *seat_add_view(struct roots_seat *seat,
		struct roots_view *view) {
	struct roots_seat_view *seat_view =
		calloc(1, sizeof(struct roots_seat_view));
	if (seat_view == NULL) {
		return NULL;
	}
	seat_view->seat = seat;
	seat_view->view = view;

	wl_list_insert(seat->views.prev, &seat_view->link);

	seat_view->view_unmap.notify = seat_view_handle_unmap;
	wl_signal_add(&view->events.unmap, &seat_view->view_unmap);
	seat_view->view_destroy.notify = seat_view_handle_destroy;
	wl_signal_add(&view->events.destroy, &seat_view->view_destroy);

	return seat_view;
}

struct roots_seat_view *roots_seat_view_from_view(
		struct roots_seat *seat, struct roots_view *view) {
	if (view == NULL) {
		return NULL;
	}

	bool found = false;
	struct roots_seat_view *seat_view = NULL;
	wl_list_for_each(seat_view, &seat->views, link) {
		if (seat_view->view == view) {
			found = true;
			break;
		}
	}
	if (!found) {
		seat_view = seat_add_view(seat, view);
		if (seat_view == NULL) {
			wlr_log(WLR_ERROR, "Allocation failed");
			return NULL;
		}
	}

	return seat_view;
}

bool roots_seat_allow_input(struct roots_seat *seat,
		struct wl_resource *resource) {
	return !seat->exclusive_client ||
			wl_resource_get_client(resource) == seat->exclusive_client;
}

static void seat_raise_view_stack(struct roots_seat *seat, struct roots_view *view) {
	PhocServer *server = phoc_server_get_default ();
	if (!view->wlr_surface) {
		return;
	}

	wl_list_remove(&view->link);
	wl_list_insert(&server->desktop->views, &view->link);
	view_damage_whole(view);

	struct roots_view *child;
	wl_list_for_each_reverse(child, &view->stack, parent_link) {
		seat_raise_view_stack(seat, child);
	}
}

void roots_seat_set_focus(struct roots_seat *seat, struct roots_view *view) {
	if (view && !roots_seat_allow_input(seat, view->wlr_surface->resource)) {
		return;
	}

	// Make sure the view will be rendered on top of others, even if it's
	// already focused in this seat
	if (view != NULL) {
		struct roots_view *parent = view;
		// reorder stack
		while (parent->parent) {
			wl_list_remove(&parent->parent_link);
			wl_list_insert(&parent->parent->stack, &parent->parent_link);
			parent = parent->parent;
		}
		seat_raise_view_stack(seat, parent);
	}

	bool unfullscreen = true;

#ifdef PHOC_XWAYLAND
	if (view && view->type == ROOTS_XWAYLAND_VIEW) {
		struct roots_xwayland_surface *xwayland_surface =
			roots_xwayland_surface_from_view(view);
		if (xwayland_surface->xwayland_surface->override_redirect) {
			unfullscreen = false;
		}
	}
#endif

	if (view && unfullscreen) {
		PhocDesktop *desktop = view->desktop;
		PhocOutput *output;
		struct wlr_box box;
		view_get_box(view, &box);
		wl_list_for_each(output, &desktop->outputs, link) {
			if (output->fullscreen_view &&
					output->fullscreen_view != view &&
					wlr_output_layout_intersects(
						desktop->layout,
						output->wlr_output, &box)) {
				view_set_fullscreen(output->fullscreen_view,
						false, NULL);
			}
		}
	}

	struct roots_view *prev_focus = roots_seat_get_focus(seat);

	if (view && view == prev_focus) {
		return;
	}

#ifdef PHOC_XWAYLAND
	if (view && view->type == ROOTS_XWAYLAND_VIEW) {
		struct roots_xwayland_surface *xwayland_surface =
			roots_xwayland_surface_from_view(view);
		if (!wlr_xwayland_or_surface_wants_focus(
				xwayland_surface->xwayland_surface)) {
			return;
		}
	}
#endif
	struct roots_seat_view *seat_view = NULL;
	if (view != NULL) {
		seat_view = roots_seat_view_from_view(seat, view);
		if (seat_view == NULL) {
			return;
		}
	}

	seat->has_focus = false;

	// Deactivate the old view if it is not focused by some other seat
	if (prev_focus != NULL && !phoc_input_view_has_focus(seat->input, prev_focus)) {
		view_activate(prev_focus, false);
	}

	if (view == NULL) {
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
		wlr_seat_keyboard_clear_focus(seat->seat);
		roots_input_method_relay_set_focus(&seat->im_relay, NULL);
		return;
	}

	wl_list_remove(&seat_view->link);
	wl_list_insert(&seat->views, &seat_view->link);

	if (seat->focused_layer) {
		return;
	}

	view_activate(view, true);
	seat->has_focus = true;

	// An existing keyboard grab might try to deny setting focus, so cancel it
	wlr_seat_keyboard_end_grab(seat->seat);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat->seat, view->wlr_surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
		/* FIXME: Move this to a better place */
		struct roots_tablet_pad *pad;
		wl_list_for_each(pad, &seat->tablet_pads, link) {
			if (pad->tablet) {
				wlr_tablet_v2_tablet_pad_notify_enter(pad->tablet_v2_pad, pad->tablet->tablet_v2, view->wlr_surface);
			}
		}
	} else {
		wlr_seat_keyboard_notify_enter(seat->seat, view->wlr_surface,
			NULL, 0, NULL);
	}

	if (seat->cursor) {
		roots_cursor_update_focus(seat->cursor);
	}

	roots_input_method_relay_set_focus(&seat->im_relay, view->wlr_surface);
}

/**
 * Focus semantics of layer surfaces are somewhat detached from the normal focus
 * flow. For layers above the shell layer, for example, you cannot unfocus them.
 * You also cannot alt-tab between layer surfaces and shell surfaces.
 */
void roots_seat_set_focus_layer(struct roots_seat *seat,
		struct wlr_layer_surface_v1 *layer) {
	if (!layer) {
		if (seat->focused_layer) {
			seat->focused_layer = NULL;
			if (!wl_list_empty(&seat->views)) {
				// Focus first view
				struct roots_seat_view *first_seat_view = wl_container_of(
					seat->views.next, first_seat_view, link);
				roots_seat_set_focus(seat, first_seat_view->view);
			} else {
				roots_seat_set_focus(seat, NULL);
			}
		}
		return;
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->seat);
	if (!roots_seat_allow_input(seat, layer->resource)) {
		return;
	}
	if (seat->has_focus) {
		struct roots_view *prev_focus = roots_seat_get_focus(seat);
		wlr_seat_keyboard_clear_focus(seat->seat);
		view_activate(prev_focus, false);
	}
	seat->has_focus = false;
	if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
		seat->focused_layer = layer;
	}
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat->seat, layer->surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(seat->seat, layer->surface,
			NULL, 0, NULL);
	}

	if (seat->cursor) {
		roots_cursor_update_focus(seat->cursor);
	}

	roots_input_method_relay_set_focus(&seat->im_relay, layer->surface);
}

void roots_seat_set_exclusive_client(struct roots_seat *seat,
		struct wl_client *client) {
	PhocServer *server = phoc_server_get_default ();
	if (!client) {
		seat->exclusive_client = client;
		// Triggers a refocus of the topmost surface layer if necessary
		// TODO: Make layer surface focus per-output based on cursor position
		PhocOutput *output;
		wl_list_for_each(output, &server->desktop->outputs, link) {
			arrange_layers(output);
		}
		return;
	}
	if (seat->focused_layer) {
		if (wl_resource_get_client(seat->focused_layer->resource) != client) {
			roots_seat_set_focus_layer(seat, NULL);
		}
	}
	if (seat->has_focus) {
		struct roots_view *focus = roots_seat_get_focus(seat);
		if (wl_resource_get_client(focus->wlr_surface->resource) != client) {
			roots_seat_set_focus(seat, NULL);
		}
	}
	if (seat->seat->pointer_state.focused_client) {
		if (seat->seat->pointer_state.focused_client->client != client) {
			wlr_seat_pointer_clear_focus(seat->seat);
		}
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	struct wlr_touch_point *point;
	wl_list_for_each(point, &seat->seat->touch_state.touch_points, link) {
		if (point->client->client != client) {
			wlr_seat_touch_point_clear_focus(seat->seat,
					now.tv_nsec / 1000, point->touch_id);
		}
	}
	seat->exclusive_client = client;
}

void roots_seat_cycle_focus(struct roots_seat *seat) {
	if (wl_list_empty(&seat->views)) {
		return;
	}

	struct roots_seat_view *first_seat_view = wl_container_of(
		seat->views.next, first_seat_view, link);
	if (!seat->has_focus) {
		roots_seat_set_focus(seat, first_seat_view->view);
		return;
	}
	if (wl_list_length(&seat->views) < 2) {
		return;
	}

	// Focus the next view
	struct roots_seat_view *next_seat_view = wl_container_of(
		first_seat_view->link.next, next_seat_view, link);
	roots_seat_set_focus(seat, next_seat_view->view);

	// Move the first view to the end of the list
	wl_list_remove(&first_seat_view->link);
	wl_list_insert(seat->views.prev, &first_seat_view->link);
}

void roots_seat_begin_move(struct roots_seat *seat, struct roots_view *view) {
	if (view->desktop->maximize)
		return;

	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_MOVE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	if (view_is_maximized(view)) {
		cursor->view_x = view->saved.x;
		cursor->view_y = view->saved.y;
	} else {
		cursor->view_x = view->box.x;
		cursor->view_y = view->box.y;
	}
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	roots_seat_maybe_set_cursor (seat, ROOTS_XCURSOR_MOVE);
}

void roots_seat_begin_resize(struct roots_seat *seat, struct roots_view *view,
		uint32_t edges) {
	if (view->desktop->maximize)
		return;

	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_RESIZE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	if (view_is_maximized(view)) {
		cursor->view_x = view->saved.x;
		cursor->view_y = view->saved.y;
		cursor->view_width = view->saved.width;
		cursor->view_height = view->saved.height;
	} else {
		cursor->view_x = view->box.x;
		cursor->view_y = view->box.y;
		struct wlr_box box;
		view_get_box(view, &box);
		cursor->view_width = box.width;
		cursor->view_height = box.height;
	}
	cursor->resize_edges = edges;
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	const char *resize_name = wlr_xcursor_get_resize_name(edges);
	roots_seat_maybe_set_cursor (seat, resize_name);
}

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view) {
	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_ROTATE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	cursor->view_rotation = view->rotation;
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	roots_seat_maybe_set_cursor (seat, ROOTS_XCURSOR_ROTATE);
}

void roots_seat_end_compositor_grab(struct roots_seat *seat) {
	struct roots_cursor *cursor = seat->cursor;
	struct roots_view *view = roots_seat_get_focus(seat);

	if (view == NULL) {
		return;
	}

	switch(cursor->mode) {
		case ROOTS_CURSOR_MOVE:
			view_move(view, cursor->view_x, cursor->view_y);
			break;
		case ROOTS_CURSOR_RESIZE:
			view_move_resize(view, cursor->view_x, cursor->view_y, cursor->view_width, cursor->view_height);
			break;
		case ROOTS_CURSOR_ROTATE:
			view->rotation = cursor->view_rotation;
			break;
		case ROOTS_CURSOR_PASSTHROUGH:
			break;
		default:
			g_error("Invalid cursor mode %d", cursor->mode);
	}

	cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
}

struct roots_seat *input_last_active_seat(PhocInput *input) {
	struct roots_seat *seat = NULL, *_seat;
	wl_list_for_each(_seat, &input->seats, link) {
		if (!seat || (seat->seat->last_event.tv_sec > _seat->seat->last_event.tv_sec &&
				seat->seat->last_event.tv_nsec > _seat->seat->last_event.tv_nsec)) {
			seat = _seat;
		}
	}
	return seat;
}

/**
 * roots_seat_maybe_set_cursor:
 *
 * Show a cursor if the seat has pointer capabilities
 *
 * @self: a struct roots_seat
 * @name: (nullable): a cursor name or %NULL for the themes default cursor
 */
void roots_seat_maybe_set_cursor(struct roots_seat *self, const char *name) {
	struct wlr_seat *wlr_seat = self->seat;

	g_return_if_fail (wlr_seat);
	if ((wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
		wlr_cursor_set_image(self->cursor->cursor, NULL, 0, 0, 0, 0, 0, 0);
	} else {
		if (!name)
			name = self->cursor->default_xcursor;
		wlr_xcursor_manager_set_cursor_image(self->cursor->xcursor_manager,
			name, self->cursor->cursor);
	}
}

/**
 * roots_seat_get_cursor:
 *
 * Get the curent cursor
 * @self: a struct roots_seat
 *
 * Returns: (transfer none): The current cursor
 */
struct roots_cursor *roots_seat_get_cursor(struct roots_seat *self) {
	g_return_val_if_fail (self, NULL);

	return self->cursor;
}
