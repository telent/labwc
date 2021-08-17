/*
 * layers.c - layer-shell implementation
 *
 * Based on:
 *  - https://git.sr.ht/~sircmpwm/wio
 *  - https://github.com/swaywm/sway
 * Copyright (C) 2019 Drew DeVault and Sway developers
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/log.h>
#include "layers.h"
#include "labwc.h"

static void
apply_exclusive(struct wlr_box *usable_area, uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right, int32_t margin_bottom,
		int32_t margin_left)
{
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
		if ((anchor & edges[i].anchors) == edges[i].anchors) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive + edges[i].margin;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			}
		}
	}
}

/**
 * @list: struct lab_layer_surface
 */
static void
arrange_layer(struct wlr_output *output, struct wl_list *list,
		struct wlr_box *usable_area, bool exclusive)
{
	struct lab_layer_surface *surface;
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output, &full_area.width,
		&full_area.height);
	wl_list_for_each_reverse(surface, list, link) {
		struct wlr_layer_surface_v1 *layer = surface->layer_surface;
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
		/* Horizontal axis */
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
		/* Vertical axis */
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
		/* Margin */
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
			wlr_log(WLR_ERROR, "surface has no positive size");
			continue;
		}

		/* Apply */
		surface->geo = box;
		apply_exclusive(usable_area, state->anchor,
			state->exclusive_zone, state->margin.top,
			state->margin.right, state->margin.bottom,
			state->margin.left);
		wlr_layer_surface_v1_configure(layer, box.width, box.height);
	}
}

void
arrange_layers(struct output *output)
{
	assert(output);

	struct wlr_box usable_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
		&usable_area.width, &usable_area.height);

	/* Exclusive surfaces */
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, true);
	memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));

	/* TODO: re-arrange all views taking into account updated usable_area */

	/* Non-exclusive surfaces */
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, false);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	size_t nlayers = sizeof(layers_above_shell) / sizeof(layers_above_shell[0]);
	struct lab_layer_surface *layer, *topmost = NULL;
	for (size_t i = 0; i < nlayers; ++i) {
		wl_list_for_each_reverse (layer,
				&output->layers[layers_above_shell[i]], link) {
			if (layer->layer_surface->current.keyboard_interactive) {
				topmost = layer;
				break;
			}
		}
		if (topmost) {
			break;
		}
	}
	struct seat *seat = &output->server->seat;
	if (topmost) {
		seat_set_focus_layer(seat, topmost->layer_surface);
	} else if (seat->focused_layer &&
			!seat->focused_layer->current.keyboard_interactive) {
		seat_set_focus_layer(seat, NULL);
	}
}

static void
output_destroy_notify(struct wl_listener *listener, void *data)
{
	struct lab_layer_surface *layer =
		wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wl_list_remove(&layer->output_destroy.link);
	wlr_layer_surface_v1_destroy(layer->layer_surface);
}

static void
surface_commit_notify(struct wl_listener *listener, void *data)
{
	struct lab_layer_surface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	arrange_layers(output_from_wlr_output(layer->server, wlr_output));
	damage_all_outputs(layer->server);
}

static void
unmap(struct lab_layer_surface *layer)
{
	struct seat *seat = &layer->server->seat;
	if (seat->focused_layer == layer->layer_surface) {
		seat_set_focus_layer(seat, NULL);
	}
	damage_all_outputs(layer->server);
}

static void
destroy_notify(struct wl_listener *listener, void *data)
{
	struct lab_layer_surface *layer = wl_container_of(
		listener, layer, destroy);
	if (layer->layer_surface->mapped) {
		unmap(layer);
	}
	wl_list_remove(&layer->link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->surface_commit.link);
	if (layer->layer_surface->output) {
		wl_list_remove(&layer->output_destroy.link);
		struct output *output = output_from_wlr_output(
			layer->server, layer->layer_surface->output);
		arrange_layers(output);
	}
	free(layer);
}

static void
unmap_notify(struct wl_listener *listener, void *data)
{
	struct lab_layer_surface *l = wl_container_of(listener, l, unmap);
	unmap(l);
}

static void
map_notify(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *l = data;
	wlr_surface_send_enter(l->surface, l->output);
}

static void
new_layer_surface_notify(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(
		listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	if (!layer_surface->output) {
		struct wlr_output *output = wlr_output_layout_output_at(
			server->output_layout, server->seat.cursor->x,
			server->seat.cursor->y);
		layer_surface->output = output;
	}

	struct output *output =
		output_from_wlr_output(server, layer_surface->output);

	struct lab_layer_surface *surface =
		calloc(1, sizeof(struct lab_layer_surface));
	if (!surface) {
		return;
	}
	surface->layer_surface = layer_surface;
	layer_surface->data = surface;
	surface->server = server;

	surface->surface_commit.notify = surface_commit_notify;
	wl_signal_add(&layer_surface->surface->events.commit,
		&surface->surface_commit);

	surface->output_destroy.notify = output_destroy_notify;
	wl_signal_add(&layer_surface->output->events.destroy,
		&surface->output_destroy);

	surface->destroy.notify = destroy_notify;
	wl_signal_add(&layer_surface->events.destroy, &surface->destroy);

	surface->map.notify = map_notify;
	wl_signal_add(&layer_surface->events.map, &surface->map);

	surface->unmap.notify = unmap_notify;
	wl_signal_add(&layer_surface->events.unmap, &surface->unmap);

	wl_list_insert(&output->layers[layer_surface->client_pending.layer],
		&surface->link);
	/*
	 * Temporarily set the layer's current state to client_pending so that
	 * it can easily be arranged.
	 */
	struct wlr_layer_surface_v1_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;
	arrange_layers(output);
	layer_surface->current = old_state;
}

void
layers_init(struct server *server)
{
	server->layer_shell = wlr_layer_shell_v1_create(server->wl_display);
	server->new_layer_surface.notify = new_layer_surface_notify;
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->new_layer_surface);
}
