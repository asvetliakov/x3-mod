"""The proxy's settings: one entry per X3M_* variable the DLL reads (docs/architecture/config-file.md).

This file is the single source of truth for the configuration file `x3m.ini`. `tools/config/generate.py` writes the C++
table `src/config/config_schema_inc.h`, the commented template `assets/x3m.ini` and checks both with `--check`; the host test
`verification/analysis/test_config_schema.py` imports this module directly.

Fields of an entry (plain data, no logic):
- key: the INI key, the environment name lower-cased without `X3M_` (the environment name is derived: `X3M_` + upper).
- type: bool | int | float | int_list | float_list | enum | string | path. A bool also accepts on/off/true/false/yes/no in
  the file (normalised to 1/0); every other value reaches the DLL's read site as written.
- default: the value the DLL resolves with no file and no environment variable, as environment text; None = nothing (the
  read site's own built-in behaviour). Since step 2 these are the launcher's promoted defaults: exactly what
  `tools/manage.py launch` sends on a default modded launch, so a bare DLL flies the launcher's default configuration.
- builtin: for display only (the template and docs): what the read site does when nothing is set (default None).
- off: the base value of the fixtures' `X3M_CONFIG=bare` profile; None = unset, i.e. the DLL's pre-config "absent" behaviour
  (every entry today).
- range: the numeric values the read site accepts, as (min, max) or a list of intervals R(min, max, open_min) (a value in
  any interval passes; open_min excludes min); for a list, every element unless `elements` is given. None = no numeric
  check here. The bounds equal the site's literal or named constants (verification/results/config-file/range_sweep.py);
  a check across elements of one value (ascending cascades, far > near) or across keys (the fade gain up to the light-map
  gain) stays with the site, which logs its own invalid row and keeps its default.
- elements: for a list, one range per position (overrides `range`).
- counts: for a list, the element counts the site accepts.
- choices: the accepted words of an enum, or extra words a number or list also accepts (`game`).
- requires: keys that must be on for this one to act (documentation and template only; the DLL's gates stay the truth).
- section: the template section.
- description: one or two plain sentences for the template (user-facing) or the docs table (developer).
- developer: True = parseable from the file and the environment but not in the template (tier internals, diagnostics,
  markers, route prerequisites, fixture seams).
- env_only: True = refused from the file (the fixture seams, the default markers, X3M_CONFIG itself).
- marker_of: a launcher default marker (`*_DEFAULT`): resolves to its default only while that key comes from the defaults.
- launcher: the tools/manage.py option that sends it, or None.
- since: the date the key appeared.
- aliases: earlier keys accepted for this one after a rename (none yet).
"""

import re

SECTIONS = ('graphics', 'hdr', 'shadows', 'fog', 'camera', 'window', 'audio', 'loading', 'engine', 'logging')
TYPES = ('bool', 'int', 'float', 'int_list', 'float_list', 'enum', 'string', 'path')
SINCE = '2026-09-26'


def R(lo, hi, open_min=False):
    """One interval of accepted values: lo..hi inclusive, or (lo, hi] with open_min."""
    return (float(lo), float(hi), bool(open_min))


def intervals(value):
    """A range as a tuple of R intervals: (lo, hi) or [R(...), ...]."""
    if value is None:
        return None
    if len(value) == 2 and all(isinstance(v, (int, float)) for v in value):
        return (R(*value),)
    return tuple(R(*v) if len(v) == 2 else tuple(v) for v in value)


def entry(key, type, section, description, default=None, *, builtin=None, off=None, range=None, elements=None, counts=None, choices=(),
          requires=(), developer=False, env_only=False, marker_of=None, launcher=None, since=SINCE, aliases=()):
    return dict(key=key, env='X3M_' + key.upper(), type=type, section=section, description=description, default=default,
                builtin=builtin, off=off, range=intervals(range), elements=tuple(intervals(e) for e in elements) if elements else None,
                counts=tuple(counts) if counts else None, choices=tuple(choices), requires=tuple(requires),
                developer=developer, env_only=env_only, marker_of=marker_of, launcher=launcher, since=since, aliases=tuple(aliases))


def dev(key, type, section, description, default=None, **kw):
    return entry(key, type, section, description, default, developer=True, **kw)


def seam(key, description):
    """A fixture seam: environment only, never from a file (a player's file cannot arm one)."""
    return entry(key, 'string', 'logging', description, developer=True, env_only=True)


def marker(key, of, launcher):
    return entry(key, 'bool', 'logging', f'Launcher marker: {of} came from the launcher\'s default (its row says default=1). '
                 f'Resolved by the DLL from the source of {of}.', '1', developer=True, env_only=True, marker_of=of, launcher=launcher)


