# Digitor working color space

The initial working identity is **linear BT.709 / linear sRGB primaries**, D65
white (`x=0.3127`, `y=0.3290`), scene-linear RGB. This conservative choice avoids
a conversion for the dominant SDR input and is broadly interoperable. It is not
ACEScg and no ACES support is claimed.

RGB is unbounded: negative and over-range values are preserved. Alpha is straight
(unpremultiplied) and never transfer-encoded or transformed. CPU reference and
validation use RGBA32_FLOAT. RGBA16_FLOAT is the intended real-time storage where
a backend qualifies FP16; no such GPU path is qualified in this milestone.
Reference white is `1.0` scene-linear. PQ additionally defines `1.0` as 10,000
cd/m² for its normalized EOTF; HLG remains a relative scene signal. No implicit
reference-display luminance is assigned. Interfaces name spaces explicitly so a
future working space can be added without changing these semantics.
