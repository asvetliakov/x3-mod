"""Deterministic spatial-fog recipes; no preview or captured-data inputs."""

import numpy as np

REQUIRED_NUMPY = "2.0.2"
PRNG = "numpy.random.default_rng/PCG64"
PERIOD = 32768.0
GRID = 128
HORIZON = 12000.0
RECIPE_ID = 1
ATLAS_WIDTH = 1560
ATLAS_HEIGHT = 1430
TEXEL_BYTES = 8
WARP_SEEDS = ((186, 189), (187, 190), (188, 191))
WARP_PERIODS = (8192.0, 4096.0)
WARP_WEIGHTS = (0.7, 0.3)
WARP_AMPLITUDE = 2048.0
CARRIER = ((192, 8192.0, 0.55), (193, 4096.0, 0.30), (194, 2048.0, 0.15))
CAVITY = ((195, 4096.0, 0.70), (196, 2048.0, 0.30))
CHROMA = (197, 4096.0)
CAVITY_QUANTILES = (70, 85)
CHROMA_QUANTILES = (10, 90)

PROFILES = {
    "bluewell": {
        "id": 1,
        "resource_id": 21101,
        "occupancy": 0.12,
        "base_sigma": 2.5e-6,
        "atlas_sha256": "4529497a5e1feda3276b334d6aaef3ba741460e4e4b539aaaf7df74cbfa7261d",
        "colours": np.array([
            [0.0, 0.21271182472443387, 1.0],
            [0.07572026794060213, 0.19668585055821633, 1.0],
            [0.051671565049836686, 0.3052151957413023, 1.0],
            [0.07417807774861103, 0.33525483945827456, 1.0],
        ], dtype=np.float32),
    },
    "foggreenoutlands": {
        "id": 2,
        "resource_id": 21102,
        "occupancy": 0.24,
        "base_sigma": 6.25e-6,
        "atlas_sha256": "d0342a05fe0e0421c980bb46bec72a765617f9bd043d84d134928a65d3dc0b0c",
        "colours": np.array([
            [0.12417480110175053, 1.0, 0.01784274291816572],
            [0.10174731107512709, 1.0, 0.03052097990922685],
            [0.1553095588422853, 1.0, 0.1038202739029337],
            [0.37638036178691936, 1.0, 0.2881417758433784],
        ], dtype=np.float32),
    },
}

