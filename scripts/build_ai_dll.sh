#!/usr/bin/env bash
# Incremental parallel MinGW-w64 x64 build script for the GoH AI DLL.
# Compiles MinHook + each translation unit into cached .o files under
# local-work/build-ai/obj/ so single-module edits relink in seconds.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

OUT_PATH="local-work/build-ai/goh_ai_mod.dll"
OBJ_DIR="local-work/build-ai/obj"
MINHOOK_DIR="${MINHOOK_DIR:-local-work/minhook}"
CLEAN=false
STANDALONE_GRENADE=false
BUILD_PROXY=true
PROXY_ONLY=false

usage() {
    cat <<'EOF'
Usage: ./scripts/build_ai_dll.sh [options]

Options:
  --out <path>          Output DLL path (default: local-work/build-ai/goh_ai_mod.dll)
  --obj-dir <dir>       Object cache directory (default: local-work/build-ai/obj)
  --minhook <dir>       MinHook checkout root (default: local-work/minhook)
  --standalone-grenade  Build standalone grenade_dodge.dll instead of unified AI DLL
  --no-proxy            Skip building the drop-in version.dll proxy beside the AI DLL
  --proxy-only          Build only the drop-in version.dll proxy
  --clean               Remove cached object files before compiling
  -h, --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)                OUT_PATH="$2"; shift 2 ;;
        --obj-dir)            OBJ_DIR="$2"; shift 2 ;;
        --minhook)            MINHOOK_DIR="$2"; shift 2 ;;
        --standalone-grenade) STANDALONE_GRENADE=true; BUILD_PROXY=false; shift ;;
        --no-proxy)           BUILD_PROXY=false; shift ;;
        --proxy-only)         PROXY_ONLY=true; BUILD_PROXY=true; shift ;;
        --clean)              CLEAN=true; shift ;;
        -h|--help)            usage; exit 0 ;;
        *)                    echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"

if ! command -v "$CC" >/dev/null 2>&1 || ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: MinGW-w64 cross-compilers ($CC / $CXX) not found on PATH." >&2
    exit 1
fi

if [[ "$PROXY_ONLY" != true ]]; then
    if [[ ! -f "$MINHOOK_DIR/include/MinHook.h" ]]; then
        if [[ -f "build/_deps/minhook-src/include/MinHook.h" ]]; then
            MINHOOK_DIR="build/_deps/minhook-src"
        else
            echo "ERROR: MinHook not found at $MINHOOK_DIR (set --minhook or MINHOOK_DIR)." >&2
            exit 1
        fi
    fi
fi

if [[ "$STANDALONE_GRENADE" == true ]] && [[ "$OUT_PATH" == "local-work/build-ai/goh_ai_mod.dll" ]]; then
    OUT_PATH="local-work/build-ai/grenade_dodge.dll"
fi
PROXY_OUT="$(dirname "$OUT_PATH")/version.dll"

if [[ "$CLEAN" == true ]]; then
    rm -rf "$OBJ_DIR"
fi
mkdir -p "$OBJ_DIR" "$(dirname "$OUT_PATH")"

COMMON_WARN=(
    -Wall
    -Wextra
    -Wshadow
    -Wformat=2
    -Wundef
    -Wnull-dereference
    -Wduplicated-cond
    -Wlogical-op
    -Werror=return-type
    -Wno-unknown-pragmas
    -Wno-cast-function-type
    -Werror
)

C_FLAGS=(-m64 -O2 -I"$MINHOOK_DIR/include")
CXX_FLAGS=(-std=c++17 -m64 -O2 -I"$REPO_ROOT/mods" -I"$MINHOOK_DIR/include" "${COMMON_WARN[@]}")
PROXY_CXX_FLAGS=(-std=c++17 -m64 -O2 "${COMMON_WARN[@]}")

