#include "fog_field_assets.h"
#include <cstdio>
using namespace x3m::renderer::fog_field;
int main(int, char** argv) {
    static FamilyTable t; load_family_file(argv[1], t);
    std::printf("status=%u families=%u disabled=%u\n", unsigned(t.status), t.families, t.rows_disabled);
    const auto* f = t.find("zza"); const auto* r = f ? t.row(Profile(f->profile)) : nullptr;
    ProfileInfo info{}; const bool ok = f && family_profile_info(t, Profile(f->profile), info);
    std::printf("find=row%ld row(profile)=row%ld row_disabled=%s family_profile_info=%d\n", f ? f - t.rows : -1L, r ? r - t.rows : -1L,
                r && r->disabled.load() ? r->disabled.load() : "-", int(ok));
    return 0;
}
