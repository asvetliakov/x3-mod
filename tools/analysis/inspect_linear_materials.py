#!/usr/bin/env python3
"""Certify bounded original SM3 DEFAULT/BUMPMAP material conversion sites, without rewriting shaders.

Input: complete local archive programs and the existing derived motion inventory.
Output: fingerprints, semantic operand locations and available resources only.
Comments (including preshaders) are opaque. Exact whole-program hashes bind the
manual semantic review; there is no motif-only admission or hash override.
Transfer policy and transformed/native-GPU qualification belong to the owning
architecture notes; this tool does not certify transformed shader behavior.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inspect_motion_output_profiles as motion

# Derived whole-program identities from the reviewed archive, not shader words.
ORIGINALS = {
    'vs_53a0a641107ed76c': ('bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c', 526),
    'vs_719856ce0c213220': ('1aa39cbd8137cfcf9fb664451cf8c21060e0093c574889749afd8e69b17c19b9', 526),
    'vs_badefd5143b3024f': ('bd820507d47cfc99b3c182d7a86ac91ea2812811f5f5d53be603b023bfb43448', 481),
    'ps_8759c7838bbc86c2': ('9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0', 1260),
    'ps_63f96eba9eea7880': ('2046f15c8d3c761508a5abf8e4b540c7d6aa8902d131f02533377e68a9bfa75b', 1292),
    'ps_593e5dea9b3457d5': ('4e52664af108eddc19c13a98c8da62f539c79a1d085fbc55676e6130220907b2', 264),
    'ps_7a0bb00a8070496a': ('5b1aa3fa94f127c6b36164cb164b114e97d279007ccd882416f2d6b9cd01c8fd', 1215),
    'ps_8d5b2ba0fb4d13bf': ('4f1a61cf0fb97d6328ede3e18d2b715727938062ae601e180382380a3279c4f0', 1183),
    'ps_dab93928f26906f7': ('b15be08d6adc08a50c9ac756300f091c9fb1a3ec756fb65a556cc523a5166221', 296),
    'ps_3b94320087e81945': ('f4f228d05aef995c3a24a74ee4a494763103511f188a60cd695d8a1ec392a12c', 1264),
    'ps_e3b7acc16da9932d': ('db4c2d86eddbb7a60ba69d55c10dc8f58bb1dadd123696ced0d093f5b4630c15', 1296),
    'ps_7a14d4dcb28f27e5': ('ad5d2399d2f0dae0a8721eb2d33adee0fa1cc90d752d523aa18f63923886d2a3', 1187),
    'ps_8ab6188a40ca15ea': ('0c6703bacf857148b3bf528e8d8814b05a2332ba1889f3274148c2283d73ce3e', 1219),
    'ps_8df6143d0e77d92e': ('a2a2cc4d1b2f1ea0b6ff7b2382f41742c4b1b7bbe1706806ee2312ec0f88e821', 268),
    'ps_e16a9806ee3544c3': ('85b9538879c97626c5181f24c07585810e99b13001c6e0c50d5b1b695a5bfee4', 300),
}
# Reviewed semantic sites: sampler instruction order s0/s1/s2/s3, affine RGB
# completion (None when absent), COLOR0 clamp, directional register:consumers,
# final RGB output. Each offset addresses the ORIGINAL stream.
PIXELS = {
    '8759c7838bbc86c2': ((1197, 1175, 1242, 1229), 1217, 1206, {5: (1170, 1183), 7: (1158, 1166)}, 1251),
    '63f96eba9eea7880': ((1229, 1207, 1274, 1261), 1249, 1238, {5: (1202, 1215), 7: (1190, 1198)}, 1283),
    '593e5dea9b3457d5': ((225, 204, 246, 233), None, 217, {2: (220,)}, 255),
    '7a0bb00a8070496a': ((1151, 1138, 1197, 1184), 1171, 1160, {5: (1175,)}, 1206),
    '8d5b2ba0fb4d13bf': ((1119, 1106, 1165, 1152), 1139, 1128, {5: (1143,)}, 1174),
    'dab93928f26906f7': ((257, 236, 278, 265), None, 249, {2: (252,)}, 287),
    '3b94320087e81945': ((1192, 1175, 1246, 1233), 1214, 1218, {5: (1170, 1183), 7: (1158, 1166)}, 1255),
    'e3b7acc16da9932d': ((1224, 1207, 1278, 1265), 1246, 1250, {5: (1202, 1215), 7: (1190, 1198)}, 1287),
    '7a14d4dcb28f27e5': ((1114, 1106, 1169, 1156), 1136, 1140, {5: (1147,)}, 1178),
    '8ab6188a40ca15ea': ((1146, 1138, 1201, 1188), 1168, 1172, {5: (1179,)}, 1210),
    '8df6143d0e77d92e': ((220, 204, 250, 237), None, 217, {2: (228,)}, 259),
    'e16a9806ee3544c3': ((252, 236, 282, 269), None, 249, {2: (260,)}, 291),
}

# Separate reviewed BUMPMAP contract; these nine originals are offline-only.
BUMP_VERTICES = ('4944d81dfe531b37', '19a246a56e9d9700', '44c4a41ca92ae2e3')
BUMP_PIXELS = {
    'ca6bfa4a6cca7e2a': ((1272, 1095, 1235, 1310, 1268), 1289, 1260, {5: (1225, 1230), 7: (1185, 1217)}, 1319),
    '5e0a10fe752b6140': ((1298, 1098, 1261, 1336, 1294), 1315, 1286, {5: (1251, 1256), 7: (1211, 1243)}, 1345),
    '63379470db8d2a86': ((1194, 1077, 1161, 1233, 1190), 1211, 1182, {5: (1219,)}, 1242),
    '68915563dd0aac9a': ((292, 175, 259, 314, 288), None, 280, {2: (300,)}, 323),
    'd086fde54698070c': ((1220, 1080, 1187, 1259, 1216), 1237, 1208, {5: (1245,)}, 1268),
    'f17fffd88d134b04': ((318, 178, 285, 340, 314), None, 306, {2: (326,)}, 349),
}
ORIGINALS.update({
    'vs_4944d81dfe531b37': ('421d25e68651ba9449cf973798ebf7ab4c0734cfb3e7965af8b7a879788181e2', 556),
    'vs_19a246a56e9d9700': ('0650b3484fb840734d006ea6bcb43891d82aaf855e53f6957d51d2ed89350e9c', 511),
    'vs_44c4a41ca92ae2e3': ('ce598a483ec842635da5d8f1e492f5883f440981f89b1456fd81e7b2942efb96', 556),
    'ps_ca6bfa4a6cca7e2a': ('f2ba5da400ac78cb685f4a2a92e57a441378690af9689b538ef49f07a09d441b', 1328),
    'ps_5e0a10fe752b6140': ('ffd612d6d0dcc9195322cfa71f11b6a76751c49e5b87fe660bfc7360bc0f1a19', 1354),
    'ps_63379470db8d2a86': ('09cf71c72e3adb6157f03802f7ecdbef1ddb07fdb46620d494886b9c25e4f6ad', 1251),
    'ps_68915563dd0aac9a': ('4464ac12b326c249d99c868bcb4cd29e2d7d227bf8f6a5f9b1d8eb9383e22306', 332),
    'ps_d086fde54698070c': ('853ec215ef1b66b60dcdad133ab5dd79188e228dedac47f76dea9ab57ce33c1c', 1277),
    'ps_f17fffd88d134b04': ('56646034e3ced67ead822a25759e4ed0f76e9adaf0c15ea34f550cd5879144bb', 358),
})

BASE_VS = '53a0a641107ed76c'
TOGGLE_VS = ('719856ce0c213220', 'badefd5143b3024f')
FAMILIES = {
    'argon': {'pixels': list(PIXELS)[:6], 'production_status': 'qualified_a56e77e',
              'aliases': ['argon'], 'coefficients': {'diffuse': 0.4000000059604645, 'specular_power': 5, 'cube': 1.0}},
    'shared_default': {'pixels': list(PIXELS)[6:12], 'production_status': 'qualified_24930b5',
                       'aliases': ['khaak', 'teladi', 'teladi_nodiff', 'xenon'],
                       'coefficients': {'diffuse': 0.5, 'specular_power': 6, 'cube': 0.5}},
}
FAMILIES['argon_bump'] = {'pixels': list(BUMP_PIXELS), 'production_status': 'qualified_df4dc09',
                          'aliases': ['argon'], 'technique': 'BUMPMAP',
                          'coefficients': {'diffuse': 0.4000000059604645, 'specular_power': 5, 'cube': 1.0}}
PIXEL_FAMILY = {key: family for family, record in FAMILIES.items() for key in record['pixels']}
PAIRS = {(BASE_VS, key) for family, record in FAMILIES.items() if family != 'argon_bump' for key in record['pixels'][:2]} | {
    (vs, key) for vs in TOGGLE_VS for family, record in FAMILIES.items() if family != 'argon_bump' for key in record['pixels'][2:]}

PAIRS |= {(BUMP_VERTICES[0], key) for key in list(BUMP_PIXELS)[:2]} | {
    (vs, key) for vs in BUMP_VERTICES[1:] for key in list(BUMP_PIXELS)[2:]}


# Reviewed original sites for complete SM3 Split DEFAULT and standard lighting.
EXTENDED_PIXELS = {
    '02606104fa59fb29': ((1117, 1104, 1163, 1150), 1137, 1126, {5: (1141,)}, 1172),
    '0c1f3f0f440e4a0c': ((1306, 1145, 1269, 1348, 1302), 1323, 1294, {5: (1259, 1264), 7: (1218, 1251)}, 1357),
    '1d638938d93421b3': ((1143, 1130, 1189, 1176), 1163, 1152, {5: (1167,)}, 1198),
    '462342e3e5781384': ((1187, 1165, 1232, 1219), 1207, 1196, {5: (1160, 1173), 7: (1148, 1156)}, 1241),
    '4f052209611387f0': ((1265, 1130, 1236, 1308, 1261), 1282, 1253, {5: (1294,)}, 1317),
    '55826dc176afe464': ((291, 279, 321, 308), None, 288, {2: (299,)}, 330),
    '64bac8bb307eb896': ((1332, 1148, 1295, 1374, 1328), 1349, 1320, {5: (1285, 1290), 7: (1244, 1277)}, 1383),
    '789449ffd931d23e': ((1239, 1127, 1210, 1282, 1235), 1256, 1227, {5: (1268,)}, 1291),
    '7c83ed50c9894e44': ((1226, 1209, 1280, 1267), 1248, 1252, {5: (1204, 1217), 7: (1192, 1200)}, 1289),
    '827d8d2d617bedce': ((1213, 1191, 1258, 1245), 1233, 1222, {5: (1186, 1199), 7: (1174, 1182)}, 1267),
    '99153c144030c396': ((1295, 1145, 1258, 1337, 1291), 1312, 1283, {5: (1248, 1253), 7: (1207, 1240)}, 1346),
    'abf3c0fad53456d8': ((331, 219, 302, 357, 327), None, 319, {2: (343,)}, 366),
    'b0f9313b77cc78ee': ((1228, 1127, 1199, 1271, 1224), 1245, 1216, {5: (1257,)}, 1280),
    'bd4d51c08486c6e0': ((217, 196, 238, 225), None, 209, {2: (212,)}, 247),
    'c1452981fd0bff64': ((1321, 1148, 1284, 1363, 1317), 1338, 1309, {5: (1274, 1279), 7: (1233, 1266)}, 1372),
    'cf449bcb069aec4f': ((363, 228, 334, 389, 359), None, 351, {2: (375,)}, 398),
    'd514bf852d8a9c58': ((1254, 1130, 1225, 1297, 1250), 1271, 1242, {5: (1283,)}, 1306),
    'db644b73b68c0547': ((1159, 1155, 1214, 1201), 1181, 1185, {5: (1192,)}, 1223),
    'de2dd381fa64193d': ((249, 228, 270, 257), None, 241, {2: (244,)}, 279),
    'dff6a3d360603fa2': ((320, 219, 291, 346, 316), None, 308, {2: (332,)}, 355),
    'e70adc744a38ca59': ((1252, 1235, 1306, 1293), 1274, 1278, {5: (1230, 1243), 7: (1218, 1226)}, 1315),
    'f1d14a7dbf7c6173': ((352, 228, 323, 378, 348), None, 340, {2: (364,)}, 387),
    'f6a501717c3e5ca8': ((265, 253, 295, 282), None, 262, {2: (273,)}, 304),
    'ff32b602a271c327': ((1185, 1181, 1240, 1227), 1207, 1211, {5: (1218,)}, 1249),
}
ORIGINALS.update({
    'ps_02606104fa59fb29': ('873651c23eaf73e286da394b8b5436f8f2459f18c17fd015f900f6e59c799456', 1181),
    'ps_0c1f3f0f440e4a0c': ('f3bb3510fa2b09cdfae2356202c18b51086308370a7cd91167ada4515f3e1ff3', 1366),
    'ps_1d638938d93421b3': ('fcf25429f17d14e760a8ff29f5d85d35b7847c2b89b26b02f4179b3050cdeb93', 1207),
    'ps_462342e3e5781384': ('aef282706e75e9067803c175fc24d04ea452b0e660c0c1c6e446ce9e76e6e08f', 1250),
    'ps_4f052209611387f0': ('00a41a4373189771936f7ac83e7402fd4de031e82d567f003e5304dc40b8c399', 1326),
    'ps_55826dc176afe464': ('2854c19830162027bbd84eee32c9a3c8230e53858dc2d9f03f7d1af292c0afc7', 339),
    'ps_64bac8bb307eb896': ('84eeded40acc811b109221fe5c7e1b5a898f3b9a34a1b4685937a24f6b1bc4a7', 1392),
    'ps_789449ffd931d23e': ('8c91873e3ddea1dedd53ab4986359e3e3ae068f2190c25fa54291b8a0adf7626', 1300),
    'ps_7c83ed50c9894e44': ('a171c7d7e3dcdfc87816fc651bf93918399594ec457ce1822dcd47a0cff2374a', 1298),
    'ps_827d8d2d617bedce': ('e206e532d3887f3ade972ca638a6480ca71d65d99825162362981da46d9b51b8', 1276),
    'ps_99153c144030c396': ('09cef378940fd33685ae5db4c914d055fc06aaf270f36c04b6714057692efe43', 1355),
    'ps_abf3c0fad53456d8': ('6151df95b22e802cf10cfc3576639e1a193518daa57c429e2f6a33e79c7820b6', 375),
    'ps_b0f9313b77cc78ee': ('3a99cad79bb9dbda01722c359edf6fe735ba4d2b6b14ee956106c3e9baf0a12f', 1289),
    'ps_bd4d51c08486c6e0': ('6327ca5d850180c635efec2a75dc11d7a559abe7bd6ec85c82ec1d8913e91d27', 256),
    'ps_c1452981fd0bff64': ('f9e660057d8f01271ff0ba08465b6e788b2c1bc62b66b9f33892675a5a4290db', 1381),
    'ps_cf449bcb069aec4f': ('df47b99fa5e10086084c807bcb975efe482de077d74909bdae176640bcc70e38', 407),
    'ps_d514bf852d8a9c58': ('06596e9cf8239e99b6ee9d98ed6a2aaa87fedeab8e1ba50936e7ea1c5e4343d6', 1315),
    'ps_db644b73b68c0547': ('09a6754d9a1956bdbd274c0ffab56261c698e430bbd4356ededb56f43c3c51f0', 1232),
    'ps_de2dd381fa64193d': ('69cc10884d902d1abde929bc5fd5fe7a98edb5fe9df28cc73736a41a9485ffd2', 288),
    'ps_dff6a3d360603fa2': ('09fbe59c69a78370eca4eea1ab7302305fe659e70971f60bf809c2975cf25b9f', 364),
    'ps_e70adc744a38ca59': ('b53cbb5b14b17f2e2f61a52687f753cc47cd48fcad6f8f79084f0de05657d59e', 1324),
    'ps_f1d14a7dbf7c6173': ('b5d4d3a813c52386f6b7d40fd423e979c1feb9cf3ea2c9ae8f5ffcdb9e41d75b', 396),
    'ps_f6a501717c3e5ca8': ('1a990338c08b5f5699a1d117f0daafcaa2c2edaa6902b45a56b687599481595a', 313),
    'ps_ff32b602a271c327': ('f883fd536225ec4bace5f3ad808db85f80a85f773e999a661d1ffd900a14d7d9', 1258),
    'vs_494fe349b8bc12ec': ('8a7049b1fab64b40e5a667350c21c55c8155d614bd5b7a3465bba6713594c2c7', 526),
})
FAMILIES['split_default'] = {'pixels': ['462342e3e5781384', '827d8d2d617bedce', '02606104fa59fb29', '1d638938d93421b3', 'bd4d51c08486c6e0', 'de2dd381fa64193d'], 'production_status': 'offline_sites_verified', 'aliases': ['split'], 'technique': 'DEFAULT', 'coefficients': {'diffuse': 0.5, 'specular_power': 10, 'cube': 1.0}}
PAIRS |= {('53a0a641107ed76c', key) for key in FAMILIES['split_default']['pixels'][:2]} | {(vs, key) for vs in TOGGLE_VS for key in FAMILIES['split_default']['pixels'][2:]}
FAMILIES['standard_default'] = {'pixels': ['7c83ed50c9894e44', 'e70adc744a38ca59', 'db644b73b68c0547', 'ff32b602a271c327', 'f6a501717c3e5ca8', '55826dc176afe464'], 'production_status': 'offline_sites_verified', 'aliases': ['standard_lighting'], 'technique': 'DEFAULT', 'coefficients': {'source': 'application', 'diffuse': 'g_MatDiffuseStrength', 'specular': 'g_MatSpecularStrength', 'specular_power': 'g_MatSpecularPower', 'cube': 'g_MatReflectionStrength', 'defaults': {'diffuse': 1.0, 'specular': 1.0, 'specular_power': 10.0, 'cube': 1.0}}}
PAIRS |= {('494fe349b8bc12ec', key) for key in FAMILIES['standard_default']['pixels'][:2]} | {(vs, key) for vs in TOGGLE_VS for key in FAMILIES['standard_default']['pixels'][2:]}
FAMILIES['standard_bump'] = {'pixels': ['0c1f3f0f440e4a0c', '64bac8bb307eb896', '789449ffd931d23e', '4f052209611387f0', 'abf3c0fad53456d8', 'cf449bcb069aec4f'], 'production_status': 'offline_sites_verified', 'aliases': ['standard_lighting'], 'technique': 'BUMPMAP', 'coefficients': {'source': 'application', 'diffuse': 'g_MatDiffuseStrength', 'specular': 'g_MatSpecularStrength', 'specular_power': 'g_MatSpecularPower', 'cube': 'g_MatReflectionStrength', 'defaults': {'diffuse': 1.0, 'specular': 1.0, 'specular_power': 10.0, 'cube': 1.0}}}
PAIRS |= {('4944d81dfe531b37', key) for key in FAMILIES['standard_bump']['pixels'][:2]} | {(vs, key) for vs in BUMP_VERTICES[1:] for key in FAMILIES['standard_bump']['pixels'][2:]}
FAMILIES['standard_bump_low'] = {'pixels': ['99153c144030c396', 'c1452981fd0bff64', 'b0f9313b77cc78ee', 'd514bf852d8a9c58', 'dff6a3d360603fa2', 'f1d14a7dbf7c6173'], 'production_status': 'offline_sites_verified', 'aliases': ['standard_lighting'], 'technique': 'BUMPMAP_LOW', 'coefficients': {'source': 'application', 'diffuse': 'g_MatDiffuseStrength', 'specular': 'g_MatSpecularStrength', 'specular_power': 'g_MatSpecularPower', 'cube': 'g_MatReflectionStrength', 'defaults': {'diffuse': 1.0, 'specular': 1.0, 'specular_power': 10.0, 'cube': 1.0}}}
PAIRS |= {('4944d81dfe531b37', key) for key in FAMILIES['standard_bump_low']['pixels'][:2]} | {(vs, key) for vs in BUMP_VERTICES[1:] for key in FAMILIES['standard_bump_low']['pixels'][2:]}
PIXEL_FAMILY.update({key: family for family, record in FAMILIES.items() for key in record['pixels']})
EXTENDED_BUMP_PIXELS = {key: value for key, value in EXTENDED_PIXELS.items() if 'bump' in PIXEL_FAMILY[key]}
ALL_PIXELS = dict(PIXELS, **BUMP_PIXELS, **EXTENDED_PIXELS)
ALL_BUMP_PIXELS = dict(BUMP_PIXELS, **EXTENDED_BUMP_PIXELS)


# Complete remaining conventional SM3 hull contracts; no new vertex identities.
HULL_PIXELS = {
    '1ed1bf0fdec00e1a': ((1220, 1080, 1187, 1263, 1216), 1237, 1208, {5: (1249,)}, 1272),
    '1f26d41bcb7dac1e': ((1272, 1095, 1235, 1314, 1268), 1289, 1260, {5: (1225, 1230), 7: (1185, 1217)}, 1323),
    '2b04461d0dae038b': ((292, 175, 259, 318, 288), None, 280, {2: (304,)}, 327),
    '78963cdc7c710e04': ((1194, 1077, 1161, 1237, 1190), 1211, 1182, {5: (1223,)}, 1246),
    'acc83ed2509d84a1': ((318, 178, 285, 344, 314), None, 306, {2: (330,)}, 353),
    'bdcdb3ab996ae4e0': ((1298, 1098, 1261, 1340, 1294), 1315, 1286, {5: (1251, 1256), 7: (1211, 1243)}, 1349),
    '22cc5b05a55ef61e': ((310, 178, 277, 332, 306), None, 298, {2: (318,)}, 341),
    '3006f8030a467739': ((1256, 1095, 1219, 1294, 1252), 1273, 1244, {5: (1209, 1214), 7: (1168, 1201)}, 1303),
    '769c3814fc0efba8': ((284, 175, 251, 306, 280), None, 272, {2: (292,)}, 315),
    'd6e8bdde0e4c515f': ((1282, 1098, 1245, 1320, 1278), 1299, 1270, {5: (1235, 1240), 7: (1194, 1227)}, 1329),
    'e5ea78b8b0b0fe07': ((1186, 1077, 1153, 1225, 1182), 1203, 1174, {5: (1211,)}, 1234),
    'f42202faf57a3c89': ((1212, 1080, 1179, 1251, 1208), 1229, 1200, {5: (1237,)}, 1260),
    '3755809bd40afc13': ((1142, 1129, 1188, 1175), 1162, 1151, {5: (1166,)}, 1197),
    '61418505e5d8f998': ((222, 201, 243, 230), None, 214, {2: (217,)}, 252),
    '91b6c09eb47f8555': ((1219, 1206, 1264, 1251), 1239, 1228, {5: (1196, 1201), 7: (1156, 1188)}, 1273),
    'b5f1d4145171026b': ((248, 227, 269, 256), None, 240, {2: (243,)}, 278),
    'cc09f17db377fd9e': ((1116, 1103, 1162, 1149), 1136, 1125, {5: (1140,)}, 1171),
    'ef2bf556f207b8bd': ((1193, 1180, 1238, 1225), 1213, 1202, {5: (1170, 1175), 7: (1130, 1162)}, 1247),
    '042c9ae16f41feff': ((1191, 1077, 1158, 1230, 1187), 1208, 1179, {5: (1216,)}, 1239),
    '3602b05ce11ca6ff': ((1268, 1095, 1235, 1306, 1264), 1285, 1256, {5: (1225, 1230), 7: (1185, 1217)}, 1315),
    '5c823b8507fa1442': ((283, 169, 250, 305, 279), None, 271, {2: (291,)}, 314),
    '68f0dd6791fd7d3d': ((1217, 1080, 1184, 1256, 1213), 1234, 1205, {5: (1242,)}, 1265),
    '8e58ac79b59b02b1': ((1294, 1098, 1261, 1332, 1290), 1311, 1282, {5: (1251, 1256), 7: (1211, 1243)}, 1341),
    'a6e1328c0bb3f401': ((315, 178, 282, 337, 311), None, 303, {2: (323,)}, 346),
}
ORIGINALS.update({
    'ps_1ed1bf0fdec00e1a': ('b144ea7b37fa3da24e8eacf4577beedf8608f1ee833e79e050ceb78c33432ecc', 1281),
    'ps_1f26d41bcb7dac1e': ('6b1d36a1d68a67c0c123dee76810f93cb9e59452ff2bf6afa0ac7ade0d78060b', 1332),
    'ps_2b04461d0dae038b': ('b711af0319a875d9ce8e37fd1be97730205fc84ced93afecdc26fb524c541a69', 336),
    'ps_78963cdc7c710e04': ('1bd7cb2ad8a48f55fa521caf72f700dbe0416cd4515736290568e9e3a699f678', 1255),
    'ps_acc83ed2509d84a1': ('743d1f2f3437a83a0ce88f265776e5b4438ff9ad2149835711a70ea7b3dad963', 362),
    'ps_bdcdb3ab996ae4e0': ('878640333bd196c9a213715e046cdf2840412deb43c8d265ff8d1828cc466236', 1358),
    'ps_22cc5b05a55ef61e': ('fc02f5734a8b555e34eb08bcfe04c379a4531e0c3b44a726ae9a10d145a8b7f5', 350),
    'ps_3006f8030a467739': ('a6138aae0b3035b28e5b394f9812e4dbc60127338101764cd6b2abd064bf63c6', 1312),
    'ps_769c3814fc0efba8': ('36e8bd790bac0395981beeaec7da54801a0baff7a8dee1480f7840a5b2445c5b', 324),
    'ps_d6e8bdde0e4c515f': ('bfc647a9dc27604f79047ca5c3158ce544f342ea7fd7b89223fece7dd81df3e9', 1338),
    'ps_e5ea78b8b0b0fe07': ('dbd52a8f2f85955006e3058cb3aeae754519845e2aa4466b27d5c80c49ba4d2b', 1243),
    'ps_f42202faf57a3c89': ('6650ca483e51e1ae9e752e1d4d1a7c19f68d3980bbaede429ade5452ae7b0ef3', 1269),
    'ps_3755809bd40afc13': ('361697166d13575c9c84609cfded80967c07cde985807ba3a63755d3fa61f376', 1206),
    'ps_61418505e5d8f998': ('420fa086c1fc623e1d70e55cea24bac4391129e50080fbd11c01cfe06d57514e', 261),
    'ps_91b6c09eb47f8555': ('c32e7ac4e383027babe5e819dbb1ce643234c1721e5a4ab522bbcbc709731f9f', 1282),
    'ps_b5f1d4145171026b': ('b52a137ea6415c75dd7d84e6f9fa8e1dde9456c80802865aa0a035b836611cc0', 287),
    'ps_cc09f17db377fd9e': ('38edca2c0f17dd2a3ef0b9e01e590fa7760cf76bc34b9445d5448d0159d10c47', 1180),
    'ps_ef2bf556f207b8bd': ('37ab024dcc9f252e7558c359a02ff4cfc45a82c8e4322821b2956773c00e354b', 1256),
    'ps_042c9ae16f41feff': ('38813e508bcbc00d21cd7e3a54a851933a03ff629d2cc1906a8752023e024a48', 1248),
    'ps_3602b05ce11ca6ff': ('b075ec5f87410f8ae7f1d7e0045528506d725eb5c89283c8579b6619e431982e', 1324),
    'ps_5c823b8507fa1442': ('8ce6dc84ed02a20ce2c44033d45ec2fb6860f979a5f70bbd915f0f3feb888383', 323),
    'ps_68f0dd6791fd7d3d': ('888d2654a8fa543b73f561bc77596aeca0d7721a790d95eab3caf43ca776937d', 1274),
    'ps_8e58ac79b59b02b1': ('93c10b47b4cae8aecf7b6ee242bee50f4c19987d2ef6838327503c56b8f8d5fd', 1350),
    'ps_a6e1328c0bb3f401': ('e060a279d7a8afe1226f252eadc61dcefb380ef561ce0ae7c01592454e545ebe', 355),
})
FAMILIES['shared_bump'] = {'pixels': ['1f26d41bcb7dac1e', 'bdcdb3ab996ae4e0', '78963cdc7c710e04', '1ed1bf0fdec00e1a', '2b04461d0dae038b', 'acc83ed2509d84a1'], 'production_status': 'offline_sites_verified', 'aliases': ['khaak', 'teladi', 'teladi_nodiff', 'xenon'], 'technique': 'BUMPMAP', 'coefficients': {'diffuse': 0.5, 'specular_power': 6, 'cube': 0.5}}
PAIRS |= {('4944d81dfe531b37', key) for key in FAMILIES['shared_bump']['pixels'][:2]} | {(vs, key) for vs in BUMP_VERTICES[1:] for key in FAMILIES['shared_bump']['pixels'][2:]}
FAMILIES['split_bump'] = {'pixels': ['3006f8030a467739', 'd6e8bdde0e4c515f', 'e5ea78b8b0b0fe07', 'f42202faf57a3c89', '769c3814fc0efba8', '22cc5b05a55ef61e'], 'production_status': 'offline_sites_verified', 'aliases': ['split'], 'technique': 'BUMPMAP', 'coefficients': {'diffuse': 0.5, 'specular_power': 10, 'cube': 1.0}}
PAIRS |= {('4944d81dfe531b37', key) for key in FAMILIES['split_bump']['pixels'][:2]} | {(vs, key) for vs in BUMP_VERTICES[1:] for key in FAMILIES['split_bump']['pixels'][2:]}
FAMILIES['terran_default'] = {'pixels': ['ef2bf556f207b8bd', '91b6c09eb47f8555', 'cc09f17db377fd9e', '3755809bd40afc13', '61418505e5d8f998', 'b5f1d4145171026b'], 'production_status': 'offline_sites_verified', 'aliases': ['terran'], 'technique': 'DEFAULT', 'coefficients': {'diffuse': 1.0, 'specular_power': 5, 'cube': 1.0}}
PAIRS |= {('53a0a641107ed76c', key) for key in FAMILIES['terran_default']['pixels'][:2]} | {(vs, key) for vs in TOGGLE_VS for key in FAMILIES['terran_default']['pixels'][2:]}
FAMILIES['terran_bump'] = {'pixels': ['3602b05ce11ca6ff', '8e58ac79b59b02b1', '042c9ae16f41feff', '68f0dd6791fd7d3d', '5c823b8507fa1442', 'a6e1328c0bb3f401'], 'production_status': 'offline_sites_verified', 'aliases': ['terran'], 'technique': 'BUMPMAP', 'coefficients': {'diffuse': 1.0, 'specular_power': 5, 'cube': 1.0}}
PAIRS |= {('4944d81dfe531b37', key) for key in FAMILIES['terran_bump']['pixels'][:2]} | {(vs, key) for vs in BUMP_VERTICES[1:] for key in FAMILIES['terran_bump']['pixels'][2:]}
PIXEL_FAMILY.update({key: family for family, record in FAMILIES.items() for key in record['pixels']})
ALL_PIXELS.update(HULL_PIXELS)
ALL_BUMP_PIXELS.update({key: value for key, value in HULL_PIXELS.items() if PIXEL_FAMILY[key] != 'terran_default'})


# Asteroid has its own UV, light and resource contracts. These descriptive
# operand proofs intentionally do not broaden the conventional hull matcher.
ASTEROID_VERTICES = {
    # bump, base (separate detail UV), point loop, first executable DWORD
    'b0602757fce6e870': (False, True, True, 335),
    '0c223ad11bce02d5': (False, False, True, 332),
    '233d17d26ce0c1fc': (False, False, False, 314),
    '167eb2d5629ab9d3': (True, True, True, 347),
    '12b8a13f13fe8cfe': (True, False, False, 326),
    '330ceb9dd874ede2': (True, False, True, 344),
}
ASTEROID_PIXELS = {
    '517540ae6d5e5410': (False, True, 257),
    '7a0c3388065bb08d': (False, False, 237),
    'd44db87778a43b61': (True, True, 274),
    '550c2a4d4d3ed70f': (True, False, 254),
}
ASTEROID_PAIRS = {
    ('b0602757fce6e870', '517540ae6d5e5410'),
    ('0c223ad11bce02d5', '7a0c3388065bb08d'),
    ('233d17d26ce0c1fc', '7a0c3388065bb08d'),
    ('167eb2d5629ab9d3', 'd44db87778a43b61'),
    ('12b8a13f13fe8cfe', '550c2a4d4d3ed70f'),
    ('330ceb9dd874ede2', '550c2a4d4d3ed70f'),
}

ORIGINALS.update({
    'vs_b0602757fce6e870': ('33cef191db2668aadef869a140b2185b1576212535bdae7d607da49573d8d785', 520),
    'vs_0c223ad11bce02d5': ('573d688222bcdee8234d60123199d5fc7b65158e8201b3e99a37bd254a697dd3', 517),
    'vs_233d17d26ce0c1fc': ('0101713ccc59fd34de25637bdfe903d8806e8f22fcd13e0810460a518ada2111', 472),
    'vs_167eb2d5629ab9d3': ('fe2bc3e8b3a76315a4225d409fcf0a25b3d2eb456745dd3dd3bcf0da81e222c6', 566),
    'vs_12b8a13f13fe8cfe': ('2bbd1f6d8d88dc72663e3b4408d0c3d12fa89127dbaa8cd95c682ee2b6198bac', 518),
    'vs_330ceb9dd874ede2': ('77280b11612c3c8c30d02e75385b292d5dd0d580c3663ba527224c6daed148c5', 563),
    'ps_517540ae6d5e5410': ('e863f6196bad66465442a9a27923953ab11b71be6ab60d325237e2f5e2f85329', 397),
    'ps_7a0c3388065bb08d': ('ac32e61edcf09cd6e3d5a2d05524131754e128b2fd2530103bdcb6bb0cfca8e7', 323),
    'ps_d44db87778a43b61': ('481ff206a4d729023fb44f930d6a30f9972318e9d1c24cab72b11869eb856dc8', 448),
    'ps_550c2a4d4d3ed70f': ('4cabe57e615dae67307a2c875fdef49c1211b2235c80b16c6b938e358128c900', 374),
})
for _family, _bump in (('asteroid_default', False), ('asteroid_bump', True)):
    FAMILIES[_family] = {'pixels':[key for key,value in ASTEROID_PIXELS.items() if value[0] == _bump],
                         'production_status':'implemented_pending_GPU', 'aliases':['asteroid'],
                         'technique':'BUMPMAP' if _bump else 'DEFAULT',
                         'coefficients':{'diffuse':1.0,'specular_power':3,'specular_outer_scale':1.0,'grazing_scale':3.0,'cube':0.0}}
    PIXEL_FAMILY.update({key:_family for key in FAMILIES[_family]['pixels']})
PAIRS |= ASTEROID_PAIRS


def asteroid_chain(decoded, start, precision=()):
    """Collect a bounded, manually specified family chain with exact lengths."""
    rows = []
    cursor = start
    def emit(op, dest, sources, modifiers=None):
        nonlocal cursor
        row = expect(decoded, cursor, op, dest, sources,
                     precision if modifiers is None else modifiers)
        rows.append(row)
        cursor = row['end_dword']
        return row['instruction_dword']
    return rows, emit


def asteroid_complete_chain(decoded, rows):
    executable = [at for at, row in decoded.items() if row['item']['opcode'] not in (31, 81)]
    require(executable == [row['instruction_dword'] for row in rows],
            'asteroid complete executable inventory changed')
    require(all(not row['item']['predicated'] and not row['item']['coissued'] for row in decoded.values()),
            'asteroid predication/coissue changed')


def prove_asteroid_vertex(decoded, key):
    """Prove complete geometry, point-light, UV/basis, fog and alpha producers."""
    bump, base, loop, start = ASTEROID_VERTICES[key]
    rows, emit = asteroid_chain(decoded, start)
    c = 'c42' if loop else 'c21'
    literals = [literal_component(decoded, c, lane, value) for lane, value in zip('xyzw', (1., 0., 3., 0.))]
    position = 'r2' if bump else 'r1' if loop else 'r0'
    world = 'r3' if bump and loop else 'r4' if bump else 'r2'
    normal = 'r4' if bump and loop else 'r3'
    normal_out = 'o5' if bump and base else 'o4' if bump or not base else 'o5'
    view_out = 'o4' if base else 'o3'
    # All geometry preparation keeps its native homogeneous W (including
    # normal/basis preparations); material work cannot borrow these live temps.
    if loop:
        emit('mad',(position,'xyzw'),[('v0','xyzx'),(c,'xxxy'),(c,'yyyx')])
        emit('dp4',(world,'z'),[(position,'xyzw'),('c30','xyzw')])
        emit('dp4',(world,'x'),[(position,'xyzw'),('c28','xyzw')])
        emit('mad',('r0','xyzw'),[('v2','xyzx'),(c,'xxxy'),(c,'yyyx')])
        emit('dp4',(world,'y'),[(position,'xyzw'),('c29','xyzw')])
        for lane, matrix in [('z',33),('x',31),('y',32)]:
            emit('dp4',(normal,lane),[('r0','xyzw'),(f'c{matrix}','xyzw')])
        emit('mov',('r0','xyz'),[(c,'yyyy')])
        counter = emit('mov',('r0','w'),[(c,'yyyy')])
        rep = emit('rep',None,[('i0','xyzw')])
        atten, scalar, cosine = ('r1','r1','r3') if bump else ('r4','r2','r3')
        emit('mul',(scalar,'w'),[('r0','wwww'),(c,'zzzz')])
        address = emit('mova',('a0','w'),[(scalar,'wwww')])
        relative_position = emit('add',('r5','xyz'),[(world,'xyzw',1),('c0','xyzw',0,'a0','w')])
        emit('dp3',(atten,'z'),[('r5','xyzw'),('r5','xyzw')])
        emit('rsq',(scalar,'w'),[(atten,'zzzz')])
        emit('mul',('r5','xyz'),[('r5','xyzw'),(scalar,'wwww')])
        emit('mul',(atten,'y'),[(atten,'zzzz'),(scalar,'wwww')])
        emit('mov',(atten,'x'),[(c,'xxxx')])
        emit('dp3',(cosine,'w'),[(normal,'xyzw'),('r5','xyzw')],('saturate',))
        relative_atten = emit('dp3',(scalar,'w'),[('c2','xyzw',0,'a0','w'),(atten,'xyzw')])
        emit('rcp',(scalar,'w'),[(scalar,'wwww')],('saturate',))
        point = emit('mul',(atten,'xyz'),[(cosine,'wwww'),('c1','xyzw',0,'a0','w')])
        emit('mad',('r0','xyz'),[(atten,'xyzw'),(scalar,'wwww'),('r0','xyzw')])
        increment = emit('add',('r0','w'),[('r0','wwww'),(c,'xxxx')])
        endrep = emit('endrep',None,[])
        material = emit('add',('o1','xyz'),[('r0','xyzw'),('c40','xyzw')])
        require(lane_writes(decoded,'a0') == [address], 'asteroid point address overwritten')
        require(lane_writes(decoded,'r0','w',counter-1,endrep+1) == [counter,increment], 'asteroid loop counter overwritten')
        relative = [(at,s['name']) for at,row in decoded.items() for s in row['sources'] if s['relative']]
        require(relative == [(relative_position,'c0'),(relative_atten,'c2'),(point,'c1')], 'asteroid relative point inventory changed')
        point_proof = {'rep_dword':rep, 'endrep_dword':endrep, 'counter_initialization_dword':counter,
                       'counter_increment_dword':increment, 'address_write_dword':address,
                       'verified_stride':3, 'runtime_count_range_required':[0,8]}
    else:
        uvprep = 'r0' if bump else 'r1'
        light = 'r1' if bump else 'r3'
        scalar = 'r0' if bump else 'r1'
        emit('mov',('r5','x'),[(c,'xxxx')])
        emit('mad',(position,'xyzw'),[('v0','xyzx'),(c,'xxxy'),(c,'yyyx')])
        emit('mad',(uvprep,'xyz'),[('v1','xyxw'),(c,'xxyw'),(c,'yyxw')])
        for lane,matrix in [('z',9),('x',7),('y',8)]:
            emit('dp4',(world,lane),[(position,'xyzw'),(f'c{matrix}','xyzw')])
        emit('dp3',('r6','y'),[(uvprep,'xyzw'),('c17','xyzw')])
        emit('add',(light,'xyz'),[(world,'xyzw',1),('c4','xyzw')])
        emit('dp3',('r6','x'),[(uvprep,'xyzw'),('c16','xyzw')])
        emit('dp3',('r5','z'),[(light,'xyzw'),(light,'xyzw')])
        emit('mov',('o2','xy'),[('r6','xyzw')])
        emit('rsq',('r1' if bump else 'r2','w'),[('r5','zzzz')])
        emit('mul',('r5','y'),[('r5','zzzz'),('r1' if bump else 'r2','wwww')])
        emit('mad',(uvprep,'xyzw'),[('v2','xyzx'),(c,'xxxy'),(c,'yyyx')])
        emit('mul',('r1' if bump else 'r4','xyz'),[(light,'xyzw'),('r1' if bump else 'r2','wwww')])
        for lane,matrix in [('z',12),('x',10),('y',11)]:
            emit('dp4',(normal,lane),[(uvprep,'xyzw'),(f'c{matrix}','xyzw')])
        emit('dp3',(scalar,'w'),[('c6','xyzw'),('r5','xyzw')])
        emit('dp3',(scalar,'z'),[(normal,'xyzw'),('r1' if bump else 'r4','xyzw')],('saturate',))
        emit('rcp',(scalar,'w'),[(scalar,'wwww')],('saturate',))
        point = emit('mul',(scalar,'xyz'),[(scalar,'zzzz'),('c5','xyzw')])
        material = emit('mad',('o1','xyz'),[(scalar,'xyzw'),(scalar,'wwww'),('c19','xyzw')])
        require(not any(s['relative'] for row in decoded.values() for s in row['sources']), 'asteroid fixed point became relative')
        point_proof = {'model':'fixed_single_point','relative_sources':0}
    point_end = len(rows)
    tangent_out, binormal_out = ('o6','o7') if base else ('o5','o6')
    it = 31 if loop else 10
    basis_start = None
    if bump:
        basis_start = emit('mad',('r1','xyzw'),[('v4','xyzx'),(c,'xxxy'),(c,'yyyx')])
        if not base: emit('dp4',(tangent_out,'z'),[('r1','xyzw'),(f'c{it+2}','xyzw')])
        emit('mad',('r0','xyzw'),[('v3','xyzx'),(c,'xxxy'),(c,'yyyx')])
        if base: emit('dp4',(tangent_out,'z'),[('r1','xyzw'),(f'c{it+2}','xyzw')])
        emit('dp4',(binormal_out,'z'),[('r0','xyzw'),(f'c{it+2}','xyzw')])
    emit('mov',(normal_out,'xyz'),[(normal,'xyzw')])
    matrix = 24 if loop else 0
    position_sites = [emit('dp4',('o0',lane),[(position,'xyzw'),(f'c{matrix+i}','xyzw')]) for i,lane in enumerate('xyz')]
    uvprep = 'r2' if bump and base else 'r4' if bump else 'r0'
    uv = 'r4' if bump and base else 'r5' if bump else 'r1'
    view = 'r2' if bump else 'r0'
    if loop:
        if base: position_sites.append(emit('dp4',('o0','w'),[(position,'xyzw'),('c27','xyzw')]))
        emit('mad',(uvprep,'xyz'),[('v1','xyxw'),(c,'xxyw'),(c,'yyxw')])
        if not base: position_sites.append(emit('dp4',('o0','w'),[(position,'xyzw'),('c27','xyzw')]))
        emit('dp3',(uv,'y'),[(uvprep,'xyzw'),('c38','xyzw')])
        emit('dp3',(uv,'x'),[(uvprep,'xyzw'),('c37','xyzw')])
        if base: emit('mov',('o2','xy'),[(uv,'xyzw')])
        for lane,index in [('y',35),('x',34),('z',36)]: emit('mov',(view,lane),[(f'c{index}','wwww')])
        if base: emit('mul',('o3','xy'),[(uv,'xyzw'),(c,'zzzz')])
        else: emit('mov',('o2','xy'),[(uv,'xyzw')])
        view_start = emit('add',(view,'xyz'),[(world,'xyzw',1),(view,'xyzw')])
    else:
        for lane,index in [('x',13),('y',14),('z',15)]: emit('mov',('r3' if bump else 'r1',lane),[(f'c{index}','wwww')])
        position_sites.append(emit('dp4',('o0','w'),[(position,'xyzw'),('c3','xyzw')]))
        view_start = emit('add',(view,'xyz'),[(world,'xyzw',1),('r3' if bump else 'r1','xyzw')])
        uv = 'r6'
    fog = 'c41' if loop else 'c20'
    alpha = 'c39' if loop else 'c18'
    emit('if',None,[('b0','xyzw')])
    emit('dp3',(view,'w'),[(view,'xyzw'),(view,'xyzw')])
    emit('rsq',(view,'w'),[(view,'wwww')])
    emit('rcp',(view,'w'),[(view,'wwww')])
    emit('mad',(view,'w'),[(fog,'yyyy'),(view,'wwww',1),(fog,'xxxx')],('saturate',))
    alpha_sites = [emit('mul',('o1','w'),[(view,'wwww'),(alpha,'xxxx')])]
    emit('else',None,[])
    alpha_sites.append(emit('mov',('o1','w'),[(alpha,'xxxx')]))
    emit('endif',None,[])
    view_export = emit('mov',(view_out,'xyz'),[(view,'xyzw')])
    if bump:
        for out,reg in [(tangent_out,'r1'),(binormal_out,'r0')]:
            for i,lane in enumerate('xy'): emit('dp4',(out,lane),[(reg,'xyzw'),(f'c{it+i}','xyzw')])
    if not base: emit('mul',('o2','zw'),[(uv,'xyxy'),(c,'zzzz')])
    asteroid_complete_chain(decoded, rows)
    require(lane_writes(decoded,'o1') == [material]+alpha_sites, 'asteroid COLOR0 output inventory changed')
    for lane in 'xyz': no_lane_writes(decoded,view,lane,view_start,view_export)
    position_prep = next(r['instruction_dword'] for r in rows if r['opcode']=='mad' and r['sources'][0]['name']=='v0')
    for lane in 'xyzw': no_lane_writes(decoded,position,lane,position_prep,max(position_sites))
    if bump:
        for reg in ('r0','r1'):
            prep = next(r['instruction_dword'] for r in rows if r['instruction_dword'] >= basis_start and r['destination'] and r['destination']['name']==reg and r['opcode']=='mad')
            last = max(r['instruction_dword'] for r in rows if r['opcode']=='dp4' and r['sources'][0]['name']==reg)
            for lane in 'xyzw': no_lane_writes(decoded,reg,lane,prep,last)
    return {'point_model': 'loop_count_i0_x_0_to_8_stride_3_a0_w' if loop else 'fixed_single_point',
            'point_rgb_dword':point, 'material_emissive_dword':material, 'alpha_dwords':alpha_sites,
            'point_loop':point_proof, 'literal_sites':literals,
            'position_dp4_dwords':position_sites, 'position_source_temporary':int(position[1:]),
            'position_matrix_register':matrix, 'position_producer_dword':position_prep,
            'geometry_and_point_sites':rows[:point_end], 'geometry_uv_and_alpha_sites':rows[point_end:],
            'complete_executable_chain_checked':len(rows), 'view_live_interval':[view_start,view_export],
            'basis_model':'native_v4_tangent_v3_binormal_world_it' if bump else 'geometric_normal',
            'detail_uv_model':'separate_texcoord1' if base else 'texcoord0_zw'}


def prove_asteroid_pixel(decoded, key):
    """Exact native cubic lobe, independent scalar weights and base-only alpha."""
    bump, base, start = ASTEROID_PIXELS[key]
    rows, emit = asteroid_chain(decoded,start,PP)
    c = 'c6' if base else 'c4'
    values = (2.,-1.,1.,3.) if bump else (3.,0.,0.,0.)
    literals = [literal_component(decoded,c,lane,value) for lane,value in zip('xyzw',values)]
    three = (c,'wwww' if bump else 'xxxx')
    v = 3 if base else 2
    if bump:
        emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
        emit('mad',('r1','xy'),[(c,'xxxx'),('r0','wyzw'),(c,'yyyy')])
        emit('dp2add',('r0','w'),[('r1','xyzw'),('r1','xyzw',1),(c,'zzzz')])
        emit('mul',('r0','xyz'),[('r1','yyyy'),(f'v{v+2}','xyzw')])
        emit('rsq',('r0','w'),[('r0','wwww')])
        emit('mad',('r0','xyz'),[('r1','xxxx'),(f'v{v+3}','xyzw'),('r0','xyzw')])
        emit('rcp',('r0','w'),[('r0','wwww')])
        emit('mad',('r1','xyz'),[('r0','wwww'),(f'v{v+1}','xyzw'),('r0','xyzw')])
    normal = emit('nrm',('r0','xyz'),[('r1' if bump else f'v{v+1}','xyzw')])
    normal_count = len(rows)
    direction = 'c2' if base else 'c0'
    emit('dp3',('r0','w'),[(direction,'xyzw',1),('r0','xyzw')])
    emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
    if base and not bump: emit('nrm',('r3','xyz'),[(f'v{v}','xyzw')])
    emit('mad',('r1','xyz'),[('r0','xyzw'),('r0','wwww',1),(direction,'xyzw',1)])
    if bump or not base: emit('nrm',('r3' if base else 'r2','xyz'),[(f'v{v}','xyzw')])
    emit('dp3',('r0','w'),[('r1','xyzw'),('r3' if base else 'r2','xyzw')],PP+('saturate',))
    if base:
        emit('mul',('r1','w'),[('r0','wwww'),('r0','wwww')])
        emit('mul',('r0','w'),[('r0','wwww'),('r1','wwww')])
        emit('dp3',('r1','z'),[('r0','xyzw'),('c2','xyzw')],PP+('saturate',))
        emit('dp3',('r1','y'),[('c0','xyzw',1),('r0','xyzw')])
        emit('mul',('r1','w'),[('r1','zzzz'),three],PP+('saturate',))
        emit('add',('r2','w'),[('r1','yyyy'),('r1','yyyy')])
        emit('mul',('r1','xyz'),[('r1','zzzz'),('c3','xyzw')])
        emit('mad',('r2','xyz'),[('r0','xyzw'),('r2','wwww',1),('c0','xyzw',1)])
        emit('mul',('r3','w'),[('r0','wwww'),('r1','wwww')])
        emit('dp3',('r0','w'),[('r2','xyzw'),('r3','xyzw')],PP+('saturate',))
        last_normal = emit('dp3',('r1','w'),[('r0','xyzw'),('c0','xyzw')],PP+('saturate',))
        emit('mul',('r0','z'),[('r0','wwww'),('r0','wwww')])
        emit('mul',('r0','w'),[('r0','wwww'),('r0','zzzz')])
        emit('mul',('r2','w'),[('r1','wwww'),three],PP+('saturate',))
        emit('mul',('r0','xyz'),[('r3','wwww'),('c3','xyzw')])
        emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
        emit('mad',('r2','xyz'),[('r1','wwww'),('c1','xyzw'),('r1','xyzw')])
        emit('mad',('r1','xyz'),[('r0','wwww'),('c1','xyzw'),('r0','xyzw')])
    else:
        last_normal = emit('dp3',('r1','z'),[('r0','xyzw'),('c0','xyzw')],PP+('saturate',))
        emit('mul',('r0','z'),[('r0','wwww'),('r0','wwww')])
        emit('mul',('r0','w'),[('r0','wwww'),('r0','zzzz')])
        emit('mul',('r0','z'),[('r1','zzzz'),three],PP+('saturate',))
        emit('mul',('r1','w'),[('r0','wwww'),('r0','zzzz')])
    spec = emit('texld',('r0','xyzw'),[('v1','xyzw'),('s2' if bump else 's1','xyzw')])
    emit('mad',('r0','xyz') if base else ('r0','w'),
         [('r1','xyzw'),('r0','xxxx'),('r2','xyzw')] if base else [('r1','wwww'),('r0','xxxx'),('r1','zzzz')])
    clamp = emit('mov',('r1' if base else 'r0','xyz'),[('v0','xyzw')],PP+('saturate',))
    if base: emit('add',('r1','xyz'),[('r0','xyzw'),('r1','xyzw')])
    else: emit('mad',('r1','xyz'),[('r0','wwww'),('c1','xyzw'),('r0','xyzw')])
    detail = emit('texld',('r0','xyzw'),[('v2','xyzw') if base else ('v1','zwzw'),('s3' if bump else 's2','xyzw')])
    detail_weight = emit('mul',('r2','xyz'),[('r0','xyzw'),('c4' if base else 'c2','xxxx')])
    diffuse = emit('texld',('r0','xyzw'),[('v1','xyzw'),('s0','xyzw')])
    base_weight = emit('mad',('r0','xyz'),[('c5' if base else 'c3','xxxx'),('r0','xyzw'),('r2','xyzw')])
    alpha = emit('mul',('oC0','w'),[('r0','wwww'),('v0','wwww')])
    final = emit('mul',('oC0','xyz'),[('r1','xyzw'),('r0','xyzw')])
    asteroid_complete_chain(decoded,rows)
    links = [('r0','xyz',normal,last_normal),('r0','w',diffuse,alpha),('r2','xyz',detail_weight,base_weight)]
    for reg,lanes,a,b in links:
        for lane in lanes: no_lane_writes(decoded,reg,lane,a,b)
    return {'normal_reconstruction_sites':rows[:normal_count], 'normal_encoding':'ag' if bump else 'geometric',
            'normal_channels':{'alpha':f'binormal_v{v+3}','green':f'tangent_v{v+2}','red_blue':'unused'} if bump else {},
            'literal_sites':literals, 'specular_power':3, 'specular_outer_scale':1.0, 'grazing_scale':3.0,
            'alpha_model':'base_alpha_times_vertex_alpha', 'diffuse_alpha_live_interval':[diffuse,alpha],
            'affine_rgb_sites':[], 'complete_executable_chain_checked':len(rows),
            'angular_and_color_sites':rows[normal_count:],
            'verified_live_ranges':[{'register':r,'mask':m,'producer_dword':a,'consumer_dword':b} for r,m,a,b in links],
            'texture_dwords':([diffuse,start,spec,detail] if bump else [diffuse,spec,detail]),
            'clamp_dword':clamp, 'alpha_dword':alpha, 'final_dword':final,
            'detail_weighting':{'base':{'register':'c5' if base else 'c3','component':'x','instruction_dword':base_weight},
                                'detail':{'register':'c4' if base else 'c2','component':'x','instruction_dword':detail_weight},
                                'native_relation':'base=1-detail supplied by opaque preshader; no reinterpretation of scalar strength'}}


def material_color1_proof(profile, stage):
    """Check actual stage DCLs before reserving a separate full-precision COLOR1.

    This certifies semantic availability and the authored declaration contract,
    not backend interpolation behavior or GPU equivalence. Native COLOR0 and
    its partial-precision alpha remain untouched in their original register.
    """
    require(stage in ('vs', 'ps'), 'unknown material semantic stage')
    role = 'output' if stage == 'vs' else 'input'
    declarations = [d for d in profile['declarations'] if d['role'] == role]
    colors = [d for d in declarations if d.get('usage') == 10]
    require(not any(d['usage_index'] == 1 for d in colors), 'material COLOR1 semantic ABI collision')
    return {'original_stage_color_declarations': colors,
            'authored_declaration_contract': {'usage': 'color', 'usage_index': 1,
                                             'mask': 'xyz', 'modifiers': []},
            'authored_rgb_saturate_modifier': False,
            'authored_rgb_partial_precision_modifier': False,
            'native_color0_preserved': True, 'separate_physical_register_required': True,
            'qualification': 'Original DCL availability and authored contract only; COLOR1 GPU behavior remains unqualified.'}


def asteroid_abi(bump, base):
    """Four fixed contracts, checked against the independently derived motion plan."""
    vm, pm, mt, vd, pd, dt, vr, pr, scratch = (
        (8,7,6,9,8,7,10,9,9) if bump and base else
        (7,6,5,8,7,6,9,8,10) if bump else
        (6,5,4,7,6,5,8,7,9) if base else
        (6,5,4,5,4,3,8,7,9))
    return {'vs_motion_output':vm, 'ps_motion_input':pm, 'motion_texcoord':mt,
            'vs_depth_output':vd, 'ps_depth_input':pd, 'depth_texcoord':dt,
            'material_vs_rgb_output':vr, 'material_ps_rgb_input':pr, 'material_rgb_usage':'color', 'material_rgb_usage_index':1,
            'vs_constants':[252,255], 'ps_constants':[216,220], 'ps_motion_temporaries':[5,6,7],
            'material_vs_def_constants':[248,249], 'material_ps_def_constants':[212,213],
            'material_vs_temporaries':[7,8,9], 'material_ps_temporaries':list(range(scratch,scratch+4)),
            'material_rgb_mask':'xyz', 'material_rgb_precision':'full',
            'required_disabled_srgb_sampler_mask':15 if bump else 7, 'current_depth_modes':[False,True]}


def asteroid_declarations(profile, stage, bump, base):
    """Check physical inputs and original centroid/PP masks, not TEXCOORD alone."""
    declarations = profile['declarations']
    if stage == 'vs':
        inputs = [('v0','position'),('v1','texcoord'),('v2','normal')]
        if bump: inputs += [('v3','binormal'),('v4','tangent')]
        expected = [(name,'xyzw',usage,0,[]) for name,usage in inputs]
        expected += [('o0','xyzw','position',0,[]),('o1','xyzw','color',0,[])]
        expected += [('o2','xy' if base else 'xyzw','texcoord',0,['centroid'] if bump and base else [])]
        if base: expected += [('o3','xy','texcoord',1,[])]
        first = 4 if base else 3
        expected += [(f'o{n}','xyz','texcoord',n-2,['centroid'] if base and not bump else [])
                     for n in range(first,first+(4 if bump else 2))]
    else:
        expected = [('v0','xyzw','color',0,list(PP)),
                    ('v1','xy' if base else 'xyzw','texcoord',0,['centroid','partial_precision'] if base and bump else list(PP))]
        if base: expected += [('v2','xy','texcoord',1,list(PP))]
        first = 3 if base else 2
        expected += [(f'v{n}','xyz','texcoord',n-1,['centroid','partial_precision'] if base and not bump else list(PP))
                     for n in range(first,first+(4 if bump else 2))]
        samplers = [(d['name'],d['mask'],d['modifiers'],d['texture_type']) for d in declarations if d['role']=='sampler']
        require(samplers == [(f's{n}','xyzw',[],2) for n in range(4 if bump else 3)], 'asteroid sampler declarations changed')
    actual = [(d['name'],d['mask'],d['usage_name'],d['usage_index'],d['modifiers']) for d in declarations if d['role']!='sampler']
    require(actual == expected, 'asteroid varying declaration changed')


def asteroid_budget(profile, stage, loop, abi):
    material_color1_proof(profile, stage)
    require((abi['material_rgb_usage'], abi['material_rgb_usage_index']) == ('color', 1), 'material COLOR1 ABI changed')
    original_temps = set(profile['temporary_registers'])
    material_temps = set(abi[f'material_{stage}_temporaries'])
    temporal_temps = set(abi['ps_motion_temporaries']) if stage=='ps' else set()
    constants = set(profile['constant_registers_direct']) | set(profile['defined_constant_registers'])
    if loop: constants.update(range(24))
    reserved_constants = set(range(216,221) if stage=='ps' else range(252,256)) | set(abi[f'material_{stage}_def_constants'])
    require(not original_temps & (material_temps|temporal_temps) and not material_temps & temporal_temps,
            'asteroid temporary ABI collision')
    require(not constants & reserved_constants, 'asteroid constant ABI collision')
    free_io = set(profile['free_input_registers' if stage=='ps' else 'free_output_registers'])
    io_role = 'input' if stage=='ps' else 'output'
    io = {abi[f'{stage}_motion_{io_role}'],abi[f'{stage}_depth_{io_role}'],abi[f'material_{stage}_rgb_{io_role}']}
    semantics = set(profile[f'declared_texcoord_{io_role}_indices'])
    reserved_semantics = {abi['motion_texcoord'],abi['depth_texcoord']}
    require(len(io)==3 and io<=free_io and max(io)<(10 if stage=='ps' else 12), 'asteroid interpolator ABI collision')
    require(len(reserved_semantics)==2 and not semantics & reserved_semantics and max(reserved_semantics)<16,
            'asteroid semantic ABI collision')
    executable = {k:v for k,v in profile['opcode_counts'].items() if k not in ('dcl','def','defi','defb')}
    return {'original_temporaries':sorted(original_temps),
            'free_temporary_ranges':ranges(set(range(32))-original_temps-material_temps-temporal_temps),
            'free_constant_ranges':ranges(set(range(224 if stage=='ps' else 256))-constants-reserved_constants),
            'free_interpolator_registers':sorted(free_io-io),
            'free_texcoord_semantic_indices':sorted(set(range(16))-semantics-reserved_semantics),
            'original_executable_instruction_count':sum(executable.values()),
            'relative_constant_bound_required':loop,
            'material_resources_proven_free_before_reservation':{
                'rgb_interpolator_register':abi[f'material_{stage}_rgb_{io_role}'],
                'rgb_usage':'color', 'rgb_usage_index':1,
                'def_constants':abi[f'material_{stage}_def_constants']},
            'reservations_apply_to_current_depth_modes':[False,True],
            'explicit_reserved_interpolator_registers':sorted(io),
            'explicit_reserved_texcoord_indices':sorted(reserved_semantics),
            'instruction_budget_note':'Original weighted slots only; final transformed bytecode requires its separate cap check.'}


def inspect_asteroid_program(code, identifier, decoded, profile, items, end):
    stage,key = identifier.split('_')
    bump,base,*_ = (ASTEROID_VERTICES if stage=='vs' else ASTEROID_PIXELS)[key]
    loop = ASTEROID_VERTICES[key][2] if stage=='vs' else False
    family = 'asteroid_bump' if bump else 'asteroid_default'
    abi = asteroid_abi(bump,base)
    asteroid_declarations(profile,stage,bump,base)
    proof = prove_asteroid_vertex(decoded,key) if stage=='vs' else prove_asteroid_pixel(decoded,key)
    output = {'id':identifier,'families':[family],'fnv1a64':key,'sha256':hashlib.sha256(code).hexdigest(),
              'word_count':len(code)//4,'header_end_dword':profile['header_end_dword'],'end_dword':end,
              'opaque_comment_dword_count':len(code)//4-2-sum(i['length']+1 for i in items),
              'budget':asteroid_budget(profile,stage,loop,abi), 'material_abi':abi,
              'motion_splice':({'declaration_insert_dword':profile['header_end_dword'],
                                'arithmetic_insert_dword':profile['position_output']['insertion_dword'],
                                'position_source_temporary':profile['position_output']['source_temporary'],
                                'position_dp4_dwords':profile['position_output']['dwords_xyzw']} if stage=='vs' else
                               {'definition_insert_dword':profile['definition_end_dword'],
                                'declaration_insert_dword':profile['header_end_dword'],'append_dword':end})}
    if stage=='vs':
        require(profile['position_output']['dwords_xyzw']==proof['position_dp4_dwords'] and
                profile['position_output']['source_temporary']==proof['position_source_temporary'],
                'asteroid motion position proof disagrees')
        point,material = proof['point_rgb_dword'],proof['material_emissive_dword']
        output.update(point_and_alpha_proof=proof,
                      point_rgb_sources=source_role(decoded,'c1' if loop else 'c5',[point],loop),
                      material_emissive_scaled_sources=source_role(decoded,'c40' if loop else 'c19',[material]),
                      point_model=proof['point_model'],final_rgb_sites=[site(decoded,material)],
                      alpha_output_sites=[site(decoded,a) for a in proof['alpha_dwords']],
                      rgb_output_declaration=next(d for d in profile['declarations'] if d['name']=='o1'))
        output['point_rgb_sources'][0]['color_constant_indices'] = list(range(1,24,3)) if loop else [5]
        output['constraints'] = ['Convert each point RGB at its source before native cosine/attenuation; preserve stride-3 i0 [0,8] draw gate for loops.',
                                 'Preserve strength-scaled material emissive RGB amplitude/tint; do not decode the accumulated point sum.',
                                 'Keep original position, world, normal/basis, UV, fog and independent o1.w writes unchanged. Added full-precision XYZ uses the explicit pair-local ABI.']
    else:
        texture_roles = ('diffuse_rgb','normal_data_alpha_green','specular_data_red','detail_rgb') if bump else ('diffuse_rgb','specular_data_red','detail_rgb')
        textures = []
        for n,(at,role) in enumerate(zip(proof['texture_dwords'],texture_roles)):
            fetch = site(decoded,at)
            color = role in ('diffuse_rgb','detail_rgb')
            textures.append({'role':role,'sampler':n,'fetch':fetch,
                             'conversion_after_dword':fetch['end_dword'] if color else None,
                             'conversion_rgb_register':'r0' if color else None,'conversion_write_mask':'xyz' if color else None})
        direct = {c:[at for at,_ in uses(decoded,c)] for c in (('c1','c3') if base else ('c1',))}
        output.update(alpha_and_affine_proof=proof,texture_sources=textures,diffuse_affine_completion=None,
                      directional_rgb_sources=[s for c,ats in direct.items() for s in source_role(decoded,c,ats)],
                      color0_rgb_clamp=site(decoded,proof['clamp_dword']),
                      color0_declaration=next(d for d in profile['declarations'] if d['name']=='v0'),
                      final_rgb_sites=[site(decoded,proof['final_dword'])],alpha_interpolation_site=None,
                      alpha_output_sites=[site(decoded,proof['alpha_dword'])],two_sided=False,
                      lobe_coefficients=FAMILIES[family]['coefficients'],detail_weighting=proof['detail_weighting'])
        output['rgb_precision_sites'] = bump_rgb_sites(decoded,output)
        output['retained_geometry_precision_sites'] = proof['normal_reconstruction_sites']
        output['constraints'] = ['Decode separate base/detail RGB immediately after each native PP fetch and before independent raw scalar weighting; no reflection cube or lightmap is present.',
                                 'Keep specular red and AG normal data raw. Native lighting uses unit diffuse, cubic specular, inner sat(3*NdotL), and no outer factor three.',
                                 'Keep v0.w partial precision and native base alpha multiplication before final RGB. Added RGB writes are XYZ only; full-precision material RGB uses the explicit pair-local ABI.',
                                 'Comments/preshaders remain opaque; do not infer new runtime scalar constraints or evaluate the preshader in the transformer.']
    output['material_rgb_semantic_proof'] = material_color1_proof(profile, stage)
    output['budget']['original_static_weighted_slots'] = weighted_slots(decoded,profile,stage)
    output['certification'] = 'original_identity_and_reviewed_sites_verified; no transformed shader or numeric equivalence claim'
    return output



PALETTE_VERTICES={
'29d7c575396ed280':('boron',False,True,True), 'a420a010b0271479':('boron',False,False,True),'ea3d15b287892410':('boron',False,False,False),
'57392213f62fef19':('boron',True,True,True),'5c17a381b149b3b9':('boron',True,False,True),'a804f173f693944a':('boron',True,False,False),
'37e6956afd8b8d76':('paranid',False,True,True),'2e0254dd999841c2':('paranid',False,False,True),'a7cddf2c98d61117':('paranid',False,False,False),
'33388c8897d428a5':('paranid',True,True,True),'b4059ab6af8fc529':('paranid',True,False,True),'2a560f246c90fa64':('paranid',True,False,False)}
PALETTE_PIXELS={
'39eb3c2258a516e1':('boron',False,True,False,False), '57acf59d19c73791':('boron',False,True,False,True),
'f917d48ee826da1f':('boron',False,False,False,False),'77a5b2d62fb3be48':('boron',False,False,False,True),
'a910daef935891ce':('boron',True,True,False,False),'62c180abe017e239':('boron',True,True,False,True),
'ed44232013f67072':('boron',True,False,False,False),'f286856c3f400377':('boron',True,False,False,True),
'9d27e7ba242f3831':('paranid',False,True,True,False),'e1acf8a03850acaf':('paranid',False,True,True,True),
'f646f03be5a8708d':('paranid',False,False,True,False),'ebf41e1ace7af45b':('paranid',False,False,True,True),
'c997a37560e266df':('paranid',False,False,False,False),'675f9077d8fd21c4':('paranid',False,False,False,True),
'18d372968af4a480':('paranid',True,True,True,False),'188c5ab9dbb98393':('paranid',True,True,True,True),
'7e5e41276b3d7514':('paranid',True,False,True,False),'43c9405568d2226f':('paranid',True,False,True,True),
'5e056627e9ff3a8d':('paranid',True,False,False,False),'fce465befff2f623':('paranid',True,False,False,True)}
def palette_bindings(stage,key):
 if stage=='vs':
  family,bump,base,loop=PALETTE_VERTICES[key]
  if family!='boron' or base:return {}
  return dict(zip(('Px','Py','Pz','Pu'),[(43,'yzw'),(44,'xyz'),(45,'xyz'),(46,'xyz')] if loop else [(21,'xyz'),(22,'xyz'),(23,'xyz'),(25,'xyz')]))
 family,bump,base,affine,face=PALETTE_PIXELS[key]
 if family=='boron':
  shift=1 if bump or face else 0
  if base:return dict(zip(('Px','Py','Pz','Pu','Pf','Pc'),[(6+shift,'xyz'),(7+shift,'xyz'),(8+shift,'xyz'),(10+shift,'xyz'),(11+shift,'xyz'),(9+shift,'xyz')]))
  return {'Pf':(5+shift,'xyz'),'Pc':(4+shift,'xyz')}
 if affine:return dict(zip(('Px','Py','Pz','Pf'),[(6,'xyz'),(7,'yzw' if bump else 'xyz'),(8,'xyz'),(10,'xyz')]))
 if bump and face:return dict(zip(('Px','Py','Pz','Pf'),[(3,'xyz'),(4,'yzw'),(5,'xyz'),(8,'xyz')]))
 return dict(zip(('Px','Py','Pz','Pf'),[(4,'xyz'),(5,'xyz'),(6,'xyz'),(8,'xyz')]))

PALETTE_PAIRS={(v,p) for v,vs in PALETTE_VERTICES.items() for p,ps in PALETTE_PIXELS.items() if vs[:3]==ps[:3]}
ORIGINALS.update({
    'vs_29d7c575396ed280': ('79897ccd08fd8852f80ff1c166c7faaba9609d27b7905bb71e0c3a26b37060bf', 577),
    'vs_a420a010b0271479': ('7b934b1981d4517149635024dee7a90e13aa7219691b798d68da458dee4122e3', 605),
    'vs_ea3d15b287892410': ('87b331b3e948290e70e5851a07dd6abb9a72d50d174eb80166fcae64ab7e62c8', 560),
    'vs_57392213f62fef19': ('5675ce318c575240ecc18ed296fecb6418208adbf6b157119dee438aeb9a7070', 617),
    'vs_5c17a381b149b3b9': ('c04e1a3d4bf26947f651b54fe59fe26dc74b90a6e46d81a1a8f4742e040c8fb8', 648),
    'vs_a804f173f693944a': ('2021d821872faaba43e2c61df3e22276734a6eca1e017ef5376e2bf0aff3ed48', 600),
    'vs_37e6956afd8b8d76': ('51dd4270e79cf88df29b436139b4b95a224b7b627da489f9b578bdea8dd547fb', 563),
    'vs_2e0254dd999841c2': ('9adf881597a1867f730bb04e2c1c182da09877ed7166f330e6e86b3ac3843eb5', 563),
    'vs_a7cddf2c98d61117': ('21c8c0397aed33e05176208658bebc0b7078f22df82700471ba9b0848a229549', 512),
    'vs_33388c8897d428a5': ('bfa98def42db1b4c096bd76ad8b329c2f5dfe3f0ce74e9e0141df1b80c40592d', 603),
    'vs_b4059ab6af8fc529': ('c606c4640fb14eb4de557775ed10b02e5a16c5a8d2d6e59d02a9b624b4ccb5ad', 603),
    'vs_2a560f246c90fa64': ('b0a63b2df4caa9cfe3f6afba2b06006f12bf5342137d7d8a708083eddfb9b452', 552),
    'ps_39eb3c2258a516e1': ('59c045239f724005744fe7bb9e58fad17356a28cb75bbec8e04e0e4bde926019', 432),
    'ps_57acf59d19c73791': ('bfa8fb6c3f473758768dc72a7dc00da3ab89d08418298aa1d5130ab614bce30d', 464),
    'ps_f917d48ee826da1f': ('7a02eda58b8432b207978affd796053cc88bcf21ae652d6d7faf3f8146370cf7', 320),
    'ps_77a5b2d62fb3be48': ('5e48ae73d2ac9672c78ac062da5a7e09ea1926e411005b675dfb4804d2981797', 352),
    'ps_a910daef935891ce': ('c21626382bc32fbc4a0c626fd6c7e8a121e2cb4990e3a0de2960b35e57f34a94', 500),
    'ps_62c180abe017e239': ('c6b804e892a61581cb2158368255f26d1de2002326bcdf827a76c600dde0fe61', 526),
    'ps_ed44232013f67072': ('afcfd2e5d45298a9ed59a8c2d85e14abcf63fb0e8d1a4a31745d23cbb9472a47', 388),
    'ps_f286856c3f400377': ('bdfff141908bf861e872b4c1c2e392acefc83130c89a1ca584cfed8a38fa7e20', 414),
    'ps_9d27e7ba242f3831': ('5f543e547530b2b3c8cfee1426446b78defa5c4c37b0b7788e282c38ab1e6a46', 1259),
    'ps_e1acf8a03850acaf': ('5923e9e9298bf80ec03c43dbc03371fcbec8bef359f95f7c76bb81f54d3b37af', 1285),
    'ps_f646f03be5a8708d': ('f31bccbaba809d448681623904e8cda84ae6507c6fad5d10e7f87bed4f916713', 1259),
    'ps_ebf41e1ace7af45b': ('b60daecd8b1092af871089538b721924ec7d588cc17108ee32e2a882ecd19b03', 1285),
    'ps_c997a37560e266df': ('bd78ae47fb1a10ce98cb8545104a3142baf3a125a56f1e3b2fff84cd8ffbe90e', 340),
    'ps_675f9077d8fd21c4': ('fbc4ce907789a1f3e89c3527f124dc179c800bc0d64f29280558c1ce7f3c43ea', 366),
    'ps_18d372968af4a480': ('65cc7dcd07af778cc72f39d1bfdec92089d32873528f41fc4933d553b3ae340b', 1321),
    'ps_188c5ab9dbb98393': ('c60ab74a97d0f956737aee8b9420aacf84af0e7c1dc934723889e8db5e2f856e', 1347),
    'ps_7e5e41276b3d7514': ('d37239e21553e3681b507b0dc3e1a249c3ea6fda762372dda4160d131bba6a20', 1321),
    'ps_43c9405568d2226f': ('d2544a28896c0cb60b9741a7cbd788ef11600e5d15016b287942de9a45717df1', 1347),
    'ps_5e056627e9ff3a8d': ('a87d5b7a2f902531821c4127085bc28765f79ce8f1f8e18f88aa4b1668b039d9', 402),
    'ps_fce465befff2f623': ('3790c9f432dd55ad750334b9f81d0beedf982c73f53c2917c1a1810149f4b8bb', 428),
})
PAIRS |= PALETTE_PAIRS
for _family in ('boron','paranid'):
    for _bump in (False,True):
        _name=_family+('_bump' if _bump else '_default')
        FAMILIES[_name]={'pixels':[k for k,v in PALETTE_PIXELS.items() if v[:2]==(_family,_bump)],'production_status':'implemented_pending_GPU','aliases':[_family],'technique':'BUMPMAP' if _bump else 'DEFAULT','coefficients':{'diffuse':0.4000000059604645 if _family=='boron' else .5,'specular_power':10,'specular_outer_scale':3 if _family=='boron' else 6,'grazing_scale':3,'palette_albedo_mix':.5,'grazing_power':5 if _family=='boron' else 9,'reflection_outer_scale':1.}}
        PIXEL_FAMILY.update({k:_name for k in FAMILIES[_name]['pixels']})


# Exact binary32 palette lanes are authored-color provenance, not shader tokens.
PALETTE_BITS = {
    'boron':{'Px':('3e189899','3edededf','3eeaeaeb'),'Py':('3f3ebebf','3ecececf','3dc8c8c9'),
             'Pz':('3f088889','3f0d8d8e','3eeaeaeb'),'Pu':('3ea2a2a3','3f7dfdfe','3f70f0f1'),
             'Pf':('3f179798','3f3bbbbc','3e949495'),'Pc':('3ee0e0e1','3f24a4a5','3f37b7b8')},
    'paranid':{'Px':('3f09898a','3f179798','3f31b1b2'),'Py':('3e929293','3ec2c2c3','3ecececf'),
               'Pz':('3edcdcdd','3e6ceced','3dc8c8c9'),'Pf':('3ed0d0d1','3f008081','3f24a4a5')},
}


def palette_complete_chain(decoded,rows):
    executable=[at for at,r in decoded.items() if r['item']['opcode'] not in (31,81)]
    require(executable==[r['instruction_dword'] for r in rows], 'palette complete executable inventory changed')
    require(all(not r['item']['predicated'] and not r['item']['coissued'] for r in decoded.values()), 'palette predication/coissue changed')


def palette_constant_proof(decoded,stage,key):
    """Bind all DEF lanes bit-for-bit, including scalar lanes sharing RGB DEFs."""
    import struct,math
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    bits=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]
    value=lambda x:struct.unpack('<f',struct.pack('<I',x))[0]
    shape=(PALETTE_VERTICES if stage=='vs' else PALETTE_PIXELS)[key];family,bump,base=shape[:3]
    definitions={}
    def assign(reg,lane,word):definitions.setdefault(reg,[0,0,0,0])['xyzw'.index(lane)]=word
    if stage=='vs':
        loop=shape[3];c=42 if loop else 24 if family=='boron' else 21
        for lane,x in zip('xyzw',(1.,0.,3.,f32(1.2)) if loop else (1.,0.,f32(1.2),f32(.1))):assign(c,lane,bits(x))
        if loop:
            assign(43,'x',bits(11. if family=='boron' and not base else f32(.1)))
            if family=='boron' and base:assign(43,'y',bits(11.))
            if family=='boron' and not base:assign(44,'w',bits(f32(.1)))
        elif family=='boron':assign(22,'w',bits(11.))
    else:
        face=shape[4]
        values={'one':1.,'minus_one':-1.,'two':2.,'zero':0. if shape[3] else -0.,'power':10.,'three':3.,
                'diffuse':f32(.4) if family=='boron' else .5,'half':.5,'outer':3. if family=='boron' else 6.,'grazing':9.}
        for role,(reg,lane) in palette_scalar_bindings(key).items():assign(reg,lane,bits(values[role]))
    binding=palette_bindings(stage,key)
    for role,(reg,lanes) in binding.items():
        for lane,word in zip(lanes,PALETTE_BITS[family][role]):assign(reg,lane,int(word,16))
    actual={motion.register_of(r['item']['words'][0])[1]:r['item'] for r in decoded.values() if r['item']['opcode']==81}
    require(len(actual)==sum(r['item']['opcode']==81 for r in decoded.values()) and set(actual)==set(definitions), 'palette DEF register inventory changed')
    for reg,words in definitions.items():
        require(len(actual[reg]['words'])==5 and list(actual[reg]['words'][1:])==words, 'palette DEF lane bits changed')
    literals=[{'register':f'c{reg}','component':lane,'literal_dword':actual[reg]['dword']+2+n,
               'value':value(words[n]),'bits_hex':f'{words[n]:08x}'} for reg,words in definitions.items() for n,lane in enumerate('xyzw')]
    sources=[]
    for role,(reg,lanes) in binding.items():
        values=[value(definitions[reg]['xyzw'.index(lane)]) for lane in lanes]
        refs=[dict(compact_operand(s),instruction_dword=at) for at,r in decoded.items() if r['destination'] and r['destination']['mask']=='xyz' for s in r['sources'] if s['name']==f'c{reg}' and s['swizzle'][:3]==lanes]
        require(len(refs)==1 and refs[0]['source_modifier']==0 and not refs[0].get('relative'), 'palette RGB source use inventory changed')
        sources.append({'role':role,'source_constant':reg,'source_lanes':lanes,'literal_values':values,
                        'literal_bits_hex':list(PALETTE_BITS[family][role]),
                        'decoded_values':[f32(math.pow(v,f32(2.2))) for v in values],
                        'decoded_constant_register':(240 if stage=='vs' else 204)+('Px','Py','Pz','Pu','Pf','Pc').index(role),
                        'uses':[dict(r,source_operand={k:v for k,v in r.items() if k!='instruction_dword'}) for r in refs],'transfer':'offline-derived pow(binary32_source,double(binary32(2.2))) rounded to binary32, emitted as immutable DEF lanes at creation; positive palette source lanes only'})
    return literals,sources


def palette_declarations(profile,stage,key):
    shape=(PALETTE_VERTICES if stage=='vs' else PALETTE_PIXELS)[key];family,bump,base=shape[:3]
    boron=family=='boron';decl=profile['declarations'];expected=[]
    if stage=='vs':
        expected=[(f'v{n}','xyzw',usage,0,[]) for n,usage in enumerate(['position','texcoord','normal']+(['binormal','tangent'] if bump else []))]
        expected += [('o0','xyzw','position',0,[]),('o1','xyzw','color',0,[])]
        prefix='o';first=2;pp=[]
    else:expected=[('v0','xyzw','color',0,list(PP))];prefix='v';first=1;pp=list(PP)
    expected += [(f'{prefix}{first}','xy','texcoord',0,(['centroid'] if bump and base else [])+pp)]
    if bump:
        expected += [(f'{prefix}{n}','xyz','texcoord',n-first,pp) for n in range(first+1,first+5)]
        expected += [(f'{prefix}{first+5}','xyz','texcoord',5 if boron else 6,(['centroid'] if base else [])+pp)]
        expected += [(f'{prefix}{first+6}','xy' if boron and base else 'x','texcoord',6 if boron else 7,pp)]
    else:
        expected += [(f'{prefix}{n}','xyz','texcoord',n-first,(['centroid'] if base else [])+pp) for n in range(first+1,first+5)]
        expected += [(f'{prefix}{first+5}','xy' if boron and base else 'x','texcoord',5,(['centroid'] if base else [])+pp)]
    if stage=='ps' and shape[4]:expected += [('vFace','xyzw','position',0,[])]
    actual=[(d['name'],d['mask'],d['usage_name'],d['usage_index'],d['modifiers']) for d in decl if d['role']!='sampler']
    require(actual==expected,'palette original varying declarations changed')
    if stage=='ps':require([(d['name'],d['mask'],d['modifiers'],d['texture_type']) for d in decl if d['role']=='sampler']==[(f's{n}','xyzw',[],3 if n==(4 if bump else 3) else 2) for n in range(5 if bump else 4)],'palette sampler declaration changed')


def palette_scalar_relocations(decoded,profile,stage,key):
    shape=(PALETTE_VERTICES if stage=='vs' else PALETTE_PIXELS)[key];family,bump,base=shape[:3]
    if not bump:return []
    source='o8' if stage=='vs' else 'v7';prefix='o' if stage=='vs' else 'v'
    declarations={d['name']:d for d in profile['declarations']};old=declarations[source]
    require(old['mask']==('xy' if family=='boron' and base else 'x') and old['usage_index']==(6 if family=='boron' else 7) and old['modifiers']==([] if stage=='vs' else list(PP)), 'palette scalar declaration changed')
    output=[]
    for role,lane,index in [('J','x',1)]+([('u11','y',2)] if family=='boron' and base else []):
        dest=f'{prefix}{index+(2 if stage=="vs" else 1)}';carrier=declarations[dest]
        require((carrier['mask'],carrier['usage_name'],carrier['usage_index'],carrier['modifiers'])==('xyz','texcoord',index,old['modifiers']), 'palette scalar carrier precision/centroid changed')
        record={'role':role,'source_register':source,'source_lane':lane,'destination_register':dest,'destination_lane':'w',
                'source_declaration':old,'destination_declaration':carrier,'source_texcoord_index':old['usage_index'],
                'destination_texcoord_index':index,'source_wrap_component':lane,'destination_wrap_component':'w',
                'extended_carrier_mask':'xyzw','retained_carrier_modifiers':carrier['modifiers']}
        if stage=='vs':
            require(not lane_writes(decoded,dest,'w'), 'palette scalar destination lane is written')
            writes=lane_writes(decoded,source,lane);require(len(writes)==1,'palette scalar producer count changed')
            row=site(decoded,writes[0]);require(row['destination']['mask']==lane and row['destination']['modifiers']==[], 'palette scalar producer is not isolated')
            record['producer_sites']=[row]
        else:
            # Relevant native operations consume XYZ vectors or scalar lanes;
            # full xyzw source spelling alone is not a W read in DP3/RGB math.
            reads=[]
            for at,row in decoded.items():
                op=motion.OPCODES[row['item']['opcode']]
                if op in ('dcl','def'):continue
                for s in row['sources']:
                    if s['name']==source and lane in s['swizzle']:
                        require(s['swizzle']==lane*4 and s['source_modifier']==0 and not s['relative'], 'palette scalar source is not isolated')
                        reads.append(dict(compact_operand(s),instruction_dword=at))
                    if s['name']==dest:
                        lanes='xyz' if op in ('nrm','dp3') else row['destination']['mask'] if row['destination'] else ''
                        require(lanes and all(s['swizzle']['xyzw'.index(x)]!='w' for x in lanes), 'palette scalar carrier W is consumed')
            require(len(reads)==1,'palette scalar consumer count changed');record['consumer_sources']=reads
        output.append(record)
    require(all(s['name']!=source or s['swizzle'] in ('xxxx','yyyy') for row in decoded.values() for s in row['sources']), 'palette scalar source inventory changed')
    return output


def palette_abi(family,bump):
    return {'vs_motion_output':9 if bump else 8,'ps_motion_input':8 if bump else 7,
            'motion_texcoord':(7 if family=='boron' else 5) if bump else 6,
            'vs_depth_output':10 if bump else 9,'ps_depth_input':9 if bump else 8,'depth_texcoord':8 if bump else 7,
            'material_vs_rgb_output':8 if bump else 10,'material_ps_rgb_input':7 if bump else 9,
            'material_rgb_usage':'color','material_rgb_usage_index':1,'material_rgb_mask':'xyz','material_rgb_precision':'full',
            'material_vs_def_constants':[248,249],'material_ps_def_constants':[212,213],
            'material_vs_palette_constants':[240,241,242,243],'material_ps_palette_constants':[204,205,206,207,208,209],
            'material_vs_temporaries':[7,8,9],'material_ps_temporaries':[10,11,12,13],
            'required_disabled_srgb_sampler_mask':31 if bump else 15,'current_depth_modes':[False,True]}


def palette_budget(profile,stage,loop,abi,relocations):
    material_color1_proof(profile,stage)
    used_constants=set(profile['constant_registers_direct'])|set(profile['defined_constant_registers'])
    if loop:used_constants.update(range(24))
    reserved_constants=set(range(252,256) if stage=='vs' else range(216,221))|set(abi[f'material_{stage}_def_constants'])|set(abi[f'material_{stage}_palette_constants'])
    original_temps=set(profile['temporary_registers']);material_temps=set(abi[f'material_{stage}_temporaries'])
    temporary_base=max(5,max(original_temps)+1) if stage=='ps' else None
    temporal_temps=set(range(temporary_base,temporary_base+4)) if stage=='ps' else set()
    require(not reserved_constants&used_constants,'palette constant ABI collision')
    require(not original_temps&(material_temps|temporal_temps) and not material_temps&temporal_temps,'palette temporary ABI collision')
    io_role='output' if stage=='vs' else 'input';free_io=set(profile[f'free_{io_role}_registers'])
    rgb=abi[f'material_{stage}_rgb_{io_role}'];motion_io=abi[f'{stage}_motion_{io_role}'];depth_io=abi[f'{stage}_depth_{io_role}']
    if relocations:
        require({r['source_register'] for r in relocations}=={('o' if stage=='vs' else 'v')+str(rgb)},'palette freed RGB register disagrees')
        require(all(r['source_lane']in r['source_declaration']['mask'] for r in relocations) and
                {r['source_lane'] for r in relocations}==set(relocations[0]['source_declaration']['mask']), 'palette scalar register not fully evacuated')
        free_io.add(rgb)
    reserved_io={rgb,motion_io,depth_io};require(len(reserved_io)==3 and reserved_io<=free_io and max(reserved_io)<(12 if stage=='vs' else 10),'palette interpolator ABI collision')
    semantics=set(profile[f'declared_texcoord_{io_role}_indices'])
    if relocations:semantics.remove(relocations[0]['source_texcoord_index'])
    temporal_semantics={abi['motion_texcoord'],abi['depth_texcoord']}
    require(len(temporal_semantics)==2 and not temporal_semantics&semantics,'palette temporal semantic ABI collision')
    return {'original_temporaries':sorted(original_temps),'free_temporary_ranges':ranges(set(range(32))-original_temps-temporal_temps-material_temps),
            'free_constant_ranges':ranges(set(range(256 if stage=='vs' else 224))-used_constants-reserved_constants),
            'free_interpolator_registers':sorted(free_io-reserved_io),'free_texcoord_semantic_indices':sorted(set(range(16))-semantics-temporal_semantics),
            'original_executable_instruction_count':profile['executable_instruction_count'],'relative_constant_bound_required':loop,
            'material_resources_proven_free_before_reservation':{'rgb_interpolator_register':rgb,'rgb_usage':'color','rgb_usage_index':1,'def_constants':abi[f'material_{stage}_def_constants']},
            'material_palette_constant_registers':abi[f'material_{stage}_palette_constants'],'explicit_reserved_interpolator_registers':sorted(reserved_io),
            'explicit_reserved_texcoord_indices':sorted(temporal_semantics),'temporal_temporary_registers_including_depth':sorted(temporal_temps),
            'reservations_apply_to_current_depth_modes':[False,True],'rgb_register_availability':'complete proved scalar evacuation' if relocations else 'unused whole physical register',
            'instruction_budget_note':'Original weighted slots only; final transformed bytecode and scalar transport require separate qualification.'}


def palette_rgb_sites(decoded,program,stage):
    """Bounded componentwise color propagation; scalar/geometry lanes never seed."""
    live=set();palette_uses={(u['instruction_dword'],u['operand_dword']) for p in program['palette_sources'] for u in p['uses']}
    if stage=='ps':
        live.update((s,lane) for s in ['v0']+[r['name'] for r in program['directional_rgb_sources']] for lane in 'xyz')
        if program['palette_varying']:live.update((program['palette_varying']['declaration']['name'],lane) for lane in 'xyz')
    else:
        live.update((r['name'],lane) for r in program['point_rgb_sources']+program['material_emissive_scaled_sources'] for lane in 'xyz')
    conversions={r['conversion_after_dword']:r['conversion_rgb_register'] for r in program.get('texture_sources',[]) if r['conversion_after_dword']}
    result=[]
    for at,row in decoded.items():
        dest=row['destination'];op=motion.OPCODES[row['item']['opcode']]
        if dest and op not in ('dcl','def'):
            depends=any((at,s['operand_dword']) in palette_uses or (s['name'],s['swizzle']['xyzw'.index(lane)]) in live for s in row['sources'] for lane in dest['mask'])
            for lane in dest['mask']:live.discard((dest['name'],lane))
            if depends and op!='texld':
                require(op in ('mov','mul','add','mad') and dest['mask']=='xyz','palette mixed RGB/data precision dependency')
                result.append(site(decoded,at));live.update((dest['name'],lane) for lane in 'xyz')
        register=conversions.get(at+row['item']['length']+1)
        if register:live.update((register,lane) for lane in 'xyz')
    return result


def palette_scalar_bindings(key):
    family,bump,base,affine,face=PALETTE_PIXELS[key]
    def b(**fields):return {role:(int(value[1:-1]),value[-1]) for role,value in fields.items()}
    if family=='boron':
        a=5 if base else 3
        if bump and face:return b(one=f'c{a}x',minus_one=f'c{a}y',two=f'c{a}z',zero=f'c{a}w',power=f'c{a+1}x',three=f'c{a+1}y',diffuse=f'c{a+1}z',half=f'c{a+1}w',outer=f'c{a+1}y')
        if bump:
            n=6 if base else 3;m=5 if base else 4
            return b(two=f'c{n}x',minus_one=f'c{n}y',one=f'c{n}z',power=f'c{n}w',three=f'c{m}x',diffuse=f'c{m}y',half=f'c{m}z',outer=f'c{m}x')
        if face:return b(one=f'c{a}x',minus_one=f'c{a}y',zero=f'c{a}z',power=f'c{a}w',three=f'c{a+1}x',diffuse=f'c{a+1}y',half=f'c{a+1}z',outer=f'c{a+1}x')
        return b(power=f'c{a}x',three=f'c{a}y',diffuse=f'c{a}z',one=f'c{a}w',half=f'c{7 if base else 5}w',outer=f'c{a}y')
    if affine:
        if bump:return b(one='c11x',minus_one='c11y' if face else 'c11w',zero='c11z' if face else 'c11y',two='c11w' if face else 'c11z',power='c9x',three='c9y',diffuse='c9z',half='c9z',outer='c9w',grazing='c7x')
        return b(one='c11x',minus_one='c11y',zero='c11z' if face else 'c11y',power='c11w' if face else 'c11z',three='c9x',diffuse='c9y',half='c9y',outer='c9z' if face else 'c11w',grazing='c9w' if face else 'c9z')
    if bump and face:return b(one='c6x',minus_one='c6y',two='c6z',zero='c6w',power='c7x',three='c7y',diffuse='c7z',half='c7z',outer='c7w',grazing='c4x')
    if bump:return b(two='c3x',minus_one='c3y',one='c3z',power='c3w',three='c7x',diffuse='c7y',half='c7y',outer='c7z',grazing='c7w')
    if face:return b(one='c3x',minus_one='c3y',zero='c3z',power='c3w',three='c7x',diffuse='c7y',half='c7y',outer='c7z',grazing='c7w')
    return b(one='c3x',power='c7x',three='c7y',diffuse='c7z',half='c7z',outer='c7w',grazing='c3y')


def prove_palette_vertex(decoded, key):
    """Exact native geometric reflection/J and point-light schedules; no interpreter."""
    family,bump,base,loop = PALETTE_VERTICES[key]
    boron = family=='boron'; special = boron and bump and not base
    starts={'29d7c575396ed280':347,'a420a010b0271479':365,'ea3d15b287892410':347,
            '57392213f62fef19':356,'5c17a381b149b3b9':374,'a804f173f693944a':356,
            '37e6956afd8b8d76':347,'2e0254dd999841c2':347,'a7cddf2c98d61117':323,
            '33388c8897d428a5':356,'b4059ab6af8fc529':356,'2a560f246c90fa64':332}
    rows,emit=asteroid_chain(decoded,starts[key])
    c='c42' if loop else 'c24' if boron else 'c21'
    power=(c,'wwww' if loop else 'zzzz'); bias=('c44','wwww') if loop and boron and not base else ('c43','xxxx') if loop else (c,'wwww')
    point=None
    # Uniform helpers are limited to these two native families and their exact
    # position/normal matrix ABI. They preserve the original scheduled order.
    def prep(dst,src,mask='xyzw'):
        return emit('mad',(dst,mask),[(src,'xyzx' if mask=='xyzw' else 'xyxw'),(c,'xxxy' if mask=='xyzw' else 'xxyw'),(c,'yyyx' if mask=='xyzw' else 'yyxw')])
    def xyz(dst,src,start,order='zxy'):
        for lane in order:emit('dp4',(dst,lane),[(src,'xyzw'),(f'c{start+"xyz".index(lane)}','xyzw')])
    def clip(src):
        return [emit('dp4',('o0',lane),[(src,'xyzw'),(f'c{(24 if loop else 0)+n}','xyzw')]) for n,lane in enumerate('xyzw')]
    def camera(dst):
        for lane in ('yxz' if loop else 'xyz'):emit('mov',(dst,lane),[(f'c{(34 if loop else 13)+"xyz".index(lane)}','wwww')])
    def point_loop(world,normal,light,atten,scalar,cosine):
        emit('mov',('r0','xyz'),[(c,'yyyy')]);counter=emit('mov',('r0','w'),[(c,'yyyy')])
        rep=emit('rep',None,[('i0','xyzw')]);emit('mul',(scalar,'w'),[('r0','wwww'),(c,'zzzz')]);address=emit('mova',('a0','w'),[(scalar,'wwww')])
        pos=emit('add',(light,'xyz'),[(world,'xyzw',1),('c0','xyzw',0,'a0','w')])
        emit('dp3',(atten,'z'),[(light,'xyzw'),(light,'xyzw')]);emit('rsq',(scalar,'w'),[(atten,'zzzz')])
        emit('mul',(light,'xyz'),[(light,'xyzw'),(scalar,'wwww')]);emit('mul',(atten,'y'),[(atten,'zzzz'),(scalar,'wwww')]);emit('mov',(atten,'x'),[(c,'xxxx')])
        emit('dp3',(cosine,'w'),[(normal,'xyzw'),(light,'xyzw')],('saturate',));att=emit('dp3',(scalar,'w'),[('c2','xyzw',0,'a0','w'),(atten,'xyzw')])
        emit('rcp',(scalar,'w'),[(scalar,'wwww')],('saturate',));pt=emit('mul',(atten,'xyz'),[(cosine,'wwww'),('c1','xyzw',0,'a0','w')])
        emit('mad',('r0','xyz'),[(atten,'xyzw'),(scalar,'wwww'),('r0','xyzw')]);inc=emit('add',('r0','w'),[('r0','wwww'),(c,'xxxx')]);end=emit('endrep',None,[])
        require(lane_writes(decoded,'a0')==[address],'palette point address overwritten')
        require(lane_writes(decoded,'r0','w',counter-1,end+1)==[counter,inc],'palette point counter overwritten')
        relative=[(at,s['name']) for at,r in decoded.items() for s in r['sources'] if s['relative']]
        require(relative==[(pos,'c0'),(att,'c2'),(pt,'c1')],'palette relative point inventory changed')
        return pt,{'rep_dword':rep,'endrep_dword':end,'address_write_dword':address,'counter_initialization_dword':counter,'counter_increment_dword':inc,'verified_stride':3,'runtime_count_range_required':[0,8]}
    if special:
        pos='r1' if loop else 'r0';world='r3' if loop else 'r1';normal='r2';view='r0'
        position_prep=prep(pos,'v0');emit('dp4',(world,'z'),[(pos,'xyzw'),('c30' if loop else 'c9','xyzw')]);position_sites=clip(pos)
        if loop:
            prep('r0','v1','xyz');emit('dp3',('o2','y'),[('r0','xyzw'),('c38','xyzw')]);emit('dp3',('o2','x'),[('r0','xyzw'),('c37','xyzw')])
            emit('dp4',(world,'x'),[(pos,'xyzw'),('c28','xyzw')]);prep('r0','v2');emit('dp4',(world,'y'),[(pos,'xyzw'),('c29','xyzw')]);xyz(normal,'r0',31)
            point,point_proof=point_loop(world,normal,'r4','r1','r1','r2')
            emit('mov',('r1','xyz'),[('r0','xyzw')]);camera('r0');material=emit('add',('o1','xyz'),[('r1','xyzw'),('c40','xyzw')])
        else:
            for lane,n in [('x',7),('y',8)]:emit('dp4',(world,lane),[(pos,'xyzw'),(f'c{n}','xyzw')])
            prep('r3','v1','xyz');emit('add',('r2','xyz'),[(world,'xyzw',1),('c4','xyzw')]);emit('dp3',('o2','y'),[('r3','xyzw'),('c17','xyzw')])
            emit('dp3',('r0','z'),[('r2','xyzw'),('r2','xyzw')]);emit('dp3',('o2','x'),[('r3','xyzw'),('c16','xyzw')]);emit('rsq',('r2','w'),[('r0','zzzz')])
            emit('mul',('r0','y'),[('r0','zzzz'),('r2','wwww')]);emit('mov',('r0','x'),[(c,'xxxx')]);emit('dp3',('r1','w'),[('c6','xyzw'),('r0','xyzw')])
            prep('r0','v2');emit('mul',('r3','xyz'),[('r2','xyzw'),('r2','wwww')]);xyz(normal,'r0',10)
            emit('rcp',('r0','w'),[('r1','wwww')],('saturate',));emit('dp3',('r0','z'),[(normal,'xyzw'),('r3','xyzw')],('saturate',))
            point=emit('mul',('r3','xyz'),[('r0','zzzz'),('c5','xyzw')]);camera('r0');material=emit('mad',('o1','xyz'),[('r3','xyzw'),('r0','wwww'),('c19','xyzw')])
            point_proof={'model':'fixed_single_point','relative_sources':0}
        emit('add',('r0','xyz'),[(world,'xyzw',1),('r0','xyzw')]);fogtemp='r0'
    else:
        pos='r2' if bump else 'r1' if loop else 'r0';uv='r5' if bump else 'r4'
        world='r3' if boron else 'r4' if bump else 'r3';normal='r4' if boron and bump else 'r3' if bump else 'r2'
        if loop:
            position_prep=prep(pos,'v0');prep(uv,'v1','xyz');emit('dp4',(world,'z'),[(pos,'xyzw'),('c30','xyzw')]);emit('dp3',('o2','y'),[(uv,'xyzw'),('c38','xyzw')])
            emit('dp4',(world,'x'),[(pos,'xyzw'),('c28','xyzw')]);prep('r0','v2');emit('dp4',(world,'y'),[(pos,'xyzw'),('c29','xyzw')]);xyz(normal,'r0',31)
            point,point_proof=point_loop(world,normal,'r6','r1' if bump else 'r5','r1' if bump else 'r2','r3')
            material=emit('add',('o1','xyz'),[('r0','xyzw'),('c40','xyzw')])
        else:
            world='r4' if bump else 'r3';normal='r3' if bump else 'r2';light='r1' if bump else 'r2';scalar='r0' if bump else 'r1'
            position_prep=prep(pos,'v0');xyz(world,pos,7);emit('add',(light,'xyz'),[(world,'xyzw',1),('c4','xyzw')]);emit('dp3',('r6','z'),[(light,'xyzw'),(light,'xyzw')])
            emit('rsq',(light,'w'),[('r6','zzzz')]);prep(uv,'v1','xyz');emit('mul',('r6','y'),[('r6','zzzz'),(light,'wwww')]);emit('mov',('r6','x'),[(c,'xxxx')]);prep(scalar,'v2')
            emit('mul',('r1' if bump else 'r5','xyz'),[(light,'xyzw'),(light,'wwww')]);xyz(normal,scalar,10)
            emit('dp3',(scalar,'w'),[('c6','xyzw'),('r6','xyzw')]);emit('dp3',(scalar,'z'),[(normal,'xyzw'),('r1' if bump else 'r5','xyzw')],('saturate',))
            emit('rcp',(scalar,'w'),[(scalar,'wwww')],('saturate',));point=emit('mul',(scalar,'xyz'),[(scalar,'zzzz'),('c5','xyzw')]);emit('dp3',('o2','y'),[(uv,'xyzw'),('c17','xyzw')])
            material=emit('mad',('o1','xyz'),[(scalar,'xyzw'),(scalar,'wwww'),('c19','xyzw')]);point_proof={'model':'fixed_single_point','relative_sources':0}
        if bump:
            prep('r1','v4');prep('r0','v3')
            emit('dp4',('o5','z'),[('r1','xyzw'),('c33' if loop else 'c12','xyzw')]);emit('dp4',('o6','z'),[('r0','xyzw'),('c33' if loop else 'c12','xyzw')])
        emit('mov',('o4','xyz'),[(normal,'xyzw')]);position_sites=clip(pos)
        fogtemp='r2' if bump else 'r0';camera(fogtemp);emit('dp3',('o2','x'),[(uv,'xyzw'),('c37' if loop else 'c16','xyzw')])
        view=fogtemp if boron and base else 'r4' if bump else 'r1'
        emit('add',(view,'xyz'),[(world,'xyzw',1),(fogtemp,'xyzw')])
    point_end=len(rows)
    fog='c41' if loop else 'c20';alpha='c39' if loop else 'c18'
    emit('if',None,[('b0','xyzw')]);emit('dp3',(fogtemp,'w'),[(view,'xyzw'),(view,'xyzw')]);emit('rsq',(fogtemp,'w'),[(fogtemp,'wwww')]);emit('rcp',(fogtemp,'w'),[(fogtemp,'wwww')])
    emit('mad',(fogtemp,'w'),[(fog,'yyyy'),(fogtemp,'wwww',1),(fog,'xxxx')],('saturate',));alphas=[emit('mul',('o1','w'),[(fogtemp,'wwww'),(alpha,'xxxx')])]
    emit('else',None,[]);alphas.append(emit('mov',('o1','w'),[(alpha,'xxxx')]));emit('endif',None,[])
    if special:
        emit('nrm',('r3','xyz'),[(view,'xyzw')]);emit('dp3',('r0','w'),[('r3','xyzw',1),('r2','xyzw')]);emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
        reflect=emit('mad',('r0','xyz'),[('r2','xyzw'),('r0','wwww',1),('r3','xyzw',1)]);emit('dp3',('r2','w'),[('r3','xyzw'),('r2','xyzw')],('saturate',))
        emit('mul',('r1','xyz'),[('r0','yyyy',11),('c44' if loop else 'c22','xyzw')]);emit('pow',('r0','w'),[('r2','wwww'),('c43','xxxx') if loop else ('c22','wwww')])
        emit('mad',('r1','xyz'),[('r0','xxxx',11),('c43','yzww') if loop else ('c21','xyzw'),('r1','xyzw')]);emit('mad',('r0','xyz'),[('r0','zzzz',11),('c45' if loop else 'c23','xyzw'),('r1','xyzw')])
        prep('r1','v4');palette_export=emit('mad',('o7','xyz'),[('r0','wwww'),('c46' if loop else 'c25','xyzw'),('r0','xyzw')])
        emit('dp4',('o5','z'),[('r1','xyzw'),('c33' if loop else 'c12','xyzw')]);prep('r0','v3');emit('add',('r3','w'),[('r2','wwww',1),(c,'xxxx')])
        emit('dp4',('o6','z'),[('r0','xyzw'),('c33' if loop else 'c12','xyzw')]);emit('pow',('r2','w'),[('r3','wwww'),power]);emit('mov',('o3','xyz'),[('r3','xyzw')])
        emit('add',('r2','w'),[('r2','wwww'),bias]);emit('mov',('o4','xyz'),[('r2','xyzw')]);scalar=emit('add',('o8','x'),[('r2','wwww'),('r2','wwww')])
    elif boron and base:
        normal_view='r3' if bump else 'r1';vtemp='r2' if bump else 'r0';weight='r3' if bump else 'r1'
        emit('nrm',(normal_view,'xyz'),[(view,'xyzw')]);emit('dp3',(vtemp,'w'),[(normal_view,'xyzw',1),(normal,'xyzw')])
        if bump:emit('mov',('o3','xyz'),[(normal_view,'xyzw')])
        emit('add',(vtemp,'w'),[(vtemp,'wwww'),(vtemp,'wwww')])
        if not bump:emit('mov',('o3','xyz'),[(normal_view,'xyzw')])
        reflect=emit('mad',(vtemp,'xyz'),[(normal,'xyzw'),(vtemp,'wwww',1),(normal_view,'xyzw',1)])
        if not bump:emit('mov',('o5','xyz'),[(vtemp,'xyzw')])
        emit('dp3',(vtemp,'w'),[(normal_view,'xyzw'),(normal,'xyzw')],('saturate',));emit('abs',('o7' if bump else 'o6','xyz'),[(vtemp,'xyzw')])
        emit('add',(weight,'w'),[(vtemp,'wwww',1),(c,'xxxx')]);emit('log',(vtemp,'z'),[(vtemp,'wwww')]);emit('pow',(vtemp,'w'),[(weight,'wwww'),power])
        emit('mul',(vtemp,'z'),[(vtemp,'zzzz'),('c43','yyyy')]);emit('add',(vtemp,'w'),[(vtemp,'wwww'),bias]);emit('exp',('o8' if bump else 'o7','y'),[(vtemp,'zzzz')]);scalar=emit('add',('o8' if bump else 'o7','x'),[(vtemp,'wwww'),(vtemp,'wwww')])
    elif boron:
        emit('nrm',('r0','xyz'),[(view,'xyzw')]);emit('dp3',('r0','w'),[('r0','xyzw',1),(normal,'xyzw')]);emit('mov',('o3','xyz'),[('r0','xyzw')]);emit('add',('r1','w'),[('r0','wwww'),('r0','wwww')])
        emit('dp3',('r0','w'),[('r0','xyzw'),(normal,'xyzw')],('saturate',));reflect=emit('mad',('r0','xyz'),[(normal,'xyzw'),('r1','wwww',1),('r0','xyzw',1)])
        emit('pow',('r1','w'),[('r0','wwww'),('c43','xxxx') if loop else ('c22','wwww')]);emit('mul',('r1','xyz'),[('r0','yyyy',11),('c44' if loop else 'c22','xyzw')])
        emit('mad',('r1','xyz'),[('r0','xxxx',11),('c43','yzww') if loop else ('c21','xyzw'),('r1','xyzw')]);emit('add',('r2','w'),[('r0','wwww',1),(c,'xxxx')])
        emit('mad',('r1','xyz'),[('r0','zzzz',11),('c45' if loop else 'c23','xyzw'),('r1','xyzw')]);emit('pow',('r0','w'),[('r2','wwww'),power])
        palette_export=emit('mad',('o6','xyz'),[('r1','wwww'),('c46' if loop else 'c25','xyzw'),('r1','xyzw')]);emit('add',('r0','w'),[('r0','wwww'),bias]);emit('mov',('o5','xyz'),[('r0','xyzw')]);scalar=emit('add',('o7','x'),[('r0','wwww'),('r0','wwww')])
    else:
        vtemp='r2' if bump else 'r0';weight='r3' if bump else 'r1'
        emit('nrm',(vtemp,'xyz'),[(view,'xyzw')]);emit('mov',('o3','xyz'),[(vtemp,'xyzw')]);emit('dp3',(vtemp,'w'),[(vtemp,'xyzw',1),(normal,'xyzw')])
        if bump:
            emit('dp3',(weight,'w'),[(vtemp,'xyzw'),(normal,'xyzw')],('saturate',));emit('add',(vtemp,'w'),[(vtemp,'wwww'),(vtemp,'wwww')]);emit('add',(weight,'w'),[(weight,'wwww',1),(c,'xxxx')])
            reflect=emit('mad',(vtemp,'xyz'),[(normal,'xyzw'),(vtemp,'wwww',1),(vtemp,'xyzw',1)])
        else:
            emit('add',(weight,'w'),[(vtemp,'wwww'),(vtemp,'wwww')]);emit('dp3',(vtemp,'w'),[(vtemp,'xyzw'),(normal,'xyzw')],('saturate',))
            reflect=emit('mad',(vtemp,'xyz'),[(normal,'xyzw'),(weight,'wwww',1),(vtemp,'xyzw',1)]);emit('add',(weight,'w'),[(vtemp,'wwww',1),(c,'xxxx')]);emit('mov',('o5','xyz'),[(vtemp,'xyzw')])
        emit('pow',(vtemp,'w'),[(weight,'wwww'),power]);emit('abs',('o7' if bump else 'o6','xyz'),[(vtemp,'xyzw')]);scalar=emit('add',('o8' if bump else 'o7','x'),[(vtemp,'wwww'),bias])
    if bump:
        for out,reg in [('o5','r1'),('o6','r0')]:xyz(out,reg,31 if loop else 10,'xy')
    palette_complete_chain(decoded,rows)
    require(lane_writes(decoded,'o1')==[material]+alphas,'palette native alpha/RGB output inventory changed')
    if not loop:require(not any(s['relative'] for r in decoded.values() for s in r['sources']),'palette fixed point became relative')
    # Exact complete scheduling plus explicit position live interval prevents
    # late palette/basis temporaries from entering the existing motion quad.
    for lane in 'xyzw':no_lane_writes(decoded,pos,lane,position_prep,max(position_sites))
    return {'point_rgb_dword':point,'material_emissive_dword':material,'alpha_dwords':alphas,
            'point_model':'loop_count_i0_x_0_to_8_stride_3_a0_w' if loop else 'fixed_single_point','point_loop':point_proof,
            'position_dp4_dwords':position_sites,'position_source_temporary':int(pos[1:]),'position_matrix_register':24 if loop else 0,
            'geometry_and_point_sites':rows[:point_end],'fog_reflection_and_palette_sites':rows[point_end:],
            'geometric_reflection_dword':reflect,'J_export_dword':scalar,
            'palette_rgb_export_dword':palette_export if boron and not base else None,
            'normalized_view_before_interpolation':True,'native_geometric_normal_normalized_in_vs':False,
            'complete_executable_chain_checked':len(rows)}


def prove_palette_pixel(decoded,key):
    """Scheduled proofs for the four palette material shapes and native faces."""
    family,bump,base,affine,face=PALETTE_PIXELS[key];boron=family=='boron'
    # Exact header ends are separate from opaque metadata and preshaders.
    starts={'39eb3c2258a516e1':214,'57acf59d19c73791':223,'f917d48ee826da1f':172,'77a5b2d62fb3be48':181,
            'a910daef935891ce':235,'62c180abe017e239':238,'ed44232013f67072':193,'f286856c3f400377':196,
            '9d27e7ba242f3831':1092,'e1acf8a03850acaf':1095,'f646f03be5a8708d':1092,'ebf41e1ace7af45b':1095,
            'c997a37560e266df':190,'675f9077d8fd21c4':193,'18d372968af4a480':1107,'188c5ab9dbb98393':1110,
            '7e5e41276b3d7514':1107,'43c9405568d2226f':1110,'5e056627e9ff3a8d':205,'fce465befff2f623':208}
    rows,emit=asteroid_chain(decoded,starts[key],PP);sat=PP+('saturate',)
    constants=palette_scalar_bindings(key)
    def S(role):
        reg,lane=constants[role];return (f'c{reg}',lane*4)
    def C(role):
        reg,lanes=palette_bindings('ps',key)[role];return (f'c{reg}',lanes+lanes[-1] if lanes!='xyz' else 'xyzw')
    def packed():
        reg,a=constants['three'];other,b=constants['diffuse'];require(reg==other and (a,b)in(('x','y'),('y','z')),'palette lobe packed constants changed')
        return (f'c{reg}','xyzw' if a=='x' else 'yzzw')
    normal='r0' if bump or boron and not base else 'r3' if boron else 'r1'
    if bump:
        if face:emit('cmp',('r1','w'),[('vFace','xyzw'),S('one'),S('minus_one')],())
        emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')]);emit('mad',('r1','xy'),[S('two'),('r0','wyzw'),S('minus_one')])
        if face:emit('cmp',('r0','w'),[('r1','wwww',1),S('zero'),S('one')])
        emit('dp2add',('r1' if face else 'r0','w'),[('r1','xyzw'),('r1','xyzw',1),S('one')]);emit('mul',('r0','xyz'),[('r1','yyyy'),('v4','xyzw')])
        emit('rsq',('r1' if face else 'r0','w'),[('r1' if face else 'r0','wwww')]);emit('mad',('r0','xyz'),[('r1','xxxx'),('v5','xyzw'),('r0','xyzw')])
        emit('rcp',('r1','z') if face else ('r0','w'),[('r1' if face else 'r0','wwww')])
        if face:emit('cmp',('r1','w'),[('vFace','xyzw'),S('zero'),S('one')])
        emit('mad',('r1','xyz'),[('r1','zzzz') if face else ('r0','wwww'),('v3','xyzw'),('r0','xyzw')])
        if face:emit('add',('r0','w'),[('r0','wwww'),('r1','wwww',1)])
        normal_at=emit('nrm',('r0','xyz'),[('r1','xyzw')])
        if face:
            normal='r4' if boron and base else 'r1'
            emit('mul',(normal,'xyz'),[('r0','wwww'),('r0','xyzw')])
    else:
        if face:
            emit('cmp',('r0','w'),[('vFace','xyzw'),S('one'),S('minus_one')],())
            emit('cmp',('r0','w'),[('r0','wwww',1),S('zero'),S('one')]);emit('cmp',('r0','z'),[('vFace','xyzw'),S('zero'),S('one')]);emit('add',('r0','w'),[('r0','wwww'),('r0','zzzz',1)])
        normal_at=emit('nrm',('r0' if face else normal,'xyz'),[('v3','xyzw')])
        if face:emit('mul',(normal,'xyz'),[('r0','wwww'),('r0','xyzw')])
    normal_count=len(rows);direction='c3' if boron and base else 'c4' if affine else 'c1';color='c5' if affine else 'c2'
    emit('dp3',('r0','w'),[(direction,'xyzw',1),(normal,'xyzw')]);emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
    reflected='r2' if bump else 'r0' if not boron or base else 'r1'
    emit('mad',(reflected,'xyz'),[(normal,'xyzw'),('r0','wwww',1),(direction,'xyzw',1)])
    if boron and base:
        view='r1' if bump else 'r2';emit('nrm',(view,'xyz'),[('v2','xyzw')])
        emit('dp3',('r0' if bump else 'r1','w'),[(reflected,'xyzw'),(view,'xyzw')],sat)
        emit('pow',('r1','w') if bump else ('r0','z'),[('r0' if bump else 'r1','wwww'),S('power')])
        emit('dp3',('r2','z') if bump else ('r0','x'),[(normal,'xyzw'),('c3','xyzw')],sat)
        emit('mul',('r2','w') if bump else ('r0','y'),[('r2','zzzz') if bump else ('r0','xxxx'),S('three')],sat)
        emit('dp3',('r0','w'),[('c1','xyzw',1),(normal,'xyzw')]);emit('mul',('r3' if bump else 'r1','xyz'),[('r2','zzzz') if bump else ('r0','xxxx'),('c4','xyzw')]);emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
        emit('mul',('r3','w'),[('r1','wwww') if bump else ('r0','zzzz'),('r2','wwww') if bump else ('r0','yyyy')]);emit('mad',(reflected,'xyz'),[(normal,'xyzw'),('r0','wwww',1),('c1','xyzw',1)])
        if not bump or face:emit('dp3',('r1' if bump else 'r2','w'),[(normal,'xyzw'),('c1','xyzw')],sat)
        emit('dp3',('r2' if bump else 'r1','w'),[(reflected,'xyzw'),(view,'xyzw')],sat)
        if bump and not face:emit('dp3',('r1','w'),[(normal,'xyzw'),('c1','xyzw')],sat)
        emit('pow',('r0','w'),[('r2' if bump else 'r1','wwww'),S('power')]);emit('mul',('r2' if bump else 'r1','w'),[('r1' if bump else 'r2','wwww'),S('three')],sat)
        emit('mul',('r2' if bump else 'r0','xyz'),[('r3','wwww'),('c4','xyzw')]);emit('mul',('r0','w'),[('r0','wwww'),('r2' if bump else 'r1','wwww')])
        if bump:
            emit('mad',('r3','xyz'),[('r1','wwww'),('c2','xyzw'),('r3','xyzw')]);emit('mad',('r4','xyz'),[('r0','wwww'),('c2','xyzw'),('r2','xyzw')])
            emit('texld',('r2','xyzw'),[('v1','xyzw'),('s2','xyzw')]);emit('mul',('r0','w'),[('r2','xxxx'),S('three')]);emit('mul',('r4','xyz'),[('r4','xyzw'),('r0','wwww')])
            emit('dp3',('r0','w'),[('r1','xyzw',1),('r0','xyzw')]);emit('mad',('r3','xyz'),[('r3','xyzw'),S('diffuse'),('r4','xyzw')]);emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')]);emit('mov',('r4','xyz'),[('v0','xyzw')],sat)
            reflection=emit('mad',('r0','xyz'),[('r0','xyzw'),('r0','wwww',1),('r1','xyzw',1)]);emit('add',('r3','xyz'),[('r3','xyzw'),('r4','xyzw')]);emit('dp3',('r1','w'),[('r1','xyzw'),('r0','xyzw')]);emit('texld',('r0','xyzw'),[('r0','xyzw'),('s4','xyzw')])
        else:
            reflection=None;emit('dp3',('r1','w'),[('r2','xyzw'),('v4','xyzw')]);emit('mad',('r2','xyz'),[('r0','wwww'),('c2','xyzw'),('r0','xyzw')]);emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
            emit('mul',('r0','w'),[('r0','xxxx'),S('three')]);emit('mad',('r1','xyz'),[('r2','wwww'),('c2','xyzw'),('r1','xyzw')]);emit('mul',('r2','xyz'),[('r2','xyzw'),('r0','wwww')]);emit('mad',('r2','xyz'),[('r1','xyzw'),S('diffuse'),('r2','xyzw')]);emit('mov',('r3','xyz'),[('v0','xyzw')],sat)
        emit('add',('r0','w'),[('r1','wwww',12),S('one')]);emit('mul',('r1','xyz'),[('v6' if bump else 'v5','yyyy'),C('Py')])
        square='r1' if bump else 'r0';lane='w' if bump else 'z'
        emit('mul',(square,lane),[('r0','wwww'),('r0','wwww')]);emit('mad',('r1','xyz'),[('v6' if bump else 'v5','xxxx'),C('Px'),('r1','xyzw')]);emit('mul',(square,lane),[(square,lane*4),(square,lane*4)])
        emit('mad',('r1','xyz'),[('v6' if bump else 'v5','zzzz'),C('Pz'),('r1','xyzw')]);emit('mul',('r0','w'),[('r0','wwww'),(square,lane*4)]);emit('mad',('r1','xyz'),[('v7' if bump else 'v6','yyyy'),C('Pu'),('r1','xyzw')])
        if not bump:emit('add',('r2','xyz'),[('r2','xyzw'),('r3','xyzw')])
        emit('mad',('r4' if bump else 'r3','xyz'),[('r0','wwww'),C('Pf'),('r1','xyzw')])
    elif bump:
        if face:emit('dp3',('r1','w'),[(normal,'xyzw'),(direction,'xyzw')],sat)
        emit('nrm',('r1','xyz'),[('v2','xyzw')])
        if not face:emit('dp3',('r1','w'),[(normal,'xyzw'),(direction,'xyzw')],sat)
        emit('dp3',('r0','w'),[('r2','xyzw'),('r1','xyzw')],sat);emit('mul',('r3','xy'),[('r1','wwww'),packed()]);emit('pow',('r1','w'),[('r0','wwww'),S('power')]);emit('mov',('r0','w'),[('r3','xxxx')],sat)
        emit('mul',('r0','w'),[('r1','wwww'),('r0','wwww')]);emit('texld',('r2','xyzw'),[('v1','xyzw'),('s2','xyzw')]);emit('mul',('r0','w'),[('r0','wwww'),('r2','xxxx')]);emit('dp3',('r1','w'),[('r1','xyzw',1),('r0','xyzw')])
        emit('mad',('r0','w'),[('r0','wwww'),S('outer'),('r3','yyyy')]);emit('add',('r1','w'),[('r1','wwww'),('r1','wwww')]);emit('mov',('r3','xyz'),[('v0','xyzw')],sat)
        reflection=emit('mad',('r0','xyz'),[('r0','xyzw'),('r1','wwww',1),('r1','xyzw',1)]);emit('mad',('r4' if affine else 'r3','xyz'),[('r0','wwww'),(color,'xyzw'),('r3','xyzw')]);emit('dp3',('r1','w'),[('r1','xyzw'),('r0','xyzw')]);emit('texld',('r0','xyzw'),[('r0','xyzw'),('s4','xyzw')])
        if boron:
            emit('add',('r0','w'),[('r1','wwww',12),S('one')]);emit('mul',('r1','w'),[('r0','wwww'),('r0','wwww')]);emit('mul',('r1','w'),[('r1','wwww'),('r1','wwww')]);emit('mul',('r0','w'),[('r0','wwww'),('r1','wwww')]);emit('mad',('r4','xyz'),[('r0','wwww'),C('Pf'),('v6','xyzw')])
    else:
        reflection=None
        emit('dp3',('r1' if boron else 'r0','w'),[(normal,'xyzw'),(direction,'xyzw')],sat);emit('nrm',('r0' if boron else 'r1','xyz'),[('v2','xyzw')])
        emit('dp3',('r0' if boron else 'r1','w'),[(reflected,'xyzw'),('r0' if boron else 'r1','xyzw')],sat)
        emit('mul',('r1' if boron else 'r2','xy'),[('r1' if boron else 'r0','wwww'),packed()]);emit('pow',('r1','w') if boron else ('r0','z'),[('r0' if boron else 'r1','wwww'),S('power')]);emit('mov',('r0','w'),[('r1' if boron else 'r2','xxxx')],sat)
        if boron:
            emit('dp3',('r1','z'),[('r0','xyzw'),('v4','xyzw')]);emit('mul',('r1','w'),[('r1','wwww'),('r0','wwww')]);emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
            emit('add',('r0','w'),[('r1','zzzz',12),S('one')]);emit('mul',('r0','z'),[('r1','wwww'),('r0','xxxx')]);emit('mul',('r0','y'),[('r0','wwww'),('r0','wwww')]);emit('mad',('r0','z'),[('r0','zzzz'),S('outer'),('r1','yyyy')]);emit('mul',('r0','y'),[('r0','yyyy'),('r0','yyyy')]);emit('mov',('r1','xyz'),[('v0','xyzw')],sat)
            emit('mul',('r0','w'),[('r0','wwww'),('r0','yyyy')]);emit('mad',('r2','xyz'),[('r0','zzzz'),(color,'xyzw'),('r1','xyzw')]);emit('mad',('r3','xyz'),[('r0','wwww'),C('Pf'),('v5','xyzw')])
        else:
            emit('mul',('r1','w'),[('r0','zzzz'),('r0','wwww')]);emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')]);emit('mul',('r0','w' if affine else 'z'),[('r1','wwww'),('r0','xxxx')])
            emit('dp3',('r0','z' if affine else 'w'),[('r1','xyzw'),('v4','xyzw')]);emit('mad',('r0','w' if affine else 'z'),[('r0','wwww' if affine else 'zzzz'),S('outer'),('r2','yyyy')]);emit('mov',('r5' if affine else 'r2','xyz'),[('v0','xyzw')],sat)
    # Palette-angle responses preserve raw geometric weights, including native
    # ABS source modifiers and POW's absolute-base rule for Paranid power nine.
    if not boron:
        w='v6' if bump else 'v5';g='w' if bump else 'z' if affine else 'w';f='w' if bump or not affine else 'z'
        emit('mul',('r1','xyz'),[(w,'yyyy'),C('Py')]);emit('add',('r1','w'),[('r1','wwww',12) if bump else ('r0',g*4,12),S('one')]);emit('mad',('r1','xyz'),[(w,'xxxx'),C('Px'),('r1','xyzw')]);emit('pow',('r0',f),[('r1','wwww'),S('grazing')]);emit('mad',('r5' if bump and affine else 'r3' if affine else 'r1','xyz'),[(w,'zzzz'),C('Pz'),('r1','xyzw')])
        if not affine:
            if not bump:emit('mad',('r2','xyz'),[('r0','zzzz'),(color,'xyzw'),('r2','xyzw')])
            emit('mad',('r4' if bump else 'r3','xyz'),[('r0',f*4),C('Pf'),('r1','xyzw')])
    diffuse=emit('texld',('r1','xyzw'),[('v1','xyzw'),('s0','xyzw')]);affine_sites=[]
    if affine:
        homogeneous='r3' if bump else 'r2';albedo='r1' if bump else 'r4';pal='r5' if bump else 'r3'
        reg,_=constants['one'];zero=constants['zero'][1]
        emit('mad',(homogeneous,'xyzw'),[('r1','xyzx'),(f'c{reg}','xxx'+zero),(f'c{reg}',zero*3+'x')],())
        emit('mad',(pal,'xyz'),[('r0','wwww' if bump else 'zzzz'),C('Pf'),(pal,'xyzw')])
        for n,lane in enumerate('xyz'):affine_sites.append(emit('dp4',(albedo,lane),[(homogeneous,'xyzw'),(f'c{n}','xyzw')]))
        if not bump:emit('mad',('r1','xyz'),[('r0','wwww'),(color,'xyzw'),('r5','xyzw')])
    if boron:
        if bump:
            emit('mul',('r5','xyz'),[('r1','xyzw'),('v7','xxxx')]);emit('mul',('r4','xyz'),[('r4','xyzw'),('r1','xyzw')]);emit('mul',('r2','xyz'),[('r2','xxxx'),('r5','xyzw')]);emit('mul',('r4','xyz'),[('r4','xyzw'),S('half')]);emit('mul',('r2','xyz'),[('r2','xyzw'),C('Pc')]);emit('mad',('r1','xyz'),[('r1','xyzw'),S('half'),('r4','xyzw')]);emit('mul',('r0','xyz'),[('r0','xyzw'),('r2','xyzw')]);emit('mad',('r1','xyz'),[('r3','xyzw'),('r1','xyzw'),('r0','xyzw')])
        else:
            emit('mul',('r3','xyz'),[('r3','xyzw'),('r1','xyzw')]);emit('mul',('r4','xyz'),[('r1','xyzw'),('v6','xxxx')]);emit('mul',('r3','xyz'),[('r3','xyzw'),S('half')]);emit('mul',('r0','xyz'),[('r0','xxxx'),('r4','xyzw')]);emit('mad',('r1','xyz'),[('r1','xyzw'),S('half'),('r3','xyzw')]);emit('mul',('r3','xyz'),[('r0','xyzw'),C('Pc')])
    elif bump:
        pal='r3' if affine else 'r4';emit('mul',(pal,'xyz'),[('r5' if affine else 'r4','xyzw'),('r1','xyzw')]);emit('mul',('r5','xyz'),[('r1','xyzw'),('v7','xxxx')]);emit('mul',(pal,'xyz'),[(pal,'xyzw'),S('half')]);emit('mul',('r2','xyz'),[('r2','xxxx'),('r5','xyzw')]);emit('mad',('r1','xyz'),[('r1','xyzw'),S('half'),(pal,'xyzw')]);emit('mul',('r0','xyz'),[('r0','xyzw'),('r2','xyzw')]);emit('mad',('r1','xyz'),[('r4' if affine else 'r3','xyzw'),('r1','xyzw'),('r0','xyzw')])
    else:
        albedo='r4' if affine else 'r1';pal='r2' if affine else 'r3';mix='r2' if affine else 'r4'
        emit('mul',(pal,'xyz'),[('r3','xyzw'),(albedo,'xyzw')]);emit('mul',(mix,'xyz'),[(pal,'xyzw'),S('half')]);emit('mul',('r3','xyz'),[(albedo,'xyzw'),('v6','xxxx')]);emit('mad',('r2' if affine else 'r1','xyz'),[(albedo,'xyzw'),S('half'),(mix,'xyzw')]);emit('mul',('r3','xyz'),[('r0','xxxx'),('r3','xyzw')])
    if not bump:
        emit('texld',('r0','xyzw'),[('v4','xyzw'),('s3','xyzw')]);emit('mul',('r0','xyz'),[('r3','xyzw'),('r0','xyzw')]);emit('mad',('r1','xyz'),[('r1','xyzw'),('r2','xyzw'),('r0','xyzw')] if affine else [('r2','xyzw'),('r1','xyzw'),('r0','xyzw')])
    lightmap=emit('texld',('r0','xyzw'),[('v1','xyzw'),('s3' if bump else 's2','xyzw')]);alpha_mix=emit('lrp',('r2','w'),[('c3' if affine else 'c0','xxxx'),('r0','wwww'),('r1','wwww')]);final=emit('add',('oC0','xyz'),[('r1','xyzw'),('r0','xyzw')]);alpha=emit('mul',('oC0','w'),[('r2','wwww'),('v0','wwww')])
    palette_complete_chain(decoded,rows)
    for reg,start,end in [('r1',diffuse,alpha_mix),('r0',lightmap,alpha_mix),('r2',alpha_mix,alpha)]:no_lane_writes(decoded,reg,'w',start,end)
    require(lane_writes(decoded,'oC0')==[final,alpha],'palette PS output inventory changed')
    return {'normal_reconstruction_sites':rows[:normal_count],'angular_palette_and_color_sites':rows[normal_count:],
            'normal_encoding':'ag' if bump else 'geometric','normal_channels':{'alpha':'binormal_v5','green':'tangent_v4','red_blue':'unused'} if bump else {},
            'native_pow_sites':[r for r in rows if r['opcode']=='pow'],'affine_rgb_sites':[site(decoded,a) for a in affine_sites],
            'specular_power':10,'specular_outer_scale':3 if boron else 6,'grazing_power':5 if boron else 9,
            'grazing_power_model':'signed_multiply_fifth' if boron else 'native_pow_absolute_base_ninth',
            'diffuse_alpha_live_interval':[diffuse,alpha_mix],'lightmap_alpha_live_interval':[lightmap,alpha_mix],'interpolated_alpha_live_interval':[alpha_mix,alpha],
            'reflection_coordinate_dword':reflection,'final_rgb_dword':final,'alpha_dword':alpha,'alpha_interpolation_dword':alpha_mix,
            'complete_executable_chain_checked':len(rows)}


def inspect_palette_program(code,identifier,decoded,profile,items,end):
    stage,key=identifier.split('_');shape=(PALETTE_VERTICES if stage=='vs' else PALETTE_PIXELS)[key]
    family,bump,base=shape[:3];loop=shape[3] if stage=='vs' else False
    require(profile['parsed'] and profile['header_is_contiguous'] and profile['control_flow_balanced'],'invalid palette original structure')
    proof=prove_palette_vertex(decoded,key) if stage=='vs' else prove_palette_pixel(decoded,key)
    literal,palettes=palette_constant_proof(decoded,stage,key);palette_declarations(profile,stage,key)
    relocations=palette_scalar_relocations(decoded,profile,stage,key);abi=palette_abi(family,bump)
    full_family=family+('_bump' if bump else '_default')
    output={'id':identifier,'families':[full_family],'fnv1a64':key,'sha256':hashlib.sha256(code).hexdigest(),'word_count':len(code)//4,
            'header_end_dword':profile['header_end_dword'],'end_dword':end,
            'opaque_comment_dword_count':len(code)//4-2-sum(i['length']+1 for i in items),
            'budget':palette_budget(profile,stage,loop,abi,relocations),'material_abi':abi,
            'material_rgb_semantic_proof':material_color1_proof(profile,stage),'scalar_relocations':relocations,
            'literal_sites':literal,'palette_sources':palettes,'palette_varying':None,
            'motion_splice':({'declaration_insert_dword':profile['header_end_dword'],'arithmetic_insert_dword':profile['position_output']['insertion_dword'],
                              'position_source_temporary':profile['position_output']['source_temporary'],'position_dp4_dwords':profile['position_output']['dwords_xyzw']} if stage=='vs' else
                             {'definition_insert_dword':profile['definition_end_dword'],'declaration_insert_dword':profile['header_end_dword'],'append_dword':end})}
    if family=='boron' and not base:
        varying=('o7' if bump else 'o6') if stage=='vs' else ('v6' if bump else 'v5')
        declaration=next(d for d in profile['declarations'] if d['name']==varying)
        require(declaration['mask']=='xyz' and declaration['modifiers']==([] if stage=='vs' else list(PP)), 'palette RGB varying contains scalar/centroid data')
        if stage=='vs':require(lane_writes(decoded,varying)==[proof['palette_rgb_export_dword']],'palette VS varying write inventory changed')
        else:require(len(uses(decoded,varying))==1 and all(s['swizzle']=='xyzw' and s['source_modifier']==0 and not s['relative'] for _,s in uses(decoded,varying)), 'palette PS varying read inventory changed')
        output['palette_varying']={'declaration':declaration,'authored_mask':'xyz','authored_modifiers':[],
                                   'role':'already_linear_palette_rgb','preserve_semantic_and_register':True,'native_alpha_or_scalar_lanes':[]}
    if stage=='vs':
        point=proof['point_rgb_dword'];material=proof['material_emissive_dword']
        require(profile['position_output']['dwords_xyzw']==proof['position_dp4_dwords'] and profile['position_output']['source_temporary']==proof['position_source_temporary'],'palette original motion position disagrees')
        output.update(point_and_alpha_proof=proof,point_rgb_sources=source_role(decoded,'c1' if loop else 'c5',[point],loop),
                      material_emissive_scaled_sources=source_role(decoded,'c40' if loop else 'c19',[material]),point_model=proof['point_model'],
                      final_rgb_sites=[site(decoded,material)],alpha_output_sites=[site(decoded,at) for at in proof['alpha_dwords']],
                      rgb_output_declaration=next(d for d in profile['declarations'] if d['name']=='o1'))
        output['point_rgb_sources'][0]['color_constant_indices']=list(range(1,24,3)) if loop else [5]
        output['constraints']=['Decode each native point RGB before its scalar/angular multiplication; preserve relative stride 3 and runtime i0 count [0,8]. Material emissive retains its already-scaled amplitude and separate gain.',
                               'Boron single palette colors are decoded separately in VS before their native geometric-weight accumulation, then retain full precision through the existing palette RGB varying.',
                               'Preserve native world/normal/view/reflection, UV, fog and independent alpha math. BUMP moves only the individually proved native scalars into matching-precision W lanes and preserves their component wrap behavior.']
    else:
        affine=shape[3];roles=('diffuse_rgb','normal_data_alpha_green','specular_data_red','lightmap_emissive_rgb','reflection_cube_rgb') if bump else ('diffuse_rgb','specular_data_red','lightmap_emissive_rgb','reflection_cube_rgb')
        textures=[]
        for sampler,role in enumerate(roles):
            fetches=[at for at,row in decoded.items() if row['item']['opcode']==66 and row['sources'][1]['name']==f's{sampler}']
            require(len(fetches)==1,'palette sampler fetch inventory changed');fetch=site(decoded,fetches[0])
            boundary=proof['affine_rgb_sites'][-1] if sampler==0 and affine else fetch
            color=role.endswith('_rgb');textures.append({'role':role,'sampler':sampler,'fetch':fetch,'conversion_after_dword':boundary['end_dword'] if color else None,
                                                        'conversion_rgb_register':boundary['destination']['name'] if color else None,'conversion_write_mask':'xyz' if color else None})
        direct=['c2','c4'] if family=='boron' and base else ['c5'] if affine else ['c2']
        clamps=[at for at,row in decoded.items() if row['item']['opcode']==1 and row['destination'] and row['destination']['mask']=='xyz' and any(s['name']=='v0' for s in row['sources'])]
        require(len(clamps)==1,'palette COLOR0 clamp inventory changed')
        output.update(alpha_and_affine_proof=proof,texture_sources=textures,diffuse_affine_completion=proof['affine_rgb_sites'][-1] if affine else None,
                      directional_rgb_sources=[s for c in direct for s in source_role(decoded,c,[at for at,_ in uses(decoded,c)])],
                      color0_rgb_clamp=site(decoded,clamps[0]),color0_declaration=next(d for d in profile['declarations'] if d['name']=='v0'),
                      final_rgb_sites=[site(decoded,proof['final_rgb_dword'])],alpha_interpolation_site=site(decoded,proof['alpha_interpolation_dword']),
                      alpha_output_sites=[site(decoded,proof['alpha_dword'])],two_sided=shape[4],lobe_coefficients=FAMILIES[full_family]['coefficients'],
                      retained_geometry_precision_sites=proof['normal_reconstruction_sites'])
        output['constraints']=['Decode each fixed palette RGB before weighted accumulation, leaving shared scalar DEF lanes untouched. Boron single palette arrives already linear from VS; do not decode its sum.',
                               'Keep native sampled mask, normal AG, geometric palette weights, J, u11, face, reflection coordinates and alpha unchanged; transform only certified RGB source/dependency sites.',
                               'Paranid retains native affine RGB completion before diffuse decode and native absolute-base POW for its ninth-power grazing response; Boron retains the signed multiply fifth power.',
                               'BUMP scalar relocation preserves existing carrier XYZ and precision/centroid flags; the live transport must map and restore native component WRAP state. Driver interpolation parity remains a separate qualification.']
    output['rgb_precision_sites']=palette_rgb_sites(decoded,output,stage)
    output['budget']['original_static_weighted_slots']=weighted_slots(decoded,profile,stage)
    output['certification']='original_identity_and_reviewed_sites_verified; no transformed shader or numeric equivalence claim'
    return output


def prove_palette_archive(inventory):
    rows=[r for r in inventory['pairs'] if (r['vs'],r['ps']) in PALETTE_PAIRS]
    require(len(rows)==32,'palette archive pair inventory changed')
    for row in rows:
        family,bump,base,loop=PALETTE_VERTICES[row['vs']];_,_,_,affine,face=PALETTE_PIXELS[row['ps']]
        if base:toggles=['(base)','hue_lights_off','hueshift_off','v_lights_off']
        elif family=='boron':toggles=['(base)','hueshift_off'] if loop else ['hue_lights_off','v_lights_off']
        else:toggles=[('(base)' if affine else 'hueshift_off') if loop else ('v_lights_off' if affine else 'hue_lights_off')]
        count=8 if base else 2 if family=='boron' else 1
        expected={'pass_occurrences':count,'effect_entries':count,'catalogues':['01.cat','addon/01.cat'] if base else ['01.cat'],
                  'basenames':[family+('2s' if face else '') if base else family+('_0001' if face else '_0000')],
                  'profile_directories':['3_0'],'toggle_directories':toggles,'techniques':['BUMPMAP' if bump else 'DEFAULT'],'pass_names':['P0']}
        require(row['effects']==expected,'palette archive alias/toggle inventory changed')



def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def decode_sites(items):
    """Attach original DWORD positions to the reused, relative-aware decoder."""
    result = {}
    for item in items:
        dest, sources = motion.split_operands(item, 3)
        at = item['dword'] + 1
        if dest:
            dest = dict(dest, operand_dword=at)
            at += 1 + int(dest['relative'])
        located = []
        for source in sources:
            token = item['words'][at - item['dword'] - 1]
            located.append(dict(source, operand_dword=at,
                                source_modifier=(token >> 24) & 15,
                                **({'relative_operand_dword': at + 1} if source['relative'] else {})))
            at += 1 + int(source['relative'])
        result[item['dword']] = {'item': item, 'destination': dest, 'sources': located}
    return result


def compact_operand(value):
    if value is None:
        return None
    return {key: item for key, item in value.items()
            if key not in ('register', 'register_type') and (key != 'relative' or item)}


def site(decoded, offset):
    require(offset in decoded, f'conversion site {offset} is not an instruction')
    row = decoded[offset]
    return {'instruction_dword': offset, 'opcode': motion.OPCODES[row['item']['opcode']], 'end_dword': offset + 1 + row['item']['length'],
            'destination': compact_operand(row['destination']),
            'sources': [compact_operand(source) for source in row['sources']]}


def uses(decoded, name):
    return [(offset, source) for offset, row in decoded.items()
            for source in row['sources'] if source['name'] == name]


def source_role(decoded, name, expected_offsets, relative=False):
    references = uses(decoded, name)
    require([at for at, _ in references] == sorted(expected_offsets), f'{name} source inventory changed')
    require(all(s['relative'] == relative and s['swizzle'] == 'xyzw' and
                s['source_modifier'] == 0 for _, s in references), f'{name} source shape changed')
    return [dict(instruction_dword=at, opcode=motion.OPCODES[decoded[at]['item']['opcode']],
                 consumer_destination=compact_operand(decoded[at]['destination']), **compact_operand(source))
            for at, source in references]


def ranges(numbers):
    """Inclusive ranges keep large, contiguous free constant sets compact."""
    result = []
    for number in sorted(numbers):
        if result and result[-1][1] + 1 == number:
            result[-1][1] = number
        else:
            result.append([number, number])
    return result


def budget(profile, stage, loop, bump=False, depth_semantic=None):
    material_color1_proof(profile, stage)
    temporary_base = max(profile['temporary_registers'], default=-1) + 1 if bump else 5
    reserved_temps = set(range(temporary_base, temporary_base + 3)) if stage == 'ps' else set()
    temporal_constants = set(range(216, 221) if stage == 'ps' else range(252, 256))
    material_constants = {212, 213} if stage == 'ps' else {248, 249}
    require(not temporal_constants & material_constants, 'material/temporal constant ABI collision')
    reserved_constants = temporal_constants | material_constants
    used_constants = set(profile['constant_registers_direct']) | set(profile['defined_constant_registers'])
    if loop:
        used_constants.update(range(24))  # Conditional on the existing i0 [0,8] draw gate.
    require(not reserved_constants & used_constants, 'constant ABI collision')
    require(not reserved_temps & set(profile['temporary_registers']), 'temporary ABI collision')
    free_temps = set(range(32)) - set(profile['temporary_registers']) - reserved_temps
    # Current-depth reuses the first motion temporary after motion has finished.
    free_io = set(profile['free_input_registers' if stage == 'ps' else 'free_output_registers'])
    reserved_io = ({6, 7, 8} if stage == 'ps' else {7, 8, 9}) if bump else ({5, 6, 7} if stage == 'ps' else {6, 7, 8})
    require(reserved_io <= free_io, 'motion/depth/material interpolator ABI collision')
    original_semantics = set(profile['declared_texcoord_input_indices' if stage == 'ps'
                                     else 'declared_texcoord_output_indices'])
    depth_semantic = (6 if bump else 5) if depth_semantic is None else depth_semantic
    motion_semantic = 5 if bump else 4
    require(depth_semantic not in original_semantics and motion_semantic not in original_semantics and
            depth_semantic != motion_semantic, 'motion/depth semantic ABI collision')
    used_semantics = original_semantics | {motion_semantic, depth_semantic}
    if bump:
        material_temps = {10, 11, 12, 13} if stage == 'ps' else {7, 8, 9}
        require(material_temps <= free_temps, 'material temporary ABI collision')
    executable = {name: count for name, count in profile['opcode_counts'].items()
                  if name not in ('dcl', 'def', 'defi', 'defb')}
    return {'original_temporaries': profile['temporary_registers'],
            'free_temporary_ranges': ranges(free_temps),
            'free_constant_ranges': ranges(set(range(224 if stage == 'ps' else 256)) - used_constants - reserved_constants),
            'free_interpolator_registers': sorted(free_io - reserved_io),
            'free_texcoord_semantic_indices': sorted(set(range(16)) - used_semantics),
            'original_executable_instruction_count': sum(executable.values()),
            'original_static_slot_estimate': sum(count * motion.SLOT_COSTS.get(name, 1) for name, count in executable.items()),
            'instruction_budget_note': 'Static original estimate only; repeated VS loop work and future transfer/motion/depth instructions are not included. Check final bytecode against device caps.',
            'relative_constant_bound_required': loop,
            'material_resources_proven_free_before_reservation': {'rgb_interpolator_register': (8 if stage == 'ps' else 9) if bump else (7 if stage == 'ps' else 8),
                                                                 'rgb_usage': 'color', 'rgb_usage_index': 1,
                                                                 'def_constants': sorted(material_constants)},
            'reservations_apply_to_current_depth_modes': [False, True]}


# Compact expected operand shapes are reviewed technical contracts, not bytecode.
PP = ('partial_precision',)


def expect(decoded, at, opcode, destination, sources, modifiers=()):
    """Check the opcode, complete operands, modifiers and their DWORD positions."""
    row = site(decoded, at)
    require(row['opcode'] == opcode, f'{at}: opcode changed')
    dst = row['destination']
    require((None if dst is None else (dst['name'], dst['mask'])) == destination,
            f'{at}: destination changed')
    cursor = at + 1
    if dst:
        require(not dst.get('relative') and dst['operand_dword'] == cursor and
                dst['modifiers'] == sorted(modifiers), f'{at}: destination flags/offset changed')
        cursor += 1
    require(len(row['sources']) == len(sources), f'{at}: source count changed')
    for actual, expected in zip(row['sources'], sources):
        # (register, swizzle, modifier=0, relative-address-register=None, lane=None)
        expected = tuple(expected) + (0, None, None)[max(0, len(expected) - 2):]
        name, swizzle, modifier, address, component = expected
        require((actual['name'], actual['swizzle'], actual['source_modifier'],
                 actual.get('address_register'), actual.get('address_component')) ==
                (name, swizzle, modifier, address, component), f'{at}: source shape changed')
        require(actual.get('relative', False) == bool(address) and actual['operand_dword'] == cursor,
                f'{at}: source relative/offset changed')
        if address:
            require(actual['relative_operand_dword'] == cursor + 1, f'{at}: address offset changed')
        cursor += 1 + bool(address)
    require(row['end_dword'] == cursor, f'{at}: instruction length changed')
    return row


def lane_writes(decoded, name, lane=None, begin=-1, end=1 << 30):
    return [at for at, row in decoded.items() if begin < at < end and row['destination'] and
            row['destination']['name'] == name and
            (lane is None or lane in row['destination']['mask'])]


def no_lane_writes(decoded, name, lane, begin, end):
    require(not lane_writes(decoded, name, lane, begin, end), f'{name}.{lane}: intervening write')


def prove_vertex(decoded, loop):
    """Prove point addressing and the complete COLOR0/alpha producer chain."""
    if loop:
        definition = [row['item'] for row in decoded.values() if row['item']['opcode'] == motion.DEF and
                      motion.register_of(row['item']['words'][0]) == (2, 42)]
        require(len(definition) == 1 and len(definition[0]['words']) == 5, 'loop stride definition missing/malformed')
        import struct
        values = struct.unpack('<4f', struct.pack('<4I', *definition[0]['words'][1:]))
        require(values == (1.0, 0.0, 3.0, 0.0), 'loop stride/initialization literal changed')
        expected = [
            (378, 'mov', ('r0', 'xyz'), [('c42', 'yyyy')]),
            (381, 'mov', ('r0', 'w'), [('c42', 'yyyy')]),
            (384, 'rep', None, [('i0', 'xyzw')]),
            (386, 'mul', ('r2', 'w'), [('r0', 'wwww'), ('c42', 'zzzz')]),
            (390, 'mova', ('a0', 'w'), [('r2', 'wwww')]),
            (393, 'add', ('r6', 'xyz'), [('r3', 'xyzw', 1), ('c0', 'xyzw', 0, 'a0', 'w')]),
            (420, 'dp3', ('r2', 'w'), [('c2', 'xyzw', 0, 'a0', 'w'), ('r5', 'xyzw')]),
            (428, 'mul', ('r5', 'xyz'), [('r3', 'wwww'), ('c1', 'xyzw', 0, 'a0', 'w')]),
            (433, 'mad', ('r0', 'xyz'), [('r5', 'xyzw'), ('r2', 'wwww'), ('r0', 'xyzw')]),
            (438, 'add', ('r0', 'w'), [('r0', 'wwww'), ('c42', 'xxxx')]),
            (442, 'endrep', None, []),
            (443, 'add', ('o1', 'xyz'), [('r0', 'xyzw'), ('c40', 'xyzw')]),
        ]
        for args in expected:
            expect(decoded, *args)
        relative = [(at, src['name'], src.get('address_register'), src.get('address_component'))
                    for at, row in decoded.items() for src in row['sources'] if src['relative']]
        require(relative == [(393, 'c0', 'a0', 'w'), (420, 'c2', 'a0', 'w'), (428, 'c1', 'a0', 'w')],
                'relative point inventory changed')
        require(lane_writes(decoded, 'a0') == [390], 'point address overwritten')
        require(lane_writes(decoded, 'r0', 'w', 380, 443) == [381, 438], 'loop counter overwritten')
        require(lane_writes(decoded, 'r0', None, 377, 443) == [378, 381, 433, 438], 'loop accumulator overwritten')
        require([at for at, _ in uses(decoded, 'i0')] == [384], 'loop count source changed')
        proof = {'rep_dword': 384, 'endrep_dword': 442, 'counter_initialization_dword': 381,
                 'counter_increment_dword': 438, 'stride_multiply_dword': 386, 'address_write_dword': 390,
                 'stride_definition_dword': definition[0]['dword'],
                 'stride_literal_dword': definition[0]['dword'] + 4, 'verified_stride': 3,
                 'relative_source_dwords': [396, 422, 431], 'runtime_count_range_required': [0, 8]}
        shift, alpha_c, fog_c, rgb = 0, 'c39', 'c41', 443
    else:
        expect(decoded, 389, 'mul', ('r1', 'xyz'), [('r1', 'zzzz'), ('c5', 'xyzw')])
        expect(decoded, 397, 'mad', ('o1', 'xyz'), [('r1', 'xyzw'), ('r1', 'wwww'), ('c19', 'xyzw')])
        require(not any(src['relative'] for row in decoded.values() for src in row['sources']), 'fixed point became relative')
        proof = {'model': 'fixed_single_point', 'relative_sources': 0}
        shift, alpha_c, fog_c, rgb = -45, 'c18', 'c20', 397
    expect(decoded, 483 + shift, 'if', None, [('b0', 'xyzw')])
    expect(decoded, 485 + shift, 'dp3', ('r0', 'w'), [('r0', 'xyzw'), ('r0', 'xyzw')])
    expect(decoded, 489 + shift, 'rsq', ('r0', 'w'), [('r0', 'wwww')])
    expect(decoded, 492 + shift, 'rcp', ('r0', 'w'), [('r0', 'wwww')])
    expect(decoded, 495 + shift, 'mad', ('r0', 'w'), [(fog_c, 'yyyy'), ('r0', 'wwww', 1), (fog_c, 'xxxx')], ('saturate',))
    expect(decoded, 500 + shift, 'mul', ('o1', 'w'), [('r0', 'wwww'), (alpha_c, 'xxxx')])
    expect(decoded, 504 + shift, 'else', None, [])
    expect(decoded, 505 + shift, 'mov', ('o1', 'w'), [(alpha_c, 'xxxx')])
    expect(decoded, 508 + shift, 'endif', None, [])
    require(lane_writes(decoded, 'o1') == [rgb, 500 + shift, 505 + shift], 'COLOR0 output inventory changed')
    flow = [(at, motion.OPCODES[row['item']['opcode']]) for at, row in decoded.items()
            if motion.OPCODES[row['item']['opcode']] in motion.FLOW_OPCODES]
    require(flow == ([(384, 'rep'), (442, 'endrep')] if loop else []) +
            [(483 + shift, 'if'), (504 + shift, 'else'), (508 + shift, 'endif')], 'vertex flow changed')
    return proof


def prove_pixel(decoded, key):
    """Prove sampled alpha liveness and exact RGB/final output source shapes."""
    tex, affine, clamp, direct, final = PIXELS[key]
    shared = PIXEL_FAMILY[key] == 'shared_default'
    clamp_register = 'r2' if shared and (len(direct) == 2 or not affine) else 'r1'
    for sampler, at in enumerate(tex):
        expect(decoded, at, 'texld', ('r1' if sampler == 0 else 'r0', 'xyzw'),
               [('v4' if sampler == 3 else 'v1', 'xyzw'), (f's{sampler}', 'xyzw')], PP)
    expect(decoded, clamp, 'mov', (clamp_register, 'xyz'), [('v0', 'xyzw')], PP + ('saturate',))
    affine_sites = []
    if affine:
        for lane, at, constant in zip('xyz', (affine - 8, affine - 4, affine), range(3)):
            affine_sites.append(expect(decoded, at, 'dp4', ('r3', lane), [('r2', 'xyzw'), (f'c{constant}', 'xyzw')], PP))
        no_lane_writes(decoded, 'r3', 'x', affine - 8, affine + 4)
        no_lane_writes(decoded, 'r3', 'y', affine - 4, affine + 4)
    # Both base directional branches independently consume each light RGB.
    if len(direct) == 2:
        shift = final - (1255 if shared else 1251)
        for at, op, dst, sources in [
            (1158, 'mul', ('r0', 'xyz'), [('r1', 'yyyy'), ('c7', 'xyzw')]),
            (1166, 'mul', ('r2' if shared else 'r1', 'xyz'), [('r2', 'wwww'), ('c7', 'xyzw')]),
            (1170, 'mad', ('r1' if shared else 'r2', 'xyz'), [('r0', 'wwww'), ('c5', 'xyzw'), ('r0', 'xyzw')]),
            (1183, 'mad', ('r3' if shared else 'r1', 'xyz'), [('r1', 'wwww'), ('c5', 'xyzw'), ('r2' if shared else 'r1', 'xyzw')]),
        ]:
            expect(decoded, at + shift, op, dst, sources, PP)
    else:
        constant, offsets = next(iter(direct.items()))
        expect(decoded, offsets[0], 'mad', ('r1' if affine else 'r2', 'xyz'),
               [('r0', 'wwww'), (f'c{constant}', 'xyzw'), (clamp_register, 'xyzw')], PP)
    lrp = tex[2] + 4
    expect(decoded, lrp, 'lrp', ('r2', 'w'),
           [('c3' if affine else 'c0', 'xxxx'), ('r0', 'wwww'), ('r1', 'wwww')], PP)
    no_lane_writes(decoded, 'r1', 'w', tex[0], lrp)
    no_lane_writes(decoded, 'r0', 'w', tex[2], lrp)
    expect(decoded, final, 'add', ('oC0', 'xyz'), [('r1', 'xyzw'), ('r0', 'xyzw')], PP)
    expect(decoded, final + 4, 'mul', ('oC0', 'w'), [('r2', 'wwww'), ('v0', 'wwww')], PP)
    no_lane_writes(decoded, 'r2', 'w', lrp, final + 4)
    outputs = [(at, row['destination']['name'], row['destination']['mask']) for at, row in decoded.items()
               if row['destination'] and row['destination']['register_type'] in (4, 5, 6, 8, 9)]
    require(outputs == [(final, 'oC0', 'xyz'), (final + 4, 'oC0', 'w')], 'pixel output inventory changed')
    require(not any(motion.OPCODES[row['item']['opcode']] in motion.FLOW_OPCODES
                    for row in decoded.values()), 'pixel alpha liveness requires straight-line flow')
    return {'diffuse_alpha_live_interval': [tex[0], lrp], 'lightmap_alpha_live_interval': [tex[2], lrp],
            'interpolated_alpha_live_interval': [lrp, final + 4], 'affine_rgb_sites': affine_sites}


def literal_component(decoded, register, component, expected):
    """Read an actual DEF component, never CTAB/preshader or application data."""
    import struct
    definitions = [row['item'] for row in decoded.values() if row['item']['opcode'] == motion.DEF and
                   motion.name_of(*motion.register_of(row['item']['words'][0])) == register]
    require(len(definitions) == 1 and len(definitions[0]['words']) == 5, 'lobe definition missing')
    definition = definitions[0]
    value = struct.unpack('<f', struct.pack('<I', definition['words'][1 + 'xyzw'.index(component)]))[0]
    require(value == expected, 'lobe coefficient literal changed')
    return {'register': register, 'component': component, 'definition_dword': definition['dword'],
            'literal_dword': definition['dword'] + 2 + 'xyzw'.index(component), 'value': value}


def sixth_power(decoded, dot, multiplies, consume):
    """Prove the reviewed scalar x²/x⁴/x⁶ chain and its last live consumer."""
    dot_site = expect(decoded, dot, 'dp3', ('r0', 'w'), [('r1', 'xyzw'), ('r2', 'xyzw')], PP + ('saturate',))
    live = {('r0', 'w'): (1, dot)}
    sites = []
    for at, expected_power in zip(multiplies, (2, 4, 6)):
        row = site(decoded, at)
        destination = row['destination']
        require(row['opcode'] == 'mul' and destination and len(destination['mask']) == 1 and
                destination['modifiers'] == list(PP) and len(row['sources']) == 2,
                'specular power instruction changed')
        exponents = []
        for source in row['sources']:
            swizzle = source['swizzle']
            lane = (source['name'], swizzle[0])
            require(len(set(swizzle)) == 1 and not source.get('relative') and
                    source['source_modifier'] == 0 and lane in live, 'specular power source changed')
            exponent, writer = live[lane]
            no_lane_writes(decoded, lane[0], lane[1], writer, at)
            exponents.append(exponent)
        require(sum(exponents) == expected_power, 'specular power exponent changed')
        result_lane = (destination['name'], destination['mask'])
        live[result_lane] = (expected_power, at)
        sites.append(row)
    no_lane_writes(decoded, result_lane[0], result_lane[1], multiplies[-1], consume)
    consumer = site(decoded, consume)
    require(any(source['name'] == result_lane[0] and source['swizzle'] == result_lane[1] * 4
                and source['source_modifier'] == 0 for source in consumer['sources']), 'sixth power result not consumed')
    return {'power': 6, 'dot': dot_site, 'multiply_sites': sites,
            'response_multiply': consumer, 'result_live_until_dword': consume}


def prove_shared_lobe(decoded, key):
    """Six individually bound shared-family shapes; no Argon offset reuse."""
    require(PIXEL_FAMILY[key] == 'shared_default', 'not the shared DEFAULT family')
    base = key in ('3b94320087e81945', 'e3b7acc16da9932d')
    tex, affine, _, _, _ = PIXELS[key]
    if base:
        shift = 0 if key == '3b94320087e81945' else 32
        half_reg, half_lane = ('c8', 'w') if shift == 0 else ('c9', 'x')
        three_reg, three_lane = 'c8', 'z' if shift == 0 else 'w'
        diffuse = expect(decoded, 1201 + shift, 'mad', ('r1', 'xyz'),
                         [('r3', 'xyzw'), (half_reg, half_lane * 4), ('r4', 'xyzw')], PP)
        cube = expect(decoded, 1229 + shift, 'mul', ('r2', 'xyz'),
                      [('r0', 'xyzw'), (half_reg, half_lane * 4)], PP)
        expect(decoded, 1126 + shift, 'mul', ('r3', 'w'), [('r2', 'wwww'), (three_reg, three_lane * 4)], PP + ('saturate',))
        expect(decoded, 1154 + shift, 'mul', ('r1', 'z'), [('r1', 'wwww'), (three_reg, three_lane * 4)], PP + ('saturate',))
        expect(decoded, 1179 + shift, 'mul', ('r0', 'w'), [('r0', 'xxxx'), (three_reg, three_lane * 4)], PP)
        expect(decoded, 1134 + shift, 'mul', ('r1', 'y'), [('r1', 'wwww'), ('r3', 'wwww')], PP)
        expect(decoded, 1162 + shift, 'mul', ('r0', 'w'), [('r0', 'wwww'), ('r1', 'zzzz')], PP)
        powers = [sixth_power(decoded, 1093 + shift, [at + shift for at in (1097, 1101, 1109)], 1134 + shift),
                  sixth_power(decoded, 1130 + shift, [at + shift for at in (1138, 1146, 1150)], 1162 + shift)]
    else:
        # dot, half DEF, packed swizzle, specular-strength DEF, lobe combine, cube scale
        facts = {
            '7a14d4dcb28f27e5': (1075, 'c6', 'w', 'zwzw', 'c6', 'z', 1123, 1152),
            '8ab6188a40ca15ea': (1107, 'c6', 'y', 'xyzw', 'c7', 'w', 1155, 1184),
            '8df6143d0e77d92e': (173, 'c3', 'y', 'xyzw', 'c3', 'x', 212, 233),
            'e16a9806ee3544c3': (205, 'c4', 'y', 'xyzw', 'c3', 'w', 244, 265),
        }
        dot, half_reg, half_lane, packed_swizzle, three_reg, three_lane, lobe, cube_at = facts[key]
        packed = 'r3' if affine else 'r1'
        diffuse = expect(decoded, dot + 16, 'mul', (packed, 'xy'), [('r0', 'yyyy'), (half_reg, packed_swizzle)], PP)
        # The packed X lane and the later mask multiply both use literal three.
        packed_three = literal_component(decoded, half_reg, packed_swizzle[0], 3.0)
        expect(decoded, dot + 24, 'mov', ('r0', 'w'), [(packed, 'xxxx')], PP + ('saturate',))
        expect(decoded, dot + 27, 'mul', ('r1', 'w'), [('r0', 'zzzz'), ('r0', 'wwww')], PP)
        expect(decoded, lobe, 'mad', ('r0', 'w'), [('r0', 'wwww'), (three_reg, three_lane * 4), (packed, 'yyyy')], PP)
        cube = expect(decoded, cube_at, 'mul', ('r2' if affine else 'r3', 'xyz'),
                      [('r0', 'xyzw'), (half_reg, half_lane * 4)], PP)
        powers = [sixth_power(decoded, dot, [dot + 8, dot + 12, dot + 20], dot + 27)]
    # The cube scalar modifies mask*albedo before the s3 sample multiply.
    pre_cube = cube['instruction_dword'] - (8 if base else 9)
    # Base: mask*albedo is eight DWORDs before cube scale. Single: the
    # intervening directional MAD is five DWORDs, so the gap is nine.
    expect(decoded, pre_cube, 'mul', ('r0', 'xyz'), [('r0', 'xxxx'), ('r3' if affine else 'r1', 'xyzw')], PP)
    expect(decoded, tex[3] + 4, 'mul', ('r0', 'xyz'), [('r2' if affine else 'r3', 'xyzw'), ('r0', 'xyzw')], PP)
    # Prove the links as well as isolated coefficients: sampled s1.x remains
    # live until both specular weighting and mask*albedo, and every diffuse /
    # specular producer survives until the exact consumer named below.
    links = []

    def live(register, lanes, producer, consumer, role):
        for lane in lanes:
            no_lane_writes(decoded, register, lane, producer, consumer)
        links.append({'role': role, 'register': register, 'lanes': lanes,
                      'producer_dword': producer, 'consumer_dword': consumer})

    expect(decoded, tex[1], 'texld', ('r0', 'xyzw'), [('v1', 'xyzw'), ('s1', 'xyzw')], PP)
    expect(decoded, tex[3], 'texld', ('r0', 'xyzw'), [('v4', 'xyzw'), ('s3', 'xyzw')], PP)
    live('r0', 'x', tex[1], pre_cube, 'specular_mask_to_reflection')
    live('r0', 'xyz', pre_cube, cube['instruction_dword'], 'masked_albedo_to_cube_scale')
    live('r2' if affine else 'r3', 'xyz', cube['instruction_dword'], tex[3] + 4, 'scaled_albedo_to_cube_sample')
    if base:
        expect(decoded, 1117 + shift, 'dp3', ('r2', 'w'), [('r0', 'xyzw'), ('c6', 'xyzw')], PP + ('saturate',))
        expect(decoded, 1142 + shift, 'dp3', ('r1', 'w'), [('r0', 'xyzw'), ('c4', 'xyzw')], PP + ('saturate',))
        expect(decoded, 1158 + shift, 'mul', ('r0', 'xyz'), [('r1', 'yyyy'), ('c7', 'xyzw')], PP)
        expect(decoded, 1166 + shift, 'mul', ('r2', 'xyz'), [('r2', 'wwww'), ('c7', 'xyzw')], PP)
        expect(decoded, 1170 + shift, 'mad', ('r1', 'xyz'), [('r0', 'wwww'), ('c5', 'xyzw'), ('r0', 'xyzw')], PP)
        expect(decoded, 1183 + shift, 'mad', ('r3', 'xyz'), [('r1', 'wwww'), ('c5', 'xyzw'), ('r2', 'xyzw')], PP)
        expect(decoded, 1188 + shift, 'mul', ('r4', 'xyz'), [('r1', 'xyzw'), ('r0', 'wwww')], PP)
        for register, lanes, producer, consumer, role in (
            ('r2', 'w', 1117, 1126, 'light1_cosine_to_response'),
            ('r3', 'w', 1126, 1134, 'light1_response_to_sixth_power'),
            ('r2', 'w', 1117, 1166, 'light1_cosine_to_diffuse_rgb'),
            ('r1', 'w', 1142, 1154, 'light0_cosine_to_response'),
            ('r1', 'z', 1154, 1162, 'light0_response_to_sixth_power'),
            ('r1', 'w', 1142, 1183, 'light0_cosine_to_diffuse_rgb'),
            ('r1', 'y', 1134, 1158, 'light1_lobe_to_specular_rgb'),
            ('r0', 'w', 1162, 1170, 'light0_lobe_to_specular_rgb'),
            ('r0', 'xyz', 1158, 1170, 'light1_specular_to_rgb_sum'),
            ('r2', 'xyz', 1166, 1183, 'light1_diffuse_to_rgb_sum'),
            ('r1', 'xyz', 1170, 1188, 'specular_rgb_sum_to_mask'),
            ('r0', 'x', 1175, 1179, 'specular_mask_to_strength'),
            ('r0', 'w', 1179, 1188, 'scaled_mask_to_specular_rgb'),
            ('r3', 'xyz', 1183, 1201, 'diffuse_rgb_sum_to_half_scale'),
            ('r4', 'xyz', 1188, 1201, 'masked_specular_rgb_to_lobe_sum'),
        ):
            live(register, lanes, producer + shift, consumer + shift, role)
    else:
        direction = 'c4' if affine else 'c1'
        expect(decoded, dot + 4, 'dp3', ('r0', 'y'), [('r0', 'xyzw'), (direction, 'xyzw')], PP + ('saturate',))
        expect(decoded, tex[1] + 4, 'mul', ('r0', 'w'), [('r1', 'wwww'), ('r0', 'xxxx')], PP)
        direct_register, direct_sites = next(iter(PIXELS[key][3].items()))
        direct_at = direct_sites[0]
        color_sum = 'r1' if affine else 'r2'
        expect(decoded, direct_at, 'mad', (color_sum, 'xyz'),
               [('r0', 'wwww'), (f'c{direct_register}', 'xyzw'), (color_sum, 'xyzw')], PP)
        live('r0', 'y', dot + 4, dot + 16, 'cosine_to_packed_diffuse_response')
        live(packed, 'x', dot + 16, dot + 24, 'packed_response_to_saturation')
        live(packed, 'y', dot + 16, lobe, 'half_diffuse_to_lobe_sum')
        live('r0', 'w', dot + 24, dot + 27, 'saturated_response_to_sixth_power')
        live('r1', 'w', dot + 27, tex[1] + 4, 'specular_response_to_mask')
        live('r0', 'x', tex[1], tex[1] + 4, 'specular_mask_to_response')
        live('r0', 'w', tex[1] + 4, lobe, 'masked_specular_to_strength_and_diffuse_sum')
        live('r0', 'w', lobe, direct_at, 'lobe_sum_to_light_rgb')
    result = {'verified_producer_consumer_links': links,
              'diffuse_and_cube_literal': literal_component(decoded, half_reg, half_lane, 0.5),
              'specular_strength_literal': literal_component(decoded, three_reg, three_lane, 3.0),
              'diffuse_coefficient_site': diffuse, 'cube_coefficient_site': cube, 'specular_power_chains': powers}
    if not base:
        result['packed_response_three_literal'] = packed_three
    return result


def prove_chain(decoded, specifications):
    """Exact reviewed straight-line sites also exclude interleaved clobbers.

    This is a bounded predicate over one semantic chain, not an evaluator. Full
    instruction occupancy between its endpoints proves the recorded ordering.
    """
    rows = [expect(decoded, *args) for args in specifications]
    offsets = sorted(row['instruction_dword'] for row in rows)
    actual = sorted(at for at in decoded if offsets[0] <= at <= offsets[-1])
    require(actual == offsets, 'chain instruction inventory/liveness changed')
    return rows


def prove_bump_vertex(decoded, loop, profile):
    """Bind geometric basis/view, original point response, RGB and fog alpha."""
    shift = 0 if loop else -45
    local = 'c42' if loop else 'c21'
    normal_base = 31 if loop else 10
    literal_component(decoded, local, 'x', 1.0)
    literal_component(decoded, local, 'y', 0.0)
    if loop:
        literal_component(decoded, local, 'z', 3.0)
    geometry = [expect(decoded, 366, 'mad', ('r0', 'xyzw'),
                       [('v2', 'xyzx'), (local, 'xxxy'), (local, 'yyyx')])]
    for at, lane, c in ((375, 'z', 2), (379, 'x', 0), (383, 'y', 1)):
        geometry.append(expect(decoded, at, 'dp4', ('r5', lane), [('r0', 'xyzw'), (f'c{normal_base+c}', 'xyzw')]))
    # Fixed-point r1 normalization occurs between the homogeneous normal and its
    # DP4s, but does not modify the normal source. Neither basis is normalized.
    for lane in 'xyzw':
        no_lane_writes(decoded, 'r0', lane, 366, 383)
    # The point and view vectors share the same original world position.
    geometry.append(expect(decoded, 344 if loop else 326, 'mad', ('r2', 'xyzw'),
                           [('v0', 'xyzx'), (local, 'xxxy'), (local, 'yyyx')]))
    world = ((354, 'z', 30), (362, 'x', 28), (371, 'y', 29)) if loop else ((331, 'z', 9), (335, 'x', 7), (339, 'y', 8))
    for at, lane, constant in world:
        geometry.append(expect(decoded, at, 'dp4', ('r3', lane), [('r2', 'xyzw'), (f'c{constant}', 'xyzw')]))
        no_lane_writes(decoded, 'r3', lane, at, 506 + shift)
    for i, lane in enumerate('xyzw'):
        geometry.append(expect(decoded, 477 + shift + 4*i, 'dp4', ('o0', lane),
                               [('r2', 'xyzw'), (f'c{(24 if loop else 0)+i}', 'xyzw')]))
        no_lane_writes(decoded, 'r2', lane, 344 if loop else 326, 489 + shift)
    require(lane_writes(decoded, 'o0') == [477 + shift + 4*i for i in range(4)], 'clip output inventory changed')
    require(not any(row['item']['opcode'] == 36 for row in decoded.values()), 'unexpected vertex normalization')
    declarations = {d['name']: d for d in profile['declarations']}
    for register, usage in (('v2', 'normal'), ('v3', 'binormal'), ('v4', 'tangent')):
        require(declarations[register]['usage_name'] == usage and declarations[register]['mask'] == 'xyzw', 'basis input declaration changed')
    for register, semantic in (('o3', 1), ('o4', 2), ('o5', 3), ('o6', 4)):
        require((declarations[register]['usage_name'], declarations[register]['usage_index'], declarations[register]['mask'], declarations[register]['modifiers']) ==
                ('texcoord', semantic, 'xyz', []), 'basis output declaration changed')
    require(declarations['o2']['modifiers'] == (['centroid'] if profile['id'].endswith('4944d81dfe531b37') else []), 'UV centroid contract changed')
    if loop:
        point = prove_chain(decoded, [
            (387, 'mov', ('r0', 'xyz'), [(local, 'yyyy')]),
            (390, 'mov', ('r0', 'w'), [(local, 'yyyy')]),
            (393, 'rep', None, [('i0', 'xyzw')]),
            (395, 'mul', ('r1', 'w'), [('r0', 'wwww'), (local, 'zzzz')]),
            (399, 'mova', ('a0', 'w'), [('r1', 'wwww')]),
            (402, 'add', ('r6', 'xyz'), [('r3', 'xyzw', 1), ('c0', 'xyzw', 0, 'a0', 'w')]),
            (407, 'dp3', ('r1', 'z'), [('r6', 'xyzw'), ('r6', 'xyzw')]),
            (411, 'rsq', ('r1', 'w'), [('r1', 'zzzz')]),
            (414, 'mul', ('r6', 'xyz'), [('r6', 'xyzw'), ('r1', 'wwww')]),
            (418, 'mul', ('r1', 'y'), [('r1', 'zzzz'), ('r1', 'wwww')]),
            (422, 'mov', ('r1', 'x'), [(local, 'xxxx')]),
            (425, 'dp3', ('r3', 'w'), [('r5', 'xyzw'), ('r6', 'xyzw')], ('saturate',)),
            (429, 'dp3', ('r1', 'w'), [('c2', 'xyzw', 0, 'a0', 'w'), ('r1', 'xyzw')]),
            (434, 'rcp', ('r1', 'w'), [('r1', 'wwww')], ('saturate',)),
            (437, 'mul', ('r1', 'xyz'), [('r3', 'wwww'), ('c1', 'xyzw', 0, 'a0', 'w')]),
            (442, 'mad', ('r0', 'xyz'), [('r1', 'xyzw'), ('r1', 'wwww'), ('r0', 'xyzw')]),
            (447, 'add', ('r0', 'w'), [('r0', 'wwww'), (local, 'xxxx')]),
            (451, 'endrep', None, []),
            (452, 'add', ('o1', 'xyz'), [('r0', 'xyzw'), ('c40', 'xyzw')]),
        ])
        require(lane_writes(decoded, 'a0') == [399] and [at for at, _ in uses(decoded, 'i0')] == [393], 'point loop control changed')
        require([(at, src['name'], src.get('address_register'), src.get('address_component'))
                 for at, row in decoded.items() for src in row['sources'] if src['relative']] ==
                [(402, 'c0', 'a0', 'w'), (429, 'c2', 'a0', 'w'), (437, 'c1', 'a0', 'w')], 'point relative inventory changed')
    else:
        point = [expect(decoded, *args) for args in [
            (343, 'add', ('r1', 'xyz'), [('r3', 'xyzw', 1), ('c4', 'xyzw')]),
            (347, 'dp3', ('r6', 'z'), [('r1', 'xyzw'), ('r1', 'xyzw')]),
            (351, 'rsq', ('r1', 'w'), [('r6', 'zzzz')]),
            (359, 'mul', ('r6', 'y'), [('r6', 'zzzz'), ('r1', 'wwww')]),
            (363, 'mov', ('r6', 'x'), [(local, 'xxxx')]),
            (371, 'mul', ('r1', 'xyz'), [('r1', 'xyzw'), ('r1', 'wwww')]),
            (387, 'dp3', ('r0', 'w'), [('c6', 'xyzw'), ('r6', 'xyzw')]),
            (391, 'dp3', ('r0', 'z'), [('r5', 'xyzw'), ('r1', 'xyzw')], ('saturate',)),
            (395, 'rcp', ('r0', 'w'), [('r0', 'wwww')], ('saturate',)),
            (398, 'mul', ('r0', 'xyz'), [('r0', 'zzzz'), ('c5', 'xyzw')]),
            (406, 'mad', ('o1', 'xyz'), [('r0', 'xyzw'), ('r0', 'wwww'), ('c19', 'xyzw')]),
        ]]
        require(not any(src['relative'] for row in decoded.values() for src in row['sources']), 'fixed point became relative')
        for name, lanes, begin, end in [('r6', 'z', 347, 387), ('r6', 'y', 359, 387), ('r6', 'x', 363, 387),
                                       ('r1', 'w', 351, 371), ('r1', 'xyz', 343, 371), ('r1', 'xyz', 371, 391),
                                       ('r0', 'w', 395, 406), ('r0', 'xyz', 398, 406)]:
            for lane in lanes:
                no_lane_writes(decoded, name, lane, begin, end)
    for lane in 'xyz':
        no_lane_writes(decoded, 'r5', lane, {'z':375, 'x':379, 'y':383}[lane], 474 + shift)
    tangent = expect(decoded, 456 + shift, 'mad', ('r1', 'xyzw'), [('v4', 'xyzx'), (local, 'xxxy'), (local, 'yyyx')])
    binormal = expect(decoded, 461 + shift, 'mad', ('r0', 'xyzw'), [('v3', 'xyzx'), (local, 'xxxy'), (local, 'yyyx')])
    geometry.extend([tangent, binormal, expect(decoded, 474 + shift, 'mov', ('o4', 'xyz'), [('r5', 'xyzw')])])
    for output, source, offsets in [('o5', 'r1', (466, 539, 543)), ('o6', 'r0', (470, 547, 551))]:
        start = 456 + shift if output == 'o5' else 461 + shift
        for at, lane, c in zip(offsets, 'zxy', (2, 0, 1)):
            geometry.append(expect(decoded, at + shift, 'dp4', (output, lane), [(source, 'xyzw'), (f'c{normal_base+c}', 'xyzw')]))
        for lane in 'xyzw':
            no_lane_writes(decoded, source, lane, start, offsets[-1] + shift)
        require(lane_writes(decoded, output) == [at + shift for at in offsets], 'basis output inventory changed')
    alpha, fog = ('c39', 'c41') if loop else ('c18', 'c20')
    view = [expect(decoded, 506 + shift, 'add', ('r2', 'xyz'), [('r3', 'xyzw', 1), ('r2', 'xyzw')])]
    camera = ((493, 'y', 35), (496, 'x', 34), (499, 'z', 36)) if loop else ((448, 'x', 13), (451, 'y', 14), (454, 'z', 15))
    for at, lane, c in camera:
        view.append(expect(decoded, at, 'mov', ('r2', lane), [(f'c{c}', 'wwww')]))
        no_lane_writes(decoded, 'r2', lane, at, 506 + shift)
    fog_sites = prove_chain(decoded, [
        (510 + shift, 'if', None, [('b0', 'xyzw')]),
        (512 + shift, 'dp3', ('r2', 'w'), [('r2', 'xyzw'), ('r2', 'xyzw')]),
        (516 + shift, 'rsq', ('r2', 'w'), [('r2', 'wwww')]),
        (519 + shift, 'rcp', ('r2', 'w'), [('r2', 'wwww')]),
        (522 + shift, 'mad', ('r2', 'w'), [(fog, 'yyyy'), ('r2', 'wwww', 1), (fog, 'xxxx')], ('saturate',)),
        (527 + shift, 'mul', ('o1', 'w'), [('r2', 'wwww'), (alpha, 'xxxx')]),
        (531 + shift, 'else', None, []), (532 + shift, 'mov', ('o1', 'w'), [(alpha, 'xxxx')]),
        (535 + shift, 'endif', None, []),
        (536 + shift, 'mov', ('o3', 'xyz'), [('r2', 'xyzw')]),
    ])
    for lane in 'xyz':
        no_lane_writes(decoded, 'r2', lane, 506 + shift, 536 + shift)
    require(lane_writes(decoded, 'o1') == [452 if loop else 406, 527 + shift, 532 + shift], 'COLOR0 output inventory changed')
    require(lane_writes(decoded, 'o3') == [536 + shift] and lane_writes(decoded, 'o4') == [474 + shift], 'normal/view output inventory changed')
    flow = [(at, motion.OPCODES[r['item']['opcode']]) for at, r in decoded.items() if motion.OPCODES[r['item']['opcode']] in motion.FLOW_OPCODES]
    require(flow == ([(393, 'rep'), (451, 'endrep')] if loop else []) + [(510 + shift, 'if'), (531 + shift, 'else'), (535 + shift, 'endif')], 'bump vertex flow changed')
    return {'point_sites': point, 'point_uses_unnormalized_geometric_normal': True,
            'basis_sites': geometry, 'basis_declarations': [declarations[name] for name in ('v2','v3','v4','o3','o4','o5','o6')], 'view_sites': view, 'fog_alpha_sites': fog_sites,
            'normalization_in_vertex': False, 'runtime_count_range_required': [0, 8] if loop else None}


def prove_bump_pixel(decoded, key):
    """Six exact normal/lobe/reflection/opacity chains; no color math on data."""
    tex, affine, clamp, direct, final = BUMP_PIXELS[key]
    base, face = len(direct) == 2, key in ('5e0a10fe752b6140', 'd086fde54698070c', 'f17fffd88d134b04')
    local = 'c9' if base else ('c6' if affine else 'c3')
    # Local components differ between the one/two-sided and non-affine originals.
    one, zero, two, minus = ('x', 'z', 'w', 'y') if face and (base or affine) else (
        ('x', 'w', 'z', 'y') if face else (('x', 'y', 'z', 'w') if base or affine else ('z', None, 'x', 'y')))
    literals = [literal_component(decoded, local, lane, value) for lane, value in ((one, 1.), (two, 2.), (minus, -1.))]
    if zero:
        literals.append(literal_component(decoded, local, zero, 0.))
    n = tex[1]
    normal_specs = [
        (n, 'texld', ('r0', 'xyzw'), [('v1', 'xyzw'), ('s1', 'xyzw')], PP),
        (n+4, 'mad', ('r1', 'xy'), [(local, two*4), ('r0', 'wyzw'), (local, minus*4)], PP),
        (n+9, 'dp2add', ('r0', 'w'), [('r1', 'xyzw'), ('r1', 'xyzw', 1), (local, one*4)], PP),
        (n+14, 'mul', ('r0', 'xyz'), [('r1', 'yyyy'), ('v4', 'xyzw')], PP),
        (n+18, 'rsq', ('r0', 'w'), [('r0', 'wwww')], PP),
        (n+21, 'mad', ('r0', 'xyz'), [('r1', 'xxxx'), ('v5', 'xyzw'), ('r0', 'xyzw')], PP),
        (n+26, 'rcp', ('r0', 'w'), [('r0', 'wwww')], PP),
        (n+29, 'mad', ('r1', 'xyz'), [('r0', 'wwww'), ('v3', 'xyzw'), ('r0', 'xyzw')], PP),
    ]
    if face:
        normal_specs += [
            (n+34, 'cmp', ('r0', 'w'), [('vFace', 'xyzw'), (local, one*4), (local, minus*4)]),
            (n+39, 'cmp', ('r0', 'w'), [('r0', 'wwww', 1), (local, zero*4), (local, one*4)], PP),
            (n+44, 'cmp', ('r1', 'w'), [('vFace', 'xyzw'), (local, zero*4), (local, one*4)], PP),
            (n+49, 'nrm', ('r0', 'xyz'), [('r1', 'xyzw')], PP),
            (n+52, 'add', ('r0', 'w'), [('r0', 'wwww'), ('r1', 'wwww', 1)], PP),
            (n+56, 'mul', ('r0', 'xyz'), [('r0', 'xyzw'), ('r0', 'wwww')], PP),
        ]
    else:
        normal_specs.append((n+34, 'nrm', ('r0', 'xyz'), [('r1', 'xyzw')], PP))
    normal = prove_chain(decoded, normal_specs)
    a = n + (60 if face else 37)
    direction = 'c6' if base else ('c4' if affine else 'c1')
    strength = 'c8' if base else ('c7' if affine else 'c4')
    strength_reg, strength_lane = (('c3', 'w') if key == '68915563dd0aac9a' else (strength, 'x'))
    literals += [literal_component(decoded, strength, 'y', 0.4000000059604645),
                 literal_component(decoded, strength, 'x', 3.),
                 literal_component(decoded, strength_reg, strength_lane, 3.)]
    angular_specs = [
        (a, 'dp3', ('r0', 'w'), [(direction, 'xyzw', 1), ('r0', 'xyzw')], PP),
        (a+4, 'add', ('r0', 'w'), [('r0', 'wwww'), ('r0', 'wwww')], PP),
        (a+8, 'mad', ('r2', 'xyz'), [('r0', 'xyzw'), ('r0', 'wwww', 1), (direction, 'xyzw', 1)], PP),
        (a+13, 'nrm', ('r1', 'xyz'), [('v2', 'xyzw')], PP),
        (a+16, 'dp3', ('r0', 'w'), [('r2', 'xyzw'), ('r1', 'xyzw')], PP+('saturate',)),
        (a+20, 'mul', ('r1', 'w'), [('r0', 'wwww'), ('r0', 'wwww')], PP),
    ]
    if base:
        angular_specs += [
            (a+24, 'mul', ('r1', 'w'), [('r1', 'wwww'), ('r1', 'wwww')], PP),
            (a+28, 'mul', ('r1', 'w'), [('r0', 'wwww'), ('r1', 'wwww')], PP),
            (a+32, 'dp3', ('r2', 'w'), [('c4', 'xyzw', 1), ('r0', 'xyzw')], PP),
            (a+36, 'dp3', ('r0', 'w'), [('r0', 'xyzw'), ('c6', 'xyzw')], PP+('saturate',)),
            (a+40, 'add', ('r2', 'z'), [('r2', 'wwww'), ('r2', 'wwww')], PP),
            (a+44, 'mul', ('r2', 'w'), [('r0', 'wwww'), ('c8', 'xxxx')], PP+('saturate',)),
            (a+48, 'mad', ('r2', 'xyz'), [('r0', 'xyzw'), ('r2', 'zzzz', 1), ('c4', 'xyzw', 1)], PP),
            (a+53, 'mul', ('r3', 'xyz'), [('r0', 'wwww'), ('c7', 'xyzw')], PP),
            (a+57, 'dp3', ('r0', 'w'), [('r2', 'xyzw'), ('r1', 'xyzw')], PP+('saturate',)),
            (a+61, 'mul', ('r2', 'z'), [('r1', 'wwww'), ('r2', 'wwww')], PP),
            (a+65, 'mul', ('r1', 'w'), [('r0', 'wwww'), ('r0', 'wwww')], PP),
            (a+69, 'mul', ('r2', 'w'), [('r1', 'wwww'), ('r1', 'wwww')], PP),
            (a+73, 'dp3', ('r1', 'w'), [('r0', 'xyzw'), ('c4', 'xyzw')], PP+('saturate',)),
            (a+77, 'mul', ('r0', 'w'), [('r0', 'wwww'), ('r2', 'wwww')], PP),
            (a+81, 'mul', ('r2', 'w'), [('r1', 'wwww'), ('c8', 'xxxx')], PP+('saturate',)),
            (a+85, 'mul', ('r2', 'xyz'), [('r2', 'zzzz'), ('c7', 'xyzw')], PP),
            (a+89, 'mul', ('r0', 'w'), [('r0', 'wwww'), ('r2', 'wwww')], PP),
            (a+93, 'mad', ('r3', 'xyz'), [('r1', 'wwww'), ('c5', 'xyzw'), ('r3', 'xyzw')], PP),
            (a+98, 'mad', ('r4', 'xyz'), [('r0', 'wwww'), ('c5', 'xyzw'), ('r2', 'xyzw')], PP),
            (a+103, 'texld', ('r2', 'xyzw'), [('v1', 'xyzw'), ('s2', 'xyzw')], PP),
            (a+107, 'mul', ('r0', 'w'), [('r2', 'xxxx'), ('c8', 'xxxx')], PP),
            (a+111, 'mul', ('r4', 'xyz'), [('r4', 'xyzw'), ('r0', 'wwww')], PP),
        ]
    else:
        angular_specs += [
            (a+24, 'dp3', ('r2', 'w'), [('r0', 'xyzw'), (direction, 'xyzw')], PP+('saturate',)),
            (a+28, 'mul', ('r1', 'w'), [('r1', 'wwww'), ('r1', 'wwww')], PP),
            (a+32, 'mul', ('r3', 'xy'), [('r2', 'wwww'), (strength, 'xyzw')], PP),
            (a+36, 'mul', ('r1', 'w'), [('r0', 'wwww'), ('r1', 'wwww')], PP),
            (a+40, 'mov', ('r0', 'w'), [('r3', 'xxxx')], PP+('saturate',)),
            (a+43, 'mul', ('r0', 'w'), [('r1', 'wwww'), ('r0', 'wwww')], PP),
            (a+47, 'texld', ('r2', 'xyzw'), [('v1', 'xyzw'), ('s2', 'xyzw')], PP),
            (a+51, 'mul', ('r1', 'w'), [('r0', 'wwww'), ('r2', 'xxxx')], PP),
        ]
    angular = prove_chain(decoded, angular_specs)
    # Geometric normal and normalized view survive scalar lobe packing until
    # the per-pixel reflection coordinate. Exact tail includes final lobe sum.
    reflect = a + (115 if base else 55)
    clamp_reg = 'r6' if base else ('r5' if affine else 'r4')
    tail_specs = [(reflect, 'dp3', ('r0', 'w'), [('r1', 'xyzw', 1), ('r0', 'xyzw')], PP)]
    tail_specs += [(reflect+4, 'mad', ('r5', 'xyz'), [('r3', 'xyzw'), (strength, 'yyyy'), ('r4', 'xyzw')], PP)] if base else [
        (reflect+4, 'mad', ('r2', 'w'), [('r1', 'wwww'), (strength_reg, strength_lane*4), ('r3', 'yyyy')], PP)]
    tail_specs += [
        (reflect+9, 'add', ('r0', 'w'), [('r0', 'wwww'), ('r0', 'wwww')], PP),
        (clamp, 'mov', (clamp_reg, 'xyz'), [('v0', 'xyzw')], PP+('saturate',)),
        (reflect+16, 'mad', ('r0', 'xyz'), [('r0', 'xyzw'), ('r0', 'wwww', 1), ('r1', 'xyzw', 1)], PP),
        (tex[4], 'texld', ('r0', 'xyzw'), [('r0', 'xyzw'), ('s4', 'xyzw')], PP),
        (tex[0], 'texld', ('r1', 'xyzw'), [('v1', 'xyzw'), ('s0', 'xyzw')], PP),
    ]
    if affine:
        tail_specs.append((tex[0]+4, 'mad', ('r3', 'xyzw'), [('r1', 'xyzx'), (local, one*3+zero), (local, zero*3+one)]))
        for i, lane in enumerate('xyz'):
            tail_specs.append((affine-8+4*i, 'dp4', ('r4', lane), [('r3', 'xyzw'), (f'c{i}', 'xyzw')], PP))
    tint = affine+4 if affine else tex[0]+4
    albedo, tint_reg, lit_reg = ('r4', 'r2', 'r1') if affine else ('r1', 'r3', 'r2')
    tail_specs += [
        (tint, 'mul', (tint_reg, 'xyz'), [('r2', 'xxxx'), (albedo, 'xyzw')], PP),
        ((tint+4, 'add', ('r1', 'xyz'), [('r5', 'xyzw'), ('r6', 'xyzw')], PP) if base else
         (tint+4, 'mad', (lit_reg, 'xyz'), [('r2', 'wwww'), (f'c{next(iter(direct))}', 'xyzw'), (clamp_reg, 'xyzw')], PP)),
        (tint+(8 if base else 9), 'mul', ('r0', 'xyz'), [('r0', 'xyzw'), (tint_reg, 'xyzw')], PP),
        (tint+(12 if base else 13), 'mad', ('r1', 'xyz'), [(lit_reg, 'xyzw'), (albedo, 'xyzw'), ('r0', 'xyzw')], PP),
        (tex[3], 'texld', ('r0', 'xyzw'), [('v1', 'xyzw'), ('s3', 'xyzw')], PP),
        (tex[3]+4, 'lrp', ('r2', 'w'), [('c3' if affine else 'c0', 'xxxx'), ('r0', 'wwww'), ('r1', 'wwww')], PP),
        (final, 'add', ('oC0', 'xyz'), [('r1', 'xyzw'), ('r0', 'xyzw')], PP),
        (final+4, 'mul', ('oC0', 'w'), [('r2', 'wwww'), ('v0', 'wwww')], PP),
    ]
    tail = prove_chain(decoded, tail_specs)
    # Occupancy checks across all three adjacent chains forbid unseen writes;
    # explicit lane checks bind the important inter-chain producer/consumers.
    all_sites = normal + angular + tail
    require(sorted(row['instruction_dword'] for row in all_sites) ==
            sorted(at for at, row in decoded.items() if row['item']['opcode'] not in (31, 81)), 'bump executable inventory changed')
    links = [('r0', 'xyz', normal[-1]['instruction_dword'], reflect+16),
             ('r1', 'xyz', a+13, reflect+16), ('r2', 'x', tex[2], tint),
             ('r1', 'w', tex[0], tex[3]+4), ('r0', 'w', tex[3], tex[3]+4),
             ('r2', 'w', tex[3]+4, final+4)]
    for name, lanes, begin, end in links:
        for lane in lanes:
            no_lane_writes(decoded, name, lane, begin, end)
    output_writes = [(at, r['destination']['name'], r['destination']['mask']) for at, r in decoded.items()
                     if r['destination'] and r['destination']['register_type'] in (4, 5, 6, 8, 9)]
    require(output_writes == [(final, 'oC0', 'xyz'), (final+4, 'oC0', 'w')], 'bump output inventory changed')
    return {'normal_reconstruction_sites': normal, 'angular_and_mask_sites': angular,
            'reflection_and_color_sites': tail, 'literal_sites': literals,
            'verified_live_ranges': [{'register': r, 'mask': lanes, 'producer_dword': begin, 'consumer_dword': end}
                                     for r, lanes, begin, end in links],
            'normal_channels': {'alpha': 'binormal_v5', 'green': 'tangent_v4', 'red_blue': 'unused'},
            'normal_result_register': 'r0', 'normal_result_dword': normal[-1]['instruction_dword'],
            'reflection_coordinate_dword': reflect+16, 'reflection_sampler': 4,
            'specular_power': 5, 'specular_mask_sampler': 2, 'specular_mask_channel': 'red',
            'diffuse_alpha_live_interval': [tex[0], tex[3]+4], 'lightmap_alpha_live_interval': [tex[3], tex[3]+4],
            'interpolated_alpha_live_interval': [tex[3]+4, final+4],
            'affine_rgb_sites': [site(decoded, affine-8+4*i) for i in range(3)] if affine else []}


def prove_extended_pixel(decoded, key):
    """Prove six bounded lighting schedules, including every live producer.

    The schedule is a semantic predicate over decoded originals, not an
    evaluator or a shader generator. Full executable occupancy excludes hidden
    writes between the normal, response, color and independent alpha chains.
    Application coefficients stay scalar operands; only the separately listed
    RGB sources are eligible for transfer conversion.
    """
    tex, affine, clamp, direct, final = EXTENDED_PIXELS[key]
    family = PIXEL_FAMILY[key]
    bump, xyz, standard, base = 'bump' in family, family == 'standard_bump_low', family != 'split_default', len(direct) == 2
    face = bool(uses(decoded, 'vFace'))
    direction = 'c6' if base else ('c4' if affine else 'c1')
    normal = 'r0' if bump else ('r2' if standard or base else 'r1')
    # Register assignments are the reviewed CTAB scalar roles, not literals.
    coefficient_base = 8 if base else 6 if affine else 3
    spec, power, reflection, diffuse = [f'c{coefficient_base+i}' for i in range(4)]
    if bump:
        local = 'c13' if base else 'c11' if affine else 'c7'
        one, zero, two, minus = ('x','z','w','y') if face and affine else ('x','y','z','w') if affine else ('x','w','z','y') if face else ('z','w','x','y')
        three = ('c12','x') if base else ('c10','x') if affine else ('c8','x') if face else ('c7','z' if xyz else 'w')
    elif standard:
        local = 'c12' if base else 'c10' if affine else 'c7'
        one, zero, minus = 'x', 'z' if face else 'y', 'y'
        three = (local, 'w' if face else 'z' if affine else 'x')
    else:
        local = 'c9' if base else 'c6' if affine else 'c3'
        one, zero, minus = 'x', 'z' if face else 'y', 'y'
        power = (local, 'w' if face else 'z' if affine else 'x')
        three = ('c8','x') if base and face else ('c9','w') if base else ('c7','x') if affine and face else ('c6','w') if affine else ('c4','x') if face else ('c3','y')
    literals = []
    def literal(reg, lane, value):
        literals.append(literal_component(decoded, reg, lane, value))
        return (reg, lane*4)
    three_source = literal(*three, 3.)
    power_source = (power, 'xxxx') if standard else literal(*power, 10.)
    if face or affine or bump:
        literal(local, one, 1.) if not (xyz and not affine and not face) else None
        if face or affine:
            literal(local, zero, 0.)
        if face or bump:
            literal(local, minus, -1.)
    if bump:
        literal(local, two, 2.)
    if not standard:
        half_reg, half_lane = ('c8', 'y' if face else 'x') if base else ('c7','y') if affine else ('c4','y') if face else ('c3','z')
        half_source = literal(half_reg, half_lane, .5)
        if not base:
            packed = ('c7','xyzw') if affine else ('c4','xyzw') if face else ('c3','yzzw')
            literal(packed[0], packed[1][0], 3.)
    executable = [at for at, row in decoded.items() if row['item']['opcode'] not in (31,81)]
    cursor = min(executable)
    specifications = []
    def emit(op, dst, sources, mods=PP):
        nonlocal cursor
        specifications.append((cursor, op, dst, sources, mods))
        cursor += 1 + bool(dst) + len(sources)
    def nrm(dst, source): emit('nrm',(dst,'xyz'),[(source,'xyzw')])
    def face_prefix(target, bump_normal=False):
        emit('cmp',('r0','w'),[('vFace','xyzw'),(local,one*4),(local,minus*4)],())
        emit('cmp',('r0','w'),[('r0','wwww',1),(local,zero*4),(local,one*4)])
        lane, reg = ('w','r1') if bump_normal else ('z','r0')
        emit('cmp',(reg,lane),[('vFace','xyzw'),(local,zero*4),(local,one*4)])
        if bump_normal:
            nrm('r0','r1')
        emit('add',('r0','w'),[('r0','wwww'),(reg,lane*4,1)])
        if not bump_normal:
            nrm('r0','v3')
        emit('mul',(target,'xyz'),[('r0','xyzw'),('r0','wwww')] if bump_normal else [('r0','wwww'),('r0','xyzw')])
    if bump:
        emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
        if xyz:
            emit('mad',('r0','xyz'),[(local,two*4),('r0','xyzw'),(local,minus*4)])
            emit('mul',('r1','xyz'),[('r0','yyyy'),('v4','xyzw')])
            emit('mad',('r1','xyz'),[('r0','xxxx'),('v5','xyzw'),('r1','xyzw')])
            emit('mad',('r1','xyz'),[('r0','zzzz'),('v3','xyzw'),('r1','xyzw')])
        else:
            emit('mad',('r1','xy'),[(local,two*4),('r0','wyzw'),(local,minus*4)])
            emit('dp2add',('r0','w'),[('r1','xyzw'),('r1','xyzw',1),(local,one*4)])
            emit('mul',('r0','xyz'),[('r1','yyyy'),('v4','xyzw')])
            emit('rsq',('r0','w'),[('r0','wwww')])
            emit('mad',('r0','xyz'),[('r1','xxxx'),('v5','xyzw'),('r0','xyzw')])
            emit('rcp',('r0','w'),[('r0','wwww')])
            emit('mad',('r1','xyz'),[('r0','wwww'),('v3','xyzw'),('r0','xyzw')])
        if face: face_prefix('r0',True)
        else: nrm('r0','r1')
    elif face: face_prefix(normal)
    else: nrm(normal,'v3')
    normal_count = len(specifications)
    # Reflection of light direction and normalized view feed the native POW.
    emit('dp3',('r0','w'),[(direction,'xyzw',1),(normal,'xyzw')])
    emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
    if base and not bump and not face: nrm('r1','v2')
    reflected = 'r2' if bump else 'r0'
    emit('mad',(reflected,'xyz'),[(normal,'xyzw'),('r0','wwww',1),(direction,'xyzw',1)])
    if not base and not standard:
        emit('dp3',('r0','w'),[(normal,'xyzw'),(direction,'xyzw')],PP+('saturate',))
    if bump or not base or face: nrm('r1','v2')
    if base:
        emit('dp3',('r0' if bump else 'r1','w'),[(reflected,'xyzw'),('r1','xyzw')],PP+('saturate',))
        emit('pow',('r1','w') if bump else ('r0','z'),[('r0','wwww') if bump else ('r1','wwww'),power_source])
        if bump:
            emit('dp3',('r2','z'),[('r0','xyzw'),('c6','xyzw')],PP+('saturate',))
            emit('mul',('r2','w'),[('r2','zzzz'),three_source],PP+('saturate',))
            emit('dp3',('r0','w'),[('c4','xyzw',1),('r0','xyzw')])
            emit('mul',('r3','xyz'),[('r2','zzzz'),('c7','xyzw')])
            emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r3','w'),[('r1','wwww'),('r2','wwww')])
            emit('mad',('r2','xyz'),[('r0','xyzw'),('r0','wwww',1),('c4','xyzw',1)])
            emit('dp3',('r2','w'),[('r2','xyzw'),('r1','xyzw')],PP+('saturate',))
            emit('dp3',('r1','w'),[('r0','xyzw'),('c4','xyzw')],PP+('saturate',))
            emit('pow',('r0','w'),[('r2','wwww'),power_source])
            emit('mul',('r2','w'),[('r1','wwww'),three_source],PP+('saturate',))
            emit('mul',('r2','xyz'),[('r3','wwww'),('c7','xyzw')])
            emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
            emit('mad',('r3','xyz'),[('r1','wwww'),('c5','xyzw'),('r3','xyzw')])
            emit('mad',('r4','xyz'),[('r0','wwww'),('c5','xyzw'),('r2','xyzw')])
        else:
            emit('dp3',('r2','w'),[('r2','xyzw'),('c6','xyzw')],PP+('saturate',))
            emit('dp3',('r0','w'),[('c4','xyzw',1),('r2','xyzw')])
            emit('mul',('r0','y'),[('r2','wwww'),three_source],PP+('saturate',))
            emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r3','w'),[('r0','zzzz'),('r0','yyyy')])
            emit('mad',('r0','xyz'),[('r2','xyzw'),('r0','wwww',1),('c4','xyzw',1)])
            emit('dp3',('r1','w'),[('r2','xyzw'),('c4','xyzw')],PP+('saturate',))
            emit('dp3',('r1','z'),[('r0','xyzw'),('r1','xyzw')],PP+('saturate',))
            emit('pow',('r0','w'),[('r1','zzzz'),power_source])
            emit('mul',('r1','z'),[('r1','wwww'),three_source],PP+('saturate',))
            emit('mul',('r0','xyz'),[('r3','wwww'),('c7','xyzw')])
            emit('mul',('r0','w'),[('r0','wwww'),('r1','zzzz')])
            emit('mul',('r2' if standard else 'r1','xyz'),[('r2','wwww'),('c7','xyzw')])
            emit('mad',('r1' if standard else 'r2','xyz'),[('r0','wwww'),('c5','xyzw'),('r0','xyzw')])
        emit('texld',('r2' if bump else 'r0','xyzw'),[('v1','xyzw'),('s2' if bump else 's1','xyzw')])
        emit('mul',('r0','w'),[('r2' if bump else 'r0','xxxx'),(spec,'xxxx') if standard else three_source])
        if not bump:
            emit('mad',('r3' if standard else 'r1','xyz'),[('r1','wwww'),('c5','xyzw'),('r2' if standard else 'r1','xyzw')])
        emit('mul',('r4' if standard else 'r2','xyz'),[('r4' if bump else 'r1' if standard else 'r2','xyzw'),('r0','wwww')])
        if not standard:
            emit('mad',('r4','xyz'),[('r1','xyzw'),half_source,('r2','xyzw')])
    else:
        if standard:
            if not bump: emit('dp3',('r2','x'),[('r2','xyzw'),(direction,'xyzw')],PP+('saturate',))
            emit('dp3',('r1','w'),[(reflected,'xyzw'),('r1','xyzw')],PP+('saturate',))
            if bump: emit('dp3',('r3','x'),[('r0','xyzw'),(direction,'xyzw')],PP+('saturate',))
            emit('pow',('r0','w'),[('r1','wwww'),power_source])
            emit('mul',('r1','w') if bump else ('r0','z'),[('r3' if bump else 'r2','xxxx'),three_source],PP+('saturate',))
            emit('mul',('r3' if bump else 'r2','y'),[('r0','wwww'),('r1','wwww') if bump else ('r0','zzzz')])
            emit('mov',('r2' if bump else 'r0','x'),[(diffuse,'xxxx')])
            emit('mov',('r2' if bump else 'r0','y'),[(spec,'xxxx')])
            packed_response = 'r3' if bump or affine else 'r1'
            emit('mul',(packed_response,'xy'),[('r3' if bump else 'r2','xyzw'),('r2' if bump else 'r0','xyzw')])
        else:
            emit('dp3',('r1','w'),[('r0','xyzw'),('r1','xyzw')],PP+('saturate',))
            emit('mul',('r1','xy'),[('r0','wwww'),packed])
            emit('pow',('r0','z'),[('r1','wwww'),power_source])
            emit('mov',('r0','w'),[('r1','xxxx')],PP+('saturate',))
            emit('mul',('r1','w'),[('r0','zzzz'),('r0','wwww')])
        emit('texld',('r2' if bump else 'r0','xyzw'),[('v1','xyzw'),('s2' if bump else 's1','xyzw')])
        if not standard:
            emit('mul',('r0','w'),[('r1','wwww'),('r0','xxxx')])
            emit('mad',('r0','w'),[('r0','wwww'),three_source,('r1','yyyy')])
        elif not bump and not affine:
            emit('mad',('r0','w'),[(packed_response,'yyyy'),('r0','xxxx'),(packed_response,'xxxx')])
    # Bump reflection uses the same reconstructed normal and normalized view.
    if bump:
        emit('dp3',('r0','w'),[('r1','xyzw',1),('r0','xyzw')])
        if base: emit('mad',('r5','xyz'),[('r3','xyzw'),(diffuse,'xxxx'),('r4','xyzw')])
        else: emit('mad',('r2','w'),[('r3','yyyy'),('r2','xxxx'),('r3','xxxx')])
        emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
        emit('mov',('r6' if base else 'r5' if affine else 'r4','xyz'),[('v0','xyzw')],PP+('saturate',))
        reflect_at = cursor
        emit('mad',('r0','xyz'),[('r0','xyzw'),('r0','wwww',1),('r1','xyzw',1)])
        emit('texld',('r0','xyzw'),[('r0','xyzw'),('s4','xyzw')])
    elif not affine:
        emit('mov',('r2' if standard else 'r1','xyz'),[('v0','xyzw')],PP+('saturate',))
        if not standard: emit('mad',('r2','xyz'),[('r0','wwww'),('c2','xyzw'),('r1','xyzw')])
    emit('texld',('r1','xyzw'),[('v1','xyzw'),('s0','xyzw')])
    if affine:
        emit('mad',('r3' if bump else 'r2','xyzw'),[('r1','xyzx'),(local,one*3+zero),(local,zero*3+one)],())
        if not bump:
            if standard:
                if base: emit('mad',('r1','xyz'),[('r3','xyzw'),(diffuse,'xxxx'),('r4','xyzw')])
                else: emit('mad',('r0','w'),[(packed_response,'yyyy'),('r0','xxxx'),(packed_response,'xxxx')])
            else: emit('mov',('r1','xyz'),[('v0','xyzw')],PP+('saturate',))
        for i,lane in enumerate('xyz'):
            emit('dp4',('r4' if bump else 'r3',lane),[('r3' if bump else 'r2','xyzw'),(f'c{i}','xyzw')])
        if not bump and standard: emit('mov',('r4' if base else 'r1','xyz'),[('v0','xyzw')],PP+('saturate',))
    albedo = 'r4' if bump and affine else 'r3' if affine else 'r1'
    tint = 'r2' if affine else 'r3'
    if standard:
        emit('mul',('r1' if bump and affine else tint,'xyz'),[(albedo,'xyzw'),(reflection,'xxxx')])
        if not bump:
            if base: emit('add',('r1','xyz'),[('r1','xyzw'),('r4','xyzw')])
            else: emit('mad',('r1' if affine else 'r2','xyz'),[('r0','wwww'),('c5' if affine else 'c2','xyzw'),('r1' if affine else 'r2','xyzw')])
        emit('mul',(tint,'xyz'),[('r2' if bump else 'r0','xxxx'),('r1' if bump and affine else tint,'xyzw')])
    else:
        if affine:
            if base: emit('add',('r1','xyz'),[('r4','xyzw'),('r1','xyzw')])
            else: emit('mad',('r1','xyz'),[('r0','wwww'),('c5','xyzw'),('r1','xyzw')])
        emit('mul',(tint,'xyz'),[('r0','xxxx'),(albedo,'xyzw')])
    lit = 'r1' if affine else 'r2'
    if bump:
        if base: emit('add',('r1','xyz'),[('r5','xyzw'),('r6','xyzw')])
        else: emit('mad',(lit,'xyz'),[('r2','wwww'),('c5' if affine else 'c2','xyzw'),('r5' if affine else 'r4','xyzw')])
    else: emit('texld',('r0','xyzw'),[('v4','xyzw'),('s3','xyzw')])
    emit('mul',('r0','xyz'),[('r0','xyzw'),(tint,'xyzw')] if bump else [(tint,'xyzw'),('r0','xyzw')])
    emit('mad',('r1','xyz'),[(lit,'xyzw'),(albedo,'xyzw'),('r0','xyzw')])
    lightmap = 3 if bump else 2
    emit('texld',('r0','xyzw'),[('v1','xyzw'),(f's{lightmap}','xyzw')])
    emit('lrp',('r2','w'),[('c3' if affine else 'c0','xxxx'),('r0','wwww'),('r1','wwww')])
    emit('add',('oC0','xyz'),[('r1','xyzw'),('r0','xyzw')])
    emit('mul',('oC0','w'),[('r2','wwww'),('v0','wwww')])
    proof = prove_chain(decoded, specifications)
    require([r['instruction_dword'] for r in proof] == executable and cursor == final+8, 'extended executable inventory changed')
    # Bind all scalar coefficient consumers and forbid hidden application aliases.
    coefficient_sites = {}
    if standard:
        defined = {motion.name_of(*motion.register_of(row['item']['words'][0])) for row in decoded.values() if row['item']['opcode'] == motion.DEF}
        for role,reg in [('specular',spec),('power',power),('reflection',reflection),('diffuse',diffuse)]:
            references = uses(decoded,reg)
            require(reg not in defined and len(references) == (2 if role == 'power' and base else 1), 'application scalar inventory changed')
            require(all(src['swizzle'] == 'xxxx' and src['source_modifier'] == 0 and not src['relative'] for _,src in references), 'application scalar shape changed')
            coefficient_sites[role] = [site(decoded,at) for at,_ in references]
    lrp = tex[lightmap]+4
    for name,lane,begin,end in [('r1','w',tex[0],lrp),('r0','w',tex[lightmap],lrp),('r2','w',lrp,final+4)]:
        no_lane_writes(decoded,name,lane,begin,end)
    return {'normal_reconstruction_sites': proof[:normal_count], 'normal_encoding': 'xyz' if xyz else 'ag' if bump else 'geometric',
            'normal_channels': ({'red':'binormal_v5','green':'tangent_v4','blue':'normal_v3','alpha':'unused'} if xyz else {'alpha':'binormal_v5','green':'tangent_v4','red_blue':'unused'}) if bump else {},
            'native_pow_sites': [row for row in proof if row['opcode'] == 'pow'], 'literal_sites': literals,
            'application_coefficient_sites': coefficient_sites, 'complete_executable_chain_checked': len(proof),
            'reflection_coordinate_dword': reflect_at if bump else None,
            'diffuse_alpha_live_interval':[tex[0],lrp], 'lightmap_alpha_live_interval':[tex[lightmap],lrp],
            'interpolated_alpha_live_interval':[lrp,final+4],
            'affine_rgb_sites':[site(decoded,affine-8+4*i) for i in range(3)] if affine else []}


def prove_hull_pixel(decoded, key):
    """Exact conventional-hull schedules with fixed coefficients and AG data.

    Full original executable occupancy binds every producer through its final
    consumer. This separately reviewed predicate preserves the earlier 49
    program proofs; it does not evaluate or generate shader bytecode.
    """
    tex, affine, clamp, direct, final = HULL_PIXELS[key]
    family = PIXEL_FAMILY[key]
    bump, shared, split = family != 'terran_default', family == 'shared_bump', family == 'split_bump'
    base, face = len(direct) == 2, bool(uses(decoded, 'vFace'))
    local = ('c8' if shared else 'c9') if bump and base else 'c6' if affine else 'c3'
    if not bump and base: local = 'c8'
    one, zero, two, minus = ('x','z','w','y') if face and affine else ('x','y','z','w') if affine else ('x','w','z','y') if face else ('z',None,'x','y')
    if not bump: one,zero,minus = 'x','z' if face else 'y','y'
    literals = []
    def literal(reg,lane,value):
        literals.append(literal_component(decoded,reg,lane,value))
        return (reg,lane*4)
    if bump:
        for lane,value in ((one,1.),(two,2.),(minus,-1.)): literal(local,lane,value)
        if zero: literal(local,zero,0.)
    elif affine or face:
        literal(local,one,1.);literal(local,zero,0.)
        if face: literal(local,minus,-1.)
    if split:
        coefficient = 'c8' if base else 'c7' if affine else 'c4'
        three = literal(coefficient,'y' if base or affine or face else 'x',3.)
        half = literal(coefficient,'z' if base or affine or face else 'y',.5)
        power = literal('c3' if not affine and not face else coefficient,'w' if not affine and not face else 'x',10.)
        packed = (coefficient,'yzzw' if affine or face else 'xyzw')
    elif shared:
        coefficient = 'c9' if base else 'c7' if affine else 'c4'
        three = literal(coefficient,'x',3.)
        half = literal(coefficient,'y',.5)
        outer_three = literal('c3','w',3.) if not affine and not face else three
        packed = (coefficient,'xyzw')
    else:
        coefficient = ('c8' if base else 'c7' if affine else 'c4' if face else 'c3') if bump else local
        three = literal(coefficient,'w' if not affine and (not bump and face or bump and not face) else 'z' if not bump and affine and not face else 'w' if not bump and face else 'x',3.)
    executable = [at for at,row in decoded.items() if row['item']['opcode'] not in (31,81)]
    cursor, specs = min(executable), []
    def emit(op,dst,sources,mods=PP):
        nonlocal cursor
        specs.append((cursor,op,dst,sources,mods));cursor += 1+bool(dst)+len(sources)
    def nrm(dst,source): emit('nrm',(dst,'xyz'),[(source,'xyzw')])
    if bump:
        emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
        emit('mad',('r1','xy'),[(local,two*4),('r0','wyzw'),(local,minus*4)])
        emit('dp2add',('r0','w'),[('r1','xyzw'),('r1','xyzw',1),(local,one*4)])
        emit('mul',('r0','xyz'),[('r1','yyyy'),('v4','xyzw')])
        emit('rsq',('r0','w'),[('r0','wwww')])
        emit('mad',('r0','xyz'),[('r1','xxxx'),('v5','xyzw'),('r0','xyzw')])
        emit('rcp',('r0','w'),[('r0','wwww')])
        emit('mad',('r1','xyz'),[('r0','wwww'),('v3','xyzw'),('r0','xyzw')])
    if face:
        emit('cmp',('r0','w'),[('vFace','xyzw'),(local,one*4),(local,minus*4)],())
        emit('cmp',('r0','w'),[('r0','wwww',1),(local,zero*4),(local,one*4)])
        emit('cmp',('r1','w') if bump else ('r0','z'),[('vFace','xyzw'),(local,zero*4),(local,one*4)])
        if bump: nrm('r0','r1')
        emit('add',('r0','w'),[('r0','wwww'),('r1','wwww',1) if bump else ('r0','zzzz',1)])
        if not bump: nrm('r0','v3')
        emit('mul',('r0','xyz'),[('r0','xyzw'),('r0','wwww')] if bump else [('r0','wwww'),('r0','xyzw')])
    else: nrm('r0','r1' if bump else 'v3')
    normal_count = len(specs)
    direction = 'c6' if base else 'c4' if affine else 'c1'
    emit('dp3',('r0','w'),[(direction,'xyzw',1),('r0','xyzw')])
    emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
    if not bump:
        if base and not face: nrm('r3','v2')
        emit('mad',('r1','xyz'),[('r0','xyzw'),('r0','wwww',1),(direction,'xyzw',1)])
        if not base or face: nrm('r3' if base else 'r2','v2')
        emit('dp3',('r0','w'),[('r1','xyzw'),('r3' if base else 'r2','xyzw')],PP+('saturate',))
        if base:
            emit('mul',('r1','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r1','w'),[('r1','wwww'),('r1','wwww')])
            emit('mul',('r1','w'),[('r0','wwww'),('r1','wwww')])
            emit('dp3',('r1','z'),[('c4','xyzw',1),('r0','xyzw')])
            emit('dp3',('r0','w'),[('r0','xyzw'),('c6','xyzw')],PP+('saturate',))
            emit('add',('r1','z'),[('r1','zzzz'),('r1','zzzz')])
            emit('mul',('r2','w'),[('r0','wwww'),three],PP+('saturate',))
            emit('mad',('r2','xyz'),[('r0','xyzw'),('r1','zzzz',1),('c4','xyzw',1)])
            emit('mul',('r1','xyz'),[('r0','wwww'),('c7','xyzw')])
            emit('dp3',('r0','w'),[('r2','xyzw'),('r3','xyzw')],PP+('saturate',))
            emit('mul',('r2','z'),[('r1','wwww'),('r2','wwww')])
            emit('mul',('r2','w'),[('r0','wwww'),('r0','wwww')])
            emit('dp3',('r1','w'),[('r0','xyzw'),('c4','xyzw')],PP+('saturate',))
            emit('mul',('r0','z'),[('r2','wwww'),('r2','wwww')])
            emit('mul',('r0','w'),[('r0','wwww'),('r0','zzzz')])
            emit('mul',('r2','w'),[('r1','wwww'),three],PP+('saturate',))
            emit('mul',('r0','xyz'),[('r2','zzzz'),('c7','xyzw')])
            emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
            emit('mad',('r2','xyz'),[('r1','wwww'),('c5','xyzw'),('r1','xyzw')])
            emit('mad',('r1','xyz'),[('r0','wwww'),('c5','xyzw'),('r0','xyzw')])
        else:
            emit('mul',('r1','z'),[('r0','wwww'),('r0','wwww')])
            emit('dp3',('r1','w'),[('r0','xyzw'),(direction,'xyzw')],PP+('saturate',))
            emit('mul',('r0','z'),[('r1','zzzz'),('r1','zzzz')])
            emit('mul',('r0','w'),[('r0','wwww'),('r0','zzzz')])
            emit('mul',('r0','z'),[('r1','wwww'),three],PP+('saturate',))
            emit('mul',('r1','z'),[('r0','wwww'),('r0','zzzz')])
        emit('texld',('r0','xyzw'),[('v1','xyzw'),('s1','xyzw')])
        emit('mul',('r0','w'),[('r0','xxxx'),three] if base else [('r1','zzzz'),('r0','xxxx')])
        if base: emit('mad',('r4','xyz'),[('r1','xyzw'),('r0','wwww'),('r2','xyzw')])
        else: emit('mad',('r0','w'),[('r0','wwww'),three,('r1','wwww')])
    else:
        emit('mad',('r2','xyz'),[('r0','xyzw'),('r0','wwww',1),(direction,'xyzw',1)])
        nrm('r1','v2')
        if split and not base: emit('dp3',('r1','w'),[('r0','xyzw'),(direction,'xyzw')],PP+('saturate',))
        emit('dp3',('r0','w'),[('r2','xyzw'),('r1','xyzw')],PP+('saturate',))
        if base and split:
            emit('pow',('r1','w'),[('r0','wwww'),power])
            emit('dp3',('r2','z'),[('r0','xyzw'),('c6','xyzw')],PP+('saturate',))
            emit('mul',('r2','w'),[('r2','zzzz'),three],PP+('saturate',))
            emit('dp3',('r0','w'),[('c4','xyzw',1),('r0','xyzw')])
            emit('mul',('r3','xyz'),[('r2','zzzz'),('c7','xyzw')])
            emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r3','w'),[('r1','wwww'),('r2','wwww')])
            emit('mad',('r2','xyz'),[('r0','xyzw'),('r0','wwww',1),('c4','xyzw',1)])
            emit('dp3',('r2','w'),[('r2','xyzw'),('r1','xyzw')],PP+('saturate',))
            emit('dp3',('r1','w'),[('r0','xyzw'),('c4','xyzw')],PP+('saturate',))
            emit('pow',('r0','w'),[('r2','wwww'),power])
            emit('mul',('r2','w'),[('r1','wwww'),three],PP+('saturate',))
            emit('mul',('r2','xyz'),[('r3','wwww'),('c7','xyzw')])
            emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
        elif base:
            emit('mul',('r0' if shared else 'r1','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r1','w'),[('r0' if shared else 'r1','wwww'),('r0' if shared else 'r1','wwww')])
            emit('mul',('r1','w'),[('r0','wwww'),('r1','wwww')])
            emit('dp3',('r2','w'),[('c4','xyzw',1),('r0','xyzw')])
            emit('dp3',('r0','w'),[('r0','xyzw'),('c6','xyzw')],PP+('saturate',))
            emit('add',('r2','z'),[('r2','wwww'),('r2','wwww')])
            emit('mul',('r2','w'),[('r0','wwww'),three],PP+('saturate',))
            emit('mad',('r2','xyz'),[('r0','xyzw'),('r2','zzzz',1),('c4','xyzw',1)])
            emit('mul',('r3','xyz'),[('r0','wwww'),('c7','xyzw')])
            emit('dp3',('r0','w'),[('r2','xyzw'),('r1','xyzw')],PP+('saturate',))
            emit('mul',('r2','z'),[('r1','wwww'),('r2','wwww')])
            emit('mul',('r0' if shared else 'r1','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r2','w'),[('r0' if shared else 'r1','wwww'),('r0' if shared else 'r1','wwww')])
            emit('dp3',('r1','w'),[('r0','xyzw'),('c4','xyzw')],PP+('saturate',))
            emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
            emit('mul',('r2','w'),[('r1','wwww'),three],PP+('saturate',))
            emit('mul',('r2','xyz'),[('r2','zzzz'),('c7','xyzw')])
            emit('mul',('r0','w'),[('r0','wwww'),('r2','wwww')])
        elif split:
            emit('mul',('r3','xy'),[('r1','wwww'),packed])
            emit('pow',('r1','w'),[('r0','wwww'),power])
        elif shared:
            emit('mul',('r0','w'),[('r0','wwww'),('r0','wwww')])
            emit('dp3',('r2','w'),[('r0','xyzw'),(direction,'xyzw')],PP+('saturate',))
            emit('mul',('r1','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r3','xy'),[('r2','wwww'),packed])
            emit('mul',('r1','w'),[('r0','wwww'),('r1','wwww')])
        else:
            emit('mul',('r1','w'),[('r0','wwww'),('r0','wwww')])
            emit('mul',('r1','w'),[('r1','wwww'),('r1','wwww')])
            emit('dp3',('r3','w'),[('r0','xyzw'),(direction,'xyzw')],PP+('saturate',))
            emit('mul',('r0','w'),[('r0','wwww'),('r1','wwww')])
            emit('mul',('r1','w'),[('r3','wwww'),three],PP+('saturate',))
        if base:
            emit('mad',('r3' if shared or split else 'r4','xyz'),[('r1','wwww'),('c5','xyzw'),('r3','xyzw')])
            emit('mad',('r4' if shared or split else 'r3','xyz'),[('r0','wwww'),('c5','xyzw'),('r2','xyzw')])
        elif shared or split:
            emit('mov',('r0','w'),[('r3','xxxx')],PP+('saturate',))
            emit('mul',('r0','w'),[('r1','wwww'),('r0','wwww')])
        else: emit('mul',('r0','w'),[('r0','wwww'),('r1','wwww')])
        emit('texld',('r2','xyzw'),[('v1','xyzw'),('s2','xyzw')])
        if base:
            emit('mul',('r0' if shared or split else 'r1','w'),[('r2','xxxx'),three])
            if shared or split: emit('mul',('r4','xyz'),[('r4','xyzw'),('r0','wwww')])
        else: emit('mul',('r1','w'),[('r0','wwww'),('r2','xxxx')])
    angular_count = len(specs)
    # Reflection shares the live reconstructed normal and normalized view;
    # a scalar/direct-lobe combination is scheduled inside its dot/double chain.
    reflect_at = None
    if bump:
        emit('dp3',('r0','w'),[('r1','xyzw',1),('r0','xyzw')])
        if base:
            emit('mad',('r5','xyz'),[('r3','xyzw'),half,('r4','xyzw')] if shared or split else [('r3','xyzw'),('r1','wwww'),('r4','xyzw')])
        else:
            emit('mad',('r2','w'),[('r1','wwww'),outer_three if shared else three,('r3','yyyy') if shared or split else ('r3','wwww')])
        emit('add',('r0','w'),[('r0','wwww'),('r0','wwww')])
        emit('mov',('r6' if base else 'r5' if affine else 'r4','xyz'),[('v0','xyzw')],PP+('saturate',))
        reflect_at = cursor
        emit('mad',('r0','xyz'),[('r0','xyzw'),('r0','wwww',1),('r1','xyzw',1)])
        emit('texld',('r0','xyzw'),[('r0','xyzw'),('s4','xyzw')])
    elif not affine:
        emit('mov',('r1','xyz'),[('v0','xyzw')],PP+('saturate',))
        emit('mad',('r2','xyz'),[('r0','wwww'),('c2','xyzw'),('r1','xyzw')])
    emit('texld',('r1','xyzw'),[('v1','xyzw'),('s0','xyzw')])
    if affine:
        # The two Terran BUMP affine singles retain PP on this authored
        # homogeneous preparation; the other hull affine preparations do not.
        emit('mad',('r3' if bump else 'r2','xyzw'),[('r1','xyzx'),(local,one*3+zero),(local,zero*3+one)],PP if family == 'terran_bump' and not base else ())
        if not bump: emit('mov',('r1','xyz'),[('v0','xyzw')],PP+('saturate',))
        for i,lane in enumerate('xyz'):emit('dp4',('r4' if bump else 'r3',lane),[('r3' if bump else 'r2','xyzw'),(f'c{i}','xyzw')])
        if not bump:
            if base: emit('add',('r1','xyz'),[('r4','xyzw'),('r1','xyzw')])
            else: emit('mad',('r1','xyz'),[('r0','wwww'),('c5','xyzw'),('r1','xyzw')])
    albedo, tint, lit = 'r4' if bump and affine else 'r3' if affine else 'r1','r2' if affine else 'r3','r1' if affine else 'r2'
    emit('mul',('r1' if affine else 'r2','xyz') if shared else (tint,'xyz'),[('r2' if bump else 'r0','xxxx'),(albedo,'xyzw')])
    if shared: emit('mul',(tint,'xyz'),[('r1' if affine else 'r2','xyzw'),half])
    if bump:
        if base:emit('add',('r1','xyz'),[('r5','xyzw'),('r6','xyzw')])
        else:emit('mad',(lit,'xyz'),[('r2','wwww'),('c5' if affine else 'c2','xyzw'),('r5' if affine else 'r4','xyzw')])
    else:emit('texld',('r0','xyzw'),[('v4','xyzw'),('s3','xyzw')])
    emit('mul',('r0','xyz'),[('r0','xyzw'),(tint,'xyzw')] if bump else [(tint,'xyzw'),('r0','xyzw')])
    emit('mad',('r1','xyz'),[(lit,'xyzw'),(albedo,'xyzw'),('r0','xyzw')])
    lm = 3 if bump else 2
    emit('texld',('r0','xyzw'),[('v1','xyzw'),(f's{lm}','xyzw')])
    emit('lrp',('r2','w'),[('c3' if affine else 'c0','xxxx'),('r0','wwww'),('r1','wwww')])
    emit('add',('oC0','xyz'),[('r1','xyzw'),('r0','xyzw')])
    emit('mul',('oC0','w'),[('r2','wwww'),('v0','wwww')])
    proof = prove_chain(decoded,specs)
    require([r['instruction_dword'] for r in proof] == executable and cursor == final+8,'hull executable inventory changed')
    lrp = tex[lm]+4
    links = [('r1','w',tex[0],lrp),('r0','w',tex[lm],lrp),('r2','w',lrp,final+4)]
    if bump:
        view = next(r['instruction_dword'] for r in proof if r['opcode'] == 'nrm' and r['sources'][0]['name'] == 'v2')
        links += [('r0','xyz',proof[normal_count-1]['instruction_dword'],reflect_at),('r1','xyz',view,reflect_at)]
    for reg,lanes,begin,end in links:
        for lane in lanes:no_lane_writes(decoded,reg,lane,begin,end)
    return {'normal_reconstruction_sites':proof[:normal_count], 'normal_encoding':'ag' if bump else 'geometric',
            'normal_channels':{'alpha':'binormal_v5','green':'tangent_v4','red_blue':'unused'} if bump else {},
            'literal_sites':literals, 'native_pow_sites':[r for r in proof if r['opcode'] == 'pow'],
            'specular_power':6 if shared else 10 if split else 5,
            'angular_and_mask_sites':proof[normal_count:angular_count], 'reflection_and_color_sites':proof[angular_count:],
            'complete_executable_chain_checked':len(proof), 'reflection_coordinate_dword':reflect_at,
            'verified_live_ranges':[{'register':r,'mask':lanes,'producer_dword':begin,'consumer_dword':end} for r,lanes,begin,end in links],
            'diffuse_alpha_live_interval':[tex[0],lrp], 'lightmap_alpha_live_interval':[tex[lm],lrp],
            'interpolated_alpha_live_interval':[lrp,final+4],
            'affine_rgb_sites':[site(decoded,affine-8+4*i) for i in range(3)] if affine else []}


def weighted_slots(decoded, profile, stage):
    # Documented SM3 costs, bounded to opcodes actually present in this corpus.
    costs = dict.fromkeys(('mov', 'add', 'mad', 'mul', 'dp3', 'dp4', 'rsq', 'rcp', 'mova', 'cmp', 'else', 'endif', 'abs', 'log', 'exp'), 1)
    costs.update({'dcl':0, 'def':0, 'nrm':3, 'pow':3, 'lrp':2, 'dp2add':2, 'rep':3, 'endrep':2, 'if':3})
    samplers = {d['name']: d['texture_type'] for d in profile['declarations'] if d['role'] == 'sampler'}
    slots = 0
    for row in decoded.values():
        opcode = motion.OPCODES[row['item']['opcode']]
        if opcode == 'texld':
            require(stage == 'ps' and len(row['sources']) == 2 and row['sources'][1]['name'] in samplers, 'unknown texture cost')
            require(samplers[row['sources'][1]['name']] in (2,3), 'unreviewed texture dimension cost')
            slots += 4 if samplers[row['sources'][1]['name']] == 3 else 1
        else:
            require(opcode in costs, 'unreviewed static slot opcode')
            slots += costs[opcode]
    return slots


def bump_rgb_sites(decoded, profile):
    # Linear tags follow only componentwise RGB consumers. Normal/data vectors
    # are never seeds; each whole sampler write clears all prior temporary tags.
    live = {('v0', lane) for lane in 'xyz'}
    live.update((source['name'], lane) for source in profile['directional_rgb_sources'] for lane in 'xyz')
    conversions = {row['conversion_after_dword']: row['conversion_rgb_register'] for row in profile['texture_sources'] if row['conversion_after_dword']}
    result = []
    for at, row in decoded.items():
        dest = row['destination']
        if dest and row['item']['opcode'] not in (31, 81):
            depends = any((source['name'], source['swizzle']['xyzw'.index(lane)]) in live
                          for source in row['sources'] for lane in dest['mask'])
            for lane in dest['mask']:
                live.discard((dest['name'], lane))
            if depends and row['item']['opcode'] != 66:
                require(motion.OPCODES[row['item']['opcode']] in ('mov','add','mad','mul') and dest['mask'] == 'xyz', 'mixed RGB/data arithmetic')
                result.append(site(decoded, at))
                live.update((dest['name'], lane) for lane in 'xyz')
        register = conversions.get(at + row['item']['length'] + 1)
        if register:
            live.update((register, lane) for lane in 'xyz')
    return result


def inspect_program(code, identifier):
    require(identifier in ORIGINALS, 'unreviewed original')
    digest, count = ORIGINALS[identifier]
    require(len(code) == count * 4 and hashlib.sha256(code).hexdigest() == digest and
            motion.fnv1a64(code) == identifier[3:], 'original fingerprint/count mismatch')
    stage, key = identifier.split('_')
    words, items, end = motion.instructions(code)
    require(words[0] == (0xfffe0300 if stage == 'vs' else 0xffff0300), 'not original SM3 stage')
    require(all(not i['predicated'] and not i['coissued'] for i in items), 'unsupported predication/coissue')
    decoded = decode_sites(items)
    profile = motion.profile(code, identifier, stage, '3_0')
    if key in PALETTE_VERTICES or key in PALETTE_PIXELS:
        return inspect_palette_program(code, identifier, decoded, profile, items, end)
    if key in ASTEROID_VERTICES or key in ASTEROID_PIXELS:
        return inspect_asteroid_program(code, identifier, decoded, profile, items, end)
    require(profile['parsed'] and profile['header_is_contiguous'] and profile['control_flow_balanced'], 'invalid original structure')
    bump = key in (BUMP_VERTICES if stage == 'vs' else ALL_BUMP_PIXELS)
    loop = stage == 'vs' and key not in ('badefd5143b3024f', '19a246a56e9d9700')
    output = {'id': identifier, 'families': [family for family in FAMILIES if any(vs == key and PIXEL_FAMILY[ps] == family for vs, ps in PAIRS)] if stage == 'vs' else [PIXEL_FAMILY[key]], 'fnv1a64': key, 'sha256': digest, 'word_count': count,
              'header_end_dword': profile['header_end_dword'], 'end_dword': end,
              'opaque_comment_dword_count': count - 2 - sum(i['length'] + 1 for i in items),
              'budget': budget(profile, stage, loop, bump, 7 if key in ('494fe349b8bc12ec', '7c83ed50c9894e44', 'e70adc744a38ca59') else None),
              'motion_splice': ({'declaration_insert_dword': profile['header_end_dword'],
                                  'arithmetic_insert_dword': profile['position_output']['insertion_dword'],
                                  'position_source_temporary': profile['position_output']['source_temporary'],
                                  'position_dp4_dwords': profile['position_output']['dwords_xyzw']}
                                 if stage == 'vs' else
                                 {'definition_insert_dword': profile['definition_end_dword'],
                                  'declaration_insert_dword': profile['header_end_dword'],
                                  'append_dword': end})}
    if stage == 'vs':
        output['point_and_alpha_proof'] = prove_bump_vertex(decoded, loop, profile) if bump else prove_vertex(decoded, loop)
        point_at, point_c, emissive_at, emissive_c = ((437, 1, 452, 40) if loop else (398, 5, 406, 19)) if bump else ((428, 1, 443, 40) if loop else (389, 5, 397, 19))
        output['point_rgb_sources'] = source_role(decoded, f'c{point_c}', [point_at], loop)
        output['point_rgb_sources'][0]['color_constant_indices'] = list(range(1, 24, 3)) if loop else [5]
        output['material_emissive_scaled_sources'] = source_role(decoded, f'c{emissive_c}', [emissive_at])
        output['point_model'] = 'loop_count_i0_x_0_to_8_stride_3_a0_w' if loop else 'fixed_single_point'
        output['final_rgb_sites'] = [site(decoded, emissive_at)]
        output['alpha_output_sites'] = [site(decoded, at) for at in (((527, 532) if loop else (482, 487)) if bump else ((500, 505) if loop else (455, 460)))]
        output['rgb_output_declaration'] = next(d for d in profile['declarations'] if d['name'] == 'o1')
        output['constraints'] = ['Convert each point RGB before its per-light multiplication; never decode the accumulated sum.',
                                 'Material emissive is already strength-scaled RGB: preserve its amplitude/tint and apply any new linear gain at this source; do not exponentiate its combined strength. Final VS RGB stays linear.',
                                 'Point and emissive consumers write XYZ only. Keep o1.w alpha/fog writes and all position/geometry instructions unchanged.']
    else:
        output['alpha_and_affine_proof'] = prove_hull_pixel(decoded, key) if key in HULL_PIXELS else prove_extended_pixel(decoded, key) if key in EXTENDED_PIXELS else (prove_bump_pixel(decoded, key) if bump else prove_pixel(decoded, key))
        tex, affine_end, clamp, direct, final = ALL_PIXELS[key]
        fetches = [(at, row) for at, row in decoded.items() if row['item']['opcode'] == 66]
        require(sorted(at for at, _ in fetches) == sorted(tex), 'texture source inventory changed')
        texture_roles = []
        roles = ('diffuse_rgb', ('normal_data_xyz' if PIXEL_FAMILY[key] == 'standard_bump_low' else 'normal_data_alpha_green'), 'specular_data_red', 'lightmap_emissive_rgb', 'reflection_cube_rgb') if bump else ('diffuse_rgb', 'specular_data_red', 'lightmap_emissive_rgb', 'reflection_cube_rgb')
        for sampler, (at, role) in enumerate(zip(tex, roles)):
            row = site(decoded, at)
            require(row['sources'][1]['name'] == f's{sampler}' and row['destination']['mask'] == 'xyzw', 'texture sampler/destination changed')
            boundary = site(decoded, affine_end if sampler == 0 and affine_end else at)
            texture_roles.append({'role': role, 'sampler': sampler, 'fetch': row,
                                  'conversion_after_dword': None if sampler in ((1, 2) if bump else (1,)) else boundary['end_dword'],
                                  'conversion_rgb_register': None if sampler in ((1, 2) if bump else (1,)) else boundary['destination']['name'],
                                  'conversion_write_mask': None if sampler in ((1, 2) if bump else (1,)) else 'xyz'})
        output['texture_sources'] = texture_roles
        output['diffuse_affine_completion'] = site(decoded, affine_end) if affine_end else None
        output['directional_rgb_sources'] = [source for c, offsets in direct.items()
                                             for source in source_role(decoded, f'c{c}', offsets)]
        output['color0_rgb_clamp'] = site(decoded, clamp)
        output['color0_declaration'] = next(d for d in profile['declarations'] if d['name'] == 'v0')
        require(output['color0_rgb_clamp']['destination']['mask'] == 'xyz' and
                'saturate' in output['color0_rgb_clamp']['destination']['modifiers'], 'COLOR0 clamp shape changed')
        output['final_rgb_sites'] = [site(decoded, final)]
        output['alpha_interpolation_site'] = site(decoded, tex[3 if bump else 2] + 4)
        output['alpha_output_sites'] = [site(decoded, final + 4)]
        output['two_sided'] = 'vFace' in profile['declared_misc_registers']
        output['lobe_coefficients'] = FAMILIES[PIXEL_FAMILY[key]]['coefficients']
        if PIXEL_FAMILY[key] == 'shared_default':
            output['lobe_proof'] = prove_shared_lobe(decoded, key)
        output['constraints'] = ['Keep diffuse affine transform in authored space; convert only after its final RGB lane.',
                                 'All texture fetches retain original partial precision. Diffuse r1.w and lightmap r0.w feed alpha interpolation; added color writes must be XYZ only.',
                                 'The specular texture red is data, not RGB color. Directional RGB is consumed before lighting multiplication at every listed source.',
                                 'COLOR0 v0 is declared partial precision for XYZW, and final alpha uses v0.w. Removing declaration partial precision wholesale does not prove original alpha precision invariance.',
                                 'The selected plan routes full-precision RGB through VS o8/COLOR1 to PS v7.xyz, preserving original partial-precision v0.w for alpha. These unused resources are proved here; no declaration or shader rewrite is emitted.',
                                 'Final output adds lighting and lightmap into oC0.xyz with partial precision. Compatibility encoding requires the complete linear sum; oC0.w must keep its independent original write.']
    if bump:
        output['constraints'] = [constraint.replace('VS o8/COLOR1 to PS v7.xyz', 'VS o9/COLOR1 to PS v8.xyz') for constraint in output['constraints']]
        output['constraints'].append('BUMPMAP normal data feeds the reviewed BINORMAL/TANGENT/geometric-normal basis; s1/s2 remain data. Keep normal, view, per-pixel cube coordinates, fog and opacity chains unchanged. Only sampler 0/3/4 RGB is converted.')
        if stage == 'ps':
            require([(d['register'], d['texture_type']) for d in profile['declarations'] if d['role'] == 'sampler'] == [(0,2),(1,2),(2,2),(3,2),(4,3)], 'bump sampler declaration changed')
            for index in range(1,6):
                d = next(d for d in profile['declarations'] if d['name'] == f'v{index}')
                require((d['usage_name'], d['usage_index'], d['mask'], d['modifiers']) == ('texcoord', index-1, 'xy' if index == 1 else 'xyz', (['centroid','partial_precision'] if index == 1 and len(direct) == 2 else ['partial_precision'])), 'bump varying declaration changed')
            output['rgb_precision_sites'] = bump_rgb_sites(decoded, output)
            output['retained_geometry_precision_sites'] = output['alpha_and_affine_proof']['normal_reconstruction_sites']
    if stage == 'ps' and (key in EXTENDED_PIXELS or key in HULL_PIXELS):
        output['rgb_precision_sites'] = bump_rgb_sites(decoded, output)
    output['material_rgb_semantic_proof'] = material_color1_proof(profile, stage)
    output['budget']['original_static_weighted_slots'] = weighted_slots(decoded, profile, stage)
    require(all(row['destination']['mask'] == 'xyz' for row in output['final_rgb_sites']), 'final RGB touches alpha')
    output['certification'] = 'original_identity_and_reviewed_sites_verified; no transformed shader or numeric equivalence claim'
    return output



def prove_asteroid_archive(inventory):
    """Bind every base/toggle alias and its independent catalog occurrence count."""
    rows = [r for r in inventory['pairs'] if (r['vs'],r['ps']) in ASTEROID_PAIRS]
    require(len(rows)==6, 'asteroid archive pair inventory changed')
    for row in rows:
        bump,base,loop,_ = ASTEROID_VERTICES[row['vs']]
        effects = row['effects']
        expected = {'pass_occurrences':8 if base else 4, 'effect_entries':8 if base else 4,
                    'catalogues':['01.cat','addon/01.cat'] if base else ['01.cat'],
                    'basenames':['asteroid'] if base else ['asteroid_0000','asteroid_0001'],
                    'profile_directories':['3_0'], 'techniques':['BUMPMAP' if bump else 'DEFAULT'],
                    'pass_names':['P0'],
                    'toggle_directories':['(base)','hue_lights_off','hueshift_off','v_lights_off'] if base else
                                         ['(base)','hueshift_off'] if loop else ['hue_lights_off','v_lights_off']}
        require(effects==expected, 'asteroid archive alias/toggle inventory changed')


def prove_archive_coverage(inventory):
    """Each named alias must independently cover its complete reviewed family."""
    prove_asteroid_archive(inventory)
    prove_palette_archive(inventory)
    for family in FAMILIES.values():
        expected = {(vs, ps) for vs, ps in PAIRS if ps in family['pixels']}
        require(len(expected) == (3 if family['aliases'] == ['asteroid'] else 6 if family['aliases'] == ['boron'] else 10), 'family pair contract changed')
        for alias in family['aliases']:
            basename = re.compile(re.escape(alias) + r'(?:2s|_000[01])?')
            actual = {(row['vs'], row['ps']) for row in inventory['pairs']
                      if family.get('technique', 'DEFAULT') in row['effects']['techniques'] and '3_0' in row['effects']['profile_directories']
                      and any(basename.fullmatch(name) for name in row['effects']['basenames'])}
            require(actual == expected, f'{alias}: complete {family.get("technique", "DEFAULT")} archive coverage changed')


def inspect(directory, inventory_path):
    inventory_data = inventory_path.read_bytes()
    inventory = json.loads(inventory_data)
    prove_archive_coverage(inventory)
    rows = [row for row in inventory['pairs'] if (row['vs'], row['ps']) in PAIRS]
    negative = [row for row in inventory['pairs'] if row['vs'] == '494fe349b8bc12ec' and row['ps'] == 'fffdabd910793aba']
    require(len(negative) == 1 and negative[0]['transformation_class'] == 'C_relocated_registers_with_static_branches' and
            ('494fe349b8bc12ec', 'fffdabd910793aba') not in PAIRS, 'future negative witness changed')
    require(len(rows) == 148 and {(row['vs'], row['ps']) for row in rows} == PAIRS, 'missing/duplicate reviewed pair')
    require(all(row['transformation_class'] == ('B_relocated_registers' if (row['ps'] in PALETTE_PIXELS or row['ps'] in ALL_BUMP_PIXELS or row['ps'] in ASTEROID_PIXELS and ASTEROID_PIXELS[row['ps']][0]) else 'A_reference_registers') for row in rows), 'motion class changed')
    for family, record in FAMILIES.items():
        scoped = [row for row in rows if PIXEL_FAMILY[row['ps']] == family]
        require(sum(row['effects']['pass_occurrences'] for row in scoped) == (16 if family.startswith('asteroid_') else 96 if family in ('shared_default', 'shared_bump') else 24), 'family occurrence count changed')
        require(all(row['effects']['techniques'] == [record.get('technique', 'DEFAULT')] and
                    row['effects']['profile_directories'] == ['3_0'] and row['effects']['pass_names'] == ['P0'] for row in scoped), 'family technique/pass scope changed')
    depth = motion.depth_plan(inventory)
    codes = {key: (directory / f'{key}.bin').read_bytes() for key in ORIGINALS}
    programs = [inspect_program(code, key) for key, code in codes.items()]
    original_motion = {key: motion.profile(code, key, key[:2], '3_0') for key, code in codes.items()}
    compact_pairs = []
    for row in sorted(rows, key=lambda row: (row['vs'], row['ps'])):
        plan, d = row['insertion_plan'], depth[row['vs'], row['ps']]
        actual = motion.classify(original_motion['vs_' + row['vs']], original_motion['ps_' + row['ps']])
        asteroid = row['ps'] in ASTEROID_PIXELS
        palette = row['ps'] in PALETTE_PIXELS
        bump = PALETTE_PIXELS[row['ps']][1] if palette else ASTEROID_PIXELS[row['ps']][0] if asteroid else row['ps'] in ALL_BUMP_PIXELS
        require(actual[0] == ('B_relocated_registers' if bump or palette else 'A_reference_registers') and not actual[1] and actual[3] == plan, 'motion source/splice plan changed')
        if palette:
            family = PALETTE_PIXELS[row['ps']][0]
            abi = palette_abi(family,bump)
            temporary_base = max(5,max(original_motion['ps_'+row['ps']]['temporary_registers'])+1)
            require((plan['vs_output_register'],plan['ps_input_register'],plan['ps_temporaries'],
                     plan['texcoord_index'],plan['vs_constant_base'],plan['ps_constant_base']) ==
                    (abi['vs_motion_output'],abi['ps_motion_input'],list(range(temporary_base,temporary_base+3)),
                     abi['motion_texcoord'],252,216), 'palette motion ABI changed')
            require(d == {'vertex_depth_output_register':abi['vs_depth_output'],'depth_texcoord_index':abi['depth_texcoord'],
                          'pixel_depth_input_register':abi['ps_depth_input'],'depth_output':True},'palette depth ABI changed')
            vp = next(p for p in programs if p['id']=='vs_'+row['vs'])
            pp = next(p for p in programs if p['id']=='ps_'+row['ps'])
            require(vp['material_abi']==pp['material_abi']==abi,'palette pair-local material ABI disagrees')
            for vs_scalar,ps_scalar in zip(vp['scalar_relocations'],pp['scalar_relocations']):
                for field in ('role','source_lane','destination_lane','source_texcoord_index','destination_texcoord_index','extended_carrier_mask'):
                    require(vs_scalar[field]==ps_scalar[field], 'palette pair-local scalar ABI disagrees')
            require(len(vp['scalar_relocations'])==len(pp['scalar_relocations']), 'palette pair-local scalar count changed')
        elif asteroid:
            abi = asteroid_abi(*ASTEROID_PIXELS[row['ps']][:2])
            require((plan['vs_output_register'],plan['ps_input_register'],plan['ps_temporaries'],
                     plan['texcoord_index'],plan['vs_constant_base'],plan['ps_constant_base']) ==
                    (abi['vs_motion_output'],abi['ps_motion_input'],abi['ps_motion_temporaries'],
                     abi['motion_texcoord'],252,216), 'asteroid motion ABI changed')
            require(d == {'vertex_depth_output_register':abi['vs_depth_output'],
                          'depth_texcoord_index':abi['depth_texcoord'],
                          'pixel_depth_input_register':abi['ps_depth_input'], 'depth_output':True},
                    'asteroid depth ABI changed')
            vp = next(p for p in programs if p['id']=='vs_'+row['vs'])
            pp = next(p for p in programs if p['id']=='ps_'+row['ps'])
            require(vp['material_abi']==pp['material_abi']==abi, 'asteroid pair-local material ABI disagrees')
        else:
            require((plan['vs_output_register'], plan['ps_input_register'], plan['ps_temporaries'],
                     plan['texcoord_index'], plan['vs_constant_base'], plan['ps_constant_base']) ==
                    ((7, 6, list(range(max(original_motion['ps_' + row['ps']]['temporary_registers'])+1, max(original_motion['ps_' + row['ps']]['temporary_registers'])+4)), 5, 252, 216) if bump else
                     (6, 5, [5, 6, 7], 4, 252, 216)), 'motion ABI changed')
            require(d == {'vertex_depth_output_register': 8 if bump else 7, 'depth_texcoord_index': 6 if bump else 7 if row['vs'] == '494fe349b8bc12ec' else 5,
                          'pixel_depth_input_register': 7 if bump else 6, 'depth_output': True}, 'depth ABI changed')
        for stage in ('vs', 'ps'):
            name = stage + '_' + row[stage]
            original = next(p for p in programs if p['id'] == name)
            require(inventory['programs'][name]['sha256'] == original['sha256'], 'stale motion program identity')
        compact_pairs.append({'vs': row['vs'], 'ps': row['ps'], 'family': PIXEL_FAMILY[row['ps']],
                              'archive_pass_occurrences': row['effects']['pass_occurrences'],
                              'archive_aliases': row['effects']['basenames'],
                              'depth_texcoord_index': d['depth_texcoord_index'],
                              'motion_class': 'B' if bump or palette else 'A', 'current_depth_supported': True})
        if asteroid or palette:
            compact_pairs[-1]['material_abi'] = abi
            compact_pairs[-1]['motion_plan'] = plan
            compact_pairs[-1]['depth_plan'] = d
    stage_constraints = {}
    for program in programs:
        stage = ('bump_' if any('bump' in family for family in program['families']) else '') + program['id'][:2]
        if program['families'][0].startswith(('asteroid_','boron_','paranid_')):
            stage = program['families'][0] + '_' + program['id'][:2]
        constraints = program.pop('constraints')
        require(stage not in stage_constraints or stage_constraints[stage] == constraints, 'inconsistent stage constraints')
        stage_constraints[stage] = constraints
        program['constraints_ref'] = stage
    return {'schema': 1, 'stage_constraints': stage_constraints,
            'scope': 'Bounded SM3 DEFAULT/BUMPMAP/BUMPMAP_LOW original-site proof: 25 VS, 90 PS, 148 archive pairs. Boron/Paranid adds 12 VS, 20 PS and all 32 pairs to the 83-original/116-pair COLOR1 baseline bf62f6e. This artifact proves original sites only; current implementation, budgets and qualification belong to docs/architecture/linear-palette-materials.md. Earlier Asteroid and conventional hull evidence remains in their owning architecture notes.',
            'families': FAMILIES,
            'production_contract': ['All 148 material pairs use a separate full-precision, unsaturated XYZ COLOR1 declaration. All 115 originals prove COLOR1 free in actual VS output or PS input DCLs. New BUMP pairs evacuate individually proved native J/u11 scalar lanes before reusing whole o8/v7 for RGB; native motion/depth TEXCOORD contracts remain explicit. Current production budgets and pending Boron/Paranid GPU qualification belong to docs/architecture/linear-palette-materials.md.',
                                    'Boron/Paranid palette RGB is decoded offline from exact native DEF binary32 lanes with binary32 exponent 2.2, rounded to binary32 and emitted as immutable creation-time DEF lanes: VS c240..243 and PS c204..209. Co-resident scalar lanes remain native. Sampler masks are DEFAULT 0x0f and BUMPMAP 0x1f.',
                                    'New DEFAULT uses RGB o10/v9 COLOR1; BUMP uses RGB o8/v7 COLOR1 after J moves to TEXCOORD1.w and Boron base u11 moves to TEXCOORD2.w. Existing carrier XYZ and flags are retained; portable live binding must map and restore the corresponding component WRAP state.',
                                    'Asteroid DEFAULT/BUMPMAP adds 6 VS, 4 PS and 6 complete alias/toggle pairs; lower shader models remain outside this slice.',
                                    'Exact maximum input guard remains 1392 DWORDs, retaining every per-profile count; Asteroid maxima are VS 566 and PS 448 DWORDs.',
                                    'Asteroid sampler masks are DEFAULT 0x07 and BUMPMAP 0x0f. Conventional DEFAULT retains 0x0f and conventional BUMPMAP 0x1f.',
                                    'Asteroid uses four explicit pair-local ABI records: DEFAULT base RGB o8/v7/COLOR1 with depth o7/v6/TEXCOORD5; DEFAULT toggles keep that RGB and motion o6/v5/TEXCOORD4 but depth uses o5/v4/TEXCOORD3.',
                                    'Asteroid BUMPMAP base RGB uses o10/v9/COLOR1 with motion o8/v7/TEXCOORD6 and depth o9/v8/TEXCOORD7. BUMPMAP toggles use RGB o9/v8/COLOR1, motion o7/v6/TEXCOORD5 and depth o8/v7/TEXCOORD6.',
                                    'Conventional standard DEFAULT base VS 494fe349b8bc12ec retains depth TEXCOORD7 and RGB COLOR1; other conventional DEFAULT pairs retain depth TEXCOORD5.',
                                    'Asteroid implementation, transformed budgets and pending GPU qualification belong to docs/architecture/linear-asteroid-materials.md. The 110-pair source baseline 10e447b and subsequent conventional-hull qualification are documented in linear-hull-materials.md.'],
            'future_negative_pair': {'vs': '494fe349b8bc12ec', 'ps': 'fffdabd910793aba', 'family': 'xt_standard_lighting', 'motion_class': 'C',
                                     'note': 'Each original stage independently accepts its class-C motion transformation in both depth modes, and the 1648-DWORD PS refuses material conversion with output rollback and no material sampler mask. The original VS/PS pair is native-linkage-invalid; these stage-local host checks do not establish portable or live fallback. Live qualification requires separately linkage-valid controls.'},
            'offset_units': 'Zero-based DWORD positions in the ORIGINAL whole program, including opaque comments; end_dword/conversion_after_dword are exclusive.',
            'motion_inventory_sha256': hashlib.sha256(inventory_data).hexdigest(),
            'reserved_abi': {'vs_constants': [252, 255], 'vs_motion_output': 6, 'vs_depth_output': 7,
                             'ps_constants': [216, 220], 'ps_temporaries': [5, 7], 'ps_motion_input': 5,
                             'ps_depth_input': 6, 'ps_motion_output': 1, 'ps_depth_output': 2,
                             'motion_texcoord': 4, 'depth_texcoord': 5,
                             'material_vs_def_constants': [248, 249], 'material_ps_def_constants': [212, 213],
                             'material_vs_rgb_output': 8, 'material_ps_rgb_input': 7,
                             'material_rgb_mask': 'xyz', 'material_rgb_usage': 'color', 'material_rgb_usage_index': 1,
                             'material_rgb_precision': 'full'},
            'bump_reserved_abi': {'vs_constants': [252,255], 'ps_constants': [216,220],
                                  'vs_motion_output': 7, 'ps_motion_input': 6, 'motion_texcoord': 5,
                                  'vs_depth_output': 8, 'ps_depth_input': 7, 'depth_texcoord': 6,
                                  'ps_motion_temporaries_by_shape': {'base':[7,8,9], 'affine_single':[6,7,8], 'nonaffine_single':[5,6,7]},
                                  'material_vs_def_constants': [248,249], 'material_ps_def_constants': [212,213],
                                  'material_vs_temporaries': [7,8,9], 'material_ps_temporaries': [10,11,12,13],
                                  'material_vs_rgb_output':9, 'material_ps_rgb_input':8, 'material_rgb_usage':'color', 'material_rgb_usage_index':1,
                                  'material_rgb_mask':'xyz', 'material_rgb_precision':'full',
                                  'required_disabled_srgb_sampler_mask':31, 'current_depth_modes':[False,True]},
            'weighted_budget_sources': ['https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0',
                                       'https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-instructions-vs-3-0'],
            'weighted_budget_note': 'original_static_weighted_slots supersedes the historical estimate field; counts cube TEXLD as four, DP2ADD as two, POW as three, REP/IF as three and ENDREP as two. No transformed budget is claimed.',
            'precision_note': 'Instruction destination modifiers and declaration modifiers contain partial_precision; source_modifier is the D3DSHADER_PARAM_SRCMOD_TYPE numeric field. Source operands have no independent partial-precision flag.',
            'limits': ['Identity binds all definitions, comments/preshaders and END. Only actual instructions are decoded.',
                       'Free resources exclude current motion, current-depth and selected material RGB/DEF reservations, including depth-off variants.',
                       'Relative VS constant availability assumes the existing runtime i0 light-count guard [0,8].',
                       'This artifact proves original sites only. Production/GPU/live evidence belongs to the owning architecture notes and install record; native Windows remains unverified.'],
            'pairs': compact_pairs, 'programs': programs}


def write_report(path, result):
    header = {key: value for key, value in result.items() if key not in ('pairs', 'programs')}
    text = json.dumps(header, indent=2)[:-2]
    for name in ('pairs', 'programs'):
        text += ',\n  ' + json.dumps(name) + ': [\n'
        text += ',\n'.join('    ' + json.dumps(row, separators=(',', ':')) for row in result[name])
        text += '\n  ]'
    path.write_text(text + '\n}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program_directory', type=Path)
    parser.add_argument('--motion-inventory', type=Path, default=Path('verification/results/motion-output-profiles.json'))
    parser.add_argument('--json', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.program_directory, args.motion_inventory)
    write_report(args.json, result)
    print(f"certified {len(result['programs'])} originals and {len(result['pairs'])} pairs; no shader transformation")


if __name__ == '__main__':
    main()