needs_rebuild() {
    local src="$1"
    local obj="$2"
    local dep="${obj}.d"
    if [[ ! -f "$obj" ]]; then
        return 0
    fi
    if [[ "$src" -nt "$obj" ]]; then
        return 0
    fi
    if [[ -f "$dep" ]]; then
        local hdr
        while IFS= read -r hdr; do
            if [[ -n "$hdr" ]] && [[ -f "$hdr" ]] && [[ "$hdr" -nt "$obj" ]]; then
                return 0
            fi
        done < <(tr ' \\' '\n' < "$dep" | grep -v ':$' | grep -v '^$' || true)
    fi
    return 1
}

MINHOOK_SRCS=(
    "$MINHOOK_DIR/src/buffer.c"
    "$MINHOOK_DIR/src/hook.c"
    "$MINHOOK_DIR/src/trampoline.c"
    "$MINHOOK_DIR/src/hde/hde32.c"
    "$MINHOOK_DIR/src/hde/hde64.c"
)

if [[ "$STANDALONE_GRENADE" == true ]]; then
    CPP_SRCS=(mods/grenade_dodge.cpp)
else
    CPP_SRCS=(
        mods/goh_ai_mod.cpp
        mods/tu_combat.cpp
        mods/tu_infantry.cpp
        mods/tu_tanks.cpp
    )
fi

OBJS=()
PIDS=()

if [[ "$PROXY_ONLY" != true ]]; then
    for src in "${MINHOOK_SRCS[@]}"; do
        base="$(basename "$src" .c)"
        obj="$OBJ_DIR/mh_${base}.o"
        OBJS+=("$obj")
        if needs_rebuild "$src" "$obj"; then
            echo "[cc]  $src -> $obj"
            "$CC" "${C_FLAGS[@]}" -MMD -MF "${obj}.d" -c "$src" -o "$obj" &
            PIDS+=($!)
        fi
    done

    for src in "${CPP_SRCS[@]}"; do
        base="$(basename "$src" .cpp)"
        obj="$OBJ_DIR/${base}.o"
        OBJS+=("$obj")
        if needs_rebuild "$src" "$obj"; then
            echo "[cxx] $src -> $obj"
            "$CXX" "${CXX_FLAGS[@]}" -MMD -MF "${obj}.d" -c "$src" -o "$obj" &
            PIDS+=($!)
        fi
    done
fi

PROXY_OBJ="$OBJ_DIR/version_proxy.o"
if [[ "$BUILD_PROXY" == true ]]; then
    if needs_rebuild "proxy/version_proxy.cpp" "$PROXY_OBJ"; then
        echo "[cxx] proxy/version_proxy.cpp -> $PROXY_OBJ"
        "$CXX" "${PROXY_CXX_FLAGS[@]}" -MMD -MF "${PROXY_OBJ}.d" -c "proxy/version_proxy.cpp" -o "$PROXY_OBJ" &
        PIDS+=($!)
    fi
fi

FAIL=0
for pid in ${PIDS[@]+"${PIDS[@]}"}; do
    if ! wait "$pid"; then
        FAIL=1
    fi
done
if [[ "$FAIL" -ne 0 ]]; then
    echo "ERROR: Compilation failed." >&2
    exit 1
fi

if [[ "$PROXY_ONLY" != true ]]; then
    echo "[ld]  Linking $OUT_PATH (${#OBJS[@]} objects)..."
    "$CXX" -shared -static -m64 -O2 "${OBJS[@]}" -o "$OUT_PATH"
    echo "[ok]  Built $OUT_PATH ($(wc -c < "$OUT_PATH" | tr -d ' ') bytes)"
fi

if [[ "$BUILD_PROXY" == true ]]; then
    echo "[ld]  Linking $PROXY_OUT..."
    "$CXX" -shared -static -m64 -O2 "$PROXY_OBJ" -o "$PROXY_OUT"
    echo "[ok]  Built $PROXY_OUT ($(wc -c < "$PROXY_OUT" | tr -d ' ') bytes)"
fi
