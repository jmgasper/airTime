/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include <stdio.h>
#include <string.h>

#include "App.h"


int
main(int argc, char** argv)
{
	// `airTime --register-default` sets the preferred application of the
	// film and music types without starting the player (the package's post
	// install script uses it).
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--register-default") == 0) {
			BApplication application("application/x-vnd.airOS-airTime-setup");
			int32 changed = airtime::App::RegisterAsDefaultPlayer(
				i + 1 < argc && strcmp(argv[i + 1], "--force") == 0);
			printf("airTime is the preferred player for %d more types\n",
				(int)changed);
			return 0;
		}
	}
	airtime::App app;
	app.Run();
	return 0;
}
