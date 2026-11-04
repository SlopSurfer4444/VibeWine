#!/bin/sh
set -eu
cd "$(dirname "$0")"
# Require the frozen fixture candidate to be the actual exported production.
cmp candidate/wave7_feedback.h ../../../../native/wave7/source/bridge/wave7_feedback.h
cmp candidate/wave7_feedback.inc ../../../../native/wave7/source/bridge/src/winemetal/unix/wave7_feedback.inc
clang -dynamiclib capture_mock.c -o capture_mock.dylib
for mode in regular asan; do
  flags=''
  if [ "$mode" = asan ]; then flags='-fsanitize=address,undefined'; fi
  for candidate in baseline candidate; do
    expected=1
    if [ "$candidate" = baseline ]; then expected=0; fi
    clang -x objective-c -fblocks -fno-objc-arc -Wall -Wextra -Werror $flags -I "$candidate" -DEXPECT_CLOSE_QUEUED="$expected" test_close.m -framework Foundation -framework Metal -o "test-close-$candidate-$mode"
    "./test-close-$candidate-$mode" > "$candidate-$mode.log" 2>&1
  done
  clang -x objective-c -fblocks -fno-objc-arc -Wall -Wextra -Werror $flags -I candidate test_feedback.m -framework Foundation -framework Metal -o "test-existing-candidate-$mode"
  W7FB_CAPTURE_MOCK="$PWD/capture_mock.dylib" "./test-existing-candidate-$mode" > "existing-candidate-$mode.log" 2>&1
done
