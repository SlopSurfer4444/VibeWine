#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang -x objective-c -fblocks -fno-objc-arc -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I ../source/bridge/src/winemetal/unix \
  test_listener.m -framework Foundation -framework Metal -framework CoreFoundation -o test_listener_asan
./test_listener_asan
