from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = root / "CMakeLists.txt"
s = p.read_text()
anchor = "    add_executable(digitor_engine_owned_platform_production_test tests/test_engine_owned_platform_production.cpp)\n"
addition = anchor + "    target_include_directories(digitor_engine_owned_platform_production_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)\n"
if anchor not in s:
    raise RuntimeError("engine-owned production test target anchor missing")
p.write_text(s.replace(anchor, addition, 1))
Path(__file__).unlink()
