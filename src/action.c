#include <strings.h>
#include <wlr/util/log.h>
#include "common/spawn.h"
#include "labwc.h"
#include "menu/menu.h"

static void
show_menu(struct server *server, const char *menu)
{
	if (!menu) {
		return;
	}
	if (!strcasecmp(menu, "root-menu")) {
		server->input_mode = LAB_INPUT_STATE_MENU;
		menu_move(server->rootmenu, server->seat.cursor->x,
			server->seat.cursor->y);
	}
	damage_all_outputs(server);
}

void
action(struct server *server, const char *action, const char *command)
{
	if (!action)
		return;
	if (!strcasecmp(action, "Close")) {
		struct view *view = topmost_mapped_view(server);
		if (view) {
			view->impl->close(view);
		}
	} else if (!strcasecmp(action, "Debug")) {
		/* nothing */
	} else if (!strcasecmp(action, "Execute")) {
		struct buf cmd;
		buf_init(&cmd);
		buf_add(&cmd, command);
		buf_expand_shell_variables(&cmd);
		spawn_async_no_shell(cmd.buf);
		free(cmd.buf);
	} else if (!strcasecmp(action, "Exit")) {
		wl_display_terminate(server->wl_display);
	} else if (!strcasecmp(action, "MoveToEdge")) {
		view_move_to_edge(topmost_mapped_view(server), command);
	} else if (!strcasecmp(action, "NextWindow")) {
		server->cycle_view =
			desktop_cycle_view(server, server->cycle_view);
		osd_update(server);
	} else if (!strcasecmp(action, "Reconfigure")) {
		spawn_async_no_shell("killall -SIGHUP labwc");
	} else if (!strcasecmp(action, "ShowMenu")) {
		show_menu(server, command);
	} else if (!strcasecmp(action, "ToggleMaximize")) {
		struct view *view = topmost_mapped_view(server);
		if (view) {
			view_toggle_maximize(view);
		}
	} else {
		wlr_log(WLR_ERROR, "action (%s) not supported", action);
	}
}