# Twelve additions (nine mapped, three unused) use a provisional artistic prior,
# ratified 2026-09-20, independent of native count, size or distance fade.
# Palette: DXT1 level 0 -> linear sRGB; Rec.709 luminance bands 25-40,
# 40-55, 55-70, 70-85 percentiles, mean RGB normalized by largest component.
# Native texture provenance identifies colours only, never physical density.
PROFILES["fogbluedistance"] = {
    "id": 3,
    "resource_id": 21103,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "f3e7deb8683a2768bf91fe436284f7106a3a73f9c54f84bc8665484bdb3042c5",
    "palette_texture": "dds/nebula_fogbluedistance_background_diff.pck",
    "palette_texture_sha256": "a7ff5761f878d5cc26edf960a857e1438f358c8741ff5e73a04815e308a2c4d3",
    "colours": np.array([
        [0.531753095, 0.792328998, 1.0],
        [0.614558603, 0.819621443, 1.0],
        [0.493291307, 0.754449951, 1.0],
        [0.478507135, 0.796915016, 1.0],
    ], dtype=np.float32),
}
PROFILES["fogcyancorner"] = {
    "id": 4,
    "resource_id": 21104,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "2535977d29170e4f35e23a572a9e495390eab5362a12fad4bf958001991c3134",
    "palette_texture": "dds/nebula_fogcyancorner_background_diff.pck",
    "palette_texture_sha256": "9ef28937dac7d361348ac6ae0ae9e8c606197b603946886340b9454197b88583",
    "colours": np.array([
        [0.346075488, 1.0, 0.736223068],
        [0.511666112, 1.0, 0.804777124],
        [0.500431708, 1.0, 0.826264367],
        [0.424702157, 1.0, 0.808137849],
    ], dtype=np.float32),
}
PROFILES["fogdeepred"] = {
    "id": 5,
    "resource_id": 21105,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "29c3b8ac38ee8ccbb41f3a91e3e41784d66ece023712094a5cd1f00c989e60ce",
    "palette_texture": "dds/nebula_fogdeepred_background_diff.pck",
    "palette_texture_sha256": "6e9902936f22c6a49a51e8c360b906a986ab08911266422ed6ba2ca7362989b2",
    "colours": np.array([
        [1.0, 0.145736102, 0.056798133],
        [1.0, 0.149507019, 0.213694711],
        [1.0, 0.113074939, 0.157847527],
        [1.0, 0.158813063, 0.169048604],
    ], dtype=np.float32),
}
PROFILES["foggreeneye"] = {
    "id": 6,
    "resource_id": 21106,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "4004b37835958771f0ac0a61fc901a6ae168cd9bac0dedd0435173189c83580f",
    "palette_texture": "dds/nebula_foggreeneye_background_diff.pck",
    "palette_texture_sha256": "593a36e5f0492607db09f0b373f58fbfaf6a32f02decc4d6cfd5cf04928f7c2b",
    "colours": np.array([
        [0.005055871, 1.0, 0.001904873],
        [0.209908174, 1.0, 0.0],
        [0.45139524, 1.0, 0.007823036],
        [0.596881805, 1.0, 0.096529326],
    ], dtype=np.float32),
}
PROFILES["fogparanid"] = {
    "id": 7,
    "resource_id": 21107,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "489ad7b5d0ac188a8a3942b443ccb191f57298afea8f4554bcef4c82f5f163a8",
    "palette_texture": "dds/nebula_fogparanid_background_dust_diff.pck",
    "palette_texture_sha256": "c1a2ce6da7bf60cda4a20434f955dab8b7f0bd1e4e185224ecd668fe33573dfd",
    "colours": np.array([
        [1.0, 0.455769741, 0.198380217],
        [1.0, 0.465190615, 0.332247112],
        [1.0, 0.443522576, 0.32235407],
        [1.0, 0.394192551, 0.255797946],
    ], dtype=np.float32),
}
PROFILES["fogred"] = {
    "id": 8,
    "resource_id": 21108,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "aa4d4724f3428224153cdedb693af42624338b3feabb2900f59833eebb124f9f",
    "palette_texture": "dds/nebula_fogred_background_diff.pck",
    "palette_texture_sha256": "5d5897dda9d8d0d6a6f4ecb60cca6394ded293985134a7ee73e6ed2e5250ba16",
    "colours": np.array([
        [1.0, 0.358147651, 0.600187659],
        [1.0, 0.342961101, 0.551704177],
        [1.0, 0.378581022, 0.561493254],
        [1.0, 0.33708978, 0.504087151],
    ], dtype=np.float32),
}
PROFILES["uranus"] = {
    "id": 9,
    "resource_id": 21109,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "b81b7093c05493eb0e890fe1faba1b9711c03f8958135a030ad05fc3d57d9ba1",
    "palette_texture": "dds/uranus_dust_noise_diff.pck",
    "palette_texture_sha256": "2142c8c035d1d3e4cedf8af3d09808fff2cc80370f3271d4b0270f143ca4fa13",
    "colours": np.array([
        [0.002897166, 1.0, 0.803517316],
        [0.028545488, 1.0, 0.999878942],
        [0.035377825, 1.0, 0.975717221],
        [0.023563601, 0.915323659, 1.0],
    ], dtype=np.float32),
}
PROFILES["uranus3"] = {
    "id": 10,
    "resource_id": 21110,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "b81b7093c05493eb0e890fe1faba1b9711c03f8958135a030ad05fc3d57d9ba1",
    "palette_texture": "dds/uranus_dust_noise_diff.pck",
    "palette_texture_sha256": "2142c8c035d1d3e4cedf8af3d09808fff2cc80370f3271d4b0270f143ca4fa13",
    "colours": np.array([
        [0.002897166, 1.0, 0.803517316],
        [0.028545488, 1.0, 0.999878942],
        [0.035377825, 1.0, 0.975717221],
        [0.023563601, 0.915323659, 1.0],
    ], dtype=np.float32),
}
PROFILES["whitenexus"] = {
    "id": 11,
    "resource_id": 21111,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "e3e0dcbe152dc86b0d831ba07c2b694937d1bdf1b6caa94c1f5f2d62ab097152",
    "palette_texture": "dds/nebula_whitenexus_dust_diff.pck",
    "palette_texture_sha256": "dd3bd6cbc175b14bb65ecd9b133f854aac52f060bf13bd6993bfce58dcf105f7",
    "colours": np.array([
        [1.0, 0.269206601, 0.124877396],
        [1.0, 0.303087822, 0.192424069],
        [1.0, 0.322340655, 0.220064431],
        [1.0, 0.327002917, 0.216113651],
    ], dtype=np.float32),
}
PROFILES["fogblue"] = {
    "id": 12,
    "resource_id": 21112,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "c8f68b7392593b3e0072878f45869a22b47307a099ef6d67c59570f33a278c2e",
    "palette_texture": "dds/nebula_fogblue_background_diff.pck",
    "palette_texture_sha256": "4376457e0d527615cbae66d78da4382c172447aa001d16a4dc335c67b6744aea",
    "colours": np.array([
        [0.0, 1.0, 0.014169737],
        [1.9676e-05, 1.0, 0.05931742],
        [0.008298776, 1.0, 0.267360918],
        [0.036311235, 1.0, 0.337220287],
    ], dtype=np.float32),
}
PROFILES["fogkhaak"] = {
    "id": 13,
    "resource_id": 21113,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "326c0a8edae60e0245a8f119f02fda11b6f81ff598016d7c16179fa5ac3bd90a",
    "palette_texture": "dds/nebula_fogkhaak_background_diff.pck",
    "palette_texture_sha256": "e70032efd41dda59d252ca514e0815dd68d529430c1af8763bb0bc52228a31a9",
    "colours": np.array([
        [0.0, 0.0, 1.0],
        [0.006095439, 0.0, 1.0],
        [0.100648216, 0.0, 1.0],
        [0.225240593, 0.0, 1.0],
    ], dtype=np.float32),
}
PROFILES["khaakhive"] = {
    "id": 14,
    "resource_id": 21114,
    "occupancy": 0.12,
    "base_sigma": 2.5e-6,
    "density_status": "provisional_artistic",
    "atlas_sha256": "326c0a8edae60e0245a8f119f02fda11b6f81ff598016d7c16179fa5ac3bd90a",
    "palette_texture": "dds/nebula_fogkhaak_background_diff.pck",
    "palette_texture_sha256": "e70032efd41dda59d252ca514e0815dd68d529430c1af8763bb0bc52228a31a9",
    "colours": np.array([
        [0.0, 0.0, 1.0],
        [0.006095439, 0.0, 1.0],
        [0.100648216, 0.0, 1.0],
        [0.225240593, 0.0, 1.0],
    ], dtype=np.float32),
}
