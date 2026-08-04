# Plugin Preview/Export Parity v1

This milestone adds a regression qualification layer only. It does not replace or redesign the render graph, shader compiler, shader reflection, pipeline cache, backend selection, Primary Wheels, preview runtime or export runtime.

For filters, effects and transitions, preview and export must use the same plugin ID, exact version, SHA-256 package identity, typed parameter values, timeline time, transition progress and input/output color metadata.

A mismatch is rejected instead of silently producing a different export. Commercial free/paid and export authorization remain entirely in the Digitor app; DigitorEngine only checks that an authorized export uses the same processing identity as preview.

The qualification covers filter, effect and transition parity, plus version, parameter and color-metadata drift rejection on Ubuntu, Windows and macOS.
