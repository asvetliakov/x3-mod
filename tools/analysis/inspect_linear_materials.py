#!/usr/bin/env python3
"""Certify bounded original SM3 DEFAULT/BUMPMAP material conversion sites, without rewriting shaders.

Input: complete local archive programs and the existing derived motion inventory.
Output: fingerprints, semantic operand locations and available resources only.
Comments (including preshaders) are opaque. Exact whole-program hashes bind the
manual semantic review; there is no motif-only admission or hash override.
Transfer policy, new instructions and native/GPU equivalence remain future work.
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
    require((7 if bump else 6) not in original_semantics, 'material TEXCOORD ABI collision')
    depth_semantic = (6 if bump else 5) if depth_semantic is None else depth_semantic
    require(depth_semantic not in original_semantics and depth_semantic != (7 if bump else 6), 'depth semantic ABI collision')
    used_semantics = original_semantics | ({5, depth_semantic, 7} if bump else {4, depth_semantic, 6})
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
                                                                 'rgb_texcoord_index': 7 if bump else 6,
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
    costs = dict.fromkeys(('mov', 'add', 'mad', 'mul', 'dp3', 'dp4', 'rsq', 'rcp', 'mova', 'cmp', 'else', 'endif'), 1)
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
                                 'The selected plan routes full-precision RGB through VS o8/TEXCOORD6 to PS v7.xyz, preserving original partial-precision v0.w for alpha. These unused resources are proved here; no declaration or shader rewrite is emitted.',
                                 'Final output adds lighting and lightmap into oC0.xyz with partial precision. Compatibility encoding requires the complete linear sum; oC0.w must keep its independent original write.']
    if bump:
        output['constraints'] = [constraint.replace('VS o8/TEXCOORD6 to PS v7.xyz', 'VS o9/TEXCOORD7 to PS v8.xyz') for constraint in output['constraints']]
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
    output['budget']['original_static_weighted_slots'] = weighted_slots(decoded, profile, stage)
    require(all(row['destination']['mask'] == 'xyz' for row in output['final_rgb_sites']), 'final RGB touches alpha')
    output['certification'] = 'original_identity_and_reviewed_sites_verified; no transformed shader or numeric equivalence claim'
    return output


def prove_archive_coverage(inventory):
    """Each named alias must independently cover its complete ten-pair family."""
    for family in FAMILIES.values():
        expected = {(vs, ps) for vs, ps in PAIRS if ps in family['pixels']}
        require(len(expected) == 10, 'family pair contract changed')
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
    require(len(rows) == 110 and {(row['vs'], row['ps']) for row in rows} == PAIRS, 'missing/duplicate reviewed pair')
    require(all(row['transformation_class'] == ('B_relocated_registers' if row['ps'] in ALL_BUMP_PIXELS else 'A_reference_registers') for row in rows), 'motion class changed')
    for family, record in FAMILIES.items():
        scoped = [row for row in rows if PIXEL_FAMILY[row['ps']] == family]
        require(sum(row['effects']['pass_occurrences'] for row in scoped) == (96 if family in ('shared_default', 'shared_bump') else 24), 'family occurrence count changed')
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
        bump = row['ps'] in ALL_BUMP_PIXELS
        require(actual[0] == ('B_relocated_registers' if bump else 'A_reference_registers') and not actual[1] and actual[3] == plan, 'motion source/splice plan changed')
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
                              'motion_class': 'B' if bump else 'A', 'current_depth_supported': True})
    stage_constraints = {}
    for program in programs:
        stage = ('bump_' if any('bump' in family for family in program['families']) else '') + program['id'][:2]
        constraints = program.pop('constraints')
        require(stage not in stage_constraints or stage_constraints[stage] == constraints, 'inconsistent stage constraints')
        stage_constraints[stage] = constraints
        program['constraints_ref'] = stage
    return {'schema': 1, 'stage_constraints': stage_constraints, 'scope': 'Bounded SM3 DEFAULT/BUMPMAP/BUMPMAP_LOW original-site proof: 7 VS, 66 PS, 110 archive pairs. This report proves original sites; the current conventional hull group and pending GPU/live qualification are documented in docs/architecture/linear-hull-materials.md. The preceding 70-pair group was qualified at 73f5c51.',
            'families': FAMILIES,
            'production_contract': ['Shared/Split BUMPMAP and Terran DEFAULT/BUMPMAP add 24 PS, no VS and 40 SM3 pairs; lower shader models remain outside this slice.',
                                                'Exact maximum input guard is 1392 DWORDs, retaining every per-profile count.',
                                                'Standard DEFAULT base VS 494fe349b8bc12ec uses depth TEXCOORD7 and RGB TEXCOORD6; the other DEFAULT pairs retain depth TEXCOORD5.',
                                                'Use explicit class-B o9/v8/TEXCOORD7 RGB and PS r10 scratch; retain existing o7/v6/TEXCOORD5 motion and o8/v7/TEXCOORD6 depth.',
                                                'Five-sampler disabled-sRGB mask 0x1f requires lifecycle resynchronization of s4. DEFAULT remains 0x0f.',
                                                'Transformed budgets and pending GPU/live qualification are recorded in docs/architecture/linear-hull-materials.md; linear-standard-materials.md retains the qualified 70-pair checkpoint and linear-bump-materials.md the historical 30-pair qualification.'],
            'future_negative_pair': {'vs': '494fe349b8bc12ec', 'ps': 'fffdabd910793aba', 'family': 'xt_standard_lighting', 'motion_class': 'C',
                                     'note': 'Existing class-C motion transforms apply in both depth modes; the 1648-DWORD PS stays outside the 1392-DWORD material guard and has no material sampler mask.'},
            'offset_units': 'Zero-based DWORD positions in the ORIGINAL whole program, including opaque comments; end_dword/conversion_after_dword are exclusive.',
            'motion_inventory_sha256': hashlib.sha256(inventory_data).hexdigest(),
            'reserved_abi': {'vs_constants': [252, 255], 'vs_motion_output': 6, 'vs_depth_output': 7,
                             'ps_constants': [216, 220], 'ps_temporaries': [5, 7], 'ps_motion_input': 5,
                             'ps_depth_input': 6, 'ps_motion_output': 1, 'ps_depth_output': 2,
                             'motion_texcoord': 4, 'depth_texcoord': 5,
                             'material_vs_def_constants': [248, 249], 'material_ps_def_constants': [212, 213],
                             'material_vs_rgb_output': 8, 'material_ps_rgb_input': 7,
                             'material_rgb_mask': 'xyz', 'material_rgb_texcoord': 6,
                             'material_rgb_precision': 'full'},
            'bump_reserved_abi': {'vs_constants': [252,255], 'ps_constants': [216,220],
                                  'vs_motion_output': 7, 'ps_motion_input': 6, 'motion_texcoord': 5,
                                  'vs_depth_output': 8, 'ps_depth_input': 7, 'depth_texcoord': 6,
                                  'ps_motion_temporaries_by_shape': {'base':[7,8,9], 'affine_single':[6,7,8], 'nonaffine_single':[5,6,7]},
                                  'material_vs_def_constants': [248,249], 'material_ps_def_constants': [212,213],
                                  'material_vs_temporaries': [7,8,9], 'material_ps_temporaries': [10,11,12,13],
                                  'material_vs_rgb_output':9, 'material_ps_rgb_input':8, 'material_rgb_texcoord':7,
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
