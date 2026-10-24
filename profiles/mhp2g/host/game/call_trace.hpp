#pragma once

namespace psprecomp {
class Runtime;
}

// A lab tool: MHP2G_TRACE_CALLS=<hex address>[,<hex address>...] (up to four functions of the executable) logs
// each call of those functions made while the hunter plays a weapon motion (1000 and up): the arguments, f12 and
// the return address, once per distinct combination. For finding which code checks what during an attack.
namespace mhp2g::game {
void prepare_call_trace(psprecomp::Runtime &runtime);
}
