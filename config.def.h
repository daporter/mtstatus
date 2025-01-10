#ifndef CONFIG_H
#define CONFIG_H

#include "component.h"

static const char divider_str[] = "   ";
static const char no_val_str[] = "???";
const char err_str[] = "err";

/* clang-format off */
static const ComponentDefn component_defns[] = {
	/* function,			args,	  	interval,	signal (SIGRTMIN+n) */
	{ comp_keyboard_indicator,	0,		-1,	 	 0 },
	{ comp_net_traffic,		"wlan0",	 1,		-1 },
	{ comp_cpu,			0,		 1,		-1 },
	{ comp_memory_available,	0,		 2,		-1 },
	{ comp_disk_free,		"/",		15,		-1 },
	{ comp_volume,			0,		60,	 	 2 },
	{ comp_wifi,			"wlan0",	 5,		-1 },
	{ comp_battery,			0,		 2,		-1 },
	{ comp_datetime,		"%a %d %b %R",	30,		-1 },
};
/* clang-format on */

#endif
