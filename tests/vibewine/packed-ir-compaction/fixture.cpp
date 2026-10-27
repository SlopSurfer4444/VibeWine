// SPDX-License-Identifier: LGPL-2.1-or-later
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <limits>
static std::unordered_map<void*,size_t> live;
static size_t reallocations=0,freed=0;
static bool fail=false;
static void check(bool x,const char*m){if(!x){fprintf(stderr,"CONTRACT: %s\n",m);exit(1);}}
static void*test_malloc(size_t n){void*p=std::malloc(n);check(p!=nullptr,"malloc");live[p]=n;return p;}
static void test_free(void*p){if(p){check(live.erase(p)==1,"free owned backing");freed++;}std::free(p);}
static void*test_realloc(void*p,size_t n){reallocations++;check(p&&live.contains(p)&&n>0&&n<=live[p],"shrink owned nonempty backing");if(fail)return nullptr;void*q=test_malloc(n);std::memcpy(q,p,n);test_free(p);return q;}
#include "packed_variant_list.hpp"
struct A{uint32_t x;};struct B{uint64_t x,y;};
using List=dxmt::PackedVariantList<A,B>;
static uint64_t visit(const List&l){uint64_t sum=0;l.for_each([&](const auto&v){check(reinterpret_cast<uintptr_t>(&v)%alignof(decltype(v))==0,"payload alignment");if constexpr(std::is_same_v<std::remove_cvref_t<decltype(v)>,B>)check(v.y==v.x*2,"secondary payload preserved");sum+=v.x;});return sum;}
static void mixed(const List&l,unsigned count){unsigned i=0;l.for_each([&](const auto&v){i++;check(v.x==i,"ordered values preserved");check((std::is_same_v<std::remove_cvref_t<decltype(v)>,B>)==(i>70||i%2==0),"ordered types preserved");});check(i==count,"visitor count preserved");unsigned visited=0;check(l.any([&](const auto&v){visited++;return v.x==7;})&&visited==7,"any early stop preserved");check(!l.any([](const auto&v){return v.x==999;}),"any miss preserved");}
int main(){
 {List empty;empty.compact();check(reallocations==0&&live.empty(),"empty no-op");}
 {List l;for(unsigned i=1;i<=70;i++)if(i%2)l.push_back(A{i});else l.push_back(B{i,i*2});auto a=l.test_state();auto sum=visit(l);mixed(l,70);l.compact();auto b=l.test_state();check(b.capacity==b.used&&b.capacity<a.capacity,"retained capacity reduced");check(sum==visit(l),"mixed payload preserved");mixed(l,70);auto count=reallocations;l.compact();check(reallocations==count,"repeated compact no-op");for(unsigned i=71;i<250;i++)l.push_back(B{i,i*2});check(visit(l)==sum+(71+249)*179/2,"append after compact");mixed(l,249);}
 {List l;l.push_back(A{77});auto a=l.test_state();fail=true;l.compact();fail=false;auto b=l.test_state();check(a.data==b.data&&a.capacity==b.capacity&&a.used==b.used&&visit(l)==77,"failed shrink preserves original state");l.compact();check(l.test_state().capacity==l.test_state().used&&visit(l)==77,"retry after failed shrink");}
 {List l;l.push_back(A{9});auto a=l.test_state();auto f=freed;auto r=reallocations;l.test_set_size(0);l.compact();check(!l.test_state().data&&!l.test_state().capacity&&!l.test_state().used&&freed==f+1&&reallocations==r,"zero occupied frees without realloc zero");l.compact();l.push_back(A{12});check(visit(l)==12,"append after empty compact");(void)a;}
 {List l;l.push_back(A{5});auto s=l.test_state();auto r=reallocations;l.test_set_capacity(s.used+255);l.compact();check(reallocations==r,"absolute threshold no-op");l.test_set_capacity(4096);l.test_set_size(4096-256);l.compact();check(reallocations==r,"relative threshold no-op");l.test_set_size(768);l.test_set_capacity(1024);fail=true;l.compact();fail=false;check(reallocations==r+1&&l.test_state().capacity==1024,"exact absolute and relative thresholds attempt shrink");l.test_set_size(s.used);l.test_set_capacity(std::numeric_limits<size_t>::max());fail=true;l.compact();fail=false;check(l.test_state().capacity==std::numeric_limits<size_t>::max()&&visit(l)==5,"threshold arithmetic no overflow");l.test_set_capacity(s.capacity);}
 check(live.empty(),"all owned backing released");printf("PASS packed IR compaction, failure, zero, thresholds, append and payload controls\n");
}
