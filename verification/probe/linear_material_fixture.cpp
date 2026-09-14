// Detached numerical qualification: local original game programs stay
// untracked. Binary cases are authored by run_linear_material.py; it owns the
// independent float64 oracle. Ordinary cases keep world position constant
// while clip geometry covers the RT, producing uniform lighting varyings.
// Flag-16 gradient cases vary world position and normal per vertex; flag 64
// additionally varies clip W. The oracle interpolates native VS outputs.
#define WIN32_LEAN_AND_MEAN
#include "../../src/renderer/linear_material.h"
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
#include "../../src/renderer/linear_distance_fade.h"
#endif
#include "../../src/renderer/material_motion.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
void api(HRESULT h) {
  if (FAILED(h)) {
    std::printf("API_FAIL %08lx\n", h);
    throw std::runtime_error("D3D9 API");
  }
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> struct Com {
  T *p = nullptr;
  Com() = default;
  Com(const Com &) = delete;
  ~Com() {
    if (p)
      p->Release();
  }
  T *operator->() const { return p; }
};
struct Case {
  unsigned id, pair, depth, lights, reverse, affine, valid, fp16, flags;
  // Asteroid uses lightmap RGBA as detail; f47..50 = base/detail weights, base UV.
  // Existing f[0..31] retain their meanings. Append normal RGBA, B, T,
  // camera, fog clip and scalar diffuse/specular/reflection/power; flags: asymmetric cube=1, fog=2, boundary=4, detail UV pattern=8; palette gradient=16, diffuse UV pattern=32, perspective gradient=64.
  float f[51];
};
static_assert(sizeof(Case) == 240, "binary case ABI");
// Appended XT rows alone use the following flags and fields. All old 148 rows
// retain their exact geometry, declarations, constants and binary meanings.
// f47..50: diffuse, specular, reflection strengths and specular power.
// Flags: 128 palette, 256 decal, 512 alternate RGB palette, 1024 alternate
// O/detail data, 2048 requested FLAT, 4096/8192 O.r=.50/.51 (default .49),
// 16384 alternate secondary UV, 32768 FLOAT2 UV, 65536 near-plane clipping.
constexpr unsigned xt_first_pair = 148;
// Standard BUMPMAP hull pair 4944d81dfe531b37/64bac8bb307eb896: the seventh
// distance-fade producer (docs/architecture/linear-station-source-over.md).
constexpr unsigned station_fade_pair = 51;
constexpr unsigned glass_first_pair = 162;
bool is_glass(const Case& c) { return c.pair >= glass_first_pair; }
bool is_xt(const Case& c) { return c.pair >= xt_first_pair && c.pair < glass_first_pair; }
bool xt_default(const Case& c) {
  return is_xt(c) && (c.pair == 148 || c.pair == 149 || c.pair == 156 || c.pair == 157);
}
const char * vertex_ids[] = {
    "53a0a641107ed76c", "719856ce0c213220", "badefd5143b3024f", "4944d81dfe531b37", "44c4a41ca92ae2e3", "19a246a56e9d9700",
    "494fe349b8bc12ec",
    "b0602757fce6e870", "0c223ad11bce02d5", "233d17d26ce0c1fc", "167eb2d5629ab9d3", "330ceb9dd874ede2", "12b8a13f13fe8cfe",
    "29d7c575396ed280", "a420a010b0271479", "ea3d15b287892410", "57392213f62fef19", "5c17a381b149b3b9", "a804f173f693944a", "37e6956afd8b8d76", "2e0254dd999841c2", "a7cddf2c98d61117", "33388c8897d428a5", "b4059ab6af8fc529", "2a560f246c90fa64",
    "37c34a7478544c14",
    "c30104cb0efb6675", "e2ad860d5fbb3e59", "74fdc00d802b4027"};
const char * pixel_ids[] = {
    "63f96eba9eea7880", "8759c7838bbc86c2", "593e5dea9b3457d5", "7a0bb00a8070496a", "8d5b2ba0fb4d13bf", "dab93928f26906f7",
    "3b94320087e81945", "e3b7acc16da9932d", "7a14d4dcb28f27e5", "8ab6188a40ca15ea", "8df6143d0e77d92e", "e16a9806ee3544c3",
    "ca6bfa4a6cca7e2a", "5e0a10fe752b6140", "63379470db8d2a86", "68915563dd0aac9a", "d086fde54698070c", "f17fffd88d134b04",
    "462342e3e5781384", "827d8d2d617bedce", "02606104fa59fb29", "1d638938d93421b3", "bd4d51c08486c6e0", "de2dd381fa64193d",
    "7c83ed50c9894e44", "e70adc744a38ca59", "db644b73b68c0547", "ff32b602a271c327", "f6a501717c3e5ca8", "55826dc176afe464",
    "0c1f3f0f440e4a0c", "64bac8bb307eb896", "789449ffd931d23e", "4f052209611387f0", "abf3c0fad53456d8", "cf449bcb069aec4f",
    "99153c144030c396", "c1452981fd0bff64", "b0f9313b77cc78ee", "d514bf852d8a9c58", "dff6a3d360603fa2", "f1d14a7dbf7c6173",
    "1f26d41bcb7dac1e", "bdcdb3ab996ae4e0", "78963cdc7c710e04", "1ed1bf0fdec00e1a", "2b04461d0dae038b", "acc83ed2509d84a1",
    "3006f8030a467739", "d6e8bdde0e4c515f", "e5ea78b8b0b0fe07", "f42202faf57a3c89", "769c3814fc0efba8", "22cc5b05a55ef61e",
    "ef2bf556f207b8bd", "91b6c09eb47f8555", "cc09f17db377fd9e", "3755809bd40afc13", "61418505e5d8f998", "b5f1d4145171026b",
    "3602b05ce11ca6ff", "8e58ac79b59b02b1", "042c9ae16f41feff", "68f0dd6791fd7d3d", "5c823b8507fa1442", "a6e1328c0bb3f401",
    "517540ae6d5e5410", "7a0c3388065bb08d", "d44db87778a43b61", "550c2a4d4d3ed70f",
    "39eb3c2258a516e1", "57acf59d19c73791", "f917d48ee826da1f", "77a5b2d62fb3be48", "a910daef935891ce", "62c180abe017e239", "ed44232013f67072", "f286856c3f400377", "9d27e7ba242f3831", "e1acf8a03850acaf", "f646f03be5a8708d", "ebf41e1ace7af45b", "c997a37560e266df", "675f9077d8fd21c4", "18d372968af4a480", "188c5ab9dbb98393", "7e5e41276b3d7514", "43c9405568d2226f", "5e056627e9ff3a8d", "fce465befff2f623",
    "fffdabd910793aba", "e6794b6ec37ff71a", "5f82ecacd39529cd", "f1b0e820c7b488c3", "6733b119142c8d42", "496049cec2066ed3", "d51cf763125cb85a", "31445adb0a62d134", "fd58e6b7e8cf969c", "dd87737d697c6764", "d22f2ce2c740e6a7", "1de3d2dde345a7e3", "75fb9c6b05e28ea2", "edaef099780fcafe",
    "a66fb1981ba755b2", "ebc9b2b3f1564e9a", "f31c9e2701c8eee4", "9d49f288800f898d"};
// Derived register-layout facts; original instructions own the lobe and normal math.
const bool pixel_affine[] = {
    true, true, false, true, true, false,
    true, true, true, true, false, false,
    true, true, true, false, true, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    true, true, true, true, false, false,
    false, false, false, false,
    false, false, false, false, false, false, false, false, true, true, true, true, false, false, true, true, true, true, false, false,
    true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    false, false, false, false};
const bool pixel_bump[] = {
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    true, true, true, true, true, true,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    true, true, true, true, true, true,
    true, true, true, true, true, true,
    true, true, true, true, true, true,
    true, true, true, true, true, true,
    false, false, false, false, false, false,
    true, true, true, true, true, true,
    false, false, true, true,
    false, false, false, false, true, true, true, true, false, false, false, false, false, false, true, true, true, true, true, true,
    false, false, true, true, true, true, true, true, false, false, true, true, true, true,
    false, false, false, false};
const bool pixel_application[] = {
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    true, true, true, true, true, true,
    true, true, true, true, true, true,
    true, true, true, true, true, true,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false, false, false,
    false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false};
const unsigned pixel_directions[] = {
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 1,
    2, 1, 2, 1,
    2, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1};
const unsigned pair_v[] = {
    0, 0, 1, 1, 1, 1,
    2, 2, 2, 2, 0, 0,
    1, 1, 1, 1, 2, 2,
    2, 2, 3, 3, 4, 4,
    4, 4, 5, 5, 5, 5,
    0, 0, 1, 1, 1, 1,
    2, 2, 2, 2, 6, 6,
    1, 1, 1, 1, 2, 2,
    2, 2, 3, 3, 4, 4,
    4, 4, 5, 5, 5, 5,
    3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 3, 3,
    4, 4, 4, 4, 5, 5,
    5, 5, 3, 3, 4, 4,
    4, 4, 5, 5, 5, 5,
    0, 0, 1, 1, 1, 1,
    2, 2, 2, 2, 3, 3,
    4, 4, 4, 4, 5, 5,
    5, 5,
    7, 8, 9, 10, 11, 12,
    13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24,
    6, 6, 25, 25, 25, 25, 25, 25, 6, 6, 25, 25, 25, 25,
    26, 26, 27, 27, 28, 28};
const unsigned pair_p[] = {
    0, 1, 2, 3, 4, 5,
    2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 8, 9,
    10, 11, 12, 13, 14, 15,
    16, 17, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23,
    20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 26, 27,
    28, 29, 30, 31, 32, 33,
    34, 35, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 41,
    38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 44, 45,
    46, 47, 48, 49, 50, 51,
    52, 53, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59,
    56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 62, 63,
    64, 65,
    66, 67, 67, 68, 69, 69,
    70, 71, 72, 73, 72, 73, 74, 75, 76, 77, 76, 77, 78, 79, 80, 81, 82, 83, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
    104, 105, 106, 107, 106, 107};
Words load(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  require(bool(in), "missing local program");
  auto bytes = in.tellg();
  require(bytes > 0 && bytes % 4 == 0 && bytes < 65536, "shader framing");
  Words w(std::size_t(bytes) / 4);
  in.seekg(0);
  in.read(reinterpret_cast<char *>(w.data()), bytes);
  require(bool(in), "shader read");
  return w;
}
// Executable opcode count is a framing/budget lower-bound screen; documented
// multi-slot instructions are not claimed to cost one slot. CreateShader below
// is the actual device qualification of each complete program.
unsigned instructions(const Words &w) {
  unsigned result = 0;
  for (std::size_t i = 1; i < w.size();) {
    unsigned op = w[i] & 65535;
    if (op == 65535) {
      require(i + 1 == w.size(), "trailing shader bytes");
      return result;
    }
    unsigned n = op == 65534 ? (w[i] >> 16) & 32767 : (w[i] >> 24) & 15;
    require(i + n < w.size(), "instruction framing");
    if (op != 65534 && op != 31 && op != 81 && op != 47 && op != 48)
      ++result;
    i += 1 + n;
  }
  throw std::runtime_error("missing shader END");
}
float from_half(unsigned short h) {
  unsigned sign = unsigned(h & 0x8000) << 16, exponent = (h >> 10) & 31,
           mantissa = h & 1023, b;
  if (exponent == 0) {
    if (!mantissa)
      b = sign;
    else {
      int e = -14;
      while (!(mantissa & 1024)) {
        mantissa <<= 1;
        --e;
      }
      b = sign | unsigned(e + 127) << 23 | (mantissa & 1023) << 13;
    }
  } else
    b = sign | (exponent == 31 ? 255 : exponent + 112) << 23 | mantissa << 13;
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
struct Pixel {
  float f[4];
};
struct Shaders {
  IDirect3DDevice9 *d;
  D3DCAPS9 caps;
  Words originals[2][108];
  std::map<std::string, IDirect3DVertexShader9 *> vertices;
  std::map<std::string, IDirect3DPixelShader9 *> pixels;
  Shaders(IDirect3DDevice9 *device, const std::string &path,
          const std::vector<Case>& cases) : d(device) {
    api(d->GetDeviceCaps(&caps));
    // Scoped runs require only their selected originals. Load each once, after
    // validating the case index; variant creation remains lazy in bind().
    for (const auto& c:cases) {
      require(c.pair < std::size(pair_v), "case shader pair bounds");
      const unsigned v=pair_v[c.pair], p=pair_p[c.pair];
      if (originals[0][v].empty())
        originals[0][v] = load(path + "\\vs_" + vertex_ids[v] + ".bin");
      if (originals[1][p].empty())
        originals[1][p] = load(path + "\\ps_" + pixel_ids[p] + ".bin");
    }
  }
  ~Shaders() {
    for (auto &x : vertices)
      x.second->Release();
    for (auto &x : pixels)
      x.second->Release();
  }
  std::string key(const Case &c, unsigned mode, bool pixel) {
    const char *id =
        pixel ? pixel_ids[pair_p[c.pair]] : vertex_ids[pair_v[c.pair]];
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%s_%u_%u_%.9g_%.9g_%.9g", id, mode,
                  (mode || xt_default(c)) ? c.depth : 0, mode >= 2 ? c.f[0] : 0,
                  mode >= 2 ? c.f[1] : 0, mode >= 2 ? c.f[2] : 0);
    return std::string(buffer) + (xt_default(c) ? "_xt_repaired" : "");
  }
  Words transform(const Case &c, unsigned mode, bool pixel) {
    const auto &original =
        originals[pixel][pixel ? pair_p[c.pair] : pair_v[c.pair]];
    const auto before = original;
    Words output = {0xdeadbeef};
    LinearMaterialConfig config{c.f[0], c.f[1], c.f[2]};
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
    if (mode == 3) {
      require((c.pair>=110 && c.pair<116) || c.pair==station_fade_pair,"seven exact fade pairs only");
      require((pixel ? linear_distance_fade_pixel_variant(original.data(),original.size(),config,output)
                     : linear_distance_fade_vertex_variant(original.data(),original.size(),config,output)) ==
                  LinearMaterialResult::Applied,"distance fade transform");
    } else
#endif
    if (xt_default(c)) {
      // No mode ever submits the incomplete original DEFAULT linkage. Mode 0
      // is the repaired ordinary pair with MRTs disabled, mode 1 enables MRTs.
      require((pixel ? linear_material_xt_default_pixel_variant(
                           original.data(), original.size(), config, output, c.depth, mode == 2)
                     : linear_material_xt_default_vertex_variant(
                           original.data(), original.size(), config, output, c.depth, mode == 2)) ==
                  LinearMaterialResult::Applied, "XT repaired transform");
    } else if (mode == 0)
      output = original;
    else if (mode == 1)
      require((pixel ? material_motion_pixel_variant(
                           original.data(), original.size(), output, c.depth)
                     : material_motion_vertex_variant(
                           original.data(), original.size(), output,
                           c.depth)) == MaterialMotionResult::Applied,
              "motion transform");
    else {
      require((pixel ? linear_material_pixel_variant(original.data(),
                                                     original.size(), config,
                                                     output, c.depth)
                     : linear_material_vertex_variant(
                           original.data(), original.size(), config, output,
                           c.depth)) == LinearMaterialResult::Applied,
              "combined transform");
      Words sentinel = {0x12345678, 0xcafebabe};
      const auto preserved = sentinel;
      auto invalid = config;
      invalid.direct_gain = -1;
      require((pixel ? linear_material_pixel_variant(original.data(),
                                                     original.size(), invalid,
                                                     sentinel, c.depth)
                     : linear_material_vertex_variant(
                           original.data(), original.size(), invalid, sentinel,
                           c.depth)) == LinearMaterialResult::InvalidConfig &&
                  sentinel == preserved,
              "invalid config publication");
      Words damaged = original;
      damaged.back() = 0;
      require(
          (pixel ? linear_material_pixel_variant(damaged.data(), damaged.size(),
                                                 config, sentinel, c.depth)
                 : linear_material_vertex_variant(
                       damaged.data(), damaged.size(), config, sentinel,
                       c.depth)) != LinearMaterialResult::Applied &&
              sentinel == preserved,
          "invalid source publication");
    }
    require(original == before, "original mutated");
    return output;
  }
  void bind(const Case &c, unsigned mode) {
    for (bool pixel : {false, true}) {
      auto k = key(c, mode, pixel);
      if (pixel ? pixels.count(k) : vertices.count(k))
        continue;
      LARGE_INTEGER start, end, freq;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&start);
      auto w = transform(c, mode, pixel);
      const unsigned count = instructions(w);
      require(count <= (pixel ? caps.MaxPixelShader30InstructionSlots
                              : caps.MaxVertexShader30InstructionSlots),
              "device instruction budget");
      if (pixel) {
        IDirect3DPixelShader9 *p = nullptr;
        api(d->CreatePixelShader(reinterpret_cast<const DWORD *>(w.data()),
                                 &p));
        pixels.emplace(k, p);
      } else {
        IDirect3DVertexShader9 *v = nullptr;
        api(d->CreateVertexShader(reinterpret_cast<const DWORD *>(w.data()),
                                  &v));
        vertices.emplace(k, v);
      }
      QueryPerformanceCounter(&end);
      std::printf("CREATE stage=%s key=%s instructions=%u words=%zu "
                  "completed_ms=%.6f\n",
                  pixel ? "ps" : "vs", k.c_str(), count, w.size(),
                  1000. * (end.QuadPart - start.QuadPart) / freq.QuadPart);
    }
    api(d->SetVertexShader(vertices.at(key(c, mode, false))));
    api(d->SetPixelShader(pixels.at(key(c, mode, true))));
  }
};
struct Gpu {
  IDirect3DDevice9 *d;
  Shaders &shaders;
  unsigned width;
  Com<IDirect3DSurface9> back, color[2], motion, current;
  Com<IDirect3DTexture9> textures[4], detail, xt_occlusion, xt_detail;
  Com<IDirect3DCubeTexture9> cube;
  Com<IDirect3DVertexDeclaration9> declaration, xt_declaration, xt_uv2_declaration;
  Gpu(IDirect3DDevice9 *device, Shaders &s, unsigned size)
      : d(device), shaders(s), width(size) {
    api(d->GetRenderTarget(0, &back.p));
    for (unsigned f = 0; f < 2; ++f)
      api(d->CreateRenderTarget(
          width, width, f ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F,
          D3DMULTISAMPLE_NONE, 0, FALSE, &color[f].p, nullptr));
    api(d->CreateRenderTarget(width, width, D3DFMT_A32B32G32R32F,
                              D3DMULTISAMPLE_NONE, 0, FALSE, &motion.p,
                              nullptr));
    api(d->CreateRenderTarget(width, width, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0,
                              FALSE, &current.p, nullptr));
    for (auto &t : textures)
      api(d->CreateTexture(1, 1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                           &t.p, nullptr));
    api(d->CreateTexture(4, 4, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                         &detail.p, nullptr));
    api(d->CreateCubeTexture(4, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                             &cube.p, nullptr));
    for (auto* t : {&xt_occlusion, &xt_detail})
      api(d->CreateTexture(4, 4, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED,
                           &t->p, nullptr));
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 0},
        {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,
         0},
        {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_BINORMAL, 0},
        {0, 44, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,
         0},
        D3DDECL_END()};
    api(d->CreateVertexDeclaration(elements, &declaration.p));
    // FLOAT4 carries genuine independent secondary UV; FLOAT2 witnesses the
    // documented missing-component defaults z=0,w=1 in the repaired producer.
    D3DVERTEXELEMENT9 xt_elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 40, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
        {0, 52, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
        D3DDECL_END()};
    api(d->CreateVertexDeclaration(xt_elements, &xt_declaration.p));
    xt_elements[1].Type = D3DDECLTYPE_FLOAT2;
    api(d->CreateVertexDeclaration(xt_elements, &xt_uv2_declaration.p));
  }
  ~Gpu() {
    for (unsigned i = 0; i < 7; ++i)
      d->SetTexture(i, nullptr);
    d->SetRenderTarget(2, nullptr);
    d->SetRenderTarget(1, nullptr);
    d->SetRenderTarget(0, back.p);
    d->SetVertexDeclaration(nullptr);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
  }
  void state(const Case &c, unsigned mode) {
    api(d->SetRenderTarget(2, nullptr));
    api(d->SetRenderTarget(1, nullptr));
    api(d->SetDepthStencilSurface(nullptr));
    api(d->SetRenderTarget(0, color[c.fp16].p));
    api(d->SetRenderTarget(1, mode ? motion.p : nullptr));
    api(d->SetRenderTarget(2, mode && c.depth ? current.p : nullptr));
    D3DVIEWPORT9 viewport{0, 0, width, width, 0, 1};
    api(d->SetViewport(&viewport));
    for (auto s :
         {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRGBWRITEENABLE,
          D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE,
          D3DRS_SCISSORTESTENABLE, D3DRS_FOGENABLE, D3DRS_DITHERENABLE,
          D3DRS_CLIPPLANEENABLE, D3DRS_LIGHTING})
      api(d->SetRenderState(s, FALSE));
    for (auto s : {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1,
                   D3DRS_COLORWRITEENABLE2})
      api(d->SetRenderState(s, 15));
    api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    api(d->SetRenderState(D3DRS_SHADEMODE, (is_xt(c) || is_glass(c)) && (c.flags & 2048) ? D3DSHADE_FLAT : D3DSHADE_GOURAUD));
    for (unsigned n=0;n<16;++n)
      api(d->SetRenderState(D3DRENDERSTATETYPE((n<8 ? D3DRS_WRAP0 : D3DRS_WRAP8)+(n%8)),0));
    api(d->SetVertexDeclaration(is_xt(c) ? (c.flags & 32768 ? xt_uv2_declaration.p : xt_declaration.p) : declaration.p));
    shaders.bind(c, mode);
    const bool bump = pixel_bump[pair_p[c.pair]];
    const bool asteroid = c.pair >= 110 && c.pair < 116;
    const bool glass = is_glass(c);
    const bool palette = c.pair >= 116 && c.pair < 148;
    // Clear the union first: DEFAULT must never retain BUMP's stage-4 cube,
    // and switching stage-3 2D/cube roles must not depend on the last family.
    for (unsigned i = 0; i < 7; ++i)
      api(d->SetTexture(i, nullptr));
    const float xt_specular[4] = {c.f[15], .08f, .91f, .64f};
    const float mask[4] = {c.f[15], 0, 0, 1};
    const float *texels[] = {c.f + 3, bump ? c.f + 32 : mask,
                             bump ? mask : c.f + 7, c.f + 7};
    for (unsigned i = 0; i < (bump ? 4u : 3u); ++i) {
      D3DLOCKED_RECT lock{};
      api(textures[i]->LockRect(0, &lock, nullptr, 0));
      std::memcpy(lock.pBits, (is_xt(c) || glass) && i == (bump ? 2u : 1u) ? xt_specular : texels[i], 16);
      api(textures[i]->UnlockRect(0));
      api(d->SetTexture(i, textures[i].p));
    }
    // Face identity plus independent U/V bands, all exactly representable.
    // The central 2x2 region is uniform so axis directions avoid a color edge.
    const float band_u[4] = {.125f, .25f, .25f, .5f},
                band_v[4] = {.125f, .375f, .375f, .75f};
    for (unsigned face = 0; face < 6; ++face) {
      D3DLOCKED_RECT lock{};
      api(cube->LockRect(D3DCUBEMAP_FACES(face), 0, &lock, nullptr, 0));
      for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
          float value[4] = {(face + 1) / 8.f, band_u[x], band_v[y], 1};
          std::memcpy(static_cast<char *>(lock.pBits) + y * lock.Pitch + x * 16,
                      c.flags & 1 ? value : c.f + 11, 16);
        }
      api(cube->UnlockRect(D3DCUBEMAP_FACES(face), 0));
    }
    if (!asteroid)
      api(d->SetTexture(glass ? 2 : bump ? 4 : 3, cube.p));
    if ((asteroid && (c.flags & 8)) || (palette && (c.flags & 32))) {
      D3DLOCKED_RECT lock{};
      api(detail->LockRect(0, &lock, nullptr, 0));
      for (unsigned y=0;y<4;++y)
        for (unsigned x=0;x<4;++x) {
          const float value[4]={(x+1)/8.f,(y+1)/8.f,(x+y+1)/16.f,asteroid ? .875f : c.f[6]};
          std::memcpy(static_cast<char *>(lock.pBits)+y*lock.Pitch+x*16,value,16);
        }
      api(detail->UnlockRect(0));
      api(d->SetTexture(asteroid ? (bump ? 3 : 2) : 0, detail.p));
    }
    if (is_xt(c)) {
      // BUMP uses all seven native samplers: D,N,S,L,cube,O,detail.
      // DEFAULT uses D,S,L,cube,O. O/detail vary spatially for genuine UV proof.
      const float threshold = c.flags & 8192 ? .51f : c.flags & 4096 ? .50f : .49f;
      for (unsigned which = 0; which < 2; ++which) {
        auto* t = which ? xt_detail.p : xt_occlusion.p;
        D3DLOCKED_RECT lock{};
        api(t->LockRect(0, &lock, nullptr, 0));
        for (unsigned y=0; y<4; ++y)
          for (unsigned x=0; x<4; ++x) {
            float value[4];
            if (!which) {
              value[0]=threshold; value[1]=.35f;
              value[2]=(c.flags & 1024 ? .2f : .6f) + float(x)/32.f;
              value[3]=(c.flags & 1024 ? .4f : .8f) - float(y)/32.f;
            } else {
              value[0]=(c.flags & 1024 ? .65f : .55f) + float(x)/64.f;
              value[1]=(c.flags & 1024 ? .35f : .45f) + float(y)/64.f;
              value[2]=c.flags & 1024 ? .8f : .2f;
              value[3]=c.flags & 1024 ? .6f : .3f;
            }
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);
          }
        api(t->UnlockRect(0));
        if (!which || bump) api(d->SetTexture(which ? 6 : bump ? 5 : 4,t));
      }
      if (c.flags & 32) {
        D3DLOCKED_RECT lock{};
        api(detail->LockRect(0,&lock,nullptr,0));
        for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x) {
          const float value[4]={(x+1)/8.f,(y+1)/8.f,(x+y+1)/16.f,c.f[6]};
          std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);
        }
        api(detail->UnlockRect(0));
        api(d->SetTexture(0,detail.p));
      }
    }
    for (unsigned i = 0; i < 7; ++i) {
      for (auto state : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
        api(d->SetSamplerState(i, state, D3DTEXF_POINT));
      api(d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
      api(d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE));
    }
    float v[256][4]{};
    bool fixed = pair_v[c.pair] == 2 || pair_v[c.pair] == 5 ||
                 pair_v[c.pair] == 9 || pair_v[c.pair] == 12 ||
                 pair_v[c.pair] == 15 || pair_v[c.pair] == 18 ||
                 pair_v[c.pair] == 21 || pair_v[c.pair] == 24 || pair_v[c.pair] == 28;
    unsigned matrix = fixed ? 0 : 24, normal = fixed ? 10 : 31,
             camera = fixed ? 13 : 34, emissive = fixed ? 19 : 40,
             alpha = fixed ? 18 : 39, tex = fixed ? 16 : 37;
    for (unsigned k = 0; k < 4; ++k) {
      v[matrix + k][k] = 1;
      v[252 + k][k] = 1;
    }
    v[252][3] = -.125f;
    if ((palette || glass || is_xt(c)) && (c.flags & 64)) {
      // FLOAT3 object z supplies clip W; all vertices project to z=.5.
      // Previous clip keeps the same W and a projected -.125 X shift.
      v[matrix+2][2] = v[254][2] = .5f;
      v[matrix+3][2] = v[255][2] = 1;
      v[matrix+3][3] = v[255][3] = 0;
      v[252][2] = -.125f; v[252][3] = 0;
    }
    if (is_xt(c) && (c.flags & 65536)) {
      v[matrix+2][0] = v[254][0] = .25f;
      v[matrix+2][2] = v[254][2] = c.flags & 64 ? .1875f : 0;
      v[matrix+2][3] = v[254][3] = c.flags & 64 ? 0 : .1875f;
    }
    if (is_xt(c) && (c.flags & 16)) {
      v[28][0]=.125f; v[29][1]=.0625f;
    }
    if ((palette || glass) && (c.flags & 16)) {
      const unsigned world = fixed ? 7 : 28;
      v[world][0]=c.f[47]; v[world+1][1]=c.f[48];
    }
    for (unsigned k = 0; k < 3; ++k)
      v[normal + k][k] = 1;
    for (unsigned k = 0; k < 3; ++k)
      v[camera + k][3] = c.f[42 + k];
    v[fixed ? 20 : 41][0] = c.f[45];
    v[fixed ? 20 : 41][1] = c.f[46];
    v[alpha][0] = .625f;
    v[tex][0] = v[tex + 1][1] = 1;
    if (asteroid || (palette && (c.flags & 32))) {
      // Constant base UV; the actual original computes detail UV = 3*base UV.
      v[tex][0] = v[tex+1][1] = 0;
      v[tex][2] = c.f[49]; v[tex+1][2] = c.f[50];
    }
    std::memcpy(v[emissive], c.f + 16, 12);
    for (unsigned i = 0; i < (fixed ? 1u : 8u); ++i) {
      unsigned base = fixed ? 4 : i * 3;
      v[base][2] = 2;
      std::memcpy(v[base + 1], c.f + 19, 12);
      v[base + 2][0] = 2;
      v[base + 2][1] = .25f;
      v[base + 2][2] = .125f;
    }
    if (is_xt(c)) {
      if (bump) {
        // Native B layout and its c46 preshader result, not D's old registers.
        v[39][0]=c.f[49]; v[40][0]=.625f;
        std::memcpy(v[41],c.f+16,12);
        v[42][0]=2; v[43][0]=.1f;
        v[44][0]=c.f[45]; v[44][1]=c.f[46];
        v[45][0]=12; v[46][0]=.9f;
      }
      if (c.flags & 32) {
        v[37][0]=v[38][1]=0;
        v[37][2]=.125f; v[38][2]=.625f;
      }
    }
    api(d->SetVertexShaderConstantF(0, v[0], 256));
    int lights[4] = {int(c.lights), 0, 1, 0};
    api(d->SetVertexShaderConstantI(0, lights, 1));
    BOOL fog = (c.flags & 2) != 0;
    api(d->SetVertexShaderConstantB(0, &fog, 1));
    float p[221][4]{};
    unsigned profile = pair_p[c.pair];
    bool affine = pixel_affine[profile];
    unsigned glow = affine ? 3 : 0, dir = (asteroid || glass) ? 0 : glow + 1;
    if (affine) {
      if (c.affine) {
        const float rows[12] = {.75f, .125f,   0,     .0625f, 0,     .5f,
                                .25f, .03125f, .125f, 0,      .875f, -.03125f};
        std::memcpy(p, rows, sizeof rows);
      } else
        for (unsigned k = 0; k < 3; ++k)
          p[k][k] = 1;
    }
    if (!asteroid && !glass) p[glow][0] = c.f[31];
    p[dir][2] = 1;
    std::memcpy(p[dir + 1], c.f + 22, 12);
    if (pixel_directions[profile] == 2) {
      p[dir + 2][2] = -1;
      std::memcpy(p[dir + 3], c.f + 25, 12);
    }
    if (asteroid) {
      const unsigned weight = 2 * pixel_directions[profile];
      p[weight][0] = c.f[48]; // actual detail scalar
      p[weight+1][0] = c.f[47]; // actual base scalar, never reconstructed
      const BOOL native_flags[2] = {FALSE,FALSE};
      api(d->SetPixelShaderConstantB(0,native_flags,2));
    }
    if (pixel_application[profile]) {
      // Original CTAB layout: specular,power,reflection,diffuse follow lights.
      const unsigned coefficient = dir + 2 * pixel_directions[profile];
      p[coefficient][0] = c.f[48];
      p[coefficient + 1][0] = c.f[50];
      p[coefficient + 2][0] = c.f[49];
      p[coefficient + 3][0] = c.f[47];
    }
    if (is_xt(c)) {
      // c3 is the effect preshader's (1-ColorWeighting), c4 is EnableGlow.
      p[3][0]=.35f; p[4][0]=c.f[31];
      p[5][0]=p[5][1]=0; p[5][2]=1;
      std::memcpy(p[6],c.f+22,12);
      p[7][0]=p[7][1]=0; p[7][2]=-1;
      std::memcpy(p[8],c.f+25,12);
      p[9][0]=c.f[48]; p[10][0]=c.f[50];
      p[11][0]=c.f[49]; p[12][0]=c.f[47];
      unsigned first=14;
      if (bump) {
        p[13][0]=.55f; p[14][0]=1.4f; p[15][0]=1.7f; p[16][0]=.18f;
        first=17;
      } else p[13][0]=1.4f;
      const float colors[5][3]={{.18f,.75f,.33f},{.83f,.24f,.58f},
        {.41f,.62f,.16f},{.90f,.12f,.47f},{.27f,.55f,.88f}};
      for (unsigned row=0;row<5;++row) for (unsigned lane=0;lane<3;++lane)
        p[first+row][lane]=colors[row][c.flags & 512 ? (lane+1)%3 : lane];
      p[first+5][0]=2.3f; p[first+6][0]=.65f;
      const bool terra=c.pair>=156;
      const BOOL native_flags[2]={BOOL((c.flags & (terra ? 128 : 256)) != 0),BOOL((c.flags & 128) != 0)};
      api(d->SetPixelShaderConstantB(0,native_flags,2));
    }
    p[216][0] = p[216][1] = 1.f / width;
    p[216][2] = .25f / width;
    p[216][3] = -.375f / width;
    p[217][0] = c.valid ? 1.f : 0.f;
    api(d->SetPixelShaderConstantF(0, p[0], 221));
  }
  void draw(const Case &c, unsigned repeats = 1) {
    if (is_xt(c)) {
      float vertices[3][16]={{-1,1,.5f,0,0,0,0}, {3,1,.5f,1,0,0,0}, {-1,-3,.5f,0,1,0,0}};
      for (unsigned i=0;i<3;++i) {
        auto& v=vertices[i];
        if (c.flags & 64) { const float w=float(1u<<i); v[0]*=w;v[1]*=w;v[2]=w; }
        v[5]=c.flags & 16384 ? .625f : .375f;
        v[6]=c.flags & 16384 ? .375f : .625f;
        std::memcpy(v+7,c.f+28,12);
        if (c.flags & 16) { v[7]+=.0625f*v[0];v[8]+=.03125f*v[1]; }
        std::memcpy(v+10,c.f+36,12); std::memcpy(v+13,c.f+39,12);
      }
      if (c.reverse) for (unsigned k=0;k<16;++k) std::swap(vertices[1][k],vertices[2][k]);
      api(d->BeginScene());
      for (unsigned i=0;i<repeats;++i) api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,64));
      api(d->EndScene());
      return;
    }
    float vertices[3][14] = {{-1, 1, .5f, 0, 0, 0, 0, 1},
                             {3, 1, .5f, 1, 0, 0, 0, 1},
                             {-1, -3, .5f, 0, 1, 0, 0, 1}};
    if (c.pair >= 116 && (c.flags & 64)) {
      for (unsigned i=0;i<3;++i) {
        const float w=float(1u<<i);
        vertices[i][0]*=w; vertices[i][1]*=w; vertices[i][2]=w;
      }
    }
    for (auto &v : vertices) {
      std::memcpy(v + 5, c.f + 28, 12);
      if (c.pair >= 116 && (c.flags & 16)) {
        v[5] += c.f[49]*v[0]; v[6] += c.f[50]*v[1];
      }
      std::memcpy(v + 8, c.f + 36, 12);
      std::memcpy(v + 11, c.f + 39, 12);
    }
    if (c.reverse)
      for (unsigned k = 0; k < 14; ++k)
        std::swap(vertices[1][k], vertices[2][k]);
    api(d->BeginScene());
    for (unsigned i = 0; i < repeats; ++i)
      api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 56));
    api(d->EndScene());
  }
  std::vector<Pixel> read(IDirect3DSurface9 *target, D3DFORMAT format) {
    Com<IDirect3DSurface9> sys;
    api(d->CreateOffscreenPlainSurface(width, width, format, D3DPOOL_SYSTEMMEM,
                                       &sys.p, nullptr));
    api(d->GetRenderTargetData(target, sys.p));
    D3DLOCKED_RECT lock{};
    api(sys->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    std::vector<Pixel> result(width * width);
    for (unsigned y = 0; y < width; ++y)
      for (unsigned x = 0; x < width; ++x) {
        auto &p = result[y * width + x];
        auto *bytes = static_cast<char *>(lock.pBits) + y * lock.Pitch +
                      x * (format == D3DFMT_R32F            ? 4
                           : format == D3DFMT_A16B16G16R16F ? 8
                                                            : 16);
        if (format == D3DFMT_R32F)
          std::memcpy(p.f, bytes, 4);
        else if (format == D3DFMT_A16B16G16R16F) {
          unsigned short half[4];
          std::memcpy(half, bytes, 8);
          for (unsigned k = 0; k < 4; ++k)
            p.f[k] = from_half(half[k]);
        } else
          std::memcpy(p.f, bytes, 16);
      }
    api(sys->UnlockRect());
    return result;
  }
  void test(const Case &c) {
    std::vector<Pixel> native;
    if (is_xt(c) || is_glass(c)) {
      state(c,0);
      api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
      draw(c);
      native=read(color[c.fp16].p,c.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F);
    }
    state(c, 1);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    draw(c);
    auto before = read(color[c.fp16].p,
                       c.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F),
         motion_before = read(motion.p, D3DFMT_A32B32G32R32F);
    auto depth_before =
        c.depth ? read(current.p, D3DFMT_R32F) : std::vector<Pixel>{};
    if (is_xt(c) || is_glass(c)) {
      require(native.size()==before.size() &&
              std::memcmp(native.data(),before.data(),before.size()*sizeof(Pixel))==0,
              "native/repaired ordinary MRT baseline identity");
      for (unsigned y : {width/4,width/2,3*width/4})
        for (unsigned x : {width/4,width/2,3*width/4}) {
          const auto& p=before[y*width+x];
          std::printf("BASELINE id=%u x=%u y=%u rgba=%.9g,%.9g,%.9g,%.9g\n",
                      c.id,x,y,p.f[0],p.f[1],p.f[2],p.f[3]);
        }
    }
    state(c, 2);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    draw(c);
    auto after = read(color[c.fp16].p,
                      c.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F),
         motion_after = read(motion.p, D3DFMT_A32B32G32R32F);
    auto depth_after =
        c.depth ? read(current.p, D3DFMT_R32F) : std::vector<Pixel>{};
    unsigned alpha_bad = 0, motion_bad = 0, depth_bad = 0, rgb_bad = 0;
    const float ceiling = c.fp16 ? 154.625f : 154.603f;
    for (unsigned i = 0; i < width * width; ++i) {
      for (unsigned k = 0; k < 3; ++k)
        rgb_bad += !std::isfinite(after[i].f[k]) || after[i].f[k] < 0 ||
                   after[i].f[k] > ceiling;
      alpha_bad += std::memcmp(&before[i].f[3], &after[i].f[3], 4) != 0;
      motion_bad += std::memcmp(&motion_before[i], &motion_after[i], 16) != 0;
      if (c.depth)
        depth_bad +=
            std::memcmp(&depth_before[i].f[0], &depth_after[i].f[0], 4) != 0;
    }
    std::printf("INVARIANT id=%u pixels=%u alpha_bad=%u motion_bad=%u "
                "depth_bad=%u rgb_bad=%u\n",
                c.id, width * width, alpha_bad, motion_bad, depth_bad, rgb_bad);
    require(!alpha_bad && !motion_bad && !depth_bad && !rgb_bad,
            "alpha, temporal identity or finite RGB storage");
    for (unsigned y : {width / 4, width / 2, 3 * width / 4})
      for (unsigned x : {width / 4, width / 2, 3 * width / 4}) {
        const auto &p = after[y * width + x];
        std::printf("SAMPLE id=%u x=%u y=%u rgba=%.9g,%.9g,%.9g,%.9g\n", c.id,
                    x, y, p.f[0], p.f[1], p.f[2], p.f[3]);
      }
  }
  void classify_flat() {
    // Independent color-ramp sentinel: distinguish effective flat COLOR
    // interpolation from a backend that accepts FLAT but interpolates smoothly.
    // TEXCOORDs remain perspective interpolated in either case.
    const DWORD vs[]={0xfffe0300,
      0x0200001f,0x80000000,0x900f0000,
      0x0200001f,0x80000000,0xe00f0000,
      0x0200001f,0x8000000a,0xe00f0001,
      0x02000001,0xe00f0000,0x90e40000,
      0x04000004,0xe00f0001,0x90000000,0xa0e40000,0xa0e40001,0x0000ffff};
    const DWORD ps[]={0xffff0300,
      0x0200001f,0x8000000a,0x900f0000,
      0x02000001,0x800f0800,0x90e40000,0x0000ffff};
    Com<IDirect3DVertexShader9> vertex;
    Com<IDirect3DPixelShader9> pixel;
    api(d->CreateVertexShader(vs,&vertex.p)); api(d->CreatePixelShader(ps,&pixel.p));
    api(d->SetRenderTarget(2,nullptr)); api(d->SetRenderTarget(1,nullptr));
    api(d->SetRenderTarget(0,color[0].p)); api(d->SetDepthStencilSurface(nullptr));
    D3DVIEWPORT9 viewport{0,0,width,width,0,1}; api(d->SetViewport(&viewport));
    api(d->SetVertexDeclaration(declaration.p));
    api(d->SetVertexShader(vertex.p)); api(d->SetPixelShader(pixel.p));
    const float constants[8]={.125f,0,0,0,.25f,0,0,1};
    api(d->SetVertexShaderConstantF(0,constants,2));
    for (auto state : {D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,
                      D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE})
      api(d->SetRenderState(state,FALSE));
    api(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    const float vertices[3][14]={{-1,1,.5f},{3,1,.5f},{-1,-3,.5f}};
    float centers[2]{};
    for (unsigned flat=0;flat<2;++flat) {
      api(d->SetRenderState(D3DRS_SHADEMODE,flat ? D3DSHADE_FLAT : D3DSHADE_GOURAUD));
      api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
      api(d->BeginScene()); api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,56)); api(d->EndScene());
      centers[flat]=read(color[0].p,D3DFMT_A32B32G32R32F)[(width/2)*width+width/2].f[0];
    }
    require(std::fabs(centers[0]-.25f)<.0001f,"GOURAUD color sentinel");
    const bool effective=std::fabs(centers[1]-.125f)<.0001f;
    require(effective || std::fabs(centers[1]-centers[0])<.0001f,"unknown FLAT color interpolation");
    std::printf("FLAT effective=%u gouraud=%.9g requested_flat=%.9g\n",unsigned(effective),centers[0],centers[1]);
  }
  void wait(IDirect3DQuery9 *event) {
    BOOL done = FALSE;
    const auto start = GetTickCount();
    for (;;) {
      HRESULT h = event->GetData(&done, sizeof done, D3DGETDATA_FLUSH);
      if (h == S_OK && done)
        return;
      require(!FAILED(h) && GetTickCount() - start < 10000, "event wait");
      Sleep(0);
    }
  }
  void timing(Case c) {
    c.fp16 = 1;
    c.depth = 1;
    // A managed grid gives the VS a meaningful workload: 24,576 distinct
    // vertices per draw, with one normal and constant world lighting. Setup
    // allocation/upload is excluded from all timed windows.
    struct Vertex {
      float x, y, z, u, v, nx, ny, nz, bx, by, bz, tx, ty, tz;
    };
    struct XtVertex { float x,y,z,u,v,s,t,nx,ny,nz,bx,by,bz,tx,ty,tz; };
    std::vector<Vertex> grid;
    std::vector<XtVertex> xt_grid;
    grid.reserve(64 * 64 * 6);
    for (unsigned y = 0; y < 64; ++y)
      for (unsigned x = 0; x < 64; ++x) {
        float left = -1 + x / 32.f, right = left + 1 / 32.f, top = 1 - y / 32.f,
              bottom = top - 1 / 32.f;
        for (auto xy : {std::pair<float, float>{left, top},
                        {right, top},
                        {left, bottom},
                        {right, top},
                        {right, bottom},
                        {left, bottom}})
          grid.push_back(
              {xy.first, xy.second, .5f, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0});
      }
    if (is_xt(c)) {
      xt_grid.reserve(grid.size());
      for (const auto& v:grid) xt_grid.push_back({v.x,v.y,v.z,v.u,v.v,.375f,.625f,
        c.f[28],c.f[29],c.f[30],c.f[36],c.f[37],c.f[38],c.f[39],c.f[40],c.f[41]});
    }
    const unsigned stride=is_xt(c) ? sizeof(XtVertex) : sizeof(Vertex);
    Com<IDirect3DVertexBuffer9> buffer;
    api(d->CreateVertexBuffer(UINT(grid.size() * stride), 0, 0,
                              D3DPOOL_MANAGED, &buffer.p, nullptr));
    void *data = nullptr;
    api(buffer->Lock(0, 0, &data, 0));
    std::memcpy(data,is_xt(c) ? static_cast<const void*>(xt_grid.data()) : grid.data(),grid.size()*stride);
    api(buffer->Unlock());
    Com<IDirect3DQuery9> event;
    api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p));
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    for (unsigned lights : {0u, 1u, 8u}) {
      if (lights==1 && !is_xt(c)) continue;
      c.lights = lights;
      for (unsigned i = 0; i < 24; ++i) {
        unsigned mode = (i / 3) % 2 ? 2 - i % 3 : i % 3;
        state(c, mode);
        api(d->SetStreamSource(0, buffer.p, 0, stride));
        api(event->Issue(D3DISSUE_END));
        wait(event.p);
        LARGE_INTEGER begin, end;
        QueryPerformanceCounter(&begin);
        api(d->BeginScene());
        for (unsigned repeat = 0; repeat < 4; ++repeat)
          api(d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 8192));
        api(d->EndScene());
        api(event->Issue(D3DISSUE_END));
        wait(event.p);
        QueryPerformanceCounter(&end);
        if (i >= 6) {
          std::printf("TIMING pair=%u lights=%u mode=%u iteration=%u draws=4 "
                      "vertices=98304 width=%u completed_ms=%.6f",
                      c.pair, lights, mode, i - 6, width,
                      1000. * (end.QuadPart - begin.QuadPart) /
                          frequency.QuadPart);
          if (is_xt(c))
            std::printf(" palette=%u repaired=%u", unsigned(c.flags & 128), unsigned(xt_default(c)));
          std::printf("\n");
        }
      }
    }
    api(d->SetStreamSource(0, nullptr, 0, 0));
  }
};
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
#include "linear_distance_fade_fixture_inc.h"
#endif
#include "linear_alpha_test_fixture_inc.h"
int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    const bool cutout_mode=argc==4 && std::strcmp(argv[3],"--alpha-test-cutout")==0;
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
    const bool fade_mode=argc==5 && std::strcmp(argv[3],"--distance-fade")==0;
    require(argc==3 || fade_mode || cutout_mode,"args: programs cases [--distance-fade composite.bin]");
