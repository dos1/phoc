#define G_LOG_DOMAIN "phoc-keyboard"

#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "input.h"
#include "keyboard.h"
#include "seat.h"

static ssize_t pressed_keysyms_index(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
		if (pressed_keysyms[i] == keysym) {
			return i;
		}
	}
	return -1;
}

static size_t pressed_keysyms_length(xkb_keysym_t *pressed_keysyms) {
	size_t n = 0;
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
		if (pressed_keysyms[i] != XKB_KEY_NoSymbol) {
			++n;
		}
	}
	return n;
}

static void pressed_keysyms_add(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i < 0) {
		i = pressed_keysyms_index(pressed_keysyms, XKB_KEY_NoSymbol);
		if (i >= 0) {
			pressed_keysyms[i] = keysym;
		}
	}
}

static void pressed_keysyms_remove(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i >= 0) {
		pressed_keysyms[i] = XKB_KEY_NoSymbol;
	}
}

static bool keysym_is_modifier(xkb_keysym_t keysym) {
	switch (keysym) {
	case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L: case XKB_KEY_Control_R:
	case XKB_KEY_Caps_Lock:
	case XKB_KEY_Shift_Lock:
	case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
	case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
	case XKB_KEY_Super_L: case XKB_KEY_Super_R:
	case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
		return true;
	default:
		return false;
	}
}

static void pressed_keysyms_update(xkb_keysym_t *pressed_keysyms,
		const xkb_keysym_t *keysyms, size_t keysyms_len,
		enum wlr_key_state state) {
	for (size_t i = 0; i < keysyms_len; ++i) {
		if (keysym_is_modifier(keysyms[i])) {
			continue;
		}
		if (state == WLR_KEY_PRESSED) {
			pressed_keysyms_add(pressed_keysyms, keysyms[i]);
		} else { // WLR_KEY_RELEASED
			pressed_keysyms_remove(pressed_keysyms, keysyms[i]);
		}
	}
}

/**
 * Execute a built-in, hardcoded compositor binding. These are triggered from a
 * single keysym.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool keyboard_execute_compositor_binding(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
			keysym <= XKB_KEY_XF86Switch_VT_12) {
		PhocServer *server = phoc_server_get_default ();

		struct wlr_session *session = wlr_backend_get_session(server->backend);
		if (session) {
			unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
			wlr_session_change_vt(session, vt);
		}

		return true;
	}

	if (keysym == XKB_KEY_Escape) {
		wlr_seat_pointer_end_grab(keyboard->seat->seat);
		wlr_seat_keyboard_end_grab(keyboard->seat->seat);
		roots_seat_end_compositor_grab(keyboard->seat);
	}

	return false;
}

/**
 * Execute keyboard bindings. These include compositor bindings and user-defined
 * bindings.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool keyboard_execute_binding(struct roots_keyboard *keyboard,
				     xkb_keysym_t *pressed_keysyms, uint32_t modifiers,
				     const xkb_keysym_t *keysyms, size_t keysyms_len)
{
  PhocServer *server = phoc_server_get_default ();
  PhocKeybindings *keybindings;

  /* TODO: should be handled via PhocKeybindings as well */
  for (size_t i = 0; i < keysyms_len; ++i) {
    if (keyboard_execute_compositor_binding(keyboard, keysyms[i])) {
      return true;
    }
  }

  size_t n = pressed_keysyms_length(pressed_keysyms);
  keybindings = server->config->keybindings;

  if (phoc_keybindings_handle_pressed (keybindings, modifiers, pressed_keysyms, n,
				       keyboard->seat))
    return true;

  return false;
}

/*
 * Get keysyms and modifiers from the keyboard as xkb sees them.
 *
 * This uses the xkb keysyms translation based on pressed modifiers and clears
 * the consumed modifiers from the list of modifiers passed to keybind
 * detection.
 *
 * On US layout, pressing Alt+Shift+2 will trigger Alt+@.
 */
static size_t keyboard_keysyms_translated(struct roots_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	*modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
		keyboard->device->keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
	*modifiers = *modifiers & ~consumed;

	return xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
		keycode, keysyms);
}

/*
 * Get keysyms and modifiers from the keyboard as if modifiers didn't change
 * keysyms.
 *
 * This avoids the xkb keysym translation based on modifiers considered pressed
 * in the state.
 *
 * This will trigger keybinds such as Alt+Shift+2.
 */
static size_t keyboard_keysyms_raw(struct roots_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	*modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);

	xkb_layout_index_t layout_index = xkb_state_key_get_layout(
		keyboard->device->keyboard->xkb_state, keycode);
	return xkb_keymap_key_get_syms_by_level(keyboard->device->keyboard->keymap,
		keycode, layout_index, 0, keysyms);
}

