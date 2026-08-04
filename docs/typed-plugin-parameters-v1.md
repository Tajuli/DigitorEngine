# Typed Plugin Parameters v1

Digitor filter, effect and transition packages can now describe their controls entirely in package metadata. A normal plugin can add rich controls without editing DigitorEngine or hard-coding a new Digitor app panel.

## Supported parameter kinds

- floating-point and integer values
- boolean toggles
- enumerations/dropdowns
- RGBA colors
- 2D points and 3D vectors
- angles
- curves and gradients
- asset and texture references
- text values

## Data-driven UI metadata

Each parameter may declare its control type, group, unit, precision, component count, default components, text default, dropdown options and conditional visibility. The Digitor app can map these declarations to native Flutter controls.

## Security and integrity

Typed UI metadata is validated before a catalog is accepted and is included in the canonical signed catalog payload. A modified control schema therefore invalidates the publisher signature.

## Compatibility

Existing numeric packages remain valid because the default schema is a floating-point automatic control. Filter, effect and transition packages all use the same schema.

## Commercial policy boundary

This schema contains no free/paid, subscription, purchase, preview-right or export-right logic. The Digitor app remains authoritative for commercial access; DigitorEngine validates metadata and processes authorized requests.
