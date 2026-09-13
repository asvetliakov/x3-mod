#include "linear_material.h"
#include "material_motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace x3m::renderer {
namespace {
using Word = std::uint32_t;
using Words = std::vector<Word>;
constexpr Word end_token = 0xffffu, relative = 0x2000u, pp = 0x200000u, sat = 0x100000u;
constexpr unsigned temp = 0, input = 1, constant = 2, output_reg = 6, color_output = 8;
constexpr unsigned mov = 1, add = 2, mad = 4, mul = 5, min_op = 10, max_op = 11;
constexpr unsigned slt = 12, dcl = 31, pow_op = 32, abs_op = 35, texld = 66, def = 81, cmp = 88;
constexpr unsigned xyz = 7, xyzw = 15, identity = 0xe4;

// Derived original-program contracts, including comments and END. The complete
// original-site proof is docs/reverse-engineering/linear-material-profiles.json.
// Only identities/offsets/resource facts are recorded here, never game bytes.
// Palette operands name only proved RGB uses. Original mixed scalar DEF
// lanes remain unmodified; these color literals are derived data, not bytecode.
struct PaletteUse { unsigned operand, source_constant, swizzle; };
struct PaletteProgram {
    std::uint64_t hash;
    std::array<PaletteUse,6> sources; // Px,Py,Pz,Pu,Pf,Pc; zero means absent.
    std::array<unsigned,2> scalar_operand; // J/u11 original destination or source.
};
constexpr PaletteProgram palette_programs[] = {
    {0x29d7c575396ed280ull,{{{},{},{},{},{},{}}},{0,0}},
    {0xa420a010b0271479ull,{{{573,43,249},{569,44,228},{582,45,228},{591,46,228},{},{}}},{0,0}},
    {0xea3d15b287892410ull,{{{528,21,228},{524,22,228},{537,23,228},{546,25,228},{},{}}},{0,0}},
    {0x57392213f62fef19ull,{{{},{},{},{},{},{}}},{597,594}},
    {0x5c17a381b149b3b9ull,{{{579,43,249},{571,44,228},{584,45,228},{594,46,228},{},{}}},{628,0}},
    {0xa804f173f693944aull,{{{531,21,228},{523,22,228},{536,23,228},{546,25,228},{},{}}},{580,0}},
    {0x37e6956afd8b8d76ull,{{{},{},{},{},{},{}}},{0,0}},
    {0x2e0254dd999841c2ull,{{{},{},{},{},{},{}}},{0,0}},
    {0xa7cddf2c98d61117ull,{{{},{},{},{},{},{}}},{0,0}},
    {0x33388c8897d428a5ull,{{{},{},{},{},{},{}}},{583,0}},
    {0xb4059ab6af8fc529ull,{{{},{},{},{},{},{}}},{583,0}},
    {0x2a560f246c90fa64ull,{{{},{},{},{},{},{}}},{532,0}},
    {0x39eb3c2258a516e1ull,{{{343,6,228},{335,7,228},{352,8,228},{361,10,228},{370,11,228},{400,9,228}}},{0,0}},
    {0x57acf59d19c73791ull,{{{375,7,228},{367,8,228},{384,9,228},{393,11,228},{402,12,228},{432,10,228}}},{0,0}},
    {0xf917d48ee826da1full,{{{},{},{},{},{258,5,228},{288,4,228}}},{0,0}},
    {0x77a5b2d62fb3be48ull,{{{},{},{},{},{290,6,228},{320,5,228}}},{0,0}},
    {0xa910daef935891ceull,{{{419,7,228},{411,8,228},{428,9,228},{437,11,228},{442,12,228},{467,10,228}}},{451,436}},
    {0x62c180abe017e239ull,{{{445,7,228},{437,8,228},{454,9,228},{463,11,228},{468,12,228},{493,10,228}}},{477,462}},
    {0xed44232013f67072ull,{{{},{},{},{},{330,6,228},{355,5,228}}},{339,0}},
    {0xf286856c3f400377ull,{{{},{},{},{},{356,6,228},{381,5,228}}},{365,0}},
    {0x9d27e7ba242f3831ull,{{{1165,6,228},{1157,7,228},{1174,8,228},{},{1188,10,228},{}}},{0,0}},
    {0xe1acf8a03850acafull,{{{1191,6,228},{1183,7,228},{1200,8,228},{},{1214,10,228},{}}},{0,0}},
    {0xf646f03be5a8708dull,{{{1165,6,228},{1157,7,228},{1174,8,228},{},{1188,10,228},{}}},{0,0}},
    {0xebf41e1ace7af45bull,{{{1191,6,228},{1183,7,228},{1200,8,228},{},{1214,10,228},{}}},{0,0}},
    {0xc997a37560e266dfull,{{{263,4,228},{255,5,228},{272,6,228},{},{282,8,228},{}}},{0,0}},
    {0x675f9077d8fd21c4ull,{{{289,4,228},{281,5,228},{298,6,228},{},{308,8,228},{}}},{0,0}},
    {0x18d372968af4a480ull,{{{1236,6,228},{1228,7,249},{1245,8,228},{},{1259,10,228},{}}},{1280,0}},
    {0x188c5ab9dbb98393ull,{{{1262,6,228},{1254,7,249},{1271,8,228},{},{1285,10,228},{}}},{1306,0}},
    {0x7e5e41276b3d7514ull,{{{1236,6,228},{1228,7,249},{1245,8,228},{},{1259,10,228},{}}},{1280,0}},
    {0x43c9405568d2226full,{{{1262,6,228},{1254,7,249},{1271,8,228},{},{1285,10,228},{}}},{1306,0}},
    {0x5e056627e9ff3a8dull,{{{334,4,228},{326,5,228},{343,6,228},{},{348,8,228},{}}},{361,0}},
    {0xfce465befff2f623ull,{{{360,3,228},{352,4,249},{369,5,228},{},{374,8,228},{}}},{387,0}},
};
constexpr Word palette_color_bits[2][6][3] = {
    {{0x3e189899u,0x3edededfu,0x3eeaeaebu},{0x3f3ebebfu,0x3ecececfu,0x3dc8c8c9u},{0x3f088889u,0x3f0d8d8eu,0x3eeaeaebu},{0x3ea2a2a3u,0x3f7dfdfeu,0x3f70f0f1u},{0x3f179798u,0x3f3bbbbcu,0x3e949495u},{0x3ee0e0e1u,0x3f24a4a5u,0x3f37b7b8u}},
    {{0x3f09898au,0x3f179798u,0x3f31b1b2u},{0x3e929293u,0x3ec2c2c3u,0x3ecececfu},{0x3edcdcddu,0x3e6cecedu,0x3dc8c8c9u},{0x00000000u,0x00000000u,0x00000000u},{0x3ed0d0d1u,0x3f008081u,0x3f24a4a5u},{0x00000000u,0x00000000u,0x00000000u}},
};
constexpr Word palette_linear_bits[2][6][3] = {
    {{0x3c78a190u,0x3e244b4eu,0x3e3877beu},{0x3f06005fu,0x3e0b5d6bu,0x3bc5f02bu},{0x3e806e22u,0x3e8b0c33u,0x3e3877beu},{0x3da449ffu,0x3f7b9a80u,0x3f600904u},{0x3ea1aba8u,0x3f016406u,0x3d86a9f9u},{0x3e27916bu,0x3ec1e1dbu,0x3ef6c272u}},
    {{0x3e828455u,0x3ea1aba8u,0x3ee54f54u},{0x3d82b15fu,0x3df440b1u,0x3e0b5d6bu},{0x3e210e33u,0x3d239fa4u,0x3bc5f02bu},{0x00000000u,0x00000000u,0x00000000u},{0x3e0e5be7u,0x3e60c9c8u,0x3ec1e1dbu},{0x00000000u,0x00000000u,0x00000000u}},
};
const PaletteProgram* palette_program(std::uint64_t hash) noexcept {
    for (const auto& p:palette_programs) if (p.hash==hash) return &p;
    return nullptr;
}

struct Pixel {
    std::uint64_t hash;
    unsigned words;
    // Physical samplers: conventional hull DEFAULT/BUMP use s0-3/s0-4;
    // Asteroid DEFAULT/BUMP use s0-2/s0-3.
    std::array<unsigned, 5> texture;
    unsigned affine_end, clamp, final_rgb, clamp_temporary;
    unsigned light0, light1; // light1 == 0 means the one-directional contract.
    std::array<unsigned, 4> color_source; // ORIGINAL source operand DWORDs.
    std::array<unsigned, 24> rgb; // Full-precision radiance destinations only.
    bool bump = false;
    // Two standard DEFAULT base pairs retain the temporal registry's TEX7
    // depth semantic. Zero uses ordinary class A/B TEX5/6; RGB uses COLOR1.
    unsigned depth_texcoord_override = 0;
    // Asteroid base/toggle DEFAULT/BUMP use four proved layouts and a
    // base/detail texture product instead of the hull cube/lightmap tail.
    unsigned asteroid_layout = 0;
    unsigned palette_style = 0; // Boron base=1, Boron single=2, Paranid=3.
    unsigned glass_fresnel = 0; // Original COLOR1.x source operand; relocated to COLOR0.x.
};
constexpr Pixel pixels[] = {
    {0x8759c7838bbc86c2ull,1260,{1197,1175,1242,1229},1217,1206,1251,1,5,7,
     {1173,1186,1161,1169},{1158,1166,1170,1183,1188,1192,1206,1221,1225,1233,1237,1251}},
    {0x63f96eba9eea7880ull,1292,{1229,1207,1274,1261},1249,1238,1283,1,5,7,
     {1205,1218,1193,1201},{1190,1198,1202,1215,1220,1224,1238,1253,1257,1265,1269,1283}},
    {0x593e5dea9b3457d5ull,264,{225,204,246,233},0,217,255,1,2,0,
     {223,0,0,0},{217,220,229,237,241,255,0,0,0,0,0,0}},
    {0x7a0bb00a8070496aull,1215,{1151,1138,1197,1184},1171,1160,1206,1,5,0,
     {1178,0,0,0},{1160,1175,1180,1188,1192,1206,0,0,0,0,0,0}},
    {0x8d5b2ba0fb4d13bfull,1183,{1119,1106,1165,1152},1139,1128,1174,1,5,0,
     {1146,0,0,0},{1128,1143,1148,1156,1160,1174,0,0,0,0,0,0}},
    {0xdab93928f26906f7ull,296,{257,236,278,265},0,249,287,1,2,0,
     {255,0,0,0},{249,252,261,269,273,287,0,0,0,0,0,0}},
    {0x3b94320087e81945ull,1264,{1192,1175,1246,1233},1214,1218,1255,2,5,7,
     {1173,1186,1161,1169},{1158,1166,1170,1183,1188,1201,1218,1221,1225,1229,1237,1241,1255}},
    {0xe3b7acc16da9932dull,1296,{1224,1207,1278,1265},1246,1250,1287,2,5,7,
     {1205,1218,1193,1201},{1190,1198,1202,1215,1220,1233,1250,1253,1257,1261,1269,1273,1287}},
    {0x7a14d4dcb28f27e5ull,1187,{1114,1106,1169,1156},1136,1140,1178,1,5,0,
     {1150,0,0,0},{1140,1143,1147,1152,1160,1164,1178}},
    {0x8ab6188a40ca15eaull,1219,{1146,1138,1201,1188},1168,1172,1210,1,5,0,
     {1182,0,0,0},{1172,1175,1179,1184,1192,1196,1210}},
    {0x8df6143d0e77d92eull,268,{220,204,250,237},0,217,259,2,2,0,
     {231,0,0,0},{217,224,228,233,241,245,259}},
    {0xe16a9806ee3544c3ull,300,{252,236,282,269},0,249,291,2,2,0,
     {263,0,0,0},{249,256,260,265,273,277,291}},
    {0xca6bfa4a6cca7e2aull,1328,{1272,1095,1235,1310,1268},1289,1260,1319,6,5,7,
     {1228,1233,1188,1220},{1185,1217,1225,1230,1243,1251,1260,1293,1297,1301,1305,1319},true},
    {0x5e0a10fe752b6140ull,1354,{1298,1098,1261,1336,1294},1315,1286,1345,6,5,7,
     {1254,1259,1214,1246},{1211,1243,1251,1256,1269,1277,1286,1319,1323,1327,1331,1345},true},
    {0x63379470db8d2a86ull,1251,{1194,1077,1161,1233,1190},1211,1182,1242,5,5,0,
     {1222},{1182,1215,1219,1224,1228,1242},true},
    {0x68915563dd0aac9aull,332,{292,175,259,314,288},0,280,323,4,2,0,
     {303},{280,296,300,305,309,323},true},
    {0xd086fde54698070cull,1277,{1220,1080,1187,1259,1216},1237,1208,1268,5,5,0,
     {1248},{1208,1241,1245,1250,1254,1268},true},
    {0xf17fffd88d134b04ull,358,{318,178,285,340,314},0,306,349,4,2,0,
     {329},{306,322,326,331,335,349},true},
    {0x02606104fa59fb29ull,1181,{1117,1104,1163,1150,0},1137,1126,1172,1,5,0,
     {1144},{1126,1141,1146,1154,1158,1172}},
    {0x0c1f3f0f440e4a0cull,1366,{1306,1145,1269,1348,1302},1323,1294,1357,6,5,7,
     {1262,1267,1221,1254},{1218,1251,1259,1264,1277,1285,1294,1327,1331,1335,1339,1343,1357},true},
    {0x1d638938d93421b3ull,1207,{1143,1130,1189,1176,0},1163,1152,1198,1,5,0,
     {1170},{1152,1167,1172,1180,1184,1198}},
    {0x462342e3e5781384ull,1250,{1187,1165,1232,1219,0},1207,1196,1241,1,5,7,
     {1163,1176,1151,1159},{1148,1156,1160,1173,1178,1182,1196,1211,1215,1223,1227,1241}},
    {0x4f052209611387f0ull,1326,{1265,1130,1236,1308,1261},1282,1253,1317,5,5,0,
     {1297},{1253,1286,1290,1294,1299,1303,1317},true},
    {0x55826dc176afe464ull,339,{291,279,321,308,0},0,288,330,2,2,0,
     {302},{288,295,299,304,312,316,330}},
    {0x64bac8bb307eb896ull,1392,{1332,1148,1295,1374,1328},1349,1320,1383,6,5,7,
     {1288,1293,1247,1280},{1244,1277,1285,1290,1303,1311,1320,1353,1357,1361,1365,1369,1383},true},
    {0x789449ffd931d23eull,1300,{1239,1127,1210,1282,1235},1256,1227,1291,5,5,0,
     {1271},{1227,1260,1264,1268,1273,1277,1291},true},
    {0x7c83ed50c9894e44ull,1298,{1226,1209,1280,1267,0},1248,1252,1289,4,5,7,
     {1207,1220,1195,1203},{1192,1200,1204,1217,1222,1235,1252,1255,1259,1263,1271,1275,1289},false,7},
    {0x827d8d2d617bedceull,1276,{1213,1191,1258,1245,0},1233,1222,1267,1,5,7,
     {1189,1202,1177,1185},{1174,1182,1186,1199,1204,1208,1222,1237,1241,1249,1253,1267}},
    {0x99153c144030c396ull,1355,{1295,1145,1258,1337,1291},1312,1283,1346,6,5,7,
     {1251,1256,1210,1243},{1207,1240,1248,1253,1266,1274,1283,1316,1320,1324,1328,1332,1346},true},
    {0xabf3c0fad53456d8ull,375,{331,219,302,357,327},0,319,366,4,2,0,
     {346},{319,335,339,343,348,352,366},true},
    {0xb0f9313b77cc78eeull,1289,{1228,1127,1199,1271,1224},1245,1216,1280,5,5,0,
     {1260},{1216,1249,1253,1257,1262,1266,1280},true},
    {0xbd4d51c08486c6e0ull,256,{217,196,238,225,0},0,209,247,1,2,0,
     {215},{209,212,221,229,233,247}},
    {0xc1452981fd0bff64ull,1381,{1321,1148,1284,1363,1317},1338,1309,1372,6,5,7,
     {1277,1282,1236,1269},{1233,1266,1274,1279,1292,1300,1309,1342,1346,1350,1354,1358,1372},true},
    {0xcf449bcb069aec4full,407,{363,228,334,389,359},0,351,398,4,2,0,
     {378},{351,367,371,375,380,384,398},true},
    {0xd514bf852d8a9c58ull,1315,{1254,1130,1225,1297,1250},1271,1242,1306,5,5,0,
     {1286},{1242,1275,1279,1283,1288,1292,1306},true},
    {0xdb644b73b68c0547ull,1232,{1159,1155,1214,1201,0},1181,1185,1223,1,5,0,
     {1195},{1185,1188,1192,1197,1205,1209,1223}},
    {0xde2dd381fa64193dull,288,{249,228,270,257,0},0,241,279,1,2,0,
     {247},{241,244,253,261,265,279}},
    {0xdff6a3d360603fa2ull,364,{320,219,291,346,316},0,308,355,4,2,0,
     {335},{308,324,328,332,337,341,355},true},
    {0xe70adc744a38ca59ull,1324,{1252,1235,1306,1293,0},1274,1278,1315,4,5,7,
     {1233,1246,1221,1229},{1218,1226,1230,1243,1248,1261,1278,1281,1285,1289,1297,1301,1315},false,7},
    {0xf1d14a7dbf7c6173ull,396,{352,228,323,378,348},0,340,387,4,2,0,
     {367},{340,356,360,364,369,373,387},true},
    {0xf6a501717c3e5ca8ull,313,{265,253,295,282,0},0,262,304,2,2,0,
     {276},{262,269,273,278,286,290,304}},
    {0xff32b602a271c327ull,1258,{1185,1181,1240,1227,0},1207,1211,1249,1,5,0,
     {1221},{1211,1214,1218,1223,1231,1235,1249}},
    {0x1ed1bf0fdec00e1aull,1281,{1220,1080,1187,1263,1216},1237,1208,1272,5,5,0,
     {1252},{1208,1241,1245,1249,1254,1258,1272},true},
    {0x1f26d41bcb7dac1eull,1332,{1272,1095,1235,1314,1268},1289,1260,1323,6,5,7,
     {1228,1233,1188,1220},{1185,1217,1225,1230,1243,1251,1260,1293,1297,1301,1305,1309,1323},true},
    {0x2b04461d0dae038bull,336,{292,175,259,318,288},0,280,327,4,2,0,
     {307},{280,296,300,304,309,313,327},true},
    {0x78963cdc7c710e04ull,1255,{1194,1077,1161,1237,1190},1211,1182,1246,5,5,0,
     {1226},{1182,1215,1219,1223,1228,1232,1246},true},
    {0xacc83ed2509d84a1ull,362,{318,178,285,344,314},0,306,353,4,2,0,
     {333},{306,322,326,330,335,339,353},true},
    {0xbdcdb3ab996ae4e0ull,1358,{1298,1098,1261,1340,1294},1315,1286,1349,6,5,7,
     {1254,1259,1214,1246},{1211,1243,1251,1256,1269,1277,1286,1319,1323,1327,1331,1335,1349},true},
    {0x22cc5b05a55ef61eull,350,{310,178,277,332,306},0,298,341,4,2,0,
     {321},{298,314,318,323,327,341},true},
    {0x3006f8030a467739ull,1312,{1256,1095,1219,1294,1252},1273,1244,1303,6,5,7,
     {1212,1217,1171,1204},{1168,1201,1209,1214,1227,1235,1244,1277,1281,1285,1289,1303},true},
    {0x769c3814fc0efba8ull,324,{284,175,251,306,280},0,272,315,4,2,0,
     {295},{272,288,292,297,301,315},true},
    {0xd6e8bdde0e4c515full,1338,{1282,1098,1245,1320,1278},1299,1270,1329,6,5,7,
     {1238,1243,1197,1230},{1194,1227,1235,1240,1253,1261,1270,1303,1307,1311,1315,1329},true},
    {0xe5ea78b8b0b0fe07ull,1243,{1186,1077,1153,1225,1182},1203,1174,1234,5,5,0,
     {1214},{1174,1207,1211,1216,1220,1234},true},
    {0xf42202faf57a3c89ull,1269,{1212,1080,1179,1251,1208},1229,1200,1260,5,5,0,
     {1240},{1200,1233,1237,1242,1246,1260},true},
    {0x3755809bd40afc13ull,1206,{1142,1129,1188,1175,0},1162,1151,1197,1,5,0,
     {1169},{1151,1166,1171,1179,1183,1197}},
    {0x61418505e5d8f998ull,261,{222,201,243,230,0},0,214,252,1,2,0,
     {220},{214,217,226,234,238,252}},
    {0x91b6c09eb47f8555ull,1282,{1219,1206,1264,1251,0},1239,1228,1273,1,5,7,
     {1199,1204,1159,1191},{1156,1188,1196,1201,1214,1228,1243,1247,1255,1259,1273}},
    {0xb5f1d4145171026bull,287,{248,227,269,256,0},0,240,278,1,2,0,
     {246},{240,243,252,260,264,278}},
    {0xcc09f17db377fd9eull,1180,{1116,1103,1162,1149,0},1136,1125,1171,1,5,0,
     {1143},{1125,1140,1145,1153,1157,1171}},
    {0xef2bf556f207b8bdull,1256,{1193,1180,1238,1225,0},1213,1202,1247,1,5,7,
     {1173,1178,1133,1165},{1130,1162,1170,1175,1188,1202,1217,1221,1229,1233,1247}},
    {0x042c9ae16f41feffull,1248,{1191,1077,1158,1230,1187},1208,1179,1239,5,5,0,
     {1219},{1179,1212,1216,1221,1225,1239},true},
    {0x3602b05ce11ca6ffull,1324,{1268,1095,1235,1306,1264},1285,1256,1315,6,5,7,
     {1228,1233,1188,1220},{1185,1217,1225,1230,1247,1256,1289,1293,1297,1301,1315},true},
    {0x5c823b8507fa1442ull,323,{283,169,250,305,279},0,271,314,4,2,0,
     {294},{271,287,291,296,300,314},true},
    {0x68f0dd6791fd7d3dull,1274,{1217,1080,1184,1256,1213},1234,1205,1265,5,5,0,
     {1245},{1205,1238,1242,1247,1251,1265},true},
    {0x8e58ac79b59b02b1ull,1350,{1294,1098,1261,1332,1290},1311,1282,1341,6,5,7,
     {1254,1259,1214,1246},{1211,1243,1251,1256,1273,1282,1315,1319,1323,1327,1341},true},
    {0xa6e1328c0bb3f401ull,355,{315,178,282,337,311},0,303,346,4,2,0,
     {326},{303,319,323,328,332,346},true},
    {0x517540ae6d5e5410ull,397,{379,355,371},0,364,392,1,1,3,
     {348,353,307,340},{304,337,345,350,359,364,367,375,383,392},false,0,1},
    {0x7a0c3388065bb08dull,323,{305,280,297},0,289,318,0,1,0,
     {295},{289,292,301,309,318},false,0,2},
    {0xd44db87778a43b61ull,448,{430,274,406,422},0,415,443,1,1,3,
     {399,404,358,391},{355,388,396,401,410,415,418,426,434,443},true,0,3},
    {0x550c2a4d4d3ed70full,374,{356,254,331,348},0,340,369,0,1,0,
     {346},{340,343,352,360,369},true,0,4},

    {0x39eb3c2258a516e1ull,432,{372,303,414,401,0},0,325,423,3,2,4,
     {301,314,256,289},{253,286,298,311,316,320,325,332,340,349,358,363,367,376,380,384,388,392,397,405,409,423},false,0,0,1},
    {0x57acf59d19c73791ull,464,{404,335,446,433,0},0,357,455,3,2,4,
     {333,346,288,321},{285,318,330,343,348,352,357,364,372,381,390,395,399,408,412,416,420,424,429,437,441,455},false,0,0,1},
    {0xf917d48ee826da1full,320,{260,218,302,289,0},0,243,311,1,2,0,
     {253},{243,250,255,264,268,272,276,280,285,293,297,311},false,0,0,2},
    {0x77a5b2d62fb3be48ull,352,{292,250,334,321,0},0,275,343,1,2,0,
     {285},{275,282,287,296,300,304,308,312,317,325,329,343},false,0,0,2},
    {0xa910daef935891ceull,500,{444,235,359,482,400},0,384,491,4,2,4,
     {352,357,311,344},{308,341,349,354,367,375,384,392,408,416,425,434,439,448,452,456,460,464,468,473,477,491},true,0,0,1},
    {0x62c180abe017e239ull,526,{470,243,385,508,426},0,410,517,4,2,4,
     {378,383,337,370},{334,367,375,380,393,401,410,418,434,442,451,460,465,474,478,482,486,490,494,499,503,517},true,0,0,1},
    {0xed44232013f67072ull,388,{332,193,269,370,307},0,290,379,3,2,0,
     {301},{290,298,327,336,340,344,348,352,356,361,365,379},true,0,0,2},
    {0xf286856c3f400377ull,414,{358,201,295,396,333},0,316,405,3,2,0,
     {327},{316,324,353,362,366,370,374,378,382,387,391,405},true,0,0,2},
    {0x9d27e7ba242f3831ull,1259,{1176,1134,1241,1228,0},1198,1151,1250,5,5,0,
     {1205},{1151,1154,1162,1171,1185,1202,1207,1211,1215,1219,1224,1232,1236,1250},false,0,0,3},
    {0xe1acf8a03850acafull,1285,{1202,1160,1267,1254,0},1224,1177,1276,5,5,0,
     {1231},{1177,1180,1188,1197,1211,1228,1233,1237,1241,1245,1250,1258,1262,1276},false,0,0,3},
    {0xf646f03be5a8708dull,1259,{1176,1134,1241,1228,0},1198,1151,1250,5,5,0,
     {1205},{1151,1154,1162,1171,1185,1202,1207,1211,1215,1219,1224,1232,1236,1250},false,0,0,3},
    {0xebf41e1ace7af45bull,1285,{1202,1160,1267,1254,0},1224,1177,1276,5,5,0,
     {1231},{1177,1180,1188,1197,1211,1228,1233,1237,1241,1245,1250,1258,1262,1276},false,0,0,3},
    {0xc997a37560e266dfull,340,{284,232,322,309,0},0,249,331,2,2,0,
     {277},{249,252,260,269,274,279,288,292,296,300,305,313,317,331},false,0,0,3},
    {0x675f9077d8fd21c4ull,366,{310,258,348,335,0},0,275,357,2,2,0,
     {303},{275,278,286,295,300,305,314,318,322,326,331,339,343,357},false,0,0,3},
    {0x18d372968af4a480ull,1321,{1247,1107,1183,1303,1221},1269,1204,1312,3,5,0,
     {1215},{1204,1212,1225,1233,1242,1256,1273,1277,1281,1285,1289,1294,1298,1312},true,0,0,3},
    {0x188c5ab9dbb98393ull,1347,{1273,1115,1209,1329,1247},1295,1230,1338,3,5,0,
     {1241},{1230,1238,1251,1259,1268,1282,1299,1303,1307,1311,1315,1320,1324,1338},true,0,0,3},
    {0x7e5e41276b3d7514ull,1321,{1247,1107,1183,1303,1221},1269,1204,1312,3,5,0,
     {1215},{1204,1212,1225,1233,1242,1256,1273,1277,1281,1285,1289,1294,1298,1312},true,0,0,3},
    {0x43c9405568d2226full,1347,{1273,1115,1209,1329,1247},1295,1230,1338,3,5,0,
     {1241},{1230,1238,1251,1259,1268,1282,1299,1303,1307,1311,1315,1320,1324,1338},true,0,0,3},
    {0x5e056627e9ff3a8dull,402,{350,205,281,384,319},0,302,393,3,2,0,
     {313},{302,310,323,331,340,345,354,358,362,366,370,375,379,393},true,0,0,3},
    {0xfce465befff2f623ull,428,{376,213,307,410,345},0,328,419,3,2,0,
     {339},{328,336,349,357,366,371,380,384,388,392,396,401,405,419},true,0,0,3},
    // Six SM3 glass pairs: diffuse s0, numeric gloss s1, environment cube s2.
    {0xa66fb1981ba755b2ull,309,{295,255,287},0,276,299,1,1,3,
     {248,253,208,240},{205,237,245,250,267,271,276,279,291,299},false,0,0,0,266},
    {0xebc9b2b3f1564e9aull,341,{327,287,319},0,308,331,1,1,3,
     {280,285,240,272},{237,269,277,282,299,303,308,311,323,331},false,0,0,0,298},
    {0xf31c9e2701c8eee4ull,226,{212,174,204},0,192,216,1,1,0,
     {202,0,0,0},{192,199,208,216},false,0,0,0,198},
    {0x9d49f288800f898dull,258,{244,206,236},0,224,248,1,1,0,
     {234,0,0,0},{224,231,240,248},false,0,0,0,230},
};
struct Vertex {
    std::uint64_t hash; unsigned words; bool loop; bool bump = false;
    unsigned asteroid_layout = 0;
    unsigned point = 0, emissive = 0, alpha = 0, point_temporary = 0;
    unsigned palette_style = 0, point_response = 0, point_accumulator = 0, alpha_temporary = 0;
    unsigned glass_fresnel = 0; // Original EXP o6.x becomes o1.x; alpha stays o1.w.
};
constexpr Vertex vertices[] = {{0x53a0a641107ed76cull,526,true},
    {0x719856ce0c213220ull,526,true},{0xbadefd5143b3024full,481,false},
    {0x4944d81dfe531b37ull,556,true,true},{0x19a246a56e9d9700ull,511,false,true},
    {0x44c4a41ca92ae2e3ull,556,true,true},
    {0x494fe349b8bc12ecull,526,true},
    {0xb0602757fce6e870ull,520,true,false,1,419,434,507,4},
    {0x0c223ad11bce02d5ull,517,true,false,2,416,431,500,4},
    {0x233d17d26ce0c1fcull,472,false,false,2,397,401,455,1},
    {0x167eb2d5629ab9d3ull,566,true,true,3,431,446,537,1},
    {0x12b8a13f13fe8cfeull,518,false,true,4,409,413,485,0},
    {0x330ceb9dd874ede2ull,563,true,true,4,428,443,530,1},
    {0x29d7c575396ed280ull,577,true,false,0,440,455,512,5,1,3,0,0},
    {0xa420a010b0271479ull,605,true,false,0,458,473,530,5,2,3,0,0},
    {0xea3d15b287892410ull,560,false,false,0,419,427,485,1,2,1,1,0},
    {0x57392213f62fef19ull,617,true,true,0,449,464,539,1,1,3,0,2},
    {0x5c17a381b149b3b9ull,648,true,true,0,487,514,539,1,2,2,1,0},
    {0xa804f173f693944aull,600,false,true,0,452,465,491,3,2,0,3,0},
    {0x37e6956afd8b8d76ull,563,true,false,0,440,455,512,5,3,3,0,0},
    {0x2e0254dd999841c2ull,563,true,false,0,440,455,512,5,3,3,0,0},
    {0xa7cddf2c98d61117ull,512,false,false,0,395,403,461,1,3,1,1,0},
    {0x33388c8897d428a5ull,603,true,true,0,449,464,539,1,3,3,0,2},
    {0xb4059ab6af8fc529ull,603,true,true,0,449,464,539,1,3,3,0,2},
    {0x2a560f246c90fa64ull,552,false,true,0,404,412,488,0,3,0,0,2},
    {0xc30104cb0efb6675ull,550,true,false,0,431,446,503,5,0,0,0,0,546},
    {0xe2ad860d5fbb3e59ull,550,true,false,0,431,446,503,5,0,0,0,0,546},
    {0x74fdc00d802b4027ull,505,false,false,0,392,400,458,1,0,0,0,0,501},
};
// Explicit archive pair contract: base shaders never gain toggle-VS admission
// from table position. The live caller caches this allocation-free contract.
constexpr bool bump_pixel(std::uint64_t hash) noexcept {
    for (const auto& pixel:pixels) if (pixel.hash==hash) return pixel.bump;
    return false;
}
constexpr LinearMaterialPairContract pair_contract(std::uint64_t hash, std::uint32_t mask) noexcept {
    LinearMaterialPairContract result{mask,bump_pixel(hash)};
    for (const auto& p:pixels) if (p.hash==hash && p.bump && p.palette_style) {
        const auto source=static_cast<std::uint8_t>(p.palette_style==3 ? 7 : 6);
        result.scalar_transport[0]={source,0,1,3}; result.scalar_transport_count=1;
        if (p.palette_style==1) { result.scalar_transport[1]={source,1,2,3}; result.scalar_transport_count=2; }
    }
    return result;
}
struct Pair {
    std::uint64_t vertex, pixel;
    LinearMaterialPairContract contract;
    constexpr Pair(std::uint64_t v, std::uint64_t p, std::uint32_t mask=0x0f) noexcept
        : vertex(v), pixel(p), contract(pair_contract(p,mask)) {}
};
constexpr Pair pairs[] = {
    {0x53a0a641107ed76cull,0x8759c7838bbc86c2ull},
    {0x53a0a641107ed76cull,0x63f96eba9eea7880ull},
    {0x53a0a641107ed76cull,0x3b94320087e81945ull},
    {0x53a0a641107ed76cull,0xe3b7acc16da9932dull},
    {0x719856ce0c213220ull,0x593e5dea9b3457d5ull},
    {0x719856ce0c213220ull,0x7a0bb00a8070496aull},
    {0x719856ce0c213220ull,0x8d5b2ba0fb4d13bfull},
    {0x719856ce0c213220ull,0xdab93928f26906f7ull},
    {0x719856ce0c213220ull,0x7a14d4dcb28f27e5ull},
    {0x719856ce0c213220ull,0x8ab6188a40ca15eaull},
    {0x719856ce0c213220ull,0x8df6143d0e77d92eull},
    {0x719856ce0c213220ull,0xe16a9806ee3544c3ull},
    {0xbadefd5143b3024full,0x593e5dea9b3457d5ull},
    {0xbadefd5143b3024full,0x7a0bb00a8070496aull},
    {0xbadefd5143b3024full,0x8d5b2ba0fb4d13bfull},
    {0xbadefd5143b3024full,0xdab93928f26906f7ull},
    {0xbadefd5143b3024full,0x7a14d4dcb28f27e5ull},
    {0xbadefd5143b3024full,0x8ab6188a40ca15eaull},
    {0xbadefd5143b3024full,0x8df6143d0e77d92eull},
    {0xbadefd5143b3024full,0xe16a9806ee3544c3ull},
    {0x19a246a56e9d9700ull,0x63379470db8d2a86ull,0x1f},
    {0x19a246a56e9d9700ull,0x68915563dd0aac9aull,0x1f},
    {0x19a246a56e9d9700ull,0xd086fde54698070cull,0x1f},
    {0x19a246a56e9d9700ull,0xf17fffd88d134b04ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x63379470db8d2a86ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x68915563dd0aac9aull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xd086fde54698070cull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xf17fffd88d134b04ull,0x1f},
    {0x4944d81dfe531b37ull,0x5e0a10fe752b6140ull,0x1f},
    {0x4944d81dfe531b37ull,0xca6bfa4a6cca7e2aull,0x1f},
    {0x19a246a56e9d9700ull,0x4f052209611387f0ull,0x1f},
    {0x19a246a56e9d9700ull,0x789449ffd931d23eull,0x1f},
    {0x19a246a56e9d9700ull,0xabf3c0fad53456d8ull,0x1f},
    {0x19a246a56e9d9700ull,0xb0f9313b77cc78eeull,0x1f},
    {0x19a246a56e9d9700ull,0xcf449bcb069aec4full,0x1f},
    {0x19a246a56e9d9700ull,0xd514bf852d8a9c58ull,0x1f},
    {0x19a246a56e9d9700ull,0xdff6a3d360603fa2ull,0x1f},
    {0x19a246a56e9d9700ull,0xf1d14a7dbf7c6173ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x4f052209611387f0ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x789449ffd931d23eull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xabf3c0fad53456d8ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xb0f9313b77cc78eeull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xcf449bcb069aec4full,0x1f},
    {0x44c4a41ca92ae2e3ull,0xd514bf852d8a9c58ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xdff6a3d360603fa2ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xf1d14a7dbf7c6173ull,0x1f},
    {0x4944d81dfe531b37ull,0x0c1f3f0f440e4a0cull,0x1f},
    {0x4944d81dfe531b37ull,0x64bac8bb307eb896ull,0x1f},
    {0x4944d81dfe531b37ull,0x99153c144030c396ull,0x1f},
    {0x4944d81dfe531b37ull,0xc1452981fd0bff64ull,0x1f},
    {0x494fe349b8bc12ecull,0x7c83ed50c9894e44ull},
    {0x494fe349b8bc12ecull,0xe70adc744a38ca59ull},
    {0x53a0a641107ed76cull,0x462342e3e5781384ull},
    {0x53a0a641107ed76cull,0x827d8d2d617bedceull},
    {0x719856ce0c213220ull,0x02606104fa59fb29ull},
    {0x719856ce0c213220ull,0x1d638938d93421b3ull},
    {0x719856ce0c213220ull,0x55826dc176afe464ull},
    {0x719856ce0c213220ull,0xbd4d51c08486c6e0ull},
    {0x719856ce0c213220ull,0xdb644b73b68c0547ull},
    {0x719856ce0c213220ull,0xde2dd381fa64193dull},
    {0x719856ce0c213220ull,0xf6a501717c3e5ca8ull},
    {0x719856ce0c213220ull,0xff32b602a271c327ull},
    {0xbadefd5143b3024full,0x02606104fa59fb29ull},
    {0xbadefd5143b3024full,0x1d638938d93421b3ull},
    {0xbadefd5143b3024full,0x55826dc176afe464ull},
    {0xbadefd5143b3024full,0xbd4d51c08486c6e0ull},
    {0xbadefd5143b3024full,0xdb644b73b68c0547ull},
    {0xbadefd5143b3024full,0xde2dd381fa64193dull},
    {0xbadefd5143b3024full,0xf6a501717c3e5ca8ull},
    {0xbadefd5143b3024full,0xff32b602a271c327ull},
    {0x19a246a56e9d9700ull,0x042c9ae16f41feffull,0x1f},
    {0x19a246a56e9d9700ull,0x1ed1bf0fdec00e1aull,0x1f},
    {0x19a246a56e9d9700ull,0x22cc5b05a55ef61eull,0x1f},
    {0x19a246a56e9d9700ull,0x2b04461d0dae038bull,0x1f},
    {0x19a246a56e9d9700ull,0x5c823b8507fa1442ull,0x1f},
    {0x19a246a56e9d9700ull,0x68f0dd6791fd7d3dull,0x1f},
    {0x19a246a56e9d9700ull,0x769c3814fc0efba8ull,0x1f},
    {0x19a246a56e9d9700ull,0x78963cdc7c710e04ull,0x1f},
    {0x19a246a56e9d9700ull,0xa6e1328c0bb3f401ull,0x1f},
    {0x19a246a56e9d9700ull,0xacc83ed2509d84a1ull,0x1f},
    {0x19a246a56e9d9700ull,0xe5ea78b8b0b0fe07ull,0x1f},
    {0x19a246a56e9d9700ull,0xf42202faf57a3c89ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x042c9ae16f41feffull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x1ed1bf0fdec00e1aull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x22cc5b05a55ef61eull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x2b04461d0dae038bull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x5c823b8507fa1442ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x68f0dd6791fd7d3dull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x769c3814fc0efba8ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0x78963cdc7c710e04ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xa6e1328c0bb3f401ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xacc83ed2509d84a1ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xe5ea78b8b0b0fe07ull,0x1f},
    {0x44c4a41ca92ae2e3ull,0xf42202faf57a3c89ull,0x1f},
    {0x4944d81dfe531b37ull,0x1f26d41bcb7dac1eull,0x1f},
    {0x4944d81dfe531b37ull,0x3006f8030a467739ull,0x1f},
    {0x4944d81dfe531b37ull,0x3602b05ce11ca6ffull,0x1f},
    {0x4944d81dfe531b37ull,0x8e58ac79b59b02b1ull,0x1f},
    {0x4944d81dfe531b37ull,0xbdcdb3ab996ae4e0ull,0x1f},
    {0x4944d81dfe531b37ull,0xd6e8bdde0e4c515full,0x1f},
    {0x53a0a641107ed76cull,0x91b6c09eb47f8555ull},
    {0x53a0a641107ed76cull,0xef2bf556f207b8bdull},
    {0x719856ce0c213220ull,0x3755809bd40afc13ull},
    {0x719856ce0c213220ull,0x61418505e5d8f998ull},
    {0x719856ce0c213220ull,0xb5f1d4145171026bull},
    {0x719856ce0c213220ull,0xcc09f17db377fd9eull},
    {0xbadefd5143b3024full,0x3755809bd40afc13ull},
    {0xbadefd5143b3024full,0x61418505e5d8f998ull},
    {0xbadefd5143b3024full,0xb5f1d4145171026bull},
    {0xbadefd5143b3024full,0xcc09f17db377fd9eull},
    {0xb0602757fce6e870ull,0x517540ae6d5e5410ull,0x07},
    {0x0c223ad11bce02d5ull,0x7a0c3388065bb08dull,0x07},
    {0x233d17d26ce0c1fcull,0x7a0c3388065bb08dull,0x07},
    {0x167eb2d5629ab9d3ull,0xd44db87778a43b61ull,0x0f},
    {0x12b8a13f13fe8cfeull,0x550c2a4d4d3ed70full,0x0f},
    {0x330ceb9dd874ede2ull,0x550c2a4d4d3ed70full,0x0f},
    {0x29d7c575396ed280ull,0x39eb3c2258a516e1ull,0x0f},
    {0x29d7c575396ed280ull,0x57acf59d19c73791ull,0x0f},
    {0x2a560f246c90fa64ull,0x43c9405568d2226full,0x1f},
    {0x2a560f246c90fa64ull,0x5e056627e9ff3a8dull,0x1f},
    {0x2a560f246c90fa64ull,0x7e5e41276b3d7514ull,0x1f},
    {0x2a560f246c90fa64ull,0xfce465befff2f623ull,0x1f},
    {0x2e0254dd999841c2ull,0x675f9077d8fd21c4ull,0x0f},
    {0x2e0254dd999841c2ull,0xc997a37560e266dfull,0x0f},
    {0x2e0254dd999841c2ull,0xebf41e1ace7af45bull,0x0f},
    {0x2e0254dd999841c2ull,0xf646f03be5a8708dull,0x0f},
    {0x33388c8897d428a5ull,0x188c5ab9dbb98393ull,0x1f},
    {0x33388c8897d428a5ull,0x18d372968af4a480ull,0x1f},
    {0x37e6956afd8b8d76ull,0x9d27e7ba242f3831ull,0x0f},
    {0x37e6956afd8b8d76ull,0xe1acf8a03850acafull,0x0f},
    {0x57392213f62fef19ull,0x62c180abe017e239ull,0x1f},
    {0x57392213f62fef19ull,0xa910daef935891ceull,0x1f},
    {0x5c17a381b149b3b9ull,0xed44232013f67072ull,0x1f},
    {0x5c17a381b149b3b9ull,0xf286856c3f400377ull,0x1f},
    {0xa420a010b0271479ull,0x77a5b2d62fb3be48ull,0x0f},
    {0xa420a010b0271479ull,0xf917d48ee826da1full,0x0f},
    {0xa7cddf2c98d61117ull,0x675f9077d8fd21c4ull,0x0f},
    {0xa7cddf2c98d61117ull,0xc997a37560e266dfull,0x0f},
    {0xa7cddf2c98d61117ull,0xebf41e1ace7af45bull,0x0f},
    {0xa7cddf2c98d61117ull,0xf646f03be5a8708dull,0x0f},
    {0xa804f173f693944aull,0xed44232013f67072ull,0x1f},
    {0xa804f173f693944aull,0xf286856c3f400377ull,0x1f},
    {0xb4059ab6af8fc529ull,0x43c9405568d2226full,0x1f},
    {0xb4059ab6af8fc529ull,0x5e056627e9ff3a8dull,0x1f},
    {0xb4059ab6af8fc529ull,0x7e5e41276b3d7514ull,0x1f},
    {0xb4059ab6af8fc529ull,0xfce465befff2f623ull,0x1f},
    {0xea3d15b287892410ull,0x77a5b2d62fb3be48ull,0x0f},
    {0xea3d15b287892410ull,0xf917d48ee826da1full,0x0f},
    {0xc30104cb0efb6675ull,0xa66fb1981ba755b2ull,0x07},
    {0xc30104cb0efb6675ull,0xebc9b2b3f1564e9aull,0x07},
    {0xe2ad860d5fbb3e59ull,0xf31c9e2701c8eee4ull,0x07},
    {0xe2ad860d5fbb3e59ull,0x9d49f288800f898dull,0x07},
    {0x74fdc00d802b4027ull,0xf31c9e2701c8eee4ull,0x07},
    {0x74fdc00d802b4027ull,0x9d49f288800f898dull,0x07},
};
// Fixed family layouts, not a varying/temporary allocator. Asteroid's native
// UV packing changes the existing motion/depth locations independently of
// whether it samples a normal map. Preserve those complete row contracts.
struct FamilyAbi { unsigned vertex_rgb, pixel_rgb, pixel_scratch;
    unsigned vertex_motion, pixel_motion, motion_texcoord;
    unsigned vertex_depth, pixel_depth, depth_texcoord; };
constexpr FamilyAbi default_abi{8,7,9,6,5,4,7,6,5}, bump_abi{9,8,10,7,6,5,8,7,6};
FamilyAbi family_abi(const Pixel& pixel) noexcept {
    if (pixel.glass_fresnel) return {6,5,9,7,6,4,8,7,5};
    if (pixel.palette_style) return pixel.bump
        ? FamilyAbi{8,7,10,9,8,pixel.palette_style==3?5u:7u,10,9,8}
        : FamilyAbi{10,9,10,8,7,6,9,8,7};
    if (pixel.asteroid_layout==2) return {8,7,9,6,5,4,5,4,3};
    if (pixel.asteroid_layout==3) return {10,9,9,8,7,6,9,8,7};
    auto result=pixel.bump ? bump_abi : default_abi;
    if (pixel.depth_texcoord_override) result.depth_texcoord=pixel.depth_texcoord_override;
    return result;
}
unsigned temporal_temporary_base(const Pixel& pixel) noexcept {
    if (pixel.palette_style) return pixel.bump || pixel.affine_end ? 6 : 5;
    if (pixel.asteroid_layout) return 5;
    return pixel.bump ? (pixel.light1 ? 7u : pixel.affine_end ? 6u : 5u) : 5u;
}
unsigned point_site(const Vertex& vertex) noexcept { return (vertex.asteroid_layout || vertex.palette_style || vertex.glass_fresnel) ? vertex.point : (vertex.loop ? 428u : 389u)+(vertex.bump ? 9u : 0u); }
unsigned emissive_site(const Vertex& vertex) noexcept { return (vertex.asteroid_layout || vertex.palette_style || vertex.glass_fresnel) ? vertex.emissive : (vertex.loop ? 443u : 397u)+(vertex.bump ? 9u : 0u); }
unsigned kind(Word token) noexcept { return ((token >> 28) & 7) | ((token >> 8) & 24); }
unsigned index(Word token) noexcept { return token & 0x7ff; }
unsigned mask(Word token) noexcept { return (token >> 16) & 15; }
unsigned length(Word token) noexcept { return (token & 0xffff) == 0xfffe ? (token >> 16) & 0x7fff : (token >> 24) & 15; }
Word reg(unsigned type, unsigned number) noexcept { return 0x80000000u | ((type & 7u) << 28) | ((type & 24u) << 8) | number; }
Word dst(unsigned type, unsigned number, unsigned lanes = xyz) noexcept { return reg(type, number) | (lanes << 16); }
Word src(unsigned type, unsigned number, unsigned sw = identity, unsigned modifier = 0) noexcept {
    return reg(type, number) | (sw << 16) | (modifier << 24);
}
Word lane(unsigned type, unsigned number, unsigned component) noexcept { return src(type, number, component * 0x55); }
Word bits(float value) noexcept { Word result; std::memcpy(&result, &value, sizeof result); return result; }
float canonical_gain(float value) noexcept { return value == 0.0f ? 0.0f : value; }
void emit(Words& out, unsigned opcode, std::initializer_list<Word> operands) {
    out.push_back((static_cast<Word>(operands.size()) << 24) | opcode);
    out.insert(out.end(), operands.begin(), operands.end());
}
void definitions(Words& out, bool vertex, const LinearMaterialConfig& config) {
    const auto base = vertex ? 248u : 212u;
    emit(out, def, {dst(constant,base,xyzw),bits(2.2f),bits(0.0f),bits(65504.0f),bits(1e-10f)});
    emit(out, def, {dst(constant,base+1,xyzw),bits(canonical_gain(config.direct_gain)),
        bits(vertex ? canonical_gain(config.material_emissive_gain) : 1.0f/2.2f),
        bits(vertex ? 0.0f : canonical_gain(config.lightmap_emissive_gain)),bits(vertex ? 0.0f : 1e-22f)});
}
struct Source { Word value, address = 0; };
void sanitize(Words& out, bool vertex, unsigned target, Source source) {
    const unsigned base = vertex ? 248 : 212;
    // Input first in MAX/MIN is intentional: DX9's documented ordered
    // comparisons map NaN/-Inf to +0 and +Inf to the finite source cap.
    if (source.value & relative)
        emit(out,max_op,{dst(temp,target),source.value,source.address,lane(constant,base,1)});
    else emit(out,max_op,{dst(temp,target),source.value,lane(constant,base,1)});
    emit(out,min_op,{dst(temp,target),src(temp,target),lane(constant,base,2)});
}
void transfer(Words& out, bool vertex, unsigned target, Source source, bool encode = false, unsigned pixel_scratch = 9) {
    const unsigned base = vertex ? 248 : 212, scratch = vertex ? 8 : pixel_scratch;
    sanitize(out,vertex,target,source);
    // VS3 has no CMP. A strict-positive SLT mask times a finite positive POW
    // yields exact +0 for either signed zero without evaluating POW(0,...).
    if (vertex) emit(out,slt,{dst(temp,9),lane(constant,base,1),src(temp,target)});
    emit(out,max_op,{dst(temp,scratch),src(temp,target),lane(constant,encode ? base+1 : base,3)});
    for (unsigned component=0; component<3; ++component)
        emit(out,pow_op,{dst(temp,scratch,1u<<component),lane(temp,scratch,component),
                        lane(constant,encode ? base+1 : base,encode ? 1 : 0)});
    if (vertex) emit(out,mul,{dst(temp,target),src(temp,scratch),src(temp,9)});
    else emit(out,cmp,{dst(temp,target),src(temp,target,identity,1),lane(constant,base,1),src(temp,scratch)});
}
void gain(Words& out, bool vertex, unsigned target, unsigned component) {
    emit(out,mul,{dst(temp,target),src(temp,target),lane(constant,vertex ? 249 : 213,component)});
}

struct Instruction { std::size_t at; unsigned opcode, count; };
struct Structure {
    std::vector<Instruction> instructions;
    std::vector<unsigned char> boundary;
    std::size_t first_declaration = 0;
};
// Only opcodes present in the reviewed originals or our authored fragments.
// Microsoft SM3 instruction tables: REP/IF=3, ENDREP=2, DP2ADD/LRP=2,
// NRM/POW=3; ordinary TEXLD is 4 for a cube declaration and 1 for 2D.
// Unknown operations/forms fail instead of receiving an assumed unit cost.
bool body_shape(unsigned op, unsigned& operands, unsigned& slots, bool& destination) noexcept {
    destination=true; slots=1;
    switch (op) {
    case mov: case 6: case 7: case 14: case 15: case abs_op: case 46: operands=2; return true;
    case add: case mul: case 8: case 9: case min_op: case max_op: case slt:
        operands=3; return true;
    case mad: case cmp: operands=4; return true;
    case pow_op: operands=3; slots=3; return true;
    case 36: operands=2; slots=3; return true;
    case 18: case 90: operands=4; slots=2; return true;
    case texld: operands=3; return true;
    case 38: case 40: operands=1; slots=3; destination=false; return true;
    case 41: operands=2; slots=3; destination=false; return true;
    case 39: operands=0; slots=2; destination=false; return true;
    case 42: case 43: operands=0; destination=false; return true;
    default: return false;
    }
}
bool reserved_varying(unsigned number, bool vertex, const FamilyAbi& abi, bool relocated_rgb=false) noexcept {
    return number==(vertex?abi.vertex_motion:abi.pixel_motion) ||
        number==(vertex?abi.vertex_depth:abi.pixel_depth) ||
        (!relocated_rgb && number==(vertex?abi.vertex_rgb:abi.pixel_rgb));
}
// This narrow SM3 walk excludes comments/DEF literal words from register scans.
// The existing motion transformer still performs its independent full proof.
bool structure(const Word* code, std::size_t words, bool vertex, Structure& result,
               bool original, const FamilyAbi& abi, unsigned original_temp_count, bool palette=false, bool relocated_rgb=false, bool xt=false) {
    if (words < 2 || code[0] != (vertex ? 0xfffe0300u : 0xffff0300u)) return false;
    result.boundary.assign(words,0);
    std::array<unsigned,16> samplers{};
    unsigned slots=0;
    for (std::size_t at=1; at<words;) {
        result.boundary[at]=1;
        const Word token=code[at]; const unsigned op=token&0xffff, n=length(token);
        if (token==end_token) return at==words-1 && slots<=512 && result.first_declaration!=0;
        if (op==0xffff || n>words-at-1 || (op!=0xfffe && (token & 0xf0ff0000u & ~(xt && op==41 ? 0x00050000u : 0u))) ||
            (op==41 && (!xt || ((token>>16)&255)!=5))) return false;
        if (op==0xfffe) { at+=n+1; continue; }
        result.instructions.push_back({at,op,n});
        if (op==dcl) {
            if (n!=2) return false;
            if (!result.first_declaration) result.first_declaration=at;
            const auto type=kind(code[at+2]), number=index(code[at+2]);
            if (type==10) {
                const auto dimension=(code[at+1]>>27)&15;
                if (vertex || number>=samplers.size() || samplers[number] || (dimension!=2 && dimension!=3)) return false;
                samplers[number]=dimension;
            }
            const auto semantic=(code[at+1]>>16)&15;
            const auto usage=code[at+1]&31;
            if (original && type==(vertex ? output_reg : input) &&
                (reserved_varying(number,vertex,abi,relocated_rgb) || (usage==5 &&
                 (semantic==abi.motion_texcoord || semantic==abi.depth_texcoord)) ||
                 (!relocated_rgb && usage==LinearMaterialAbi::rgb_usage && semantic==LinearMaterialAbi::rgb_usage_index))) return false;
            if ((type==output_reg && number>=12) || (type==input && !vertex && number>=10)) return false;
        } else if (op==def) {
            if (n!=5) return false;
            const auto number=index(code[at+1]);
            if (kind(code[at+1])!=constant || number>=(vertex ? 256u : 224u)) return false;
            if (original && ((number>=(vertex ? 248u : 212u) && number<=(vertex ? 249u : 213u)) ||
                (palette && number>=(vertex?240u:204u) && number<=(vertex?245u:209u)))) return false;
        } else {
            unsigned expected=0, cost=0; bool destination=false;
            if (!body_shape(op,expected,cost,destination) || (!vertex && !destination && !(xt && (op==40 || op==41 || op==42 || op==43))) ||
                (vertex && (op==cmp || op==texld || op==90))) return false;
            unsigned parameters=0;
            for (unsigned offset=1; offset<=n; ++offset) {
                const Word parameter=code[at+offset];
                if (!(parameter&0x80000000u)) return false;
                const auto type=kind(parameter), number=index(parameter);
                if ((type==temp && number>=32) || (type==constant && number>=(vertex ? 256u : 224u)) ||
                    (type==output_reg && number>=12) || (type==input && !vertex && number>=10) ||
                    (type==color_output && number>=4)) return false;
                if (original && ((type==temp && number>=original_temp_count) ||
                    (type==constant && ((number>=(vertex ? 248u : 212u) && number<=(vertex ? 249u : 213u)) ||
                     (palette && number>=(vertex?240u:204u) && number<=(vertex?245u:209u)))) ||
                    (type==(vertex ? output_reg : input) && reserved_varying(number,vertex,abi,relocated_rgb)))) return false;
                ++parameters;
                if (parameter&relative) {
                    if ((destination && offset==1) || ++offset>n || !vertex || kind(parameter)!=constant || index(parameter)>2 ||
                        code[at+offset]!=src(3,0,255)) return false;
                }
            }
            if (parameters!=expected) return false;
            if (op==texld) {
                if (n!=3 || kind(code[at+3])!=10 || index(code[at+3])>=samplers.size()) return false;
                const auto dimension=samplers[index(code[at+3])];
                if (!dimension) return false;
                cost=dimension==3 ? 4 : 1;
            }
            slots+=cost;
        }
        at+=n+1;
    }
    return false;
}
bool exact(const Word* code, const Structure& s, unsigned at, unsigned opcode,
           Word destination, std::initializer_list<Word> sources) noexcept {
    if (at>=s.boundary.size() || !s.boundary[at] || code[at] != ((sources.size()+1)<<24 | opcode) ||
        code[at+1]!=destination) return false;
    return std::equal(sources.begin(),sources.end(),code+at+2);
}
bool no_write(const Word* code, const Structure& s, unsigned number, unsigned lanes,
              unsigned begin, unsigned end) noexcept {
    for (const auto& instruction:s.instructions)
        if (instruction.at>begin && instruction.at<end && instruction.count && instruction.opcode!=dcl &&
            instruction.opcode!=def && kind(code[instruction.at+1])==temp &&
            index(code[instruction.at+1])==number && (mask(code[instruction.at+1])&lanes)) return false;
    return true;
}
bool vertex_sites(const Word* code, const Structure& s, const Vertex& vertex) noexcept {
    const bool loop=vertex.loop;
    const unsigned point=point_site(vertex), emissive=emissive_site(vertex);
    const unsigned alpha=(vertex.asteroid_layout || vertex.palette_style || vertex.glass_fresnel) ? vertex.alpha : (loop?500:455)+(vertex.bump?27:0);
    const unsigned point_temp=(vertex.asteroid_layout || vertex.palette_style || vertex.glass_fresnel) ? vertex.point_temporary : vertex.bump ? (loop?1:0) : (loop?5:1);
    const unsigned response=vertex.palette_style?vertex.point_response:loop?3u:point_temp;
    const unsigned accumulator=vertex.palette_style?vertex.point_accumulator:0u;
    const unsigned alpha_temp=vertex.palette_style?vertex.alpha_temporary:vertex.bump?2u:0u;
    if (loop) {
        if (!exact(code,s,point,mul,dst(temp,point_temp),{lane(temp,response,3),src(constant,1)|relative,src(3,0,255)}) ||
            !exact(code,s,emissive,add,dst(output_reg,1),{src(temp,accumulator),src(constant,40)})) return false;
    } else if (!exact(code,s,point,mul,dst(temp,point_temp),{lane(temp,response,2),src(constant,5)}) ||
               !exact(code,s,emissive,mad,dst(output_reg,1),{src(temp,point_temp),lane(temp,response,3),src(constant,19)})) return false;
    if (!exact(code,s,alpha,mul,dst(output_reg,1,8),{lane(temp,alpha_temp,3),lane(constant,loop?39:18,0)}) ||
        !exact(code,s,alpha+5,mov,dst(output_reg,1,8),{lane(constant,loop?39:18,0)})) return false;
    unsigned writes=0;
    for (const auto& instruction:s.instructions)
        if (instruction.count && instruction.opcode!=dcl && kind(code[instruction.at+1])==output_reg && index(code[instruction.at+1])==1) {
            if (instruction.at!=emissive && instruction.at!=alpha && instruction.at!=alpha+5) return false;
            ++writes;
        }
    return writes==3;
}
// Both COLOR semantics retain native flat/Gouraud interpolation. Only the
// dead original P lanes are reused; no declaration-PP substitution is made
// for Fresnel or alpha, and no TEXCOORD/WRAP state is introduced.
bool glass_color_sites(const Word* code,const Structure& s,bool vertex,unsigned fresnel,
                       unsigned clamp,unsigned alpha) noexcept {
    unsigned color0=0,color1=0,uses0=0,uses1=0;
    for (const auto& ins:s.instructions) {
        const unsigned at=static_cast<unsigned>(ins.at);
        if (ins.opcode==dcl && kind(code[at+2])==(vertex?output_reg:input)) {
            const auto n=index(code[at+2]);
            if (n== (vertex?1u:0u)) {
                if (code[at+1]!=(0x80000000u|10u) || code[at+2]!=(dst(vertex?output_reg:input,n,xyzw)|(vertex?0:pp))) return false;
                ++color0;
            } else if (n==(vertex?6u:5u)) {
                if (code[at+1]!=(0x80000000u|10u|(1u<<16)) || code[at+2]!=(dst(vertex?output_reg:input,n,1)|(vertex?0:pp))) return false;
                ++color1;
            }
            continue;
        }
        if (ins.opcode==dcl || ins.opcode==def || !ins.count) continue;
        if (vertex) {
            if (kind(code[at+1])==output_reg && index(code[at+1])==6) {
                if (at!=fresnel || !exact(code,s,at,14,dst(output_reg,6,1),{lane(temp,0,3)})) return false;
                ++uses1;
            }
        } else for (unsigned n=2;n<=ins.count;++n) {
            if (kind(code[at+n])!=input) continue;
            if (index(code[at+n])==0) {
                if (!((at==clamp && n==2 && code[at+n]==src(input,0)) ||
                      (at==alpha && n==3 && code[at+n]==lane(input,0,3)))) return false;
                ++uses0;
            } else if (index(code[at+n])==5) {
                if (at+n!=fresnel || n!=3 || ins.opcode!=mul || mask(code[at+1])!=8 ||
                    !(code[at+1]&pp) || code[at+n]!=lane(input,5,0)) return false;
                ++uses1;
            }
        }
    }
    return color0==1 && color1==1 && uses1==1 && (vertex || uses0==2);
}

bool pixel_sites(const Word* code, const Structure& s, const Pixel& p) noexcept {
    if (p.asteroid_layout) {
        const unsigned detail=p.bump?3:2, specular=p.bump?2:1;
        const Word detail_coordinate=p.light1 ? src(input,2) : src(input,1,0xee);
        for (unsigned sampler=0; sampler<=detail; ++sampler)
            if (!exact(code,s,p.texture[sampler],texld,dst(temp,0,xyzw)|pp,
                       {sampler==detail?detail_coordinate:src(input,1),src(10,sampler)})) return false;
        if (!exact(code,s,p.clamp,mov,dst(temp,p.clamp_temporary)|pp|sat,{src(input,0)}) ||
            !exact(code,s,p.final_rgb,mul,dst(color_output,0)|pp,{src(temp,1),src(temp,0)}) ||
            !exact(code,s,p.final_rgb-4,mul,dst(color_output,0,8)|pp,{lane(temp,0,3),lane(input,0,3)}) ||
            !exact(code,s,p.texture[detail]+4,mul,dst(temp,2)|pp,{src(temp,0),lane(constant,p.light1?4:2,0)}) ||
            !exact(code,s,p.texture[0]+4,mad,dst(temp,0)|pp,{lane(constant,p.light1?5:3,0),src(temp,0),src(temp,2)}) ||
            !no_write(code,s,0,8,p.texture[0],p.final_rgb-4) ||
            !no_write(code,s,2,xyz,p.texture[detail]+4,p.texture[0]+4)) return false;
        unsigned outputs=0, textures=0, directional=0;
        for (const auto& instruction:s.instructions) {
            const auto at=static_cast<unsigned>(instruction.at);
            if (instruction.opcode==texld) ++textures;
            if (instruction.count && instruction.opcode!=dcl && kind(code[at+1])==color_output) {
                if (at!=p.final_rgb && at!=p.final_rgb-4) return false;
                ++outputs;
            }
            if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end() &&
                (mask(code[at+1])!=xyz || !(code[at+1]&pp) ||
                 (instruction.opcode!=mov && instruction.opcode!=mul && instruction.opcode!=add && instruction.opcode!=mad))) return false;
            if (instruction.opcode==dcl || instruction.opcode==def) continue;
            for (unsigned operand=2; operand<=instruction.count; ++operand) {
                const auto value=code[at+operand];
                if (kind(value)!=constant || (index(value)!=p.light0 && (!p.light1 || index(value)!=p.light1))) continue;
                const auto found=std::find(p.color_source.begin(),p.color_source.end(),at+operand);
                if (found==p.color_source.end() || value!=src(constant,index(value)) ||
                    index(value)!=(found-p.color_source.begin()<2?p.light0:p.light1)) return false;
                ++directional;
            }
        }
        for (unsigned at:p.rgb) if (at && (at>=s.boundary.size() || !s.boundary[at])) return false;
        // The exact physical specular fetch is data; the source proof retains
        // its scalar red chain and the optional AG normal reconstruction.
        return p.texture[specular]!=0 && outputs==2 && textures==detail+1 && directional==(p.light1?4u:1u);
    }
    if (p.glass_fresnel) {
        for (unsigned sampler=0;sampler<3;++sampler)
            if (!exact(code,s,p.texture[sampler],texld,dst(temp,0,xyzw)|pp,
                       {src(input,sampler==2?4:1),src(10,sampler)})) return false;
        if (!exact(code,s,p.clamp,mov,dst(temp,1)|pp|sat,{src(input,0)}) ||
            !exact(code,s,p.final_rgb,mad,dst(color_output,0)|pp,{src(temp,1),src(temp,0),src(temp,2)}) ||
            !exact(code,s,p.final_rgb+5,mul,dst(color_output,0,8)|pp,{lane(temp,0,3),lane(input,0,3)}) ||
            !no_write(code,s,0,8,p.texture[0],p.final_rgb+5)) return false;
        unsigned textures=0,outputs=0,lights=0;
        for (const auto& ins:s.instructions) {
            const unsigned at=static_cast<unsigned>(ins.at);
            if (ins.opcode==texld) ++textures;
            if (ins.opcode==dcl || ins.opcode==def || !ins.count) continue;
            if (kind(code[at+1])==color_output) {
                if (at!=p.final_rgb && at!=p.final_rgb+5) return false;
                ++outputs;
            }
            if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end() &&
                (mask(code[at+1])!=xyz || !(code[at+1]&pp) ||
                 (ins.opcode!=mov && ins.opcode!=add && ins.opcode!=mul && ins.opcode!=mad))) return false;
            for (unsigned offset=2;offset<=ins.count;++offset) {
                const auto value=code[at+offset];
                if (kind(value)!=constant || (index(value)!=p.light0 && (!p.light1 || index(value)!=p.light1))) continue;
                const auto found=std::find(p.color_source.begin(),p.color_source.end(),at+offset);
                if (found==p.color_source.end() || value!=src(constant,index(value)) ||
                    index(value)!=(found-p.color_source.begin()<2?p.light0:p.light1)) return false;
                ++lights;
            }
        }
        for (const unsigned at:p.rgb) if (at && (at>=s.boundary.size() || !s.boundary[at])) return false;
        return textures==3 && outputs==2 && lights==(p.light1?4u:1u);
    }
    const unsigned lightmap=p.bump?3:2, cube=p.bump?4:3;
    const unsigned albedo=p.palette_style?(p.bump?1:4):p.bump?4:3, affine_source=p.bump?3:2;
    for (unsigned sampler=0; sampler<(p.bump?5u:4u); ++sampler) {
        const unsigned target=sampler==0?1:(p.bump && sampler==2?2:0);
        const Word coordinate=p.bump && sampler==cube ? src(temp,0) : src(input,!p.bump && sampler==cube?4:1);
        if (!exact(code,s,p.texture[sampler],texld,dst(temp,target,xyzw)|pp,
                   {coordinate,src(10,sampler)})) return false;
    }
    if (!exact(code,s,p.clamp,mov,dst(temp,p.clamp_temporary)|pp|sat,{src(input,0)}) ||
        !exact(code,s,p.final_rgb,add,dst(color_output,0)|pp,{src(temp,1),src(temp,0)}) ||
        !exact(code,s,p.final_rgb+4,mul,dst(color_output,0,8)|pp,{lane(temp,2,3),lane(input,0,3)}) ||
        !exact(code,s,p.texture[lightmap]+4,18,dst(temp,2,8)|pp,
               {lane(constant,p.affine_end?3:0,0),lane(temp,0,3),lane(temp,1,3)})) return false;
    if (!no_write(code,s,1,8,p.texture[0],p.texture[lightmap]+4) ||
        !no_write(code,s,0,8,p.texture[lightmap],p.texture[lightmap]+4) ||
        !no_write(code,s,2,8,p.texture[lightmap]+4,p.final_rgb+4)) return false;
    if (p.affine_end)
        for (unsigned lane_index=0; lane_index<3; ++lane_index)
            if (!exact(code,s,p.affine_end-8+lane_index*4,9,dst(temp,albedo,1u<<lane_index)|pp,
                       {src(temp,affine_source),src(constant,lane_index)})) return false;
    unsigned outputs=0, textures=0, directional=0;
    for (const auto& instruction:s.instructions) {
        const unsigned at=static_cast<unsigned>(instruction.at);
        if (instruction.opcode==texld) ++textures;
        if (instruction.count && instruction.opcode!=dcl && kind(code[at+1])==color_output) {
            if (at!=p.final_rgb && at!=p.final_rgb+4) return false;
            ++outputs;
        }
        if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end()) {
            if (mask(code[at+1])!=xyz || !(code[at+1]&pp) ||
                (instruction.opcode!=mov && instruction.opcode!=mul && instruction.opcode!=add && instruction.opcode!=mad)) return false;
        }
        if (instruction.opcode==dcl || instruction.opcode==def) continue;
        for (unsigned operand=2; operand<=instruction.count; ++operand) {
            const auto value=code[at+operand];
            if (kind(value)==constant && (index(value)==p.light0 || (p.light1 && index(value)==p.light1))) {
                const unsigned source_at=at+operand;
                const auto found=std::find(p.color_source.begin(),p.color_source.end(),source_at);
                if (found==p.color_source.end() || value!=src(constant,index(value))) return false;
                const auto ordinal=static_cast<unsigned>(found-p.color_source.begin());
                if (index(value)!=(ordinal<2 ? p.light0 : p.light1)) return false;
                ++directional;
            }
        }
    }
    for (unsigned at:p.rgb) if (at && (at>=s.boundary.size() || !s.boundary[at])) return false;
    return outputs==2 && textures==(p.bump?5u:4u) && directional==(p.light1?4u:1u);
}

