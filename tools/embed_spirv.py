#!/usr/bin/env python3
import pathlib
import struct
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: embed_spirv.py <input.spv> <symbol> <output.hpp>", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1])
    symbol = sys.argv[2]
    output = pathlib.Path(sys.argv[3])
    data = source.read_bytes()
    if len(data) < 20 or len(data) % 4 != 0:
        raise ValueError("SPIR-V payload must be non-empty and 32-bit aligned")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError("SPIR-V magic mismatch")
    lines = ["#pragma once", "", "#include <cstddef>", "#include <cstdint>", "", "namespace digitor::generated {", f"inline constexpr std::uint32_t {symbol}[] = {{"]
    for index in range(0, len(words), 8):
        chunk = ", ".join(f"0x{word:08x}u" for word in words[index:index + 8])
        lines.append(f"  {chunk},")
    lines.extend(["};", f"inline constexpr std::size_t {symbol}_word_count = sizeof({symbol}) / sizeof({symbol}[0]);", "}  // namespace digitor::generated", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
