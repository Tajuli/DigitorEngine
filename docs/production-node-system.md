# Production Node System

`ProductionNodeGraph` is the authoritative clip color/effect recipe used by both preview and export.

## Topology

- Input and Output are protected, non-selectable endpoints.
- Serial insertion automatically reconnects downstream consumers.
- Parallel insertion creates two branches and an automatic mixer.
- Deleting one parallel branch removes the mixer and converts the survivor to a serial node.
- Cycles, duplicate connections, endpoint deletion, and endpoint selection are rejected.

## Selected-node operations

The selected grade node owns an ordered operation stack supporting Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, 1D/3D LUTs, effects, and Power Windows. Bypass preserves topology while skipping the node stack.

Qualifier and Power Window mattes constrain subsequent operations in the same node. Preview and export should persist and execute the same `recipe_identity()`; only execution quality/resolution may differ.

## Current boundary

The graph has a deterministic CPU/reference executor and uses the existing effect command contract. Native backend graph submission should consume the same topology and operation identities rather than creating a second recipe.
