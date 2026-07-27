# Implementation status — v4.5.0

Environment for CPU evidence: local Linux x86-64, GCC 13.3, Debug static,
`digitor_tests`; measured assertions use max error bounds from
`test_color_science`. CI and native hardware have not run for this commit.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Test Evidence | Status |
|---|---|---|---|---|---|---|---|
| Linear transfer | yes | no | no | no | no | `src/gpu/color_science.cpp`; round trip, <=8e-6 | CPU reference only |
| sRGB transfer | yes | no | no | no | no | thresholds/round trip, <=8e-6 | CPU reference only |
| BT.709 transfer | yes | no | no | no | no | thresholds/round trip, <=8e-6 | CPU reference only |
| Gamma 2.2 | yes | no | no | no | no | round trip, <=8e-6 | CPU reference only |
| Gamma 2.4 | yes | no | no | no | no | round trip, <=8e-6 | CPU reference only |
| PQ | yes | no | no | no | no | 100-nit known value/round trip, <=8e-6 | CPU reference only |
| HLG | yes | no | no | no | no | 0.5 boundary/round trip, <=8e-6 | CPU reference only |
| BT.601 YUV conversion | yes | no | no | no | no | limited white fixture, <=2e-6 | CPU reference only |
| BT.709 YUV conversion | yes | no | no | no | no | limited black fixture, <=2e-6 | CPU reference only |
| BT.2020 YUV conversion | yes | no | no | no | no | full black fixture, <=2e-6 | CPU reference only |
| Limited range | yes | no | no | no | no | black/white fixtures | CPU reference only |
| Full range | yes | no | no | no | no | centered black fixture | CPU reference only |
| sRGB/BT.709 primaries | yes | no | no | no | no | published D65 matrix, <=1e-9 | CPU reference only |
| Display P3 primaries | yes | no | no | no | no | P3 red to 709, <=1e-8 | CPU reference only |
| BT.2020 primaries | yes | no | no | no | no | derivation API/round trip | CPU reference only |
| Bradford adaptation | yes | no | no | no | no | D65-to-D50 white, <=2e-7 | CPU reference only |
| Working-space transform | yes | no | no | no | no | matrix graph stage | CPU reference only |
| Tone-map passthrough | yes | no | no | no | no | negative/over-range exact sample | CPU reference only |
| RGB Curves contract | contract | no | no | no | no | contract assertion | Interface/design only |
| Primary Wheels contract | contract | no | no | no | no | compile coverage | Interface/design only |
| Log Wheels contract | contract | no | no | no | no | compile coverage | Interface/design only |
| HSL Qualifier contract | contract | no | no | no | no | compile coverage | Interface/design only |
| LUT contract | contract | no | no | no | no | contract assertion | Interface/design only |

No row is “Production implementation, verified”: native GPU pipeline compilation,
independent hardware comparison and full CI evidence remain absent. Older grading,
qualifier and LUT classes are not v4.5 color-foundation implementations.
