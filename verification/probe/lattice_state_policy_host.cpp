#include "lattice_state_policy.h"
#include <cstdio>
#include <limits>
using namespace x3m::lattice_state;
int main(){unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u\n",unsigned(__LINE__));}}while(0)
    Arguments a{4,3784,0,9680,0,0},b{4,940,0,1267,0,0};
    CHECK(signature(a)==0);CHECK(signature(b)==1);a.base=1;CHECK(signature(a)==-1);
    a.base=0;a.start=1;CHECK(signature(a)==-1);a.start=0;a.minimum=1;CHECK(signature(a)==-1);
    a.minimum=0;a.topology=5;CHECK(signature(a)==-1);
    Object o{true,1,0x54b3,0,{position[0],position[1],position[2]},7,0x1234,99};
    CHECK(object_matches(o));o.position[0]^=1;CHECK(!object_matches(o));o.position[0]^=1;
    o.valid=0;CHECK(!object_matches(o));o.valid=1;o.scoped=false;CHECK(!object_matches(o));o.scoped=true;
    Policy p;CHECK(p.status==Status::Off);p.arm();CHECK(p.accept(1,o));CHECK(p.accept(0,o));CHECK(p.finish()==Status::Complete);
    p.arm();CHECK(p.accept(0,o));CHECK(!p.accept(0,o));CHECK(p.finish()==Status::Ambiguous);
    p.arm();CHECK(p.accept(0,o));++o.node;CHECK(!p.accept(1,o));CHECK(p.finish()==Status::Ambiguous);--o.node;
    p.arm();CHECK(p.accept(0,o));++o.session;CHECK(!p.accept(1,o));CHECK(p.finish()==Status::Ambiguous);--o.session;
    p.arm();CHECK(p.finish()==Status::NoMatch);p.arm();CHECK(p.accept(1,o));CHECK(p.finish()==Status::Partial);
    p.arm();p.refuse(Status::Reset);CHECK(!p.accept(0,o));CHECK(p.finish()==Status::Reset);
    p.arm();p.refuse(Status::Unavailable);p.refuse(Status::Capacity);CHECK(p.finish()==Status::Unavailable);
    p.arm();CHECK(p.accept_match(0,o,source_vs,source_ps,true));
    CHECK(!p.accept_match(0,o,source_vs,source_ps^1,true));CHECK(p.matches[0]==1&&p.status==Status::Armed);
    CHECK(!p.accept_match(0,o,source_vs,source_ps,false));CHECK(p.matches[0]==1&&p.status==Status::Armed);
    CHECK(p.accept_match(1,o,source_vs,source_ps,true));CHECK(p.finish()==Status::Complete);
    CHECK(fits(0,word_limit,word_limit));CHECK(!fits(word_limit,1,word_limit));
    CHECK(!fits(std::numeric_limits<unsigned>::max(),1,word_limit));
    CHECK(!fits(1,std::numeric_limits<unsigned>::max(),word_limit));
    std::printf("lattice_state_policy checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