// Immutable palette colors were decoded from the exact source float32
// lanes with a float32 2.2 exponent and rounded once to float32. The host
// proof recomputes every component independently. Creation validates sources
// and emits DEFs without a libm call or changes to native mixed scalar lanes.
void palette_definitions(Words& out, const PaletteProgram& p, bool vertex, unsigned style) {
    const auto& colors=palette_linear_bits[style==3?1:0];
    for (unsigned role=0; role<p.sources.size(); ++role) if (p.sources[role].operand)
        emit(out,def,{dst(constant,(vertex?240u:204u)+role,xyzw),colors[role][0],colors[role][1],colors[role][2],0});
}
bool palette_sites(const Word* code, const Structure& s, const PaletteProgram& p,
                   bool vertex, bool bump, unsigned style) noexcept {
    const auto& colors=palette_color_bits[style==3?1:0];
    for (unsigned role=0; role<p.sources.size(); ++role) {
        const auto& use=p.sources[role]; if (!use.operand) continue;
        bool source=false, literal=false;
        for (const auto& ins:s.instructions) {
            if (ins.opcode==def && index(code[ins.at+1])==use.source_constant) {
                literal=true;
                for (unsigned c=0;c<3;++c)
                    if (code[ins.at+2+((use.swizzle>>(2*c))&3)]!=colors[role][c]) return false;
            }
            if ((ins.opcode==mul || ins.opcode==mad) && use.operand>=ins.at+2 && use.operand<=ins.at+ins.count) {
                if (mask(code[ins.at+1])!=xyz || code[use.operand]!=src(constant,use.source_constant,use.swizzle)) return false;
                source=true;
            }
        }
        if (!source || !literal || (vertex && role>3)) return false;
    }
    unsigned scalar_declarations=0, view_declarations=0, normal_declarations=0, palette_declarations=0, scalar_uses=0;
    for (const auto& ins:s.instructions) {
        if (ins.opcode==dcl && kind(code[ins.at+2])==(vertex?output_reg:input)) {
            const auto number=index(code[ins.at+2]);
            if (bump && number==(vertex?8u:7u)) {
                if (code[ins.at+1]!=(0x80000005u|((style==3?7u:6u)<<16)) ||
                    code[ins.at+2]!=(dst(vertex?output_reg:input,number,style==1?3u:1u)|(vertex?0:pp))) return false;
                ++scalar_declarations;
            }
            if (bump && (number==(vertex?3u:2u) || (style==1 && number==(vertex?4u:3u)))) {
                const unsigned semantic=number-(vertex?2u:1u);
                if (code[ins.at+1]!=(0x80000005u|(semantic<<16)) ||
                    code[ins.at+2]!=(dst(vertex?output_reg:input,number)|(vertex?0:pp))) return false;
                if (semantic==1) ++view_declarations; else ++normal_declarations;
            }
            if (!vertex && style==2 && number==(bump?6u:5u)) {
                if (code[ins.at+1]!=(0x80000005u|((bump?5u:4u)<<16)) || code[ins.at+2]!=(dst(input,number)|pp)) return false;
                ++palette_declarations;
            }
        } else if (bump && ins.opcode!=def) {
            const unsigned start=vertex?1u:2u, stop=vertex?1u:ins.count;
            for (unsigned operand=start; operand<=stop; ++operand) {
                const auto at=static_cast<unsigned>(ins.at)+operand;
                if (kind(code[at])==(vertex?output_reg:input) && index(code[at])==(vertex?8u:7u)) {
                    const auto found=std::find(p.scalar_operand.begin(),p.scalar_operand.end(),at);
                    if (found==p.scalar_operand.end()) return false;
                    const unsigned scalar=static_cast<unsigned>(found-p.scalar_operand.begin());
                    if (vertex) {
                        if (scalar==1) {
                            if (!exact(code,s,static_cast<unsigned>(ins.at),14,dst(output_reg,8,2),{lane(temp,2,2)})) return false;
                        } else if (style!=3) {
                            if (!exact(code,s,static_cast<unsigned>(ins.at),add,dst(output_reg,8,1),{lane(temp,2,3),lane(temp,2,3)})) return false;
                        } else if (!exact(code,s,static_cast<unsigned>(ins.at),add,dst(output_reg,8,1),{lane(temp,2,3),lane(constant,43,0)}) &&
                                   !exact(code,s,static_cast<unsigned>(ins.at),add,dst(output_reg,8,1),{lane(temp,2,3),lane(constant,21,3)})) return false;
                    } else if (code[at]!=lane(input,7,scalar) || mask(code[ins.at+1])!=xyz || (ins.opcode!=mul && ins.opcode!=mad)) return false;
                    ++scalar_uses;
                }
            }
        }
    }
    return (!bump || (scalar_declarations==1 && view_declarations==1 && normal_declarations==(style==1?1u:0u) && scalar_uses==(style==1?2u:1u))) &&
           (vertex || style!=2 || palette_declarations==1);
}

