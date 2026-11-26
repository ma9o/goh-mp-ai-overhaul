#!/usr/bin/env bash
# C++ formatting, compiler-warning, and clang-tidy enforcement script.
# Compatible with macOS bash 3.2+ and Linux bash.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

MODE="changed"
FIX=false
WERROR=false
RUN_FORMAT=true
RUN_COMPILE=true
RUN_TIDY=true
REQUIRE_TIDY=false
EXPLICIT_FILES=()

usage() {
    cat <<'EOF'
Usage: ./scripts/lint_cpp.sh [options] [file ...]

Target selection (ignored when explicit files are passed):
  --changed        Check modified/staged tracked C++ files (default)
  --all            Check all tracked C++ files under proxy/, mods/
  --all-files      Check all C++ files on disk under proxy/, mods/ (incl. untracked)

Actions:
  --fix            Apply clang-format in place (instead of dry-run check)
  --werror         Treat compiler warnings as errors (-Werror)
  --no-format      Skip clang-format
  --no-compile     Skip MinGW compiler warning check
  --no-tidy        Skip clang-tidy
  --require-tidy   Fail if clang-tidy is not installed
  -h, --help       Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --changed)      MODE="changed"; shift ;;
        --all)          MODE="all"; shift ;;
        --all-files)    MODE="all-files"; shift ;;
        --fix)          FIX=true; shift ;;
        --werror)       WERROR=true; shift ;;
        --no-format)    RUN_FORMAT=false; shift ;;
        --no-compile)   RUN_COMPILE=false; shift ;;
        --no-tidy)      RUN_TIDY=false; shift ;;
        --require-tidy) REQUIRE_TIDY=true; shift ;;
        -h|--help)      usage; exit 0 ;;
        -*)             echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)              EXPLICIT_FILES+=("$1"); shift ;;
    esac
done

is_cpp_file() {
    case "$1" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx) return 0 ;;
        *) return 1 ;;
    esac
}

is_cpp_source() {
    case "$1" in
        *.cc|*.cpp|*.cxx) return 0 ;;
        *) return 1 ;;
    esac
}

