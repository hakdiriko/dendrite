// How hard each of the cut's neurons works on ordinary music: the fly's mean rate on six reference
// sounds (generated per fly by tools/rig/reference.py). The 2-DG film holds this session against it.
#pragma once

#if defined (DENDRITE_FLY_FLYWIRE)
 #include "../flies/flywire/Reference.h"
#else
 #include "../flies/malecns/Reference.h"
#endif