struct Insertion { std::size_t at, begin, end; };
// Prove the exact original -> ordinary-motion partition before editing. All
// copied original spans must match byte-for-byte. Ambiguous/new temporal
// layouts refuse instead of deriving offsets by searching shader contents.
bool motion_insertions(const Word* original, std::size_t words, const Words& motion,
    const MotionOutputProfile& row, bool vertex, bool depth, const Structure& s,
    std::vector<Insertion>& insertions) {
    insertions.reserve(vertex ? 2 : 3);
    if (vertex) {
        const std::size_t declarations=3+(depth?3:0), arithmetic=16+(depth?8:0);
        if (motion.size()!=words+declarations+arithmetic) return false;
        insertions.push_back({row.vertex_declaration_insert_dword,row.vertex_declaration_insert_dword,
                              row.vertex_declaration_insert_dword+declarations});
        insertions.push_back({row.vertex_arithmetic_insert_dword,row.vertex_arithmetic_insert_dword+declarations,
                              row.vertex_arithmetic_insert_dword+declarations+arithmetic});
    } else {
        const std::size_t declarations=3+(depth?3:0), definitions_count=18;
        if (motion.size()<=words+definitions_count+declarations) return false;
        const std::size_t body=motion.size()-words-definitions_count-declarations;
        insertions.push_back({row.pixel_definition_insert_dword,row.pixel_definition_insert_dword,
                              row.pixel_definition_insert_dword+definitions_count});
        insertions.push_back({row.pixel_declaration_insert_dword,row.pixel_declaration_insert_dword+definitions_count,
                              row.pixel_declaration_insert_dword+definitions_count+declarations});
        insertions.push_back({row.pixel_append_dword,row.pixel_append_dword+definitions_count+declarations,
                              row.pixel_append_dword+definitions_count+declarations+body});
    }
    std::size_t original_at=0, motion_at=0;
    for (const auto& insertion:insertions) {
        if (insertion.at>=words || !s.boundary[insertion.at] || insertion.at<original_at ||
            insertion.begin!=motion_at+insertion.at-original_at || insertion.end<=insertion.begin || insertion.end>motion.size()) return false;
        if (!std::equal(original+original_at,original+insertion.at,motion.begin()+motion_at)) return false;
        original_at=insertion.at; motion_at=insertion.end;
    }
    return motion.size()-motion_at==words-original_at &&
        std::equal(original+original_at,original+words,motion.begin()+motion_at);
}
const Pixel* pixel_for(std::uint64_t hash, std::size_t words) noexcept {
    for (const auto& p:pixels) if (p.hash==hash && p.words==words) return &p;
    return nullptr;
}
const Vertex* vertex_for(std::uint64_t hash, std::size_t words) noexcept {
    for (const auto& v:vertices) if (v.hash==hash && v.words==words) return &v;
    return nullptr;
}
const MotionOutputProfile* selected_row(bool vertex, std::uint64_t hash) noexcept {
    if (vertex) {
        for (const auto& pixel:pixels) if (linear_material_pair_reviewed(hash,pixel.hash))
            return material_motion_profile(hash,pixel.hash);
    } else {
        for (const auto& v:vertices) if (linear_material_pair_reviewed(v.hash,hash))
            return material_motion_profile(v.hash,hash);
    }
    return nullptr;
}
#include "linear_xt_material_inc.h"

