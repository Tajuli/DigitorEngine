# Transfer functions

All functions reject NaN and infinity. Linear, sRGB, BT.709 and pure gamma use an
odd, sign-preserving extension for negative values. PQ and HLG reject negatives.
Nothing clamps output.

For magnitude `x`: sRGB decode is `x/12.92` through 0.04045, otherwise
`((x+0.055)/1.055)^2.4`; encode is `12.92L` through 0.0031308, otherwise
`1.055 L^(1/2.4)-0.055`. BT.709 decode is `E/4.5` below 0.081, otherwise
`((E+0.099)/1.099)^(1/0.45)`; encode uses 0.018, 4.5, 1.099 and 0.45.
Gamma 2.2/2.4 and the ideal-black BT.1886 foundation use the named exponent.

ST 2084 uses `m1=2610/16384`, `m2=2523/32`, `c1=3424/4096`,
`c2=2413/128`, `c3=2392/128`: `L=((max(E^(1/m2)-c1,0))/(c2-c3E^(1/m2)))^(1/m1)`
and the algebraic inverse. HLG uses `a=0.17883277`, `b=1-4a`, `c=0.55991073`:
decode is `E²/3` through 0.5, otherwise `(exp((E-c)/a)+b)/12`; encode is
`sqrt(3L)` through `1/12`, otherwise `a ln(12L-b)+c`.
