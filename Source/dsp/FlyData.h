// Which fly lives in this build: CMake's DENDRITE_FLY picks one folder in Source/flies/.
//   malecns (the default since 3.1): Janelia's male CNS v1.0, the brain of an adult male (CC BY 4.0)
//   flywire: FlyWire 783, an adult female brain (CC BY-NC 4.0), the fly of 2.1 to 3.0
// Each folder holds the generated wiring (FlyData), who each neuron is (Identities) and the
// atlas facts the model and the window need (Atlas.h).
#pragma once

#if defined (DENDRITE_FLY_FLYWIRE)
 #include "../flies/flywire/FlyData.h"
 #include "../flies/flywire/Atlas.h"
#else
 #include "../flies/malecns/FlyData.h"
 #include "../flies/malecns/Atlas.h"
#endif