LinearMaterialResult transform(const Word* original, std::size_t words, const LinearMaterialConfig& config,
    Words& output, bool current_depth, bool vertex) noexcept {
    if (!original || words<2) return LinearMaterialResult::InvalidInput;
    if (!linear_material_config_valid(config)) return LinearMaterialResult::InvalidConfig;
    // Bound the read before hashing; none of the reviewed original programs exceeds
    // 1392 DWORDs, including opaque CTAB/preshader comments.
    if (words>1392) return LinearMaterialResult::UnsupportedShader;
    const auto hash=material_motion_fingerprint(original,words);
    const auto* v=vertex?vertex_for(hash,words):nullptr;
    const auto* p=vertex?nullptr:pixel_for(hash,words);
    if ((!v && vertex) || (!p && !vertex)) return LinearMaterialResult::UnsupportedShader;
    const bool bump=vertex ? v->bump : p->bump;
    const auto* row=selected_row(vertex,hash);
    const auto* row_pixel=row ? pixel_for(row->pixel_fingerprint,row->pixel_dword_count) : nullptr;
    if (!row || !row_pixel || row_pixel->bump!=bump ||
        (vertex && (row_pixel->asteroid_layout!=v->asteroid_layout || row_pixel->palette_style!=v->palette_style || bool(row_pixel->glass_fresnel)!=bool(v->glass_fresnel)))) return LinearMaterialResult::ProfileMismatch;
    const auto abi=family_abi(*row_pixel);
    const auto* palette=row_pixel->palette_style ? palette_program(hash) : nullptr;
    if (row_pixel->palette_style && !palette) return LinearMaterialResult::ProfileMismatch;
    if (row->transformation_class!=((bump || row_pixel->palette_style || row_pixel->glass_fresnel) ? MotionOutputClass::RelocatedRegisters : MotionOutputClass::ReferenceRegisters) ||
        row->vertex_output_register!=abi.vertex_motion || row->pixel_input_register!=abi.pixel_motion ||
        row->pixel_temporary_base!=temporal_temporary_base(*row_pixel) ||
        row->vertex_constant_base!=252 || row->pixel_constant_base!=216 || row->pixel_output_register!=1 ||
        row->texcoord_index!=abi.motion_texcoord || row->vertex_depth_output_register!=abi.vertex_depth ||
        row->pixel_depth_input_register!=abi.pixel_depth || row->depth_texcoord_index!=abi.depth_texcoord ||
        !row->depth_output) return LinearMaterialResult::ProfileMismatch;
    const unsigned original_temp_count=vertex ? 7u : temporal_temporary_base(*p);
    try {
        Structure original_structure;
        if (!structure(original,words,vertex,original_structure,true,abi,original_temp_count,row_pixel->palette_style!=0,(bump && row_pixel->palette_style!=0) || row_pixel->glass_fresnel!=0) ||
            !(vertex?vertex_sites(original,original_structure,*v):pixel_sites(original,original_structure,*p)) ||
            (row_pixel->glass_fresnel && !glass_color_sites(original,original_structure,vertex,vertex?v->glass_fresnel:p->glass_fresnel,vertex?0:p->clamp,vertex?0:p->final_rgb+5)) ||
            (row_pixel->palette_style && !palette_sites(original,original_structure,*palette,vertex,bump,row_pixel->palette_style)))
            return LinearMaterialResult::ProfileMismatch;
        Words motion;
        const auto motion_result=vertex ? material_motion_vertex_variant_for(*row,original,words,motion,current_depth) :
                                         material_motion_pixel_variant_for(*row,original,words,motion,current_depth);
        if (motion_result!=MaterialMotionResult::Applied)
            return motion_result==MaterialMotionResult::AllocationFailure ? LinearMaterialResult::AllocationFailure : LinearMaterialResult::ProfileMismatch;
        const bool depth=vertex?material_motion_vertex_exports_depth(*row,current_depth):material_motion_pixel_writes_depth(*row,current_depth);
        std::vector<Insertion> insertions;
        if (!motion_insertions(original,words,motion,*row,vertex,depth,original_structure,insertions)) return LinearMaterialResult::ProfileMismatch;
        Words combined;
        combined.reserve(motion.size()+400);
        combined.push_back(original[0]);
        std::size_t insertion_index=0;
        for (std::size_t at=1; at<words;) {
            while (insertion_index<insertions.size() && insertions[insertion_index].at==at) {
                const auto& insertion=insertions[insertion_index++];
                combined.insert(combined.end(),motion.begin()+insertion.begin,motion.begin()+insertion.end);
            }
            if (at==original_structure.first_declaration) {
                definitions(combined,vertex,config);
                if (palette) palette_definitions(combined,*palette,vertex,row_pixel->palette_style);
            }
            const auto declaration_at=vertex?row->vertex_declaration_insert_dword:row->pixel_declaration_insert_dword;
            if (at==declaration_at) {
                // COLOR1 retains native color interpolation behavior, including
                // FLAT shading, without inheriting D3DRS_WRAPn texture state.
                // Its distinct physical register leaves the original COLOR0
                // declaration and alpha precision unchanged.
                if (!row_pixel->glass_fresnel) emit(combined,dcl,{0x80000000u|LinearMaterialAbi::rgb_usage|(LinearMaterialAbi::rgb_usage_index<<16),
                                  dst(vertex?output_reg:input,vertex?abi.vertex_rgb:abi.pixel_rgb)});
                if (!vertex) {
                    transfer(combined,false,12,{src(constant,p->light0)},false,abi.pixel_scratch); gain(combined,false,12,0);
                    if (p->light1) { transfer(combined,false,13,{src(constant,p->light1)},false,abi.pixel_scratch); gain(combined,false,13,0); }
                }
            }
            if (original[at]==end_token) { combined.push_back(end_token); ++at; continue; }
            const unsigned op=original[at]&0xffff, n=length(original[at]);
            if (vertex && at==point_site(*v)) {
                transfer(combined,true,7,{original[at+3],v->loop?original[at+4]:0}); gain(combined,true,7,0);
            }
            if (vertex && at==emissive_site(*v)) {
                sanitize(combined,true,7,{original[at+(v->loop?3:4)]});
                // Material emissive already includes native strength: no POW.
                // ABS canonicalizes a signed-zero sanitizer result explicitly.
                emit(combined,abs_op,{dst(temp,7),src(temp,7)}); gain(combined,true,7,1);
            }
            // The scalar carrier is fully vacated, so its old declaration is
            // replaced by the separately declared whole COLOR1 register.
            if (palette && bump && op==dcl && kind(original[at+2])==(vertex?output_reg:input) &&
                index(original[at+2])==(vertex?8u:7u)) { at+=n+1; continue; }
            const auto copied=combined.size();
            combined.insert(combined.end(),original+at,original+at+n+1);
            if (palette && op!=0xfffe) {
                if (op==dcl && kind(original[at+2])==(vertex?output_reg:input)) {
                    const unsigned number=index(original[at+2]);
                    if (bump && (number==(vertex?3u:2u) || (row_pixel->palette_style==1 && number==(vertex?4u:3u))))
                        combined[copied+2]|=8u<<16;
                    // Boron single palette RGB is already decoded in VS; the
                    // existing XYZ-only palette varying must transfer fully.
                    if (!vertex && row_pixel->palette_style==2 && number==(bump?6u:5u)) combined[copied+2]&=~pp;
                }
                for (unsigned role=0; role<palette->sources.size(); ++role) {
                    const auto operand=palette->sources[role].operand;
                    if (operand>at && operand<=at+n) combined[copied+operand-at]=src(constant,(vertex?240u:204u)+role);
                }
                for (unsigned scalar=0; scalar<2; ++scalar) {
                    const auto operand=palette->scalar_operand[scalar];
                    if (operand>at && operand<=at+n)
                        combined[copied+operand-at]=vertex ? dst(output_reg,3+scalar,8) : lane(input,2+scalar,3);
                }
            }
            if (row_pixel->glass_fresnel && op==dcl && kind(original[at+2])==(vertex?output_reg:input) &&
                index(original[at+2])==(vertex?6u:5u)) {
                // COLOR1 is now full-precision P; native Fresnel moved to the
                // existing COLOR0.x lane, keeping its original input PP hint.
                combined[copied+2]=dst(vertex?output_reg:input,vertex?6:5);
            }
            if (vertex) {
                if (v->glass_fresnel && at==v->glass_fresnel) combined[copied+1]=dst(output_reg,1,1);
                if (at==point_site(*v)) {
                    combined[copied+3]=src(temp,7);
                    if (v->loop) { combined.erase(combined.begin()+copied+4); combined[copied]=(3u<<24)|mul; }
                }
                if (at==emissive_site(*v)) {
                    combined[copied+1]=dst(output_reg,abi.vertex_rgb);
                    combined[copied+(v->loop?3:4)]=src(temp,7);
                }
            } else if (op!=0xfffe) {
                if (p->glass_fresnel>at && p->glass_fresnel<=at+n)
                    combined[copied+p->glass_fresnel-at]=lane(input,0,0);
                if (std::find(p->rgb.begin(),p->rgb.end(),at)!=p->rgb.end()) combined[copied+1]&=~pp;
                if (at==p->clamp) {
                    combined[copied+1]&=~sat;
                    combined[copied+2]=src(input,abi.pixel_rgb);
                }
                for (unsigned ordinal=0; ordinal<p->color_source.size(); ++ordinal)
                    if (p->color_source[ordinal]>at && p->color_source[ordinal]<=at+n)
                        combined[copied+p->color_source[ordinal]-at]=src(temp,ordinal<2?12:13);
                if (p->glass_fresnel) {
                    if (at==p->texture[0] || at==p->texture[2])
                        transfer(combined,false,0,{src(temp,0)},false,abi.pixel_scratch);
                } else if (p->asteroid_layout) {
                    if (at==p->texture[0] || at==p->texture[p->bump?3:2])
                        transfer(combined,false,0,{src(temp,0)},false,abi.pixel_scratch);
                } else {
                    if (at==(p->affine_end?p->affine_end:p->texture[0])) {
                        const unsigned albedo=p->palette_style?(p->affine_end?(p->bump?1:4):1):p->affine_end?(p->bump?4:3):1;
                        transfer(combined,false,albedo,{src(temp,albedo)},false,abi.pixel_scratch);
                    }
                    if (at==p->texture[p->bump?4:3]) transfer(combined,false,0,{src(temp,0)},false,abi.pixel_scratch);
                    if (at==p->texture[p->bump?3:2]) {
                        transfer(combined,false,0,{src(temp,0)},false,abi.pixel_scratch); gain(combined,false,0,2);
                    }
                }
                if (at==p->final_rgb) {
                    combined[copied+1]=dst(temp,11);
                    transfer(combined,false,11,{src(temp,11)},true,abi.pixel_scratch);
                    emit(combined,mov,{dst(color_output,0),src(temp,11)});
                }
            }
            at+=n+1;
        }
        if (insertion_index!=insertions.size()) return LinearMaterialResult::ProfileMismatch;
        Structure final_structure;
        if (!structure(combined.data(),combined.size(),vertex,final_structure,false,abi,original_temp_count)) return LinearMaterialResult::ResourceLimit;
        output.swap(combined);
        return LinearMaterialResult::Applied;
    } catch (...) { return LinearMaterialResult::AllocationFailure; }
}
} // namespace

