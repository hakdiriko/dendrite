// Who each of the cut's neurons is in its atlas (see dsp/FlyData.h for which fly this build holds).
#pragma once

#if defined (DENDRITE_FLY_FLYWIRE)
 #include "../flies/flywire/Identities.h"
#else
 #include "../flies/malecns/Identities.h"
#endif
