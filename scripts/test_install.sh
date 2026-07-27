#!/usr/bin/env sh
set -eu

build_input=${1:-build}
configuration=${2:-Release}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
consumer_source=$(CDPATH= cd -- "${script_dir}/../tests/consumer" && pwd -P)

# Resolve the build and install locations before passing them to CMake.  This
# keeps package discovery independent of the directory from which this script
# is invoked.
build_dir=$(CDPATH= cd -- "${build_input}" && pwd -P)
install_dir="${build_dir}/install"
cmake --install "${build_dir}" --config "${configuration}" --prefix "${install_dir}"
install_prefix=$(CDPATH= cd -- "${install_dir}" && pwd -P)

package_config=$(find "${install_prefix}" -type f -name DigitorEngineConfig.cmake -print -quit)
if [ -z "${package_config}" ] || [ ! -f "${package_config}" ]; then
    echo "DigitorEngineConfig.cmake was not installed." >&2
    echo "Install prefix: ${install_prefix}" >&2
    echo "Searched path: ${install_prefix}/**/DigitorEngineConfig.cmake" >&2
    echo "Files installed under lib/cmake or lib64/cmake:" >&2
    found_listing=false
    for cmake_root in "${install_prefix}/lib/cmake" "${install_prefix}/lib64/cmake"; do
        if [ -d "${cmake_root}" ]; then
            find "${cmake_root}" -maxdepth 6 -type f -print >&2
            found_listing=true
        fi
    done
    if [ "${found_listing}" = false ]; then
        echo "  (neither directory exists; all installed files follow)" >&2
        find "${install_prefix}" -maxdepth 6 -type f -print >&2
    fi
    exit 1
fi
package_dir=$(CDPATH= cd -- "$(dirname -- "${package_config}")" && pwd -P)

consumer_build="${build_dir}/consumer"
rm -rf "${consumer_build}"

printf 'Installed consumer qualification:\n  install prefix: %s\n  package directory: %s\n' \
    "${install_prefix}" "${package_dir}"
cmake -S "${consumer_source}" -B "${consumer_build}" \
    "-DDigitorEngine_DIR=${package_dir}" \
    "-DCMAKE_PREFIX_PATH=${install_prefix}" \
    "-DCMAKE_BUILD_TYPE=${configuration}"
cmake --build "${consumer_build}" --config "${configuration}" --parallel 2
ctest --test-dir "${consumer_build}" -C "${configuration}" --output-on-failure
