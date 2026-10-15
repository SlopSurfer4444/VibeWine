#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang -dynamiclib capture_mock.c -o capture_mock.dylib
clang -x objective-c -fblocks -fno-objc-arc -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I ../source/bridge -I ../source/bridge/src/winemetal/unix \
  test_feedback.m -framework Foundation -framework Metal -o test_feedback_asan
W7FB_CAPTURE_MOCK="$PWD/capture_mock.dylib" ./test_feedback_asan
