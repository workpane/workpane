#pragma once

// The vendored Ghostty headers declare a member named emit, which collides with the Qt keyword macro.
#if defined(emit)
#undef emit
#include <ghostty/vt.h>
#define emit
#else
#include <ghostty/vt.h>
#endif
