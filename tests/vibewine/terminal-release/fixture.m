// SPDX-License-Identifier: MIT
// Runs only in a sacrificial child supervised by run.py.
#import <Foundation/Foundation.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wineunixlib.h"

extern void NSObject_release(uint64_t object);
static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t key;
static unsigned char detaching;
static int ready[2], cleanup[2];
static unsigned int releases;

static void mark(char value) {
  if (write(STDOUT_FILENO, &value, 1) != 1) _exit(91);
}
static void expect_byte(int fd, char expected) {
  char value;
  if (read(fd, &value, 1) != 1 || value != expected) _exit(92);
}
unsigned char RtlDllShutdownInProgress(void) { return detaching; }

@interface HostObject : NSObject
@end
@implementation HostObject
- (void)dealloc {
  mark('N'); // Native release actually reached before any potentially stuck lock.
  if (pthread_mutex_lock(&host_lock)) _exit(93);
  ++releases;
  if (pthread_mutex_unlock(&host_lock)) _exit(94);
  [super dealloc];
}
@end

NTSTATUS fixture_unix_call(unsigned int opcode, void *args) {
  if (opcode != 1) _exit(95);
  [(id)(uintptr_t)*(uint64_t *)args release];
  return 0;
}
static void destructor(void *unused) {
  (void)unused;
  if (write(cleanup[1], "D", 1) != 1) _exit(96);
  // Simulate a host TSD destructor reentering a lock held at interruption.
  // This is the real pthread_exit cleanup path, not a mocked lock response.
  pthread_mutex_lock(&host_lock);
  _exit(97);
}
static void quit(int sig) {
  (void)sig;
  pthread_exit(NULL);
}
static void *worker(void *unused) {
  (void)unused;
  if (pthread_setspecific(key, (void *)1)) _exit(98);
  if (pthread_mutex_lock(&host_lock)) _exit(99);
  if (write(ready[1], "W", 1) != 1) _exit(100);
  for (;;) pause();
}
int main(int argc, char **argv) {
  if (argc != 2) return 64;
  HostObject *object = [[HostObject alloc] init];
  if (!object) return 101;
  if (!strcmp(argv[1], "runtime")) {
    NSObject_release((uint64_t)(uintptr_t)object);
    NSObject_release(0); // Objective-C nil release remains harmless.
    if (releases != 1) return 102;
    mark('S');
    return 20;
  }
  if (strcmp(argv[1], "terminal")) return 64;
  if (pipe(ready) || pipe(cleanup) || pthread_key_create(&key, destructor)) return 103;
  struct sigaction action = {0};
  action.sa_handler = quit;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGQUIT, &action, NULL)) return 104;
  pthread_t thread;
  if (pthread_create(&thread, NULL, worker, NULL)) return 105;
  expect_byte(ready[0], 'W');
  if (pthread_kill(thread, SIGQUIT)) return 106;
  expect_byte(cleanup[0], 'D');
  mark('D');
  detaching = 1;
  NSObject_release((uint64_t)(uintptr_t)object);
  mark('S');
  // Avoid host atexit cleanup, as final Windows process termination does.
  _exit(20);
}
