#include "media_presentation_gate_contract_fixture.h"
int main(){gate_fixture::run();std::printf("MEDIA GATE HOST checks=%u failures=%u\n",gate_fixture::checks,gate_fixture::failures);return gate_fixture::failures?1:0;}