void roots_keyboard_handle_key(struct roots_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	xkb_keycode_t keycode = event->keycode + 8;

	bool handled = false;
	uint32_t modifiers;
	const xkb_keysym_t *keysyms;
	size_t keysyms_len;

	// Handle translated keysyms

	keysyms_len = keyboard_keysyms_translated(keyboard, keycode, &keysyms,
		&modifiers);
	pressed_keysyms_update(keyboard->pressed_keysyms_translated, keysyms,
		keysyms_len, event->state);
	if (event->state == WLR_KEY_PRESSED) {
		handled = keyboard_execute_binding(keyboard,
			keyboard->pressed_keysyms_translated, modifiers, keysyms,
			keysyms_len);
	}

	// Handle raw keysyms
	keysyms_len = keyboard_keysyms_raw(keyboard, keycode, &keysyms, &modifiers);
	pressed_keysyms_update(keyboard->pressed_keysyms_raw, keysyms, keysyms_len,
		event->state);
	if (event->state == WLR_KEY_PRESSED && !handled) {
		handled = keyboard_execute_binding(keyboard,
			keyboard->pressed_keysyms_raw, modifiers, keysyms, keysyms_len);
	}

	if (!handled) {
		wlr_seat_set_keyboard(keyboard->seat->seat, keyboard->device);
		wlr_seat_keyboard_notify_key(keyboard->seat->seat, event->time_msec,
			event->keycode, event->state);
	}
}

void roots_keyboard_handle_modifiers(struct roots_keyboard *r_keyboard) {
	struct wlr_seat *seat = r_keyboard->seat->seat;
	wlr_seat_set_keyboard(seat, r_keyboard->device);
	wlr_seat_keyboard_notify_modifiers(seat,
		&r_keyboard->device->keyboard->modifiers);
}

static void keyboard_config_merge(struct roots_keyboard_config *config,
		struct roots_keyboard_config *fallback) {
	if (fallback == NULL) {
		return;
	}
	if (config->rules == NULL) {
		config->rules = fallback->rules;
	}
	if (config->model == NULL) {
		config->model = fallback->model;
	}
	if (config->layout == NULL) {
		config->layout = fallback->layout;
	}
	if (config->variant == NULL) {
		config->variant = fallback->variant;
	}
	if (config->options == NULL) {
		config->options = fallback->options;
	}
	if (config->meta_key == 0) {
		config->meta_key = fallback->meta_key;
	}
	if (config->name == NULL) {
		config->name = fallback->name;
	}
	if (config->repeat_rate <= 0) {
		config->repeat_rate = fallback->repeat_rate;
	}
	if (config->repeat_delay <= 0) {
		config->repeat_delay = fallback->repeat_delay;
	}
}

struct roots_keyboard *roots_keyboard_create(struct wlr_input_device *device,
		struct roots_input *input) {
	struct roots_keyboard *keyboard = calloc(sizeof(struct roots_keyboard), 1);
	if (keyboard == NULL) {
		return NULL;
	}
	device->data = keyboard;
	keyboard->device = device;
	keyboard->input = input;

	struct roots_keyboard_config *config =
		calloc(1, sizeof(struct roots_keyboard_config));
	if (config == NULL) {
		free(keyboard);
		return NULL;
	}
	keyboard_config_merge(config, roots_config_get_keyboard(input->config, device));
	keyboard_config_merge(config, roots_config_get_keyboard(input->config, NULL));

	struct roots_keyboard_config env_config = {
		.rules = getenv("XKB_DEFAULT_RULES"),
		.model = getenv("XKB_DEFAULT_MODEL"),
		.layout = getenv("XKB_DEFAULT_LAYOUT"),
		.variant = getenv("XKB_DEFAULT_VARIANT"),
		.options = getenv("XKB_DEFAULT_OPTIONS"),
	};
	keyboard_config_merge(config, &env_config);
	keyboard->config = config;

	struct xkb_rule_names rules = { 0 };
	rules.rules = config->rules;
	rules.model = config->model;
	rules.layout = config->layout;
	rules.variant = config->variant;
	rules.options = config->options;
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		wlr_log(WLR_ERROR, "Cannot create XKB context");
		return NULL;
	}

	struct xkb_keymap *keymap = xkb_map_new_from_names(context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		xkb_context_unref(context);
		wlr_log(WLR_ERROR, "Cannot create XKB keymap");
		return NULL;
	}

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	int repeat_rate = (config->repeat_rate > 0) ? config->repeat_rate : 25;
	int repeat_delay = (config->repeat_delay > 0) ? config->repeat_delay : 600;
	wlr_keyboard_set_repeat_info(device->keyboard, repeat_rate, repeat_delay);

	return keyboard;
}

void roots_keyboard_destroy(struct roots_keyboard *keyboard) {
	wl_list_remove(&keyboard->link);
	free(keyboard->config);
	free(keyboard);
}