bool linear_material_config_valid(const LinearMaterialConfig& config) noexcept {
    for (float value:{config.direct_gain,config.material_emissive_gain,config.lightmap_emissive_gain})
        if (!std::isfinite(value) || value<0.0f || value>16.0f) return false;
    return true;
}
std::uint32_t linear_material_sampler_mask(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    return linear_material_pair_contract(vertex,pixel).sampler_mask;
}
LinearMaterialPairContract linear_material_pair_contract(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    if (const auto* p=xt_pixel(pixel); p && vertex==(p->bump?xt_bump_vs:xt_default_vs)) {
        LinearMaterialPairContract result{p->bump?0x39u:0x1du,p->bump};
        if (p->bump) {result.scalar_transport[0]={6,0,1,3};result.scalar_transport[1]={6,1,2,3};result.scalar_transport_count=2;}
        return result;
    }
    for (const auto& pair:pairs)
        if (pair.vertex==vertex && pair.pixel==pixel) return pair.contract;
    return {};
}
bool linear_material_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    return linear_material_sampler_mask(vertex,pixel)!=0;
}
LinearMaterialResult linear_material_vertex_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth) noexcept {
    if (original && words==768 && material_motion_fingerprint(original,words)==xt_bump_vs)
        return xt_transform(original,words,config,output,current_depth,true,*xt_pixel(0x5f82ecacd39529cdull),true);
    return transform(original,words,config,output,current_depth,true);
}
LinearMaterialResult linear_material_pixel_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth) noexcept {
    if (original && words>=1561 && words<=1791 && std::any_of(std::begin(xt_pixels),std::end(xt_pixels),[&](const XtPixel& p){return p.words==words;})) if (const auto* p=xt_pixel(material_motion_fingerprint(original,words)))
        return xt_transform(original,words,config,output,current_depth,false,*p,true);
    return transform(original,words,config,output,current_depth,false);
}
bool linear_material_xt_default_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    const auto* p=xt_pixel(pixel);return vertex==xt_default_vs && p && !p->bump;
}
LinearMaterialResult linear_material_xt_default_vertex_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth, bool linear) noexcept {
    return xt_transform(original,words,config,output,current_depth,true,*xt_pixel(0xfffdabd910793abaull),linear);
}
LinearMaterialResult linear_material_xt_default_pixel_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth, bool linear) noexcept {
    if (!original || words<2) return LinearMaterialResult::InvalidInput;
    if (!linear_material_config_valid(config)) return LinearMaterialResult::InvalidConfig;
    if (words>1791) return LinearMaterialResult::UnsupportedShader;
    const auto* p=xt_pixel(material_motion_fingerprint(original,words));
    return !p || p->bump ? LinearMaterialResult::UnsupportedShader : xt_transform(original,words,config,output,current_depth,false,*p,linear);
}
} // namespace x3m::renderer
