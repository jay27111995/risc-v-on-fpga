#!/bin/bash
# Build all example programs

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

PASS=0
FAIL=0

for src in examples/*.c; do
    name=$(basename "$src" .c)
    if ./build.sh "$src" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} $name"
        ((PASS++))
    else
        echo -e "${RED}✗${NC} $name"
        ((FAIL++))
    fi
done

echo ""
echo "Built: $PASS passed, $FAIL failed"
