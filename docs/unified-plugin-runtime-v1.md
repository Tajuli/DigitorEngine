# Unified Plugin Runtime v1

DigitorEngine exposes one execution façade for code-free filters, effects and video transitions.

## Request routing

- filters and effects use the single-input zero-copy request contract;
- transitions use the outgoing/incoming/output/progress request contract;
- preview and export are represented as surfaces on the same app-facing envelope;
- a transition submitted through the single-input path is rejected.

The façade delegates to the existing production GPU runtimes. It does not duplicate shader execution, introduce CPU readback or silently fall back after a GPU backend has been selected.

## App boundary

The Digitor app can use one integration path for every website-imported or built-in plugin. The app remains authoritative for free/paid access, purchases, subscriptions, preview permission and export permission. DigitorEngine receives only requests the app has authorized and performs technical validation and processing.

## Extensibility

A new package stays code-free when it conforms to either the generic single-input or generic transition GPU contract. No new switch statement keyed by plugin ID is introduced in the engine.
