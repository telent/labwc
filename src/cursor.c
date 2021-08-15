#define _XOPEN_SOURCE 600
#define _GNU_SOURCE 1
#include <assert.h>
#include <linux/input-event-codes.h>
#include <sys/time.h>
#include <time.h>
#include "labwc.h"
#include "menu/menu.h"
#include "ssd.h"

static void
request_cursor_notify(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(listener, seat, request_cursor);
	/*
	 * This event is rasied by the seat when a client provides a cursor
	 * image
	 */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		seat->seat->pointer_state.focused_client;

	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use
		 * the provided surface as the cursor image. It will set the
		 * hardware cursor on the output that it's currently on and
		 * continue to do so as the cursor moves between outputs. */
		wlr_cursor_set_surface(seat->cursor, event->surface,
				       event->hotspot_x, event->hotspot_y);
	}
}

static void
request_set_selection_notify(struct wl_listener *listener, void *data)
{
	struct seat *seat = wl_container_of(
		listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->seat, event->source,
		event->serial);
}

static void
process_cursor_move(struct server *server, uint32_t time)
{
	damage_all_outputs(server);
	double dx = server->seat.cursor->x - server->grab_x;
	double dy = server->seat.cursor->y - server->grab_y;
	struct view *view = server->grabbed_view;

	/* Move the grabbed view to the new position. */
	view->impl->move(view, server->grab_box.x + dx, server->grab_box.y + dy);
}

#define MIN_VIEW_WIDTH (100)
#define MIN_VIEW_HEIGHT (60)

static void
process_cursor_resize(struct server *server, uint32_t time)
{
	damage_all_outputs(server);
	double dx = server->seat.cursor->x - server->grab_x;
	double dy = server->seat.cursor->y - server->grab_y;

	struct view *view = server->grabbed_view;
	struct wlr_box new_view_geo = {
		.x = view->x, .y = view->y, .width = view->w, .height = view->h
	};

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_view_geo.y = server->grab_box.y + dy;
		new_view_geo.height = server->grab_box.height - dy;
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_view_geo.height = server->grab_box.height + dy;
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_view_geo.x = server->grab_box.x + dx;
		new_view_geo.width = server->grab_box.width - dx;
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_view_geo.width = server->grab_box.width + dx;
	}
	if ((new_view_geo.height < MIN_VIEW_HEIGHT) ||
	    (new_view_geo.width < MIN_VIEW_WIDTH)) {
		return;
	}

	view_move_resize(view, new_view_geo);
}

static void
set_cursor(struct server *server, const char *cursor_name)
{
	wlr_xcursor_manager_set_cursor_image(
		server->seat.xcursor_manager, cursor_name, server->seat.cursor);
}

