#include "policy.h"
int instrument_discoverable(int paired, int revoking, uint64_t now, uint64_t window) { return !paired && !revoking && now < window; }
int instrument_can_start(int release_required, int gate_valid, int prepared, int busy, uint64_t held_ms) { return !release_required && gate_valid && prepared && !busy && held_ms >= 1500; }
