#pragma once
#include "engine_patch.h"

// Mid-function game markers, in recorder ID order. Exact instruction boundaries,
// incoming edges and relocated targets are independently checked by
// verification/probe/verify_game_phase_sites.py. See the owning native study:
// docs/reverse-engineering/selection-frame-phases.md. No backend layout is used.
namespace x3m::game_phases::sites {
enum Index : unsigned {
    LoopSetup = 0,
    Clock = 1,
    Pump = 2,
    Channels = 3,
    PendingVm = 4,
    Services = 5,
    Input = 6,
    Simulation = 7,
    Cockpits = 8,
    Render = 9,
    PostRender = 10,
    Detail = 11,
    Presentation = 12,
    Tail = 13,
    Exit = 14,
    DelayedBegin = 15,
    DelayedEnd = 16,
    AcquisitionBegin = 17,
    AcquisitionEnd = 18,
    ColdBegin = 19,
    ColdEnd = 20,
    PresentBegin = 21,
    PresentEnd = 22,
    InputBody = 23,
    InputAfter = 24,
    PublisherBegin = 25,
    PublisherEnd = 26,
    PlaybackBegin = 27,
    PlaybackEnd = 28,
    CreateBegin = 29,
    CreateEnd = 30,
    SeekBegin = 31,
    SeekEnd = 32,
    // Audio-path witnesses (X3M_AUDIO_SITES=1, load hang runs 29-35), after
    // the phase markers so the phase group installs unchanged without them.
    // docs/reverse-engineering/voice-startup-sequence.md, voice-cue-timing.md.
    AudioSetStateAfter = 33,  // return of IMultiMediaStream::SetState(RUN) at 4d03f5; EAX = HRESULT
    AudioPauseAfter = 34,     // return of IMediaControl::Pause at 4d0407; EAX = HRESULT
    AudioPumpEntry = 35,      // 4d34b0 Win32 message pump entry
    AudioPumpBody = 36,       // 4d3532 drain loop body (one hit per iteration)
    AudioManagerPreloop1 = 37,// 403a7f call 498370 (manager update); 403b04 is Services
    AudioManagerPreloop2 = 38,// 403a98
    AudioManagerInit = 39,    // 49729b
    AudioManagerBody = 40,    // 486809
    AudioManagerAsset = 41,   // 492dbe
    AudioRefillEntry = 42,    // 4d0700 per-media refill state machine entry
    AudioPollCall = 43,       // 4d0762 CompletionStatus(0,0) call setup (state 2)
    AudioPollAfter = 44,      // 4d0774 first instruction after the poll's join; EAX = HRESULT when paired with 43
    AudioUpdateAfter = 45,    // return of IStreamSample::Update(ASYNC) at 4d0a61; EAX = HRESULT
    AudioCuePlay = 46,        // 498e30 cue play entry
    Count = 47,
    PhaseCount = 33           // the phase group; audio sites follow
};
constexpr unsigned kSiteCount = Count;
// Native CALL/stack effects are replayed. Publisher entry/epilogue describe
// RET4 metadata; the marker never emits an extra function return.
constexpr engine_patch::SiteSpec kSites[Count] = {
    {"game_phase_loop_setup",0x00403ab0,{0x81,0xa0,0x08,0x01,0x00,0x00,0xff,0xbf,0xff,0xff},10,0,0},
    {"game_phase_clock",0x00403af0,{0xe8,0x0b,0xd3,0x0a,0x00},5,0,1},
    {"game_phase_pump",0x00403af5,{0xe8,0xb6,0xf9,0x0c,0x00},5,0,1},
    {"game_phase_channels",0x00403afa,{0xe8,0x31,0x66,0x09,0x00},5,0,1},
    {"game_phase_pending_vm",0x00403aff,{0xe8,0x6c,0xbc,0x09,0x00},5,0,1},
    {"game_phase_services",0x00403b04,{0xe8,0x67,0x48,0x09,0x00},5,0,1},
    {"game_phase_input",0x00403b09,{0xf6,0x86,0xa0,0x04,0x00,0x00,0x01},7,0,0},
    {"game_phase_simulation",0x00403f2a,{0xe8,0x21,0x28,0x01,0x00},5,0,1},
    {"game_phase_cockpits",0x00403f2f,{0xe8,0xac,0x8e,0x01,0x00},5,0,1},
    {"game_phase_render",0x00403f34,{0xe8,0x17,0xe0,0x06,0x00},5,0,1},
    {"game_phase_post_render",0x00403f39,{0x39,0x1d,0x50,0x7c,0x60,0x00},6,0,0},
    {"game_phase_detail",0x00403f5a,{0xe8,0x21,0x30,0x09,0x00},5,0,1},
    {"game_phase_presentation",0x00403f5f,{0x39,0x1d,0x50,0x7c,0x60,0x00},6,0,0},
    {"game_phase_tail",0x00403f9e,{0x8b,0x15,0x60,0xfc,0x57,0x00},6,0,0},
    {"game_phase_exit",0x004041eb,{0x8b,0x86,0xdc,0x04,0x00,0x00},6,0,0},
    {"game_phase_delayed_begin",0x0042a45d,{0xe8,0xae,0xb5,0xff,0xff},5,0,1},
    {"game_phase_delayed_end",0x0042a462,{0xe9,0x6f,0x01,0x00,0x00},5,0,1},
    {"game_phase_acquisition_begin",0x00425bac,{0x6a,0xff,0x6a,0x01,0x55},5,0,0},
    {"game_phase_acquisition_end",0x00425bed,{0x8b,0x15,0x34,0x6f,0x60,0x00},6,0,0},
    {"game_phase_cold_begin",0x0049a3e4,{0xe8,0x87,0x9a,0x05,0x00},5,0,1},
    {"game_phase_cold_end",0x0049a3e9,{0x83,0xc4,0x08,0x85,0xc0},5,0,0},
    {"game_phase_present_begin",0x004dac45,{0x8b,0x42,0x44,0xff,0xd0},5,0,0},
    {"game_phase_present_end",0x004dac4a,{0x3d,0x68,0x08,0x76,0x88},5,0,0},
    {"game_phase_input_body",0x00403b3a,{0x39,0xae,0xd8,0x04,0x00,0x00},6,0,0},
    {"game_phase_input_after",0x00403dc5,{0xf6,0x86,0xa0,0x04,0x00,0x00,0x04},7,0,0},
    {"game_phase_publisher_begin",0x00425a10,{0x53,0x8b,0x5c,0x24,0x08},5,4,0},
    {"game_phase_publisher_end",0x00425c79,{0x5f,0x5e,0x5d,0x5b,0xc2,0x04,0x00},7,4,0},
    {"game_phase_playback_begin",0x00499849,{0xe8,0xe2,0xf5,0xff,0xff},5,0,1},
    {"game_phase_playback_end",0x0049984e,{0x83,0xc4,0x18,0x5f,0xb8,0x01,0x00,0x00,0x00},9,0,0},
    {"game_phase_create_begin",0x00498ef8,{0xe8,0x43,0xf2,0xff,0xff},5,0,1},
    {"game_phase_create_end",0x00498f00,{0x83,0xef,0x01,0x66,0x85,0xff},6,0,0},
    {"game_phase_seek_begin",0x00498f55,{0xe8,0xd6,0x74,0x03,0x00},5,0,1},
    {"game_phase_seek_end",0x00498f5a,{0x83,0xc4,0x04,0x85,0xc0},5,0,0},
    {"game_phase_audio_setstate_after",0x004d03f7,{0x85,0xc0,0x0f,0x8c,0x61,0xfd,0xff,0xff},8,0,4},
    {"game_phase_audio_pause_after",0x004d0409,{0x85,0xc0,0x0f,0x8c,0x4f,0xfd,0xff,0xff},8,0,4},
    {"game_phase_audio_pump_entry",0x004d34b0,{0x83,0xec,0x24,0x53,0x55},5,0,0},
    {"game_phase_audio_pump_body",0x004d3532,{0x6a,0x00,0x6a,0x00,0x6a,0x00},6,0,0},
    {"game_phase_audio_manager_preloop1",0x00403a7f,{0xe8,0xec,0x48,0x09,0x00},5,0,1},
    {"game_phase_audio_manager_preloop2",0x00403a98,{0xe8,0xd3,0x48,0x09,0x00},5,0,1},
    {"game_phase_audio_manager_init",0x0049729b,{0xe8,0xd0,0x10,0x00,0x00},5,0,1},
    {"game_phase_audio_manager_body",0x00486809,{0xe8,0x62,0x1b,0x01,0x00},5,0,1},
    {"game_phase_audio_manager_asset",0x00492dbe,{0xe8,0xad,0x55,0x00,0x00},5,0,1},
    {"game_phase_audio_refill_entry",0x004d0700,{0x83,0xec,0x44,0x53,0x55},5,0,0},
    {"game_phase_audio_poll_call",0x004d0762,{0x6a,0x00,0x6a,0x00,0x50,0xff,0xd2},7,0,0},
    {"game_phase_audio_poll_after",0x004d0774,{0x83,0x7e,0x64,0x04,0x0f,0x85,0xb6,0x02,0x00,0x00},10,0,6},
    {"game_phase_audio_update_after",0x004d0a63,{0x8b,0xf8,0x81,0xff,0x0e,0x00,0x07,0x80},8,0,0},
    {"game_phase_audio_cue_play",0x00498e30,{0x51,0x8b,0x44,0x24,0x1c},5,0,0},
};
static_assert(sizeof(kSites)/sizeof(kSites[0]) == Count, "All phase markers are one group");
}