static void
process_cursor_motion(struct server *server, uint32_t time)
{
	static bool cursor_name_set_by_server;

	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->input_mode == LAB_INPUT_STATE_MOVE) {
		process_cursor_move(server, time);
		return;
	} else if (server->input_mode == LAB_INPUT_STATE_RESIZE) {
		process_cursor_resize(server, time);
		return;
	} else if (server->input_mode == LAB_INPUT_STATE_MENU) {
		menu_set_selected(server->rootmenu,
			server->seat.cursor->x, server->seat.cursor->y);
		damage_all_outputs(server);
		return;
	}

	/* Otherwise, find the view under the pointer and send the event along */
	double sx, sy;
	struct wlr_seat *wlr_seat = server->seat.seat;
	struct wlr_surface *surface = NULL;
	int view_area = LAB_SSD_NONE;
	struct view *view =
		desktop_view_at(server, server->seat.cursor->x, server->seat.cursor->y,
				&surface, &sx, &sy, &view_area);
	if (!view) {
		set_cursor(server, XCURSOR_DEFAULT);
	} else {
		uint32_t resize_edges = ssd_resize_edges(view_area);
		if (resize_edges) {
			cursor_name_set_by_server = true;
			set_cursor(server, wlr_xcursor_get_resize_name(resize_edges));
		} else if (view_area != LAB_SSD_NONE) {
			/* title and buttons */
			set_cursor(server, XCURSOR_DEFAULT);
			cursor_name_set_by_server = true;
		} else if (cursor_name_set_by_server) {
			set_cursor(server, XCURSOR_DEFAULT);
			cursor_name_set_by_server = false;
		}
	}


	if (view && rc.focus_follow_mouse) {
		if (rc.raise_on_focus) {
			desktop_focus_view(&server->seat, view);
		} else {
			desktop_set_focus_view_only(&server->seat, view);
		}
	}

	/* Required for iconify/maximize/close button mouse-over deco */
	damage_all_outputs(server);

	if (surface) {
		bool focus_changed =
			wlr_seat->pointer_state.focused_surface != surface;
		/*
		 * "Enter" the surface if necessary. This lets the client know
		 * that the cursor has entered one of its surfaces.
		 *
		 * Note that this gives the surface "pointer focus", which is
		 * distinct from keyboard focus. You get pointer focus by moving
		 * the pointer over a window.
		 */
		wlr_seat_pointer_notify_enter(wlr_seat, surface, sx, sy);
		if (!focus_changed) {
			/*
			 * The enter event contains coordinates, so we only need
			 * to notify on motion if the focus did not change.
			 */
			wlr_seat_pointer_notify_motion(wlr_seat, time, sx, sy);
		}
	} else {
		/* Clear pointer focus so future button events and such are not
		 * sent to the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(wlr_seat);
	}
}

void
cursor_motion(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a
	 * _relative_ pointer motion event (i.e. a delta)
	 */
	struct seat *seat = wl_container_of(listener, seat, cursor_motion);
	struct wlr_event_pointer_motion *event = data;

	/*
	 * The cursor doesn't move unless we tell it to. The cursor
	 * automatically handles constraining the motion to the output layout,
	 * as well as any special configuration applied for the specific input
	 * device which generated the event. You can pass NULL for the device
	 * if you want to move the cursor around without any input.
	 */
	wlr_cursor_move(seat->cursor, event->device, event->delta_x,
		event->delta_y);
	process_cursor_motion(seat->server, event->time_msec);
}

void
cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an
	 * _absolute_ motion event, from 0..1 on each axis. This happens, for
	 * example, when wlroots is running under a Wayland window rather than
	 * KMS+DRM, and you move the mouse over the window. You could enter the
	 * window from any edge, so we have to warp the mouse there. There is
	 * also some hardware which emits these events.
	 */
	struct seat *seat = wl_container_of(
		listener, seat, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(seat->cursor, event->device, event->x, event->y);
	process_cursor_motion(seat->server, event->time_msec);
}

static void
handle_cursor_button_with_meta_key(struct view *view, uint32_t button,
		double lx, double ly)
{
	if (!view) {
		return;
	}

	/* move */
	if (button == BTN_LEFT) {
		interactive_begin(view, LAB_INPUT_STATE_MOVE, 0);
		return;
	}

	/* resize */
	uint32_t edges;
	edges = lx < view->x + view->w / 2 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
	edges |= ly < view->y + view->h / 2 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
	interactive_begin(view, LAB_INPUT_STATE_RESIZE, edges);
}

static bool
is_double_click(long double_click_speed)
{
	static struct timespec last_double_click;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (now.tv_sec - last_double_click.tv_sec) * 1000 +
		(now.tv_nsec - last_double_click.tv_nsec) / 1000000;
	last_double_click = now;
	return ms < double_click_speed && ms >= 0;
}

