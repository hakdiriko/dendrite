// How deep each of the cut's neurons sits, front to back (see dsp/FlyData.h for which fly this
// build holds; generated per fly by tools/rig/depth.py, the place proven against FlyData's).
#pragma once

#if defined (DENDRITE_FLY_FLYWIRE)
 #include "../flies/flywire/Depth.h"
#else
 #include "../flies/malecns/Depth.h"
#endif