SETTINGS = [
    # ---------------------------------------------------------------- graphics
    entry('taa', 'bool', 'graphics', 'Temporal anti-aliasing. Smooths edges and flicker by blending each frame with the previous ones. '
          '1 = on, 0 = off. Needs hdr = 1 for the full effect. Costs some frame rate.', '1',
          launcher='--taa'),
    entry('taa_history_weight', 'float', 'graphics', 'How much of the previous frames the anti-aliasing keeps. Higher is smoother but '
          'moving objects leave slightly longer trails. 0.5 to 0.98.', builtin='0.9', range=(0.5, 0.98), requires=('taa',),
          launcher='--taa-history-weight'),
    entry('taa_sharpen', 'float', 'graphics', 'Sharpening applied after the anti-aliasing, against its slight softness. '
          '0 = none, 1 = strongest.', '0.75', range=(0.0, 1.0), requires=('taa',), launcher='--taa-sharpen'),
    entry('taa_mip_bias', 'float', 'graphics', 'Texture detail while the anti-aliasing is on. Negative values pick sharper texture '
          'levels; 0 = the game\'s own choice. -8 to 8.', '-0.5', range=(-8.0, 8.0), requires=('taa',), launcher='--taa-mip-bias'),
    entry('motion_jitter', 'bool', 'graphics', 'Tiny sub-pixel camera shifts every frame that the anti-aliasing turns into extra edge '
          'detail. 1 = on, 0 = off (the anti-aliasing then only blends).', '1', requires=('taa',), launcher='--motion-jitter'),
    entry('taa_far_stabiliser', 'float_list', 'graphics', 'Calms the shimmer of small distant details (antennas, struts). The first '
          'number is how strongly (0.5 to 0.99); the others tune when it applies. 0 = disabled.', '0.985,0,60,68,0.03,0.25',
          counts=(1, 2, 4, 6), elements=((0, 0.99), (0, 4), [R(0, 1e6, True)], [R(0, 1e6, True)], (0, 64), [R(0, 64, True)]),
          requires=('taa',), launcher='--taa-far-stabiliser'),
    entry('taa_thin_region', 'float_list', 'graphics', 'Calms the crawling of thin lattices and struts. The first number is how '
          'strongly (0.5 to 0.99); 0 = disabled.', '0.97,1', counts=(1, 2, 4), elements=((0, 0.99), (0, 1), (0, 64), [R(0, 64, True)]),
          requires=('taa',),
          launcher='--taa-thin-region'),
    entry('taa_thin_region_emissive', 'float', 'graphics', 'Also calms small lights on thin structures that are brighter than this '
          '(in the scene\'s brightness units): lower catches dimmer lights. 0 = off, up to 65000.', '1',
          range=(0.0, 65000.0), requires=('taa_thin_region', 'hdr'), launcher='--taa-thin-region-emissive'),
    entry('taa_alpha_history', 'bool', 'graphics', 'Lets the bloom glow of see-through effects follow the anti-aliasing. Experimental. '
          '1 = on, 0 = off.', builtin='0', requires=('taa',), launcher='--taa-alpha-history'),
    entry('cull_small_parts_px', 'float', 'graphics', 'Skips drawing model parts smaller than this many pixels on screen, for a higher '
          'frame rate in busy scenes. 0 = draw everything; up to 64.', '4.0000', range=(0.0, 64.0), launcher='--cull-small-parts'),
    entry('cull_small_parts_projectiles', 'enum', 'graphics', 'Whether weapon bolts are exempt from the small-part skipping above. '
          'on = bolts are always drawn, off = they are skipped like any small part.', 'on', choices=('on', 'off'),
          launcher='--cull-small-parts-projectiles'),
    entry('bolt_footprint', 'float_list', 'graphics', 'Minimum on-screen width and length of weapon bolts in the chase view, in pixels, '
          'so distant shots stay visible. 0 = the game\'s own size; the width up to 64, the length up to 256.', '3,12',
          counts=(1, 2), elements=((0, 64), [R(0, 256, True)]),
          requires=('camera', 'screen_emission_additive'), launcher='--bolt-footprint'),
    # ---------------------------------------------------------------- hdr
    entry('hdr', 'bool', 'hdr', 'High dynamic range rendering: lights brighter than white, automatic exposure and a filmic '
          'tone curve. 1 = on, 0 = off (the game\'s original look). Costs some frame rate and video memory.', '1', launcher='--hdr'),
    entry('hdr_tonemap', 'enum', 'hdr', 'The tone curve that maps bright light to the screen. agx = filmic (soft highlights), '
          'identity = no curve.', 'agx', choices=('agx', 'identity', '1', '0'), requires=('hdr',), launcher='--hdr-tonemap'),
    entry('hdr_look', 'enum', 'hdr', 'A colour style on top of the tone curve. none = neutral, golden = warmer, punchy = more '
          'contrast and saturation.', 'none', choices=('none', 'golden', 'punchy'), requires=('hdr_tonemap',), launcher='--hdr-look'),
    entry('hdr_exposure', 'enum', 'hdr', 'How the brightness is chosen. auto = adapts to the scene like an eye, manual = fixed at '
          'hdr_ev_manual, fixed = no adaptation.', 'auto', choices=('auto', 'manual', 'fixed'), requires=('hdr',), launcher='--hdr-exposure'),
    entry('hdr_ev', 'float', 'hdr', 'Overall brightness correction in exposure stops: +1 is twice as bright, -1 half. -16 to 16.',
          '0.0', range=(-16.0, 16.0), requires=('hdr',), launcher='--hdr-ev'),
    entry('hdr_ev_manual', 'float', 'hdr', 'The fixed exposure in stops when set (it switches the exposure to manual). Empty = not '
          'used. -16 to 16.', '', range=(-16.0, 16.0), requires=('hdr',), launcher='--hdr-ev-manual'),
    entry('hdr_key', 'float', 'hdr', 'The brightness the automatic exposure aims for; higher is brighter. Above 0, up to 64.',
          builtin='0.18', range=[R(0, 64, True)], requires=('hdr',)),
    entry('hdr_ev_min', 'float', 'hdr', 'The darkest the automatic exposure may go, in stops. -16 to 16.', '-3.0', range=(-16.0, 16.0),
          requires=('hdr',), launcher='--hdr-ev-min'),
    entry('hdr_ev_max', 'float', 'hdr', 'The brightest the automatic exposure may go, in stops. -16 to 16.', '1.3', range=(-16.0, 16.0),
          requires=('hdr',), launcher='--hdr-ev-max'),
    entry('hdr_adapt_up', 'float', 'hdr', 'Seconds the exposure takes to adapt when the scene gets darker (the view brightens). '
          'Above 0, up to 60.', builtin='0.4', range=[R(0, 60, True)], requires=('hdr',)),
    entry('hdr_adapt_down', 'float', 'hdr', 'Seconds the exposure takes to adapt when the scene gets brighter (the view darkens). '
          'Above 0, up to 60.', builtin='1.2', range=[R(0, 60, True)], requires=('hdr',)),
    entry('hdr_dither', 'bool', 'hdr', 'Adds invisible noise to the final image so smooth gradients (nebulae, sky) show no banding. '
          '1 = on, 0 = off.', '1', requires=('hdr',), launcher='--hdr-dither'),
    entry('hdr_bloom', 'bool', 'hdr', 'Glow around bright lights. 1 = on, 0 = off.', '1', requires=('hdr_tonemap',), launcher='--hdr-bloom'),
    entry('bloom_source_clamp', 'float', 'hdr', 'Caps how bright a light may be before it feeds the glow, so a single hot pixel does '
          'not flare. Lower = subtler glow. Above 0, up to 64.', '1.0', range=[R(0, 64, True)], requires=('hdr_bloom',), launcher='--bloom-source-clamp'),
    entry('emission_source_gain', 'float', 'hdr', 'Brightness of engine exhausts, weapon effects and ship guide lights. 1 = the game\'s '
          'own, up to 8.', '2.0', range=(1.0, 8.0), requires=('hdr',), launcher='--emission-source-gain'),
    entry('screen_emission_additive', 'float', 'hdr', 'Brightness of weapon bolts and other additive effects above plain white, so '
          'they glow. 0 = off, else 1 to 8.', '2.0', range=[(0, 0), (1, 8)], requires=('hdr',), launcher='--screen-emission-additive'),
    entry('hull_lightmap_gain', 'float', 'hdr', 'Brightness of the lit windows and markings on ship and station hulls. 1 = the '
          'game\'s own, up to 8.', '4.0', range=(1.0, 8.0), requires=('hdr',), launcher='--hull-lightmap-gain'),
    entry('original_fill', 'float', 'hdr', 'A faint light on the dark side of ships and stations, tinted by the local sun. 0 = none '
          '(fully dark), up to 0.5.', '0.01', range=(0.0, 0.5), requires=('hdr',), launcher='--original-fill'),
    # ---------------------------------------------------------------- shadows
    entry('sun_shadow_lane', 'bool', 'shadows', 'Sun shadows: the main switch (ships and stations cast shadows from the local sun). '
          '1 = on, 0 = off. Costs frame rate and video memory (see the shadow map settings below).', '1', requires=('hdr', 'taa'), launcher='--sun-shadow-lane'),
    entry('shadow_replay_depth', 'bool', 'shadows', 'Draws the shadow casters into the shadow maps; needed for any sun shadow. '
          '1 = on, 0 = off.', '1', launcher='--shadow-replay-depth'),
    entry('sun_shadow_apply', 'bool', 'shadows', 'Darkens the scene where the shadow maps say it is in shadow. 1 = on, 0 = off.', '1',
          requires=('sun_shadow_lane', 'shadow_replay_depth'), launcher='--sun-shadow-apply'),
    entry('shadow_cascades', 'float_list', 'shadows', 'Distances (in game units) at which the five shadow maps hand over to the next '
          'one, nearest first. Larger numbers push the shadows farther out at lower detail. Five numbers separated by commas, each '
          'larger than the one before, 50 to 150000; 0 = no sun shadows.', '250.0,1500.0,7500.0,37500.0,150000.0',
          range=[(0, 0), (50, 150000)], counts=(1, 2, 3, 4, 5),
          requires=('shadow_replay_depth',), launcher='--shadow-cascades'),
    entry('shadow_cascade_sizes', 'int_list', 'shadows', 'Resolution of each of the five shadow maps in pixels, nearest first. Higher '
          'is sharper and costs video memory and time. 64 to 4096 each; one number applies to every map.', '2048,4096,4096,4096,2048',
          range=(64, 4096), counts=(1, 2, 3, 4, 5),
          requires=('shadow_cascades',), launcher='--shadow-cascade-sizes'),
    entry('shadow_cascade_records', 'int_list', 'shadows', 'How many objects each shadow map may draw per frame, nearest first. '
          'Higher shows more shadows in busy scenes and costs time. 1 to 4096 each.', '1024,1024,2048,4096,4096', range=(1, 4096),
          counts=(1, 2, 3, 4, 5),
          requires=('shadow_cascades',), launcher='--shadow-cascade-records'),
    entry('shadow_cascade_drop_order', 'enum', 'shadows', 'Which shadows go first when a map is over its limit. importance = the '
          'smallest and farthest, submission = whatever the game draws last.', 'importance', choices=('importance', 'submission'),
          requires=('shadow_cascades',), launcher='--shadow-cascade-drop-order'),
    entry('shadow_cascade_adaptive_c0', 'float', 'shadows', 'Sizes the nearest shadow map to your own ship, as this many times its '
          'radius, so big ships keep sharp shadows. 0.5 to 8.', '1.5', range=(0.5, 8.0), requires=('shadow_cascades',),
          launcher='--shadow-cascade-adaptive-c0'),
    entry('shadow_alpha_casters', 'bool', 'shadows', 'Lets see-through cut-out parts (grilles, lattices) cast their real shadow '
          'shape. 1 = on, 0 = off.', '1', requires=('shadow_replay_depth',), launcher='--shadow-alpha-casters'),
    entry('shadow_caster_retention', 'bool', 'shadows', 'Keeps a shadow for a few frames when the game briefly skips drawing its '
          'object, against flickering shadows. 1 = on, 0 = off.', '1', requires=('shadow_cascades',), launcher='--shadow-caster-retention'),
    entry('sun_shadow_bias_units', 'float', 'shadows', 'Shadow offset in game units against speckled self-shadowing ("shadow acne"). '
          'Higher removes speckles but detaches shadows slightly. 0 to 1000.', '0.53571875', range=(0.0, 1000.0), requires=('sun_shadow_apply',),
          launcher='--sun-shadow-bias-units'),
    entry('sun_shadow_bias_clamp_texels', 'float', 'shadows', 'Upper limit of the shadow offset, in shadow-map pixels. 1 to 64.', '20.97152',
          range=(1.0, 64.0), requires=('sun_shadow_apply',), launcher='--sun-shadow-bias-clamp-texels'),
    entry('sun_shadow_bias_slope_texels', 'float', 'shadows', 'Extra shadow offset on surfaces that face away from the sun, in '
          'shadow-map pixels. 0 to 8.', '0.2', range=(0.0, 8.0), requires=('sun_shadow_apply',), launcher='--sun-shadow-bias-slope-texels'),
    # ---------------------------------------------------------------- fog
    entry('volumetric_fog', 'bool', 'fog', 'Volumetric nebula fog that the sun lights and ships cast shadows into, replacing the '
          'flat fog cards. 1 = on, 0 = the game\'s own fog. Costs frame rate inside nebulae (fog_march_scale trades it against '
          'sharpness).', '1', requires=('taa', 'hdr', 'shadow_replay_depth', 'shadow_cascades'),
          launcher='--volumetric-fog'),
    entry('volumetric_fog_strength', 'float', 'fog', 'Density of the volumetric fog. 0 to 0.1.', '0.02', range=(0.0, 0.1),
          requires=('volumetric_fog',), launcher='--volumetric-fog'),
    entry('volumetric_fog_cards', 'enum', 'fog', 'What happens to the game\'s own flat fog cards. replace = hidden behind the '
          'volumetric fog, keep = drawn as well.', 'replace', choices=('replace', 'keep'), requires=('volumetric_fog',),
          launcher='--volumetric-fog-cards'),
    entry('volumetric_fog_range', 'enum', 'fog', 'Where the fog is. stored = the per-sector fog shapes shipped with the mod (run '
          'x3m-regenerate once), legacy = an older estimate from the game\'s fog cards.', 'stored', choices=('stored', 'legacy'),
          requires=('volumetric_fog',), launcher='--volumetric-fog-range'),
    entry('fog_march_scale', 'enum', 'fog', 'Resolution of the fog calculation. 4 = quarter resolution (fast), 2 = half '
          'resolution (sharper, slower).', '4', choices=('2', '4'), requires=('volumetric_fog_range',), launcher='--fog-march-scale'),
    entry('fog_dust_motes', 'float_list', 'fog', 'Dust specks near the camera inside fog: count (64 to 8192), size (2 to 16) and streak length (0 to 512). '
          'A count of 0 turns them off.', '1300,3,128', counts=(3,), elements=([(0, 0), (64, 8192)], (2, 16), (0, 512)), requires=('volumetric_fog_range',), launcher='--fog-dust-motes'),
    entry('fog_motes_max_px', 'float', 'fog', 'Largest on-screen size of a dust speck, in pixels. 2 to 64 (raised to the speck size when below it).', '8', range=(2, 64),
          requires=('fog_dust_motes',), launcher='--fog-dust-motes'),
    entry('fog_docked', 'bool', 'fog', 'Keeps the fog visible while docked. 1 = on, 0 = off.', '1', requires=('volumetric_fog',),
          launcher='--fog-docked'),
    entry('fog_handover_step', 'bool', 'fog', 'Fades the fog in over a few frames after a sector change instead of popping. '
          '1 = on, 0 = off.', '1', requires=('volumetric_fog_range',), launcher='--fog-handover-step'),
    entry('fog_handover_coldfill', 'bool', 'fog', 'Fills the fog in right away on the first frames of a new sector. 1 = on, 0 = off.',
          '1', requires=('volumetric_fog_range',), launcher='--fog-handover-coldfill'),
    entry('fog_handover_prefill', 'bool', 'fog', 'Prepares the next sector\'s fog while the jump is still under way. 1 = on, 0 = off.',
          '1', requires=('volumetric_fog_range',), launcher='--fog-handover-prefill'),
    # ---------------------------------------------------------------- camera
    entry('camera', 'enum', 'camera', 'The external (back) view. chase = a smoothly following chase camera, vanilla = the game\'s own.',
          'chase', choices=('chase', 'vanilla'), launcher='--camera'),
    entry('fov', 'float', 'camera', 'Field of view in degrees, counted like X4: the horizontal angle on a 16:9 screen, 70 to 100 '
          '(the same on every screen shape). game = the game\'s own field of view.', '90', range=(70.0, 100.0), choices=('game',),
          launcher='--fov'),
    entry('chase_pitch_down_deg', 'float', 'camera', 'How far the chase camera looks down on your ship, in degrees. 0 to 30.', '0.5',
          range=(0.0, 30.0), requires=('camera',), launcher='--chase-pitch-down-deg'),
    entry('chase_offset_y', 'float', 'camera', 'Where your ship sits on screen in the chase view: 0 = centre, 0.5 = halfway down, '
          'negative = above centre. -1 to 1.', '0.5', range=(-1.0, 1.0), requires=('camera',), launcher='--chase-offset-y'),
    entry('chase_distance_scale', 'float', 'camera', 'Distance of the chase camera as a multiple of the game\'s. Larger = farther '
          'back. Above 0, up to 10.', '1.05', range=[R(0, 10, True)], requires=('camera',), launcher='--chase-distance-scale'),
    entry('chase_fov_compensate', 'bool', 'camera', 'Keeps your ship the same size on screen in the chase view whatever the field of '
          'view. 1 = on, 0 = off.', '1', requires=('camera',), launcher='--chase-fov-compensate'),
    entry('chase_hud_anchor', 'enum', 'camera', 'Where the aiming reticle sits in the chase view. forward = where the ship points, '
          'centre = the screen centre.', 'forward', choices=('forward', 'centre'), requires=('camera',), launcher='--chase-hud-anchor'),
    entry('chase_view_restore', 'bool', 'camera', 'Returns to the chase view after a gate jump or jumpdrive. 1 = on, 0 = off.', '1',
          requires=('camera',), launcher='--chase-view-restore'),
    entry('chase_scene_fix', 'bool', 'camera', 'Experimental: moves the cockpit-layer scene with the smoothed chase view. 1 = on, '
          '0 = off.', '0', requires=('camera',), launcher='--chase-scene-fix'),
    entry('sun_occlusion', 'bool', 'camera', 'The sun\'s flare fades gradually as a ship or station covers the sun, instead of '
          'switching off at once. 1 = on, 0 = the game\'s own.', '1', launcher='--sun-occlusion'),
    # ---------------------------------------------------------------- window
    entry('window_monitor_rect', 'bool', 'window', 'Places the borderless game window over the whole monitor (on a Mac: under the '
          'menu bar). 1 = on, 0 = the game\'s own placement.', '1', launcher='--window-monitor-rect'),
    entry('cursor_reassert', 'bool', 'window', 'Re-hides the system mouse cursor after switching back to the game, against a '
          'second cursor. 1 = on, 0 = off.', builtin='0', launcher='--cursor-reassert'),
    entry('pause_key_only', 'bool', 'window', 'Only the Pause key (or a click) ends the pause; other keys and switching windows '
          'do not. 1 = on, 0 = the game\'s own.', '1', launcher='--pause-key-only'),
    entry('pause_key', 'string', 'window', 'The game\'s key code of a rebound pause key (decimal or 0x hex). Empty = the Pause key.',
          builtin='', requires=('pause_key_only',), launcher='--pause-key'),
    # ---------------------------------------------------------------- audio
    entry('music_keep', 'bool', 'audio', 'Sector music keeps playing where it was across switching windows, saving and pausing, '
          'instead of restarting. 1 = on, 0 = off.', '1', launcher='--music-keep'),
    entry('voice_dmo_fallback', 'bool', 'audio', 'Makes speech play when the system lacks the game\'s speech decoder, by using the '
          'standard Windows one. Does nothing where speech already works. 1 = on, 0 = off.', '1', launcher='--voice-decoder'),
    # ---------------------------------------------------------------- loading
    entry('crypt_cache', 'bool', 'loading', 'Faster loading: reuses the game\'s file-signature checks instead of repeating them. '
          '1 = on, 0 = off.', '1', launcher='--crypt-cache'),
    entry('gz_buffer', 'bool', 'loading', 'Faster loading: reads compressed game files in large chunks. 1 = on, 0 = off.', '1',
          launcher='--gz-buffer'),
    entry('resource_read', 'enum', 'loading', 'Faster loading: how game data files are read. fast = the mod\'s reader, native = '
          'the game\'s own.', 'fast', choices=('fast', 'native', 'verify'), launcher='--resource-read'),
    entry('dat_handles', 'bool', 'loading', 'Faster loading: keeps the game\'s data archives open instead of reopening them. '
          '1 = on, 0 = off.', '1', launcher='--dat-handles'),
    entry('mesh_adjacency', 'enum', 'loading', 'Faster loading: how model outlines are prepared. fast = the mod\'s method, native = '
          'the game\'s own.', 'fast', choices=('fast', 'native', 'verify'), launcher='--mesh-adjacency'),
    # ---------------------------------------------------------------- engine
    entry('collide_sat_sse2', 'bool', 'engine', 'Faster collision checks with modern processor instructions. 1 = on, 0 = off.', '1',
          launcher='--collide-sat-sse2'),
    entry('collide_memo', 'bool', 'engine', 'Faster collision checks: remembers ship pairs that cannot touch. 1 = on, 0 = off.', '1',
          launcher='--collide-memo'),
    entry('collide_box_cull', 'bool', 'engine', 'Faster collision checks: a quick box test first. 1 = on, 0 = off.', '1',
          launcher='--collide-box-cull'),
    entry('lod_occlusion', 'enum', 'engine', 'Keeps stations\' shading correct on their simplified distant models. all = on, '
          'record0 = the game\'s own behaviour, off = none.', 'all', choices=('off', 'record0', 'all'), launcher='--lod-occlusion'),
    entry('terran_station_lod', 'enum', 'engine', 'How Terran stations pick their detail level. size = by their size on screen like '
          'every other station, distance = the game\'s own.', 'size', choices=('size', 'distance'), launcher='--terran-station-lod'),
    entry('sun_flare_fix', 'enum', 'engine', 'Keeps the sun flare visible near the screen centre on wide screens. on or off.', 'on',
          choices=('on', 'off'), launcher='--sun-flare-fix'),
    entry('light_map_far_fade', 'float_list', 'engine', 'Fades the lit hull windows on far-away ships and stations, where they would '
          'otherwise shimmer. The first two numbers say how far: how many game units one screen pixel covers at the object where the '
          'fade starts and where it ends (larger = farther away); the third is the brightness left beyond that (up to '
          'hull_lightmap_gain; equal to it = no fade).', '80,220,1',
          counts=(2, 3), elements=([R(0, 1e6, True)], [R(0, 1e6, True)], (0, 8)),
          requires=('hull_lightmap_gain',), launcher='--light-map-far-fade'),
    entry('point_light_root_admission', 'bool', 'engine', 'Experimental: lights up small station parts by a light near the whole '
          'station. May leave some modules darker. 1 = on, 0 = off.', builtin='0', launcher='--point-light-root-admission'),
    # ---------------------------------------------------------------- logging
    entry('debug', 'bool', 'logging', 'Detailed diagnostic log for a bug report: enable, reproduce the problem, send x3m.log. About '
          '8 KB of log per frame (about 1.7 GB per hour at 60 fps), so turn it off again afterwards. 1 = on, 0 = off. F8 then records a '
          'short capture.', builtin='0', launcher='--debug'),
    entry('perf', 'bool', 'logging', 'Performance log and an on-screen frame rate counter (about 0.36 GB of log per hour at 60 fps). '
          '1 = on, 0 = off.', builtin='0',
          launcher='--perf'),
    entry('capture_frames', 'int', 'logging', 'Frames recorded by one F8 capture (with debug = 1). 0 to 64 (more counts as 64).', '8',
          range=(0, 4294967295),
          requires=('debug',), launcher='--capture-frames'),
    entry('log_file', 'path', 'logging', 'Writes the log to this file instead of x3m.log next to the mod. Empty = the default.',
          builtin=''),

    # ================================================================ developer: parseable, not in the template
    # Route prerequisites (on in every default launch; off breaks the features above).
    dev('ownership', 'bool', 'engine', 'The D3D9 ownership wrapper: prerequisite of the TAA history, the thin vote, the sun lane and '
        'the shadows.', '1', launcher='--ownership'),
    dev('object_trace', 'bool', 'engine', 'Verified submission identity (prerequisite of the TAA history).', '1', launcher='--object-trace'),
    dev('object_lifetime', 'bool', 'engine', 'Render-registry lifetimes (prerequisite of the TAA history).', '1',
        launcher='--object-lifetime'),
    dev('motion_output', 'bool', 'engine', 'The same-draw motion route: prerequisite of TAA, HDR and the shadows.', '1',
        launcher='--motion-output'),
    dev('scene_hook', 'enum', 'engine', 'Engine scene-end hook at the compositing call; 1 with the route.', '1', choices=('1', '0'),
        launcher='--scene-hook'),
    dev('motion_rt_mode', 'enum', 'engine', 'RT1/RT2 binding policy of the route.', 'lazy', choices=('lazy', 'perdraw'),
        launcher='--motion-rt-mode'),
    dev('state_shadow', 'string', 'engine', 'Render-state configuration of the route: 1, 0, else auto (unset).', launcher='--state-shadow'),
    dev('shadow_replay_candidates', 'bool', 'shadows', 'Caster-candidate counter and the lock bookends of the replay.', '1',
        launcher='--shadow-replay-candidates'),
    dev('depth_copy', 'bool', 'engine', 'Ownership auto-depth copy (the ownership-integration suite).'),
    dev('finite_positions', 'bool', 'engine', 'Finite-position capture (the ownership-integration suite).'),
    dev('admission', 'bool', 'engine', 'Publishes the process admission monitor (the ownership runners).'),
    dev('scene_depth_capture', 'bool', 'engine', 'Scene depth capture diagnostic.'),
    dev('motion_capture', 'bool', 'engine', 'Private rigid-motion capture diagnostic (always refused).'),
    # TAA tuning behind the promoted defaults.
    dev('taa_box_resolution', 'enum', 'graphics', 'Resolution of the thin region\'s camera-gate box.', 'half', choices=('half', 'full'),
        launcher='--taa-box-resolution'),
    dev('taa_far_gate', 'enum', 'graphics', 'Motion gate of the far stabiliser weight.', 'camera', choices=('camera', 'screen'),
        launcher='--taa-far-gate'),
    dev('taa_far_clip', 'enum', 'graphics', 'History clip of far pixels outside the thin region.', '7x7', choices=('7x7', '3x3'),
        launcher='--taa-far-clip'),
    dev('taa_thin_vote', 'enum', 'graphics', 'Draw-time triangle-height vote flagging thin geometry into the thin region.', 'on',
        choices=('on', 'off'), launcher='--taa-thin-vote'),
    dev('fade_rt2_owner', 'enum', 'graphics', 'Fade-band draws write their depth into RT2.', 'on', choices=('on', 'off'),
        launcher='--fade-rt2-owner'),
    dev('taa_unmatched_static', 'string', 'graphics', 'A routed draw with a new motion key reprojects as static for one frame: off, '
        'node or all.', 'node', launcher='--taa-unmatched-static'),
    dev('taa_sky_history', 'string', 'graphics', 'Sky pixels accept sentinel history only (strict) or any (loose).', 'strict',
        launcher='--taa-sky-history'),
    dev('taa_sky_history_band_px', 'float', 'graphics', 'Band threshold of the strict sky history (DLL default 3).',
        launcher='--taa-sky-history-band-px', range=(1, 16)),
    dev('taa_sky_history_exit_px', 'float', 'graphics', 'Exit reset of the strict sky history.', '0.25',
        launcher='--taa-sky-history-exit-px', range=[(0, 0), (0.125, 16)]),
    dev('taa_motion_weight', 'string', 'graphics', 'History-weight cap of fast-parallax pixels: F[,V0,V1] or 0.', '0.7,2,8',
        launcher='--taa-motion-weight'),
    dev('camera_cut_deg', 'float', 'graphics', 'Camera rotation per frame above which the resolve declares a cut.', '20.0',
        launcher='--camera-cut-deg', range=[R(0, 180, True)]),
    dev('motion_cut_median_px', 'float', 'graphics', 'Global cut heuristic: median displacement bound (1e30 = disabled).', '1e30'),
    dev('motion_cut_missing', 'float', 'graphics', 'Global cut heuristic: missing-key fraction bound (1 = disabled).', '1'),
    dev('motion_jitter_samples', 'int', 'graphics', 'Halton jitter sample count, 2..64 (DLL default 8).', range=(2, 64)),
    dev('taa_debug', 'int', 'logging', 'Raw resolve and age dumps on F8 frames (~40 MB per frame).', launcher='--taa-debug', range=(0, 4294967295)),
    # HDR internals.
    dev('hdr_clamp', 'float', 'hdr', 'Scene clamp of the FP16 target (0 = none).', '0.0', launcher='--hdr-clamp', range=[R(0, 65504, True)]),
    dev('hdr_decode', 'string', 'hdr', 'Decode of the game\'s colours into linear light: gamma2.2, srgb or none.', 'gamma2.2',
        launcher='--hdr-decode'),
    dev('hdr_ev_deadband', 'float', 'hdr', 'Exposure meter dead band in stops.', '0.25', launcher='--hdr-ev-deadband', range=(0, 8)),
    dev('hdr_key_pull', 'float', 'hdr', 'Exposure meter key pull.', '0.25', launcher='--hdr-key-pull', range=(0, 1)),
    dev('hdr_meter_bg', 'float', 'hdr', 'Exposure meter background level.', '0.001953125', launcher='--hdr-meter-bg', range=(1e-4, 64)),
    dev('hdr_meter_edge_weight', 'float', 'hdr', 'Exposure meter edge weight.', '0.35', launcher='--hdr-edge-weight', range=(0, 1)),
    dev('hdr_meter_min_lit', 'float', 'hdr', 'Lit fraction below which the exposure target is neutral (DLL default 0.01).', range=(0, 1)),
    dev('hdr_white_target', 'float', 'hdr', 'Exposure meter white target.', '0.9', launcher='--hdr-white-target', range=(0, 4)),
    dev('hdr_dt_ms', 'float', 'hdr', 'Fixed frame time of the exposure meter (fixtures; 0 = live).', range=[R(0, 1000, True)]),
    dev('hull_emission_gain', 'float', 'hdr', 'Hull guide-light gain; the launcher sends the effects gain.', '2.0',
        launcher='--emission-source-gain', range=(1, 8)),
    dev('hull_emissive_widening', 'string', 'hdr', 'Light-map fetch widening K[,B] for thin emitters, or off.', '4,4',
        launcher='--hull-emissive-widening'),
    dev('screen_emission_additive_alpha', 'float', 'hdr', 'Bloom alpha of the additive bullets.', '0.0',
        launcher='--screen-emission-additive-alpha', range=(0, 1)),
    # Removed options whose DLL reads stay as fixture seams (inventory section 5).
    dev('linear_materials', 'bool', 'hdr', 'Converted-material route (fixtures only since 2026-09-25).'),
    dev('material_fill', 'float', 'hdr', 'Converted-material fill (fixtures only).', range=(0, 0.5)),
    dev('material_direct_gain', 'float', 'hdr', 'Converted-material direct gain (fixtures only).', range=(0, 16)),
    dev('material_emissive_gain', 'float', 'hdr', 'Converted-material emissive gain (fixtures only).', range=(0, 16)),
    dev('lightmap_emissive_gain', 'float', 'hdr', 'Converted-material light-map gain (fixtures only).', range=(0, 16)),
    dev('linear_distance_fade', 'bool', 'hdr', 'Converted-material distance fade (fixtures only).'),
    dev('linear_emissions', 'bool', 'hdr', 'Linear emission composition (fixtures only).'),
    dev('emission_gain', 'float', 'hdr', 'Linear emission gain (fixtures only).', range=(0, 16)),
    dev('screen_emission', 'bool', 'hdr', 'Packed screen-emission route (fixtures only).'),
    dev('screen_emission_bound', 'bool', 'hdr', 'Locked-prefix bound of the screen-emission route (fixtures only).'),
    dev('screen_emission_gain', 'float', 'hdr', 'Screen-emission gain (fixtures only).', range=(0.5, 8)),
    dev('screen_emission_timing', 'bool', 'hdr', 'Screen-emission timing rows (fixtures only).'),
    dev('fade_witness', 'string', 'hdr', 'Fade witness digits (fixtures only).'),
    dev('fade_route', 'string', 'graphics', 'Fade-band motion arm threshold in permille (DLL default 500).'),
    dev('shadow_replay_size', 'string', 'shadows', 'Removed single map (2026-09-25): refused with a row when set.'),
    dev('shadow_replay_extent', 'string', 'shadows', 'Removed single map (2026-09-25): refused with a row when set.'),
    dev('shadow_replay_depth_half', 'string', 'shadows', 'Removed single map (2026-09-25): refused with a row when set.'),
    dev('shadow_replay_cap', 'string', 'shadows', 'Removed single map (2026-09-25): refused with a row when set.'),
    # Shadow tuning knobs behind the promoted defaults.
    dev('shadow_sun_poll', 'bool', 'shadows', 'Sun direction from the engine\'s brightest directional light node (DLL default on).', '1',
        launcher='--shadow-sun-poll'),
    dev('shadow_cascade_min_footprint', 'float', 'shadows', 'Per-part minimum light-space footprint of the cascades (0 = off).',
        '8.0', launcher='--shadow-cascade-min-footprint', range=(-1e300, 64)),
    dev('shadow_cascade_backface_from', 'string', 'shadows', 'First cascade replaying back faces, or none (DLL default by texel).',
        launcher='--shadow-cascade-backface-from'),
    dev('shadow_cascade_caps', 'int_list', 'shadows', 'Per-cascade draw caps, 1..4096 (one number for every cascade).',
        range=(1, 4096), counts=(1, 2, 3, 4, 5), launcher='--shadow-cascade-caps'),
    dev('shadow_cascade_budget', 'int', 'shadows', 'Replay budget of the cascades.', launcher='--shadow-cascade-budget', range=(1, 4096)),
    dev('shadow_cascade_ladder_ratio', 'float', 'shadows', 'Ratio of the sliding ladder behind the adaptive cascade 0.',
        launcher='--shadow-cascade-ladder-ratio', range=(2, 16)),
    dev('shadow_cascade_large_min', 'float', 'shadows', 'Minimum size of a large caster.', launcher='--shadow-cascade-large-min', range=(0, 1e6)),
    dev('shadow_cascade_static_from', 'int', 'shadows', 'First static-only cascade.', launcher='--shadow-cascade-static-from', range=(1, 4)),
    dev('shadow_caster_retention_age', 'int', 'shadows', 'Frames a retained caster is kept.', launcher='--shadow-caster-retention-age', range=(1, 10000000)),
    dev('shadow_caster_retention_eps', 'float', 'shadows', 'Retention match tolerance.', launcher='--shadow-caster-retention-eps', range=(1e-4, 100)),
    # Fog, camera, audio, window, engine internals.
    dev('fog_families', 'path', 'fog', 'Fog family file override (0 or none = disabled; default <game>\\x3m\\fog-families.bin).'),
    dev('volumetric_fog_timing', 'bool', 'logging', 'Fog cost rows (member of the perf group).'),
    dev('chase_combat_tightness', 'float', 'camera', 'Chase combat tightening (unverified in game).', '0.0',
        launcher='--chase-combat-tightness', range=(0, 1)),
    dev('chase_rot_tau', 'float', 'camera', 'Chase rotation spring time constant (DLL compiled default).', launcher='--chase-rot-tau', range=[R(0, 10, True)]),
    dev('chase_pos_tau', 'float', 'camera', 'Chase position spring time constant (DLL compiled default).', launcher='--chase-pos-tau', range=[R(0, 10, True)]),
    dev('chase_lag_clamp_deg', 'float', 'camera', 'Chase rotation lag clamp (DLL compiled default).', launcher='--chase-lag-clamp-deg', range=(0, 90)),
    dev('chase_pos_lag_clamp', 'float', 'camera', 'Chase position lag clamp (DLL compiled default).', launcher='--chase-pos-lag-clamp', range=(0, 1)),
    dev('sun_occlusion_core_f', 'bool', 'camera', 'Core dimming of the sun occlusion (only 0 restores clip-only).', '1',
        launcher='--sun-occlusion-core-f'),
    dev('sun_occlusion_radius', 'float', 'camera', 'Sun occlusion probe radius.', launcher='--sun-occlusion-radius', range=(0.005, 0.25)),
    dev('sun_occlusion_curve', 'float', 'camera', 'Sun occlusion response curve.', launcher='--sun-occlusion-curve', range=(0.25, 4)),
    dev('media_cue_cache', 'bool', 'audio', 'Negative cache of the sector selector\'s media cue restart.', '1', launcher='--media-cue-cache'),
    dev('media_cue_retry_s', 'int', 'audio', 'Seconds before a cached media-cue failure is retried, 1..3600.', '30',
        launcher='--media-cue-retry-s', range=(1, 3600)),
    dev('gz_buffer_kb', 'int', 'loading', 'Chunk size of the gz read-ahead buffer in KB.', '256', launcher='--gz-buffer-kb', range=(0, 4294967295)),
    dev('collide_memo_verify', 'bool', 'engine', 'Every memo answer re-run in the engine and compared (fixtures).'),
    dev('capture_start', 'int', 'logging', 'Present count of an automatic capture burst; 999999 = never (the launcher\'s value).',
        '999999', range=(0, 4294967295)),
    # Logging internals: the groups' individual switches, cadences and developer options.
    dev('draw_trace', 'bool', 'logging', 'The heavy per-draw attribution (needs debug or perf).', launcher='--draw-trace'),
    dev('telemetry', 'bool', 'logging', 'Telemetry counters and summaries (member of both groups).'),
    dev('telemetry_draw', 'bool', 'logging', 'Per-draw route cost fields (member of draw_trace).'),
    dev('frame_timing', 'bool', 'logging', 'Frame-time windows (member of perf).'),
    dev('frame_timing_state_stamps', 'int', 'logging', 'Stamp every Nth hooked state call (fixtures).', range=(0, 999999)),
    dev('frame_phases', 'bool', 'logging', 'Frame-phase stamps (member of debug and perf).'),
    dev('frame_end_stride', 'int', 'logging', 'Frames between frame_end rows, 1..100000 (an explicit value wins over the groups).', range=(1, 100000)),
    dev('fps_overlay', 'bool', 'logging', 'On-screen frame-rate line (member of perf).'),
    dev('game_phases', 'bool', 'logging', 'Engine segment stamps (member of draw_trace).'),
    dev('game_phase_threshold_ms', 'int', 'logging', 'Segment-tape threshold (DLL default 20 ms).', range=(1, 10000)),
    dev('pass_phases', 'bool', 'logging', 'Pass-phase stamps (member of draw_trace).'),
    dev('residual_phases', 'bool', 'logging', 'Residual-phase stamps (member of draw_trace).'),
    dev('light_phases', 'bool', 'logging', 'Light-phase stamps (member of draw_trace).'),
    dev('loop_phases', 'bool', 'logging', 'Loop-phase stamps (member of draw_trace).'),
    dev('submit_phases', 'bool', 'logging', 'View-submit candidate stamps (fixtures; in no group).'),
    dev('collide_query_phases', 'bool', 'logging', 'Collision query phases (member of debug).'),
    dev('collide_narrow_census', 'bool', 'logging', 'Narrow-phase collision census (member of debug).'),
    dev('cull_census', 'bool', 'logging', 'Cull census on F8 frames (member of debug).'),
    dev('lod_switch_log', 'int', 'logging', 'LOD switch rows per census (member of debug).', range=(0, 4096)),
    dev('object_bounds_log', 'bool', 'logging', 'Object bounds on F8 frames (member of debug).'),
    dev('motion_frame_log', 'int', 'logging', 'motion_output_frame cadence, 1..100000 (DLL default 60, 1 with debug).', range=(1, 100000)),
    dev('camera_log', 'int', 'logging', 'camera_state cadence (member of debug).', range=(1, 1000000)),
    dev('shadow_rows', 'bool', 'logging', 'The shadow and sun state rows every frame (member of debug).'),
    dev('shadow_timing', 'bool', 'logging', 'The two shadow cost rows every frame (member of perf).'),
    dev('shadow_retention_census', 'bool', 'logging', 'Caster retention census (member of debug).'),
    dev('shadow_retention_timing', 'bool', 'logging', 'Caster retention timing (member of debug).'),
    dev('shadow_sun_trace', 'bool', 'logging', 'Sun poll trace (member of debug).'),
    dev('sector_background', 'bool', 'logging', 'Sector background trace (member of debug).'),
    dev('loading_probes', 'bool', 'logging', 'Loading probes (member of debug).'),
    dev('media_cue_trace', 'bool', 'logging', 'Media cue trace (member of debug).'),
    dev('music_trace', 'bool', 'logging', 'Music keep trace (member of debug).'),
    dev('window_trace', 'bool', 'logging', 'Window trace (member of debug).'),
    dev('locked_prefix_log', 'bool', 'logging', 'Per-draw locked_prefix row every frame (run_locked_prefix_live.py).'),
    dev('gpu_sync_timing', 'bool', 'logging', 'Serialised GPU cost per proxy pass; halves the frame rate.', launcher='--gpu-sync-timing'),
    dev('profile', 'bool', 'logging', 'Sampling profiler thread.', launcher='--profile'),
    dev('profile_interval_us', 'int', 'logging', 'Sampling profiler interval (DLL default 2000 us).', launcher='--profile-interval-us', range=(100, 1000000)),
    dev('profile_report_s', 'int', 'logging', 'Sampling profiler report period (DLL default 5 s).', range=(1, 3600)),
    dev('sun_occlusion_log', 'bool', 'logging', 'Sun occlusion readback row per frame and lens dumps on F8.', launcher='--sun-occlusion-log'),
    # The launcher's default markers (env only; resolved from the source of the key they mark).
    marker('fade_rt2_owner_default', 'fade_rt2_owner', '--fade-rt2-owner'),
    marker('lod_occlusion_default', 'lod_occlusion', '--lod-occlusion'),
    marker('original_fill_default', 'original_fill', '--original-fill'),
    marker('sun_occlusion_default', 'sun_occlusion', '--sun-occlusion'),
    marker('taa_box_resolution_default', 'taa_box_resolution', '--taa-box-resolution'),
    marker('taa_far_clip_default', 'taa_far_clip', '--taa-far-clip'),
    marker('taa_far_gate_default', 'taa_far_gate', '--taa-far-gate'),
    marker('taa_thin_vote_default', 'taa_thin_vote', '--taa-thin-vote'),
    marker('window_monitor_rect_default', 'window_monitor_rect', '--window-monitor-rect'),
    # The resolver's own control: environment only.
    entry('config', 'path', 'logging', 'Which configuration file: unset = x3m.ini next to the DLL, none = no file, bare = no file '
          'and no defaults (the fixtures), else that file.', developer=True, env_only=True, launcher='--config'),
    # Fixture seams: environment only (inventory section 5).
    seam('fixture_exception', 'The seam raises an unhandled access violation after log_open.'),
    seam('fixture_log_bench', 'The seam logs N benchmark rows.'),
    seam('fixture_taa_k', 'The k0 identity case of the TAA resolve.'),
    seam('fixture_taa_sentinel', 'The sentinel policy twins of the TAA resolve.'),
    seam('fixture_fade_rect', 'Seam fade rectangle.'),
    seam('fixture_motion_depth', 'Seam motion depth off switch.'),
    seam('fixture_quad_fvf', 'Quad vertex program FVF switch (X3M_QUAD_FVF_SWITCH builds).'),
    seam('fixture_screen_caps_fault', 'Seam screen caps fault.'),
    seam('fixture_screen_rect', 'Seam screen rectangle.'),
    seam('fixture_shadow_cascades', 'Seam cascade extents.'),
    seam('fixture_slice_near', 'Seam slice near plane.'),
    seam('fixture_stretch_fault', 'Seam stretch fault.'),
    seam('fixture_sun_lane_fault', 'Seam sun lane faults.'),
    seam('fixture_taa_filter_fault', 'Seam TAA filter fault.'),
]

# Sent by tools/manage.py on a default launch but not a DLL setting (the file selection itself).
LAUNCHER_ONLY = {'X3M_CONFIG': 'bare'}

BY_KEY = {e['key']: e for e in SETTINGS}
BY_ENV = {e['env']: e for e in SETTINGS}


def user_facing():
    return [e for e in SETTINGS if not e['developer']]


def natural(text):
    """A number in its natural spelling: 4.0000 -> 4, 250.0 -> 250, 0.0 -> 0 (every element of a list); anything else as is."""
    def one(part):
        if re.fullmatch(r'-?[0-9]+\.[0-9]*', part):
            part = part.rstrip('0').rstrip('.')
            return '0' if part in ('', '-', '-0') else part
        return part
    return ','.join(one(part) for part in text.split(','))


def shown_value(e):
    """The value the template shows: the default, else the site's built-in value; a float or float list in its natural
    spelling (the launcher sends 4.0000 and 250.0,...; every float site parses 4 and 250 the same)."""
    value = e['default'] if e['default'] is not None else (e['builtin'] or '')
    return natural(value) if e['type'] in ('float', 'float_list') else value
