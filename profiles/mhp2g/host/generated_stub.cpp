#include "psprecomp/runtime.hpp"

namespace psprecomp {

// Linked only while profiles/mhp2g/generated is empty so the host builds
// before the first AOT generation.
void register_generated_functions(Runtime &) {}

} // namespace psprecomp