void
cursor_button(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a button
	 * event.
	 */
	struct seat *seat = wl_container_of(listener, seat, cursor_button);
	struct server *server = seat->server;
	struct wlr_event_pointer_button *event = data;

	double sx, sy;
	struct wlr_surface *surface;
	int view_area = LAB_SSD_NONE;
	uint32_t resize_edges;

	struct view *view = desktop_view_at(server, server->seat.cursor->x,
		server->seat.cursor->y, &surface, &sx, &sy, &view_area);

	/* handle alt + _press_ on view */
	struct wlr_input_device *device = seat->keyboard_group->input_device;
	uint32_t modifiers = wlr_keyboard_get_modifiers(device->keyboard);
	if (modifiers & XKB_KEY_Alt_L && event->state == WLR_BUTTON_PRESSED) {
		handle_cursor_button_with_meta_key(view, event->button,
			server->seat.cursor->x, server->seat.cursor->y);
		return;
	};

	/* Notify client with pointer focus of button press */
	wlr_seat_pointer_notify_button(seat->seat, event->time_msec,
		event->button, event->state);

	/* handle _release_ */
	if (event->state == WLR_BUTTON_RELEASED) {
		if (server->input_mode == LAB_INPUT_STATE_MENU) {
			return;
		}
		/* Exit interactive move/resize/menu mode. */
		server->input_mode = LAB_INPUT_STATE_PASSTHROUGH;
		damage_all_outputs(server);
		return;
	}

	if (server->input_mode == LAB_INPUT_STATE_MENU) {
		menu_action_selected(server, server->rootmenu);
		server->input_mode = LAB_INPUT_STATE_PASSTHROUGH;
		return;
	}

	/* Handle _press_ on a layer surface */
	if (!view && surface) {
		/* ...if we've ended up here it must be a layer surface */
		assert(wlr_surface_is_layer_surface(surface));
		struct wlr_layer_surface_v1 *layer =
			wlr_layer_surface_v1_from_wlr_surface(surface);
		if (layer->current.keyboard_interactive) {
			seat_set_focus_layer(&server->seat, layer);
		}
		return;
	}

	/* Handle _press_ on root window */
	if (!view) {
		action(server, "ShowMenu", "root-menu");
		return;
	}

	/* Handle _press_ on view */
	desktop_focus_view(&server->seat, view);
	damage_all_outputs(server);

	if (is_double_click(500) && view_area == LAB_SSD_PART_TITLEBAR) {
		view_toggle_maximize(view);
		return;
	}

	resize_edges = ssd_resize_edges(view_area);
	if (resize_edges) {
		interactive_begin(view, LAB_INPUT_STATE_RESIZE, resize_edges);
		return;
	}

	switch (view_area) {
	case LAB_SSD_BUTTON_CLOSE:
		view->impl->close(view);
		break;
	case LAB_SSD_BUTTON_ICONIFY:
		view_minimize(view, true);
		break;
	case LAB_SSD_PART_TITLEBAR:
		interactive_begin(view, LAB_INPUT_STATE_MOVE, 0);
		break;
	case LAB_SSD_BUTTON_MAXIMIZE:
		view_toggle_maximize(view);
		break;
	}
}

void
cursor_axis(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an axis
	 * event, for example when you move the scroll wheel.
	 */
	struct seat *seat = wl_container_of(listener, seat, cursor_axis);
	struct wlr_event_pointer_axis *event = data;

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source);
}

void
cursor_frame(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen
	 * at the same time, in which case a frame event won't be sent in
	 * between.
	 */
	struct seat *seat = wl_container_of(listener, seat, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat->seat);
}

void
cursor_init(struct seat *seat)
{
	seat->xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(seat->xcursor_manager, 1);

	seat->cursor_motion.notify = cursor_motion;
	wl_signal_add(&seat->cursor->events.motion, &seat->cursor_motion);
	seat->cursor_motion_absolute.notify = cursor_motion_absolute;
	wl_signal_add(&seat->cursor->events.motion_absolute,
		      &seat->cursor_motion_absolute);
	seat->cursor_button.notify = cursor_button;
	wl_signal_add(&seat->cursor->events.button, &seat->cursor_button);
	seat->cursor_axis.notify = cursor_axis;
	wl_signal_add(&seat->cursor->events.axis, &seat->cursor_axis);
	seat->cursor_frame.notify = cursor_frame;
	wl_signal_add(&seat->cursor->events.frame, &seat->cursor_frame);

	seat->request_cursor.notify = request_cursor_notify;
	wl_signal_add(&seat->seat->events.request_set_cursor, &seat->request_cursor);
	seat->request_set_selection.notify = request_set_selection_notify;
	wl_signal_add(&seat->seat->events.request_set_selection, &seat->request_set_selection);

	/* TODO:
	 * seat->request_set_primary_selection.notify =
	 *	request_set_primary_selectioni_notify;
	 * wl_signal_add(&seat->seat->events.request_set_primary_selection,
	 *	&seat->request_set_primary_selection);
	 */
}