in_cpp_scope() {
    case "$1" in
        proxy/*|mods/*) return 0 ;;
        *) return 1 ;;
    esac
}

find_clang_format() {
    if [[ -n "${CLANG_FORMAT:-}" ]] && [[ -x "$CLANG_FORMAT" ]]; then
        echo "$CLANG_FORMAT"; return 0
    fi
    if command -v clang-format >/dev/null 2>&1; then
        command -v clang-format; return 0
    fi
    if [[ -x "/opt/homebrew/opt/llvm/bin/clang-format" ]]; then
        echo "/opt/homebrew/opt/llvm/bin/clang-format"; return 0
    fi
    if command -v xcrun >/dev/null 2>&1; then
        local xc
        if xc="$(xcrun --find clang-format 2>/dev/null)" && [[ -x "$xc" ]]; then
            echo "$xc"; return 0
        fi
    fi
    if [[ -x "/Library/Developer/CommandLineTools/usr/bin/clang-format" ]]; then
        echo "/Library/Developer/CommandLineTools/usr/bin/clang-format"; return 0
    fi
    return 1
}

find_clang_tidy() {
    if [[ -n "${CLANG_TIDY:-}" ]] && [[ -x "$CLANG_TIDY" ]]; then
        echo "$CLANG_TIDY"; return 0
    fi
    if command -v clang-tidy >/dev/null 2>&1; then
        command -v clang-tidy; return 0
    fi
    if [[ -x "/opt/homebrew/opt/llvm/bin/clang-tidy" ]]; then
        echo "/opt/homebrew/opt/llvm/bin/clang-tidy"; return 0
    fi
    return 1
}

find_mingw_cxx() {
    if [[ -n "${MINGW_CXX:-}" ]] && [[ -x "$MINGW_CXX" ]]; then
        echo "$MINGW_CXX"; return 0
    fi
    if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
        command -v x86_64-w64-mingw32-g++; return 0
    fi
    return 1
}

find_minhook_inc() {
    if [[ -n "${MINHOOK_INCLUDE_DIR:-}" ]] && [[ -f "$MINHOOK_INCLUDE_DIR/MinHook.h" ]]; then
        echo "$MINHOOK_INCLUDE_DIR"; return 0
    fi
    if [[ -f "$REPO_ROOT/local-work/minhook/include/MinHook.h" ]]; then
        echo "$REPO_ROOT/local-work/minhook/include"; return 0
    fi
    if [[ -f "$REPO_ROOT/build/_deps/minhook-src/include/MinHook.h" ]]; then
        echo "$REPO_ROOT/build/_deps/minhook-src/include"; return 0
    fi
    return 1
}

# Collect target files
FILES=()
if [[ ${#EXPLICIT_FILES[@]} -gt 0 ]]; then
    for f in "${EXPLICIT_FILES[@]}"; do
        f="${f#./}"
        if [[ -f "$f" ]] && is_cpp_file "$f"; then
            FILES+=("$f")
        fi
    done
elif [[ "$MODE" == "all" ]]; then
    while IFS= read -r f; do
        if [[ -f "$f" ]] && in_cpp_scope "$f" && is_cpp_file "$f"; then
            FILES+=("$f")
        fi
    done < <(git ls-files proxy mods)
elif [[ "$MODE" == "all-files" ]]; then
    while IFS= read -r f; do
        f="${f#./}"
        if [[ -f "$f" ]] && is_cpp_file "$f"; then
            FILES+=("$f")
        fi
    done < <(find proxy mods -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) | sort)
else
    while IFS= read -r f; do
        if [[ -n "$f" ]] && [[ -f "$f" ]] && in_cpp_scope "$f" && is_cpp_file "$f"; then
            FILES+=("$f")
        fi
    done < <({ git diff --name-only HEAD -- proxy mods 2>/dev/null || true; git diff --cached --name-only -- proxy mods 2>/dev/null || true; } | sort -u)
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "[lint_cpp] No matching C++ files to check (mode: $MODE)."
    exit 0
fi

CPP_SOURCES=()
for f in "${FILES[@]}"; do
    if is_cpp_source "$f"; then
        CPP_SOURCES+=("$f")
    fi
done

echo "[lint_cpp] Checking ${#FILES[@]} file(s) (${#CPP_SOURCES[@]} translation unit(s))..."

FAILURES=0

# 1. clang-format
if [[ "$RUN_FORMAT" == true ]]; then
    if CF_BIN="$(find_clang_format)"; then
        if [[ "$FIX" == true ]]; then
            echo "[format] Formatting ${#FILES[@]} file(s) with $CF_BIN..."
            "$CF_BIN" -i --style=file "${FILES[@]}"
            echo "[format] OK (formatted in place)"
        else
            echo "[format] Checking ${#FILES[@]} file(s) with $CF_BIN..."
            if "$CF_BIN" --dry-run --Werror --style=file "${FILES[@]}"; then
                echo "[format] OK"
            else
                echo "[format] FAILED (re-run with --fix to format)" >&2
                FAILURES=$((FAILURES + 1))
            fi
        fi
    else
        echo "[format] ERROR: clang-format not found (install via 'brew install clang-format' or 'apt install clang-format')" >&2
        FAILURES=$((FAILURES + 1))
    fi
fi

# 2. MinGW compiler warnings check
MINGW_BIN=""
if MINGW_BIN="$(find_mingw_cxx)"; then :; else MINGW_BIN=""; fi

MINHOOK_INC=""
if MINHOOK_INC="$(find_minhook_inc)"; then :; else MINHOOK_INC=""; fi

if [[ "$RUN_COMPILE" == true ]] && [[ ${#CPP_SOURCES[@]} -gt 0 ]]; then
    if [[ -n "$MINGW_BIN" ]]; then
        CXX_FLAGS=(
            -std=c++17
            -m64
            -fsyntax-only
            -I"$REPO_ROOT/mods"
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
        )
        if [[ -n "$MINHOOK_INC" ]]; then
            CXX_FLAGS+=(-I"$MINHOOK_INC")
        else
            echo "[compile] NOTE: MinHook.h not found under local-work/minhook/include or build/_deps/minhook-src/include" >&2
        fi
        if [[ "$WERROR" == true ]]; then
            CXX_FLAGS+=(-Werror)
        fi

        echo "[compile] Syntax/warning check (${#CPP_SOURCES[@]} file(s)) with $MINGW_BIN..."
        COMPILE_FAIL=0
        for src in "${CPP_SOURCES[@]}"; do
            if ! "$MINGW_BIN" "${CXX_FLAGS[@]}" "$src"; then
                COMPILE_FAIL=1
            fi
        done
        if [[ $COMPILE_FAIL -eq 0 ]]; then
            echo "[compile] OK"
        else
            echo "[compile] FAILED" >&2
            FAILURES=$((FAILURES + 1))
        fi
    else
        echo "[compile] SKIP: x86_64-w64-mingw32-g++ not found (install mingw-w64 to enable cross-compile warning checks)"
    fi
fi

# 3. clang-tidy
if [[ "$RUN_TIDY" == true ]] && [[ ${#CPP_SOURCES[@]} -gt 0 ]]; then
    if CT_BIN="$(find_clang_tidy)"; then
        echo "[tidy] Running $CT_BIN on ${#CPP_SOURCES[@]} file(s)..."
        TIDY_FAIL=0
        if [[ -f "$REPO_ROOT/build/compile_commands.json" ]]; then
            TIDY_ARGS=(-p "$REPO_ROOT/build")
            if grep -q 'mingw32' "$REPO_ROOT/build/compile_commands.json" 2>/dev/null; then
                TIDY_ARGS+=(--extra-arg=--target=x86_64-w64-mingw32)
            fi
            for src in "${CPP_SOURCES[@]}"; do
                if ! "$CT_BIN" "${TIDY_ARGS[@]}" "$src"; then
                    TIDY_FAIL=1
                fi
            done
        elif [[ -n "$MINGW_BIN" ]]; then
            EXTRA_ARGS=(
                --target=x86_64-w64-mingw32
                -std=c++17
                -D_WIN32
                -D_WIN64
                -I"$REPO_ROOT/mods"
            )
            if [[ -n "$MINHOOK_INC" ]]; then
                EXTRA_ARGS+=(-I"$MINHOOK_INC")
            fi
            while IFS= read -r inc; do
                inc="$(echo "$inc" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                if [[ -z "$inc" ]] || [[ ! -d "$inc" ]]; then
                    continue
                fi
                inc="$(cd "$inc" && pwd -P)"
                case "$inc" in
                    */lib/gcc/*/*/include|*/lib/gcc/*/*/include-fixed) continue ;;
                esac
                EXTRA_ARGS+=(-isystem "$inc")
            done < <("$MINGW_BIN" -v -x c++ /dev/null -fsyntax-only 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list\./p' | sed '1d;$d')

            for src in "${CPP_SOURCES[@]}"; do
                if ! "$CT_BIN" "$src" -- "${EXTRA_ARGS[@]}"; then
                    TIDY_FAIL=1
                fi
            done
        else
            echo "[tidy] SKIP: neither build/compile_commands.json nor x86_64-w64-mingw32-g++ found"
        fi

        if [[ $TIDY_FAIL -eq 0 ]]; then
            echo "[tidy] OK"
        else
            echo "[tidy] FAILED" >&2
            FAILURES=$((FAILURES + 1))
        fi
    else
        if [[ "$REQUIRE_TIDY" == true ]]; then
            echo "[tidy] ERROR: clang-tidy not found (install via 'brew install llvm' or 'apt install clang-tidy')" >&2
            FAILURES=$((FAILURES + 1))
        else
            echo "[tidy] SKIP: clang-tidy not installed (install via 'brew install llvm' on macOS or 'apt install clang-tidy' on Linux)"
        fi
    fi
fi

if [[ $FAILURES -ne 0 ]]; then
    exit 1
fi
echo "[lint_cpp] All checks passed."
