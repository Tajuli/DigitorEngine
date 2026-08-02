# DigitorEngine v5.44.0 — Professional Color Management, HDR and Video Scopes

## Scope

This milestone adds the missing production color-management and scopes orchestration layer. It does not replace or rewrite the existing color-science primitives, Primary Wheels, Log Wheels, RGB Curves, HSL Qualifier, LUT processing, node graph, GPU backends, playback, import, audio or export systems.

## Color pipeline

The new `ProfessionalColorManagement` session separates four explicit spaces:

1. input/source space,
2. scene working space,
3. display/preview space,
4. export/output space.

The same working-space pixel can therefore produce a display-referred preview and a separately encoded delivery pixel without changing grading operations.

Supported managed spaces include:

- Rec.709 Gamma 2.4,
- sRGB,
- Display P3,
- Rec.2020 Gamma 2.4,
- Rec.2020 PQ,
- Rec.2020 HLG,
- Linear Rec.709,
- Linear Rec.2020,
- ACEScg,
- ACEScct,
- Sony S-Log3 / S-Gamut3.Cine,
- Canon Log 2 / Cinema Gamut,
- Panasonic V-Log / V-Gamut,
- ARRI LogC3 / Wide Gamut,
- Blackmagic Film Gen 5.

Existing `color_science` transfer functions, RGB/XYZ matrices, Bradford chromatic adaptation and color types are reused. Camera-log and ACEScct adapters are added only where the existing enum did not expose those encodings.

## HDR

The milestone includes:

- PQ and HLG display/output transforms,
- mastering-display metadata storage,
- MaxCLL and MaxFALL storage,
- reference-white, content-peak and display-peak policy,
- Reinhard, Hable and BT.2390-like tone-map policies,
- no-tone-map HDR output behavior,
- clip or compress gamut mapping,
- optional legal-range output mapping,
- FP32 working values and alpha preservation.

Static HDR metadata is retained by the session for the existing encode/mux host adapter to write into the selected delivery format.

## Video scopes

The scope result model includes:

- luma waveform,
- RGB waveform,
- RGB parade,
- vectorscope,
- RGB histogram,
- luma histogram,
- CIE xy chromaticity distribution,
- false-color RGBA overlay,
- HDR peak and average luminance,
- skin-tone indicator angle metadata.

A backend callback permits D3D12, Vulkan, Metal or GLES compute implementations to fill the exact same result contract. A deterministic CPU reference implementation remains available for qualification and unsupported hosts. Scope generation never changes preview or export pixels.

## Validation

`digitor_professional_color_management` verifies:

- camera-log to ACEScg conversion,
- separate SDR preview and HDR output transforms,
- alpha preservation,
- sRGB round-trip behavior,
- image transformation,
- all scope buffer dimensions and sample accounting,
- HDR luminance measurement,
- histogram conservation,
- GPU callback selection,
- invalid-input rejection,
- transform telemetry.

Real HDR monitors, ICC profiles, OS HDR presentation, GPU compute-scope performance, encoder metadata interoperability and calibrated reference-display measurements remain physical-device qualification gates.
