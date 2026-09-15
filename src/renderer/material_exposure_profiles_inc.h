// Derived diffuse-only joins from the immutable selected originals. The join
// is after native angular clamps and diffuse strength, before specular mixing.
// See docs/reverse-engineering/remaining-hull-materials.md. No game words.
struct ExposureSeed { std::uint64_t hash; unsigned at, lanes; bool product; };
constexpr ExposureSeed exposure_seeds[] = {
    {0x8759c7838bbc86c2ull,1192,7,true},
    {0x63f96eba9eea7880ull,1224,7,true},
    {0x593e5dea9b3457d5ull,212,8,false},
    {0x7a0bb00a8070496aull,1146,8,false},
    {0x8d5b2ba0fb4d13bfull,1114,8,false},
    {0xdab93928f26906f7ull,244,8,false},
    {0x3b94320087e81945ull,1201,7,true},
    {0xe3b7acc16da9932dull,1233,7,true},
    {0x7a14d4dcb28f27e5ull,1123,8,false},
    {0x8ab6188a40ca15eaull,1155,8,false},
    {0x8df6143d0e77d92eull,212,8,false},
    {0xe16a9806ee3544c3ull,244,8,false},
    {0xca6bfa4a6cca7e2aull,1251,7,true},
    {0x5e0a10fe752b6140ull,1277,7,true},
    {0x63379470db8d2a86ull,1173,8,false},
    {0x68915563dd0aac9aull,271,8,false},
    {0xd086fde54698070cull,1199,8,false},
    {0xf17fffd88d134b04ull,297,8,false},
    {0x02606104fa59fb29ull,1112,8,false},
    {0x0c1f3f0f440e4a0cull,1285,7,true},
    {0x1d638938d93421b3ull,1138,8,false},
    {0x462342e3e5781384ull,1182,7,true},
    {0x4f052209611387f0ull,1244,8,false},
    {0x55826dc176afe464ull,283,8,false},
    {0x64bac8bb307eb896ull,1311,7,true},
    {0x789449ffd931d23eull,1218,8,false},
    {0x7c83ed50c9894e44ull,1235,7,true},
    {0x827d8d2d617bedceull,1208,7,true},
    {0x99153c144030c396ull,1274,7,true},
    {0xabf3c0fad53456d8ull,310,8,false},
    {0xb0f9313b77cc78eeull,1207,8,false},
    {0xbd4d51c08486c6e0ull,204,8,false},
    {0xc1452981fd0bff64ull,1300,7,true},
    {0xcf449bcb069aec4full,342,8,false},
    {0xd514bf852d8a9c58ull,1233,8,false},
    {0xdb644b73b68c0547ull,1168,8,false},
    {0xde2dd381fa64193dull,236,8,false},
    {0xdff6a3d360603fa2ull,299,8,false},
    {0xe70adc744a38ca59ull,1261,7,true},
    {0xf1d14a7dbf7c6173ull,331,8,false},
    {0xf6a501717c3e5ca8ull,257,8,false},
    {0xff32b602a271c327ull,1194,8,false},
    {0x1ed1bf0fdec00e1aull,1199,8,false},
    {0x1f26d41bcb7dac1eull,1251,7,true},
    {0x2b04461d0dae038bull,271,8,false},
    {0x78963cdc7c710e04ull,1173,8,false},
    {0xacc83ed2509d84a1ull,297,8,false},
    {0xbdcdb3ab996ae4e0ull,1277,7,true},
    {0x22cc5b05a55ef61eull,289,8,false},
    {0x3006f8030a467739ull,1235,7,true},
    {0x769c3814fc0efba8ull,263,8,false},
    {0xd6e8bdde0e4c515full,1261,7,true},
    {0xe5ea78b8b0b0fe07ull,1165,8,false},
    {0xf42202faf57a3c89ull,1191,8,false},
    {0x3755809bd40afc13ull,1137,8,false},
    {0x61418505e5d8f998ull,209,8,false},
    {0x91b6c09eb47f8555ull,1214,7,false},
    {0xb5f1d4145171026bull,235,8,false},
    {0xcc09f17db377fd9eull,1111,8,false},
    {0xef2bf556f207b8bdull,1188,7,false},
    {0x042c9ae16f41feffull,1170,8,false},
    {0x3602b05ce11ca6ffull,1247,7,false},
    {0x5c823b8507fa1442ull,262,8,false},
    {0x68f0dd6791fd7d3dull,1196,8,false},
    {0x8e58ac79b59b02b1ull,1273,7,false},
    {0xa6e1328c0bb3f401ull,294,8,false},
    {0x517540ae6d5e5410ull,359,7,false},
    {0x7a0c3388065bb08dull,284,8,false},
    {0xd44db87778a43b61ull,410,7,false},
    {0x550c2a4d4d3ed70full,335,8,false},
    {0x39eb3c2258a516e1ull,320,7,true},
    {0x57acf59d19c73791ull,352,7,true},
    {0xf917d48ee826da1full,234,4,false},
    {0x77a5b2d62fb3be48ull,266,4,false},
    {0xa910daef935891ceull,375,7,true},
    {0x62c180abe017e239ull,401,7,true},
    {0xed44232013f67072ull,281,8,false},
    {0xf286856c3f400377ull,307,8,false},
    {0x9d27e7ba242f3831ull,1146,8,false},
    {0xe1acf8a03850acafull,1172,8,false},
    {0xf646f03be5a8708dull,1146,8,false},
    {0xebf41e1ace7af45bull,1172,8,false},
    {0xc997a37560e266dfull,244,4,false},
    {0x675f9077d8fd21c4ull,270,4,false},
    {0x18d372968af4a480ull,1195,8,false},
    {0x188c5ab9dbb98393ull,1221,8,false},
    {0x7e5e41276b3d7514ull,1195,8,false},
    {0x43c9405568d2226full,1221,8,false},
    {0x5e056627e9ff3a8dull,293,8,false},
    {0xfce465befff2f623ull,319,8,false},
    {0xa66fb1981ba755b2ull,271,7,true},
    {0xebc9b2b3f1564e9aull,303,7,true},
    {0xf31c9e2701c8eee4ull,187,8,false},
    {0x9d49f288800f898dull,219,8,false},
    {0xfffdabd910793abaull,1519,7,true},
    {0xe6794b6ec37ff71aull,1545,7,true},
    {0x5f82ecacd39529cdull,1636,7,true},
    {0xf1b0e820c7b488c3ull,1662,7,true},
    {0x6733b119142c8d42ull,1625,7,true},
    {0x496049cec2066ed3ull,1651,7,true},
    {0xd51cf763125cb85aull,1591,7,true},
    {0x31445adb0a62d134ull,1617,7,true},
    {0xfd58e6b7e8cf969cull,1403,7,true},
    {0xdd87737d697c6764ull,1429,7,true},
    {0xd22f2ce2c740e6a7ull,1522,7,true},
    {0x1de3d2dde345a7e3ull,1548,7,true},
    {0x75fb9c6b05e28ea2ull,1511,7,true},
    {0xedaef099780fcafeull,1537,7,true},
};
const ExposureSeed* exposure_seed(std::uint64_t hash) noexcept {
    for (const auto& seed:exposure_seeds) if (seed.hash==hash) return &seed;
    return nullptr;
}
// Check complete register operands, excluding comments and definition literals.
// r16 is transient across each insertion; no new varying or persistent GPU state.
bool exposure_reservations(const Word* code, const Structure& s, bool vertex) noexcept {
    for (const auto& i:s.instructions) {
        if (i.opcode==dcl) continue;
        const unsigned last=i.opcode==def?1:i.count;
        for (unsigned n=1;n<=last;++n) {
            const Word value=code[i.at+n];
            if ((kind(value)==constant && index(value)==(vertex?250u:222u)) ||
                (kind(value)==temp && index(value)==16)) return false;
        }
    }
    return true;
}
// The motion transformer already validated this complete stream. Scan only
// the new reservations without allocating a second instruction inventory.
bool exposure_motion_reservations(const Words& code, bool vertex) noexcept {
    for (std::size_t at=1; at<code.size();) {
        const auto opcode=code[at]&0xffff, count=length(code[at]);
        if (code[at]==end_token) return at==code.size()-1;
        if (count>code.size()-at-1) return false;
        if (opcode!=dcl && opcode!=0xfffe) {
            const unsigned last=opcode==def?1:count;
            for (unsigned n=1;n<=last;++n) {
                const auto value=code[at+n];
                if ((kind(value)==constant && index(value)==(vertex?250u:222u)) ||
                    (kind(value)==temp && index(value)==16)) return false;
            }
        }
        at+=count+1;
    }
    return false;
}
bool exposure_seed_valid(const Word* code, const Structure& s, const ExposureSeed* seed) noexcept {
    if (!seed || seed->at>=s.boundary.size() || !s.boundary[seed->at]) return false;
    const auto at=seed->at;
    return code[at]==((4u<<24)|mad) && kind(code[at+1])==temp &&
        mask(code[at+1])==seed->lanes && !(code[at+1]&sat) &&
        !(code[at+2]&relative) && !(code[at+3]&relative) && !(code[at+4]&relative);
}
// Run on the already material-rewritten join so decoded palette/light operands
// retain their meanings. Only the diffuse term changes; original specular
// operands remain byte-exact. Each instruction reads at most one distinct c#.
void exposure_join(Words& code, std::size_t at, const ExposureSeed& seed) {
    const Word destination=code[at+1]&~pp;
    const Word a=code[at+2], b=code[at+3], c=code[at+4];
    code.resize(at);
    if (seed.product) {
        emit(code,mul,{dst(temp,16,seed.lanes),a,b});
        emit(code,mul,{dst(temp,16,seed.lanes),src(temp,16),lane(constant,222,0)});
        emit(code,add,{destination,src(temp,16),c});
    } else {
        emit(code,mul,{dst(temp,16,seed.lanes),c,lane(constant,222,0)});
        emit(code,mad,{destination,a,b,src(temp,16)});
    }
}
void exposure_invalid_sun(Words& code) {
    // New Q-domain extraction is deliberately unqualified. Leave RT2.r intact.
    emit(code,mov,{dst(temp,16,1)|sat,lane(constant,212,0)});
    emit(code,mov,{dst(color_output,2,2),src(temp,16,0,1)});
}
