// The camera-gate resolve of A' with the mask fold: it computes the per-pixel tests itself (no mask draw), composes the region
// and closure holds and writes the depth history as RT2; see resolve.hlsl, X3M_REGION_HOLD.
#define X3M_REGION_HOLD 1
#include "resolve.hlsl"
