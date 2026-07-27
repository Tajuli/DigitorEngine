# YUV conversion

The CPU reference supports integer 8–16-bit code values and BT.601, BT.709, or
BT.2020 non-constant-luminance coefficients. For `n` bits limited range uses
`Y=(code-16*2^(n-8))/(219*2^(n-8))` and
`Cb,Cr=(code-128*2^(n-8))/(224*2^(n-8))`. Full range uses `Y=code/(2^n-1)` and
chroma `(code-2^(n-1))/(2^n-1)`. With `Kg=1-Kr-Kb`:
`R=Y+2(1-Kr)Cr`, `B=Y+2(1-Kb)Cb`, and
`G=Y-2Kb(1-Kb)Cb/Kg-2Kr(1-Kr)Cr/Kg`.
Coefficients `(Kr,Kb)` are 601 `(.299,.114)`, 709 `(.2126,.0722)`, and 2020
`(.2627,.0593)`. No clamp is performed.

The existing video-plane facade handles NV12/YUV420P odd dimensions, top-left
row order, and padded strides, but predates metadata-aware graph execution.
Connecting it to this authoritative conversion and implementing native texture
processing remain not implemented; it must not be described as GPU execution.
