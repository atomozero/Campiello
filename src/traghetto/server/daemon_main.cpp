// daemon_main.cpp
//
// Entry point for the resident Campiello daemon. Haiku-only: it runs a BApplication so the
// pairing prompt has an app_server connection. On other systems it is a no-op (the daemon is
// a Haiku product), so the file still compiles in portable CI.

#ifdef __HAIKU__

#include "DaemonApp.h"

int main()
{
	campiello::net::DaemonApp app;
	app.Run();
	return 0;
}

#else

int main()
{
	return 0;
}

#endif // __HAIKU__
