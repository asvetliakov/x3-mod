# Stored final density: one fixed two-level runtime comparison

**Reopened 2026-09-21 by user decision: the user inspected the four fixed images and finds the stored-density appearance acceptable (no visible difference between the analytic and filtered columns). The .001 T p99 quadrature gate, inherited from the reference-convergence screens, is below display resolution (one 8-bit code ≈ .0039); for the runtime candidate the accuracy gate is now half a display code, T p99 ≤ .002 and max ≤ .003, temporal ≤ .003 unchanged. The measured run (p99 .00197, max .00236, temporal .00184) passes that gate. Next step is a production integration design, not another host screen. Earlier closure text follows.** Closed 2026-09-21 (superseded): the single authorized run failed the far-interval quadrature gate (candidate-vs-dense64 T p99 .00117–.00197 against .001; max, temporal and convergence gates pass) and the stored representation is 6–10× further from the exact field (p99 .0067–.0147) and visibly blurs the liked detail. See the [verification ledger](../verification/volumetric-fog.md#stored-density-runtime-screen-closed-2026-09-21). Original ratification text follows.** Ratified by the parent on 2026-09-21 for the single bounded host experiment below. No production integration, full cache, shader, Wine or game execution is authorized by this experiment. At ratification no numerical experiment had been performed.**

## Selected next experiment

Test a filtered bake of the **final selected density**, using two fixed128³ scalar grids, rather than caching four exact noise functions. Preserve all mass/detail seeds, scales, coefficients, family sigma/chroma and the30–40km taper. A normal sample needs two filtered2-D texture reads; only the short LOD transition samples both levels.

Use24 samples over the current near0–2.4km interval and40 over the remaining range, at most64 total. This reallocates the fixed budget to the detailed foreground instead of increasing64 to96/128. It is a new represented field plus a fixed nonuniform allocation, so keep their errors separate. It is not another authoring variant, parameter sweep or claim of exact density reproduction.

The cheap experiment lazily generates only the grid nodes touched by a small frozen ray set. Do not allocate or generate complete production caches, implement a cache manager, compile a shader or run Wine. Return one finite numerical/visual/cost comparison; the parent decides whether it warrants a prototype build.

## Evidence behind the change

`/tmp/x3-fog-analytic64-screen/report.md` closes the exact-field64-total-step attempt: nine failed gates, A full T p99/max .002622/.003182 and temporal residual .004867 versus .003. Dense convergence and laws pass. Its four-octave sampling would need roughly359 mean/364 median to452 maximum density reads per ray, versus current48. Building the32.52MiB corner cache before resolving those costs would not help quadrature.

The current source uses a1560×1430 RGBA16F atlas, two2-D reads per field sample,24 midpoint samples and optional shadow reads (`src/fog/fog_field_inc.h`). `FogPass::attach/prepare_field` already qualifies filtered2-D FP16, SYSTEMMEM upload and DEFAULT-pool texture use; volume textures and FP16 blending are not established. Existing program sizes315/506 slots constrain later integration. `fog_field_assets` recipe packets are immutable family inputs; this dynamic density cache must not masquerade as the old packet identity.

The refined field is much smoother than the old128³/6.5536km periodic carrier: its mass scales are13.1072/6.5536km, modulation1.6384km and finest erosion .4096km. Earlier isotropic filtering and angular-prefix failures concerned the old sharper field. They warn against equating average density with column accuracy; they do not prove this fixed representation fails. Its filtering must nevertheless be measured, especially because the user specifically requested internal variation.

## Frozen represented field

Use the reviewed analytic rho from `/tmp/x3-fog-mass-detail-refinement/checkpoint.json`: `max(0,B*(.50+.50*Dlo)-.20*(1-B)*Dhi)`. Do not change its hash, thresholds, coordinates or amplitude. One render unit=.2m;40km=200000.

Both levels have128³ **world-grid nodes**, with scalar spacing:

- Fine: delta=512 render units=102.4m; side between outer nodes13.0048km.
- Far: delta=4096 render units=819.2m; side104.0384km.

At integer world-grid node k, bake

`v_delta(k)=FP16( (1/8) * sum rho(delta*k + delta*(sx,sy,sz)/4) )`,

where sx,sy,sz independently take-1,+1. This is one fixed eight-point box-quadrature prefilter of final density, not an exact box integral. Define those eight offsets as the representation; no adaptive filtering, gain, sigma factor or occupancy compensation. Keep density in[0,1]. Reconstruct each level with ordinary trilinear interpolation of these stored values. Quintic noise interpolation remains inside the analytic bake, not the density sampler.

Both lattice levels remain permanently anchored to world origin. No camera reseeding or periodic tiling is allowed. Sampling a given world point through two overlapping cache windows of one level must give the same stored corners and reconstructed result. Quantization, prefilter and interpolation deliberately alter density; fine retains about four nodes per smallest erosion cell, while far is coarser than that erosion scale. The far level may suppress precisely the small mottling the user liked. Measure/show that loss rather than naming the result “the same field.”

At physical ray distance s use `lambda=1-smoothstep(20000,30000,s)` and `rho_hat=lambda*rho_fine+(1-lambda)*rho_far`. Thus fine is used to4km and far from6km; the2km transition samples both. Apply the unchanged30–40km window `w=1-smoothstep(150000,200000,s)` separately in extinction/source. This is camera-distance-dependent LOD approximation to a fixed world field, not exact camera-independent density. Smooth weighting prevents a hard switch but does not prove temporal equivalence; test its bias during movement.

Filtering can introduce low density immediately outside original support. It must preserve an all-zero sampled neighbourhood exactly, but cannot claim every analytically empty point remains empty. Record false-support optical depth and lost internal contrast. There is no homogeneous density floor; distant clear gaps must not quietly be filled by the reconstruction.

## Storage, coverage and costs

Pack four consecutive localZ nodes into RGBA lanes. There are32 Z groups, arranged8×4, each a128×128 XY tile: one1024×512 RGBA16F texture per level,4MiB each, **8MiB resident total**. This fits within the already-required2-D dimensions and format class. One lookup reads the two groups containing z0/z1, selects lanes and lerps Z; XY uses normal fractional coordinates with texel-centre correction. Always count two reads, including when the group is the same. Do not claim a conditional single-fetch saving before measuring it.

Use node-window origin `floor(camera/delta)-63` per axis. The minimum centred coverage radius is63*delta:6.4512km fine and51.6096km far. Fine therefore encloses the entire6km LOD domain and far the40km horizon. The exact node-index validity range is floor(X/delta)-origin in[0,126], so both corners exist. Do not clamp missing cache data or pretend a missing tile is zero density. Density sampling and physical shadow/depth positions are separate coordinate paths.

Two GPU generations plus one staging generation would consume24MiB payload; CPU backing, temporary generation arrays and any fourth retained copy add to that. A full two-level bake has4,194,304 stored nodes and33,554,432 analytic density evaluations with this eight-point filter. A one-node slab has16,384 new nodes,32KiB scalar payload and131,072 density evaluations per level before deduplication. RGBA packing/dirty rectangles may force larger actual uploads. These counts make generation/loading a material issue, even though texture memory is modest.

For a full40km sky ray the frozen24+40 grid has two far samples in4–6km, giving132 density reads:128 for64 single-level samples plus4 for the two blend samples. This is about2.75× old density reads, substantially less than the exact-corner route; it is **not** GPU time. Geometry-clipped rays can have a larger proportion of blend samples; report actual and worst-case read counts rather than applying132 universally. Shader field arithmetic is much smaller than four quintic noise reconstructions, but repair, shadows, state traffic and uploads remain costs.

Future cache preparation must be outside per-draw submission, reusing old valid coordinates while replacement data is prepared. Fine has only .4512km minimum margin beyond its6km domain: a provisional refill trigger at .25km remaining margin must be tested against real generation latency and motion, not assumed sufficient. Far has11.6096km minimum margin and can prepare at4km remaining. Store world-node identity separately from storage position; publish origins and completed data together. On slow preparation/teleport expose not-ready rather than drawing partial range. Reuse the existing admission/Reset contract only after its new cache-generation behavior is designed.

A later CPU generator should reuse repeated noise-corner work within spatial bricks; naïvely hashing32 corners for every one of33.55million stations is not a credible free loading operation. This experiment measures one fixed small brick per level and reports extrapolated full-bake/slab cost as an estimate, explicitly distinct from observed cold-load/native timings. Do not implement a background worker, toroidal cache or disk format here.

## Exact cheap comparison

Reuse green A/B saved128×72 camera/light/chroma, strength .03 and sigma_eff9.375e-6. Use the existing stratified subset x=4i+2,y=4j+2 for32×18 rays per pose. Add one central32×18 contiguous crop x=48..79,y=27..44 of each saved image, to inspect internal detail at the original angular pixel spacing. These are fixed subsets of the saved full camera, not new low-resolution FOVs. Deduplicate overlapping rays. No new family, camera seed, path search or full image replay.

Lazy bake only required nodes of each anchored level, memoized by(level,absolute signed node coordinates); all eight prefilter samples use the frozen analytic field. Do not replace this with a dense cube merely for convenience. Compute three transports on identical physical rays:

1. Analytic original rho, dense64-render-unit reference and128 convergence check.
2. Filtered/interpolated rho_hat, dense64-render-unit reference and128 convergence check.
3. The same rho_hat, fixed24-near+40-far candidate.

For valid radial ray length L=min(depth.b*length(view),200000), or200000 for sky, near interval is[0,min(L,12000)] with24 equal midpoint bins when nonempty. Far interval is[12000,L] with40 equal midpoint bins only when L>12000. Never add64 steps per segment. Evaluate rho_hat at each candidate midpoint, including that point's LOD weight and range window. For near/middle/shell diagnostics split the existing bins by overlap with[0,12000],[12000,150000],[150000,L], retaining their midpoint density/weights. The split transports compose to the candidate full result; no extra resampling for the shell. Dense references use their single global n=ceil(L/h) midpoint grid and the same overlap accounting.

Reuse the preceding analytic64 screen's five corner/centre depth rays and radial lengths1000,11999,12001,149999,150001,199999,200001, plus invalid0/NaN/+infinity and source-alpha0/.37/1 laws. Keep physical geometry clipping before fog integration. No change to sample counts after results.

### Temporal and cache witnesses

Use the five fixed sky rays from each pose, fixed orientation, camera translations `d*forward` with d in{-5500,-5000,-4500,0,4500,5000,5500} render units. This moves±1.1km along view direction. The fixed point Q=original_origin+25000*forward traverses relative distances3.9/4/4.1/5/5.9/6/6.1km, deliberately straddling both LOD ends; record both level values, lambda and blended density at Q even if its analytic density is zero. Do not relocate Q to find a stronger witness. Compute candidate/dense temporal differences for those rays and report both integration residual and LOD representation residual against original analytic transport.

At all fixed witness world points, emulate cache origins shifted by one grid node along each axis, including a negative-coordinate case. Generate both overlapping windows from identical absolute keys. Shared FP16 node words must match; reconstructed **per-level** density at identical X must match within ordinary floating roundoff. Hold the old origin/data pair active during a simulated pending replacement, then switch to the complete new pair. A cache shift must not add density change; LOD weighting changes are reported separately. No real upload/cache manager is implemented.

Probe every RGBA lane transition including3→0, first/last valid interpolation cells and rejected outside-cache points. A zero synthetic cache must produce exact empty identity. This is a finite sampler/transport test, not a new artifact framework.

### CPU/numerical implementation constraint

The existing host environment has a known Accelerate matrix-multiply warning path. Future Python must use explicit component rotations for R/R² and the saved camera basis, not the problematic NumPy matmul path. Compare coordinates against independent scalar three-term row sums, including signed boundary vectors and saved camera rays, before field sampling; require only stated float64 rounding tolerance, not a new rotated field. Do not suppress warnings and assume equivalent coordinates. Preserve the reference's integer hash wrapping and signed floor behavior.

Time generation of one fixed32³ node brick at each level, with the exact eight-point prefilter, and a one-node32×32 entering slab using the same generator. Record unique density/noise-corner evaluations, wall time, memory and platform. Separately count the lazy ray test's unique nodes and cache hits. Estimate full128³/128² work from counts with explicit caveats; host Python time is not x86/native-Windows or flight loading time. If the estimate is plainly unaffordable without sharing/caching, report it before proposing a build.

## Technical gates versus appearance

Keep dense128/64 convergence gates T p99/max .00025/.00075 for each represented field. Keep the previous **quadrature** gates T .001/.003 and normalized S per-channel .0005/.002 for candidate rho_hat versus dense rho_hat. Temporal quadrature residual max remains .003. These thresholds are not relaxed to obtain a pass. Laws, cache-shift identity and valid coverage are hard gates.

Separately report dense rho_hat versus dense original rho: near/full/shell T/S differences, mean and angular opacity variation, clear/support changes, internal-detail contrast and movement differences. Also report candidate-versus-original total error against the old combined thresholds, explicitly marking exceedances. **A quadrature pass does not mean equivalence to the refined field.** Representation filtering is an intentional approximation; its acceptable visible difference is an appearance decision for parent/user, not a hidden relaxation of technical gates or bit-equivalence requirement.

Produce fixed-scale contact sheets for the stratified views and central crops: original dense, filtered dense, filtered24+40 and signed error. Keep nonclipped numeric opacity statistics alongside the existing normalized-light display. If far detail is smoothed away or gaps fill, show it. The earlier128×72 B area witness found point/area T differences up to .00295, demonstrating that an error around the old .003 maximum is not automatically a visible defect nor proof of invisibility. Do not infer a perceptual threshold from that one witness. A later passing candidate still needs pixel-area/TAA and user-flight assessment.

Return one result with separate statuses for reference convergence, filtered-field quadrature, cache laws, representation difference and cost estimate. If quadrature fails or filtering visibly destroys the requested mottling, close this fixed candidate; do not change grid spacing, filter width, LOD range, samples or density. If technically sound and visually useful, the parent can ratify one isolated D3D9 sampler/march/upload fixture before production integration. No automatic broader search follows.

## Portability and remaining work

The intended storage uses public D3D9 filtered2-D A16B16G16R16F textures with the existing documented SYSTEMMEM→DEFAULT upload model. It introduces no volume-texture requirement, FP16 blend dependency or Wine-private export. Native Windows must receive the same feature. A later fixture must verify actual interpolation, shader slots/register/sampler limits, state restoration and Reset/failure recovery; source compatibility is not native verification.

Do not put the new density sampler/march inside the current506-slot composite. Preserve one colour decode/composite/encode, physical shadow coordinates, opaque depth, source alpha and TAA; repair has to be costed separately. Subsequent timing uses the established CPU-submit median<=.25ms and completed fog transaction median<=1.25/2ms at1280/1920,1920 p95<=2.5ms, with upload and repair stress separately visible. No timing claim is made by read-count reduction.

A single coarse bake would also blur the liked near interior; a single fine80km bake costs far more memory/generation. Exact procedural/corner sampling retained too many reads and already failed the fixed grid. This two-level final-density approximation is the one bounded next experiment. CPU streaming/loading, spatial family chroma, other profiles, GPU timing and native execution remain open for a useful candidate, not reasons to expand this test now.
