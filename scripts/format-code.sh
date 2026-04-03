#!/usr/bin/env bash
# QuantClaw Code Formatting Script
# This script formats all C++ source files using clang-format

set -euo pipefail

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}QuantClaw Code Formatter${NC}"
echo "================================"

# Check if clang-format is installed
CLANG_FORMAT_BIN=""
if command -v clang-format >/dev/null 2>&1; then
    CLANG_FORMAT_BIN="clang-format"
elif command -v clang-format-14 >/dev/null 2>&1; then
    CLANG_FORMAT_BIN="clang-format-14"
fi

if [ -z "$CLANG_FORMAT_BIN" ]; then
    echo -e "${RED}Error: clang-format not found${NC}"
    echo ""
    echo "Please install clang-format first:"
    echo "  Ubuntu/Debian: sudo apt-get install clang-format"
    echo "  macOS:         brew install clang-format"
    echo "  Windows:       Download from LLVM releases"
    exit 1
fi

CLANG_FORMAT_VERSION=$($CLANG_FORMAT_BIN --version | grep -oP '\d+\.\d+' | head -1)
echo -e "Using clang-format version: ${GREEN}${CLANG_FORMAT_VERSION}${NC}"
echo ""

# Get project root directory
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Check if .clang-format exists
if [ ! -f ".clang-format" ]; then
    echo -e "${RED}Error: .clang-format file not found${NC}"
    exit 1
fi

echo "Project root: $PROJECT_ROOT"
echo ""

# Find all C++ source and module files
echo "Finding C++ source files..."
mapfile -d '' FILES < <(find src include tests -type f \( \
    -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o \
    -name "*.hpp" -o -name "*.h" -o -name "*.hh" -o \
    -name "*.cppm" -o -name "*.ixx" \
\) -print0 2>/dev/null || true)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo -e "${YELLOW}No C++ files found${NC}"
    exit 0
fi

FILE_COUNT=${#FILES[@]}
echo -e "Found ${GREEN}${FILE_COUNT}${NC} files to format"
echo ""

CHECK_ONLY=false
if [[ "${1:-}" == "--check" ]]; then
    CHECK_ONLY=true
elif [[ $# -gt 0 ]]; then
    echo -e "${RED}Error: Unknown argument '$1'${NC}"
    echo "Usage: ./scripts/format-code.sh [--check]"
    exit 1
fi

# Check if --check flag is passed
if [[ "$CHECK_ONLY" == "true" ]]; then
    echo "Running format check (dry-run)..."
    FAILED=0

    for FILE in "${FILES[@]}"; do
        if ! $CLANG_FORMAT_BIN --dry-run --Werror "$FILE" >/dev/null 2>&1; then
            echo -e "${RED}✗${NC} $FILE"
            FAILED=$((FAILED + 1))
        else
            echo -e "${GREEN}✓${NC} $FILE"
        fi
    done

    echo ""
    if [ $FAILED -gt 0 ]; then
        echo -e "${RED}Format check failed: $FAILED file(s) need formatting${NC}"
        echo "Run './scripts/format-code.sh' to fix formatting"
        exit 1
    else
        echo -e "${GREEN}All files are properly formatted!${NC}"
        exit 0
    fi
else
    # Format all files
    echo "Formatting files..."
    FORMATTED=0

    for FILE in "${FILES[@]}"; do
        $CLANG_FORMAT_BIN -i "$FILE"
        echo -e "${GREEN}✓${NC} $FILE"
        FORMATTED=$((FORMATTED + 1))
    done

    echo ""
    echo -e "${GREEN}Successfully formatted ${FORMATTED} file(s)!${NC}"
fi
