// Campiello easter egg: an old-school demoscene tribute.
//
// Ten clicks on the WON header globe open this window: a pirate galleon sailing across a
// copper-striped moonlit sea, a bouncing raster logo, a sine scroller, and an original
// Caribbean-flavoured chiptune synthesised live through the Media Kit. The window and drawing are
// BeAPI/C++ (a GUI cannot be pure assembly), but the animation core - an 8-bit sine-table lookup -
// is written in x86-64 inline assembly, the way it would have been back in the day.
//
// The music is an ORIGINAL tune written in the jaunty steel-drum spirit of classic pirate games,
// not a copy of any copyrighted melody.

#ifndef CAMPIELLO_OLD_SCHOOL_DEMO_H
#define CAMPIELLO_OLD_SCHOOL_DEMO_H

class BWindow;

// Creates (but does not Show) the demo window. Caller does ->Show(). Closing the window stops the
// music and deletes itself; it never quits the host application.
BWindow* CreateOldSchoolDemoWindow();

#endif // CAMPIELLO_OLD_SCHOOL_DEMO_H
