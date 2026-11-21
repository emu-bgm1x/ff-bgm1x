# 모든 스크립트가 공유하는 경로 (MSYS2 UCRT64 셸에서 실행)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/src"
WORK="$ROOT/work"
BUILD="$WORK/build"
RELEASE="$ROOT/release"