#else
    require(argc == 3 || cutout_mode, "args: programs cases [--alpha-test-cutout]");
#endif
    std::ifstream file(argv[2], std::ios::binary);
    unsigned count = 0;
    file.read(reinterpret_cast<char *>(&count), 4);
    require(count > 0 && count <= 5000, "case count");
    std::vector<Case> cases(count);
    file.read(reinterpret_cast<char *>(cases.data()),
              cases.size() * sizeof(Case));
    require(bool(file) && file.peek() == EOF, "case file framing");
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3LinearMaterialFixture";
    require(RegisterClassA(&cls) != 0, "window class");
    HWND window = CreateWindowA(cls.lpszClassName, "Detached linear material",
                                WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr,
                                nullptr, cls.hInstance, nullptr);
    require(window != nullptr, "window");
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    require(runtime != nullptr, "D3D9 runtime");
    auto raw = GetProcAddress(runtime, "Direct3DCreate9");
    IDirect3D9 *(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &raw, sizeof create);
    require(create != nullptr, "D3D9 factory export");
    {
      Com<IDirect3D9> factory;
      factory.p = create(D3D_SDK_VERSION);
      require(factory.p != nullptr, "factory");
      D3DPRESENT_PARAMETERS pp{};
      pp.Windowed = TRUE;
      pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
      pp.hDeviceWindow = window;
      pp.BackBufferWidth = pp.BackBufferHeight = 32;
      pp.BackBufferFormat = D3DFMT_A8R8G8B8;
      Com<IDirect3DDevice9> device;
      api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                &device.p));
      Shaders shaders(device.p, argv[1], cases);
      std::printf("CAPS mrt=%lu vs_slots=%lu ps_slots=%lu\n",
                  shaders.caps.NumSimultaneousRTs,
                  shaders.caps.MaxVertexShader30InstructionSlots,
                  shaders.caps.MaxPixelShader30InstructionSlots);
      require(shaders.caps.NumSimultaneousRTs >= 3, "three MRTs");
      if (cutout_mode) alpha_test_cutout_fixture(device.p,shaders,cases);
      else {
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
      if (fade_mode) distance_fade_fixture(device.p,shaders,cases,argv[4]);
      else {
#endif
      {
        Gpu gpu(device.p, shaders, 16);
        bool has_color_fixture=false;
        for (const auto& c:cases) has_color_fixture=has_color_fixture || is_xt(c) || is_glass(c);
        if (has_color_fixture) gpu.classify_flat();
        for (const auto &c : cases) {
          require(c.pair < std::size(pair_v) && c.fp16 < 2 && c.depth < 2, "case bounds");
          gpu.test(c);
        }
      }
      {
        Gpu gpu(device.p, shaders, 256);
        for (unsigned pair : {0u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u, 100u, 110u, 113u, 116u, 122u, 128u, 138u, 162u}) {
          bool selected=false;
          for (const auto& c:cases) selected=selected || c.pair==pair;
          if (!selected) continue;
          Case timed = cases.front();
          // Glass uses the same material in full/scoped runs, including nonzero Fresnel.
          if (pair == glass_first_pair)
            for (const auto& c:cases) if (c.pair==pair) { timed=c; break; }
          timed.pair = pair;
          timed.flags = pixel_bump[pair_p[pair]] ? 1 : 0;
          // Neutral normal for both AG reconstruction and LOW signed XYZ.
          timed.f[32] = timed.f[33] = timed.f[35] = .5f;
          timed.f[34] = 1.f;
          if (pair >= 110 && pair < 116) {
            timed.flags = 0;
            timed.f[47]=1; timed.f[48]=.5f;
            timed.f[49]=.0625f; timed.f[50]=.1875f;
          }
          gpu.timing(timed);
        }
        // Matched extra VS repair cost, and B's five runtime palette decodes.
        // Do not derive timing material coefficients from an unrelated old row.
        for (unsigned pair : {148u,150u}) {
          const Case* source=nullptr;
          for (const auto& c:cases) if (c.pair==pair) {source=&c;break;}
          if (!source) continue;
          for (unsigned enabled : {0u,128u}) {
            Case timed=*source; timed.flags=enabled;
            gpu.timing(timed);
          }
        }
      }
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
      }
#endif
      } // ordinary or selected cutout mode
    } // Release every D3D object before destroying its device window.
    require(DestroyWindow(window) != 0, "destroy window");
    require(FreeLibrary(runtime) != 0, "unload D3D9 runtime");
    require(UnregisterClassA(cls.lpszClassName, cls.hInstance) != 0,
            "unregister window class");
    std::printf("RESULT PASS cases=%u\n", count);
    return 0;
  } catch (const std::exception &e) {
    std::printf("RESULT FAIL %s\n", e.what());
    return 1;
  }
}
