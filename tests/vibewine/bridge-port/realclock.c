#include "../../../src/winemetal/unix/wave7_acquire_diag.inc"
#include <assert.h>

int main(void)
{
    assert(w7ad_file);
    struct W7ADSample sample = w7ad_begin();
    usleep((useconds_t)(W7AD_THRESHOLD_NS / 1000 + 20000));
    w7ad_end(sample, 0xabc, 0xdef);
    struct W7ADRecord *r = &w7ad_file->records[0];
    assert(atomic_load_explicit(&r->committed, memory_order_acquire) == 1);
    assert((r->flags & 14) == 14 && !(r->flags & W7AD_NIL));
    assert(r->wall_end - r->wall_start >= W7AD_THRESHOLD_NS);
    assert(r->cpu_end >= r->cpu_start && r->cpu_end - r->cpu_start < r->wall_end - r->wall_start);
    assert(w7ad_file->header.started == 1 && w7ad_file->header.finished == 1);
    w7ad_skip_occluded();
    assert(w7ad_file->header.skipped_occluded == 1 && w7ad_file->header.started == 1);
    return 0;
}
