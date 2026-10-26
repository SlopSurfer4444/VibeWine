// Existing ring fixture defines exact FIFO snapshots, native nil/bad_alloc and
// global-new seams. Its strict-equality test is always run unchanged elsewhere.
uint64_t boundary(uint64_t current,uint64_t completed) {
  return dxmt::ResourceInitializer::uploadReuseBoundary(current,completed);
}
int main() {
  constexpr auto max=std::numeric_limits<uint64_t>::max();
  assert(boundary(2,1)==(INCLUSIVE?2u:1u));
  assert(boundary(max-1,max-2)==(INCLUSIVE?max-1:max-2));
  if(INCLUSIVE) {
    assert(boundary(0,0)==0 && boundary(max,max-1)==0);
    assert(boundary(2,2)==0 && boundary(2,max)==0);
    assert(boundary(1,0)==1);
  }
  // Equality is safe only through this initializer adapter. Direct generic
  // checked calls continue to reject it (old fixture stays byte-for-byte).
  {Counts counts; Ring ring{Allocator{{&counts},0}};
    auto *a=&ring.allocate(5,0,standard,1).first;
    auto before=Snapshot{ring}; auto out=sentinels(); counts.fail_call=counts.calls+1;
    bool ok=ring.allocateChecked(6,boundary(6,5),std::array<size_t,1>{standard},256,std::span(out).first(1));
    assert(ok==INCLUSIVE);
    if(ok) assert(out[0].block==a && out[0].offset==0 && counts.calls==1);
    else {assert(Snapshot{ring}==before); unchanged(out);}
  }
  // First-plane reuse and second-plane creation form one transaction. Fail at
  // each actual allocation edge; a planned reuse must never mutate on failure.
  for(int fault=0;fault<3;++fault) {Counts counts; Ring ring{Allocator{{&counts},0}};
    auto *a=&ring.allocate(5,0,standard,1).first;
    auto before=Snapshot{ring}; auto out=sentinels();
    if(fault==0) counts.fail_call=counts.calls+1;
    if(fault==1) counts.throw_call=counts.calls+1;
    if(fault==2) fail_new=0;
    assert(!ring.allocateChecked(6,boundary(6,5),std::array<size_t,2>{standard,standard+1},256,out));
    assert(Snapshot{ring}==before && counts.live==1 && a->buffer); unchanged(out); fail_new=-1;
  }
  {Counts counts; Ring ring{Allocator{{&counts},0}};
    auto *a=&ring.allocate(5,0,standard,1).first;
    auto *b=&ring.allocate(5,0,standard,1).first;
    auto out=sentinels(); counts.fail_call=counts.calls+1;
    bool ok=ring.allocateChecked(6,boundary(6,5),std::array<size_t,2>{standard,8*MB},256,out);
    assert(ok==INCLUSIVE);
    if(ok) {
      assert(out[0].block==a && out[1].block==b && a!=b && counts.calls==2);
      assert(out[0].offset==0 && out[1].offset==0);
      // Exact original two backing objects; current generation remains pending.
      auto before=Snapshot{ring}; auto future=sentinels();
      assert(!ring.allocateChecked(6,boundary(6,5),std::array<size_t,1>{standard},256,std::span(future).first(1)));
      assert(Snapshot{ring}==before && a->buffer && b->buffer); unchanged(future);
    }
  }
  // Stale completion and impossible future completion cannot reset pending A.
  for(uint64_t completed : {uint64_t(4),uint64_t(6),max}) {
    Counts counts; Ring ring{Allocator{{&counts},0}};
    ring.allocate(5,0,standard,1); auto before=Snapshot{ring}; auto out=sentinels();
    counts.fail_call=counts.calls+1;
    if(INCLUSIVE || completed==4) {
      assert(!ring.allocateChecked(6,boundary(6,completed),std::array<size_t,1>{standard},256,std::span(out).first(1)));
      assert(Snapshot{ring}==before); unchanged(out);
    }
  }
  assert(host_live==0 && fail_new==-1 && fail_malloc==-1);
  std::cout << "PASS initializer_frontier inclusive=" << INCLUSIVE
            << " equality/pair_identity/rollback_nil_bad_alloc_listnode/stale_pending/exhaustion\n";
}
