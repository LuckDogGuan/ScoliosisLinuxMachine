#ifndef INSTRUMENT_POLICY_H
#define INSTRUMENT_POLICY_H
#include <stdint.h>
int instrument_discoverable(int paired, int revoking, uint64_t now, uint64_t window);
int instrument_can_start(int release_required, int gate_valid, int prepared, int busy, uint64_t held_ms);
#endif
