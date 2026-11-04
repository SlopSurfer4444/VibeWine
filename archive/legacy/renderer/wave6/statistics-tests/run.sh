#!/bin/sh
set -eu
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
case "${1:-native}" in
  native) sanitizer=; define=-DDXMT_DEBUG; source=statistics_test.cpp; binary=statistics_test ;;
  tsan) sanitizer=-fsanitize=thread; define=-DDXMT_DEBUG; source=statistics_test.cpp; binary=statistics_test_tsan ;;
  asan) sanitizer=-fsanitize=address,undefined; define=-DDXMT_DEBUG; source=statistics_test.cpp; binary=statistics_test_asan ;;
  release) sanitizer=; define=; source=statistics_release_test.cpp; binary=statistics_release_test ;;
  *) echo 'usage: run.sh [native|tsan|asan|release]' >&2; exit 2 ;;
esac
"$CXX" -std=c++20 -g -O1 -Wall -Wextra -Werror $sanitizer $define -pthread -I../source/src/dxmt -I../source/src/util "$source" -o "$binary"
./"$binary"
