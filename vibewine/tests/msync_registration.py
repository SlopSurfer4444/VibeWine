#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check MSync registration cleanup using extracted production C bodies.

The transport/ulock seams are deterministic; this is not a real-Mach or runtime test.
The pool warning hunk is not exercised. All generated sources and binaries are temporary.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile

FILES = ('dlls/ntdll/unix/msync.c', 'server/msync.c')
DEFAULT_BASELINE = '7469fcf53c4f537dfe305e961bffb0495d7b750d'
EXPECTED_BASELINE_FAILURES = {
    'early contention unregisters all server nodes',
    'early contention balances alert/object waiter counts',
    'early contention queues registration then removal',
    'server signaled index one releases index zero',
    'repeated lost-race registrations remain bounded',
    'repeated alert count remains balanced',
}

def extract(text, name):
    match = re.search(r'(?m)^static[^;\n]*\b' + re.escape(name) + r'\s*\(', text)
    if not match:
        raise ValueError('production function missing: ' + name)
    start = text.index('{', match.start())
    depth, index = 1, start + 1
    while depth:
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
        index += 1
    return text[match.start():index]

PRELUDE = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
enum { MSYNC_ATTR_REGISTER_SEND, MSYNC_ATTR_UNREGISTER_SEND, MSYNC_ATTR_SIGNAL_SEND, MSYNC_ATTR_ACK };
static uint64_t msync_attribution_begin(unsigned int kind) { return 0; }
static void msync_attribution_end(unsigned int kind,uint64_t start,int status) {}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAXIMUM_WAIT_OBJECTS 64
#define MACH_MSG_SUCCESS 0
#define MACH_SEND_MSG 1
#define MACH_PORT_NULL 0
#define MACH_MSG_TIMEOUT_NONE 0
#define MSYNC_MUTEX 3
#define STATUS_PENDING 0x103
#define STATUS_TIMEOUT 0x102
#define STATUS_SUCCESS 0
#define UL_COMPARE_AND_WAIT_SHARED 3
#define ULF_NO_ERRNO 0x1000000
#define ERR(...) fprintf(stderr, __VA_ARGS__)
#define pthread_sigmask(...) 0
#define max(a,b) ((a) > (b) ? (a) : (b))
typedef int NTSTATUS;
typedef unsigned long long ULONGLONG;
typedef int mach_msg_return_t;
typedef struct { unsigned int msgh_remote_port,msgh_bits,msgh_id,msgh_size; } mach_msg_header_t;
typedef struct { mach_msg_header_t header; unsigned int shm_idx[65]; } mach_register_message_t;
typedef struct { mach_msg_header_t header; int entry; } mach_map_message_t;
struct event { int signaled,unused; unsigned short msync_type,refcount; int multiple_waiters; };
struct mutex { int tid,count; unsigned short msync_type,refcount; int multiple_waiters; };
struct msync_shm { int low,high; unsigned short msync_type,refcount; int multiple_waiters; };
struct tid_node { int tid; struct tid_node *next; };
struct tid_list { struct tid_node *head; };
static struct tid_list *tid_map;
static size_t tid_map_size;
static int thread_states[128], *shm_tid_map = thread_states;
static int server_port = 9, msgh_bits_send = 0;
static struct event objects[8];
static int nodes,peak,mode,send_count,qread,qwrite;
static mach_register_message_t queue[128];
static jmp_buf drained;
static struct tid_node *pool_alloc(void) { struct tid_node *p=calloc(1,sizeof(*p)); assert(p); if (++nodes>peak) peak=nodes; return p; }
static void pool_free(struct tid_node *p) { --nodes; free(p); }
static void *get_shm(unsigned int i) { assert(i<8); return &objects[i]; }
static void wake_tid(int tid) { thread_states[tid]=0; }
static void send_shm_to_client(mach_map_message_t *m) { abort(); }
static void destroy_all_internal(unsigned int i) { abort(); }
static long long update_timeout(ULONGLONG end) { return end; }
static int ulock_wait(uint32_t op,void *addr,uint64_t value,uint64_t timeout) { *(int*)addr=0; return mode==3 ? -ETIMEDOUT : 0; }
static int mach_msg2(mach_msg_header_t *m,int options,int send,int recv,int port,int timeout,int priority) {
    assert(qwrite<128);
    memset(&queue[qwrite],0,sizeof(queue[qwrite]));
    memcpy(&queue[qwrite++],m,send);
    queue[qwrite-1].header.msgh_remote_port=0; /* Mach receive header disposition */
    ++send_count;
    if (mode==1 && !(queue[qwrite-1].shm_idx[0] & (1u<<29))) {
        objects[2].signaled=1;
        /* A real setter also queues signal_all for work. Preserve it: that
         * signal removes work's nodes but cannot unregister quit/alert. */
        assert(qwrite<128);
        memset(&queue[qwrite],0,sizeof(queue[qwrite]));
        queue[qwrite].header.msgh_id=2;
        queue[qwrite++].header.msgh_size=sizeof(mach_msg_header_t);
    }
    if (mode==2 || mode==3) thread_states[m->msgh_id>>8]=1;
    return 0;
}
static int receive_mach_msg(mach_register_message_t *out) {
    if(qread==qwrite) longjmp(drained,1);
    *out=queue[qread++]; return 0;
}
static int __ulock_wake(uint32_t op, void *addr, uint64_t value) { return 0; }
typedef unsigned int mach_port_type_t;
typedef int kern_return_t;
#define KERN_INVALID_NAME 15
#define KERN_SUCCESS 0
#define MACH_PORT_TYPE_DEAD_NAME 16
static int mach_task_self(void) { return 1; }
static int mach_port_type(int task, int port, mach_port_type_t *type) { *type=1; return 0; }
static void abort_thread(int status) { abort(); }
'''

TESTS = r'''
static void drain(void) { if(!setjmp(drained)) mach_message_pump(NULL); qread=qwrite=0; }
static void reset(void) {
    for(size_t i=0;i<tid_map_size;i++) while(tid_map[i].head) remove_tid(i,tid_map[i].head->tid);
    memset(objects,0,sizeof(objects)); memset(thread_states,0,sizeof(thread_states));
    peak=send_count=qread=qwrite=mode=0;
}
static int fail;
#define CHECK(cond, name) do { if(!(cond)) { fprintf(stderr,"FAIL %s nodes=%d alert_refs=%d sends=%d\n",name,nodes,objects[3].multiple_waiters,send_count); ++fail; } }while(0)
int main(void) {
    int ids[]={1,2}; void *shm[]={&objects[1],&objects[2]};
    reset(); mode=1;
    int ret=msync_wait_multiple(ids,shm,3,&objects[3],2,NULL,7);
    objects[2].signaled=0; /* competing consumer consumes transient signal before server receives */
    drain();
    CHECK(ret==STATUS_PENDING,"early contention status");
    CHECK(nodes==0,"early contention unregisters all server nodes");
    CHECK(objects[1].multiple_waiters==0 && objects[2].multiple_waiters==0 && objects[3].multiple_waiters==0,"early contention balances alert/object waiter counts");
    CHECK(send_count==2,"early contention queues registration then removal");
    reset();
    objects[2].signaled=1;
    server_register_wait((7<<8)|2,ids,shm,0,NULL,2); drain();
    CHECK(nodes==0,"server signaled index one releases index zero");
    CHECK(thread_states[7]==0,"server wakes immediately signaled waiter");
    server_remove_wait((7<<8)|2,ids,shm,0,NULL,2); drain();
    CHECK(nodes==0,"redundant client removal is safe");
    reset(); mode=1;
    for(int i=0;i<1000;i++) {
        ret=msync_wait_multiple(ids,shm,3,&objects[3],2,NULL,7);
        objects[2].signaled=0; drain();
    }
    CHECK(nodes==0 && peak<=3,"repeated lost-race registrations remain bounded");
    CHECK(objects[3].multiple_waiters==0,"repeated alert count remains balanced");
    reset(); mode=2;
    ret=msync_wait_multiple(ids,shm,0,NULL,2,NULL,7); drain();
    CHECK(ret==STATUS_SUCCESS && nodes==0,"normal wait still removes registration");
    CHECK(objects[1].multiple_waiters==0 && objects[2].multiple_waiters==0,"normal wait refs balanced");
    reset(); mode=3;
    ULONGLONG end=1;
    ret=msync_wait_multiple(ids,shm,0,NULL,2,&end,7); drain();
    CHECK(ret==STATUS_TIMEOUT && nodes==0,"ulock timeout removes registration");
    CHECK(objects[1].multiple_waiters==0 && objects[2].multiple_waiters==0,"timeout refs balanced");
    reset(); mode=3; end=0;
    ret=msync_wait_multiple(ids,shm,0,NULL,2,&end,7); drain();
    CHECK(ret==STATUS_TIMEOUT && nodes==0,"expired deadline removes registration");
    reset();
    server_register_wait((8<<8)|2,ids,shm,0,NULL,2);
    server_register_wait((7<<8)|2,ids,shm,0,NULL,2);
    server_remove_wait((7<<8)|2,ids,shm,0,NULL,2); drain();
    CHECK(nodes==2 && tid_map[1].head->tid==8 && tid_map[2].head->tid==8,"removal preserves unrelated waiter");
    server_remove_wait((8<<8)|2,ids,shm,0,NULL,2); drain();
    CHECK(nodes==0 && objects[1].multiple_waiters==0 && objects[2].multiple_waiters==0,"final unrelated removal balances all refs");
    reset();
    printf("checks_failed=%d\n",fail);
    return fail?1:0;
}
'''


def build_and_run(label, sources, workdir, compiler):
    client, server = sources
    parts = [client[:client.index('*/') + 2], PRELUDE]
    if 'static ULONGLONG msync_timeout_ns' in client:
        parts.append(extract(client, 'msync_timeout_ns'))
    if 'MSYNC_ACK_PENDING' in client:
        parts.append(client[client.index('#define MSYNC_ACK_PENDING'):client.index('static NTSTATUS msync_wait_multiple')])
    for name in ('server_register_wait', 'server_remove_wait', 'check_shm_contention', 'msync_wait_multiple'):
        parts.append(extract(client, name))
    if 'static inline void acknowledge_wait' in server:
        parts.extend(extract(server, name) for name in ('wake_tid_word', 'acknowledge_wait'))
    for name in ('grow_tid_map', 'get_tid_list', 'add_tid', 'remove_tid', 'unregister_wait',
                 'signal_all_internal', 'decode_msgh_id', 'check_bit', 'mach_message_pump'):
        parts.append(extract(server, name))
    parts.append(TESTS)
    generated = '\n\n'.join(parts)
    source = workdir / (label + '.c')
    source.write_text(generated)
    binary = workdir / (label + '.test')
    subprocess.run([compiler, '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function',
                    '-Wno-unused-value', '-fsanitize=address,undefined', str(source), '-o', str(binary)],
                   check=True, timeout=60, capture_output=True, text=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
    failures = [line[len('FAIL '):].split(' nodes=')[0]
                for line in result.stderr.splitlines() if line.startswith('FAIL ')]
    return result, failures, hashlib.sha256(generated.encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', required=True, type=pathlib.Path,
                        help='Wine component root containing the current candidate sources and Git history')
    parser.add_argument('--baseline-ref', default=DEFAULT_BASELINE,
                        help='known baseline Git revision; read with git show without checkout')
    parser.add_argument('--cc', default='clang', help='Clang executable with ASan and UBSan support')
    args = parser.parse_args()
    root = args.source_dir.resolve()
    baseline, candidate = [], []
    for rel in FILES:
        result = subprocess.run(['git', '-C', str(root), 'show', args.baseline_ref + ':' + rel],
                                check=True, capture_output=True, timeout=20)
        baseline.append(result.stdout.decode())
        candidate.append((root / rel).read_text())
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-registration-') as tmp:
        workdir = pathlib.Path(tmp)
        before, failures, baseline_fixture_hash = build_and_run('baseline', baseline, workdir, args.cc)
        if (before.returncode != 1 or before.stdout.strip() != 'checks_failed=6'
                or len(failures) != 6 or set(failures) != EXPECTED_BASELINE_FAILURES
                or len(before.stderr.splitlines()) != 6):
            raise RuntimeError('baseline did not fail the exact six expected assertions: '
                               + repr((before.returncode, before.stdout, before.stderr)))
        after, failures, candidate_fixture_hash = build_and_run('candidate', candidate, workdir, args.cc)
        if after.returncode != 0 or after.stdout.strip() != 'checks_failed=0' or after.stderr:
            raise RuntimeError('candidate regression failure: '
                               + repr((after.returncode, after.stdout, after.stderr)))
    print(json.dumps({
        'result': 'PASS', 'baseline_ref': args.baseline_ref,
        'baseline_expected_assertion_failures': 6, 'candidate_assertions_passed': 16,
        'baseline_source_sha256': {rel: hashlib.sha256(text.encode()).hexdigest()
                                   for rel, text in zip(FILES, baseline)},
        'candidate_source_sha256': {rel: hashlib.sha256(text.encode()).hexdigest()
                                    for rel, text in zip(FILES, candidate)},
        'generated_fixture_sha256': {'baseline': baseline_fixture_hash, 'candidate': candidate_fixture_hash},
        'sanitizers': ['address', 'undefined'],
        'limits': 'deterministic Mach queue including work-signal handling; ulock result seam; '
                  'pool warning not exercised; no kernel concurrency or Wine runtime acceptance'
    }, indent=2))


if __name__ == '__main__':
    main()
