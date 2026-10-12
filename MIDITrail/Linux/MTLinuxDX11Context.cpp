#include "MTLinuxDX11ContextImpl.h"

#include "../resource.h"
#include "../DXColorUtil.h"
#include "../MTConfFile.h"
#include "../MIDITrailVersion.h"
#include "../../SMIDILib/SMFileReader.h"
#include "../../SMIDILib/SMSimpleList.h"
#include "../../SMIDILib/SMBarList.h"
#include "../../SMIDILib/SMNoteList.h"
#include "../../SMIDILib/SMTrack.h"
#include "../../SMIDILib/SMEvent.h"
#include "../../YNBaseLib/YNErrCtrl.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace SMIDILib;

namespace miditrail::linux_native {

namespace {

constexpr int kMultiSampleMax = 16;

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool is_midi_file_name(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    return strcasecmp(name.c_str() + dot, ".mid") == 0 || strcasecmp(name.c_str() + dot, ".midi") == 0;
}

// throttled loading-screen progress for the SMF parser / note-field build
struct LoadProgress {
    DXRenderer11* renderer = nullptr;
    const char* label = "";
    float base = 0.0f;
    float span = 0.0f;
    std::uint64_t last_ticks = 0;
};

void parse_progress(unsigned long current, unsigned long total, void* user) {
    auto* progress = static_cast<LoadProgress*>(user);
    const std::uint64_t now = SDL_GetTicks();
    if (now - progress->last_ticks < 50 && current + 1 < total) return;
    progress->last_ticks = now;
    char message[128];
    std::snprintf(message, sizeof(message), "%s  %lu / %lu", progress->label, current + 1, total);
    const float ratio = total > 0 ? static_cast<float>(current + 1) / static_cast<float>(total) : 1.0f;
    progress->renderer->DrawLoadingScreen(message, progress->base + progress->span * ratio);
}

} // namespace

//******************************************************************************
// construction / conf files
//******************************************************************************
LinuxDX11Context::Impl::Impl() {
    const char* home = std::getenv("HOME");
    user_conf_dir = std::string(home ? home : ".") + "/.config/MIDITrail/yknk/MIDITrail/";
    std::error_code error;
    std::filesystem::create_directories(user_conf_dir, error);
}

LinuxDX11Context::Impl::~Impl() {
    stop_worker();
    midi_in.close();
    midi_out.close_all();
    if (midi_decoder != nullptr) snd_midi_event_free(midi_decoder);
    if (play_status != PlayStatus::NoData && auto_save_viewpoint) save_viewpoint();
    save_view_conf();
    release_howto_textures();
    detach_scene();
    renderer.SetConfigManager11(nullptr);
    renderer.SetImGuiDrawCallback(nullptr, nullptr);
    note_box.Release(); note_rain.Release(); note_ring.Release();
    note_box_live.Release(); note_rain_live.Release();
    keyboard.Release(); keyboard_rain.Release(); grid_box.Release(); grid_ring.Release();
    stars.Release(); time_indicator.Release(); time_indicator_ring.Release();
    ripple.Release(); pict_board_ring.Release(); lyrics.Release();
    dashboard.Release(); background_image.Release();
    renderer.Terminate();
}

std::string LinuxDX11Context::Impl::conf_path(const char* name) const {
    return user_conf_dir + name + ".ini";
}

const char* LinuxDX11Context::Impl::scene_name_of(SceneType type) {
    switch (type) {
    case SceneType::PianoRoll2D: return "PianoRoll2D";
    case SceneType::PianoRollRain: return "PianoRollRain";
    case SceneType::PianoRollRain2D: return "PianoRollRain2D";
    case SceneType::PianoRollRing: return "PianoRollRing";
    case SceneType::PianoRoll3D: default: return "PianoRoll3D";
    }
}

const char* LinuxDX11Context::Impl::scene_name() const {
    return scene_name_of(scene_type);
}

void LinuxDX11Context::Impl::load_view_conf() {
    view_conf.Initialize(conf_path("View").c_str());
    view_conf.SetCurSection("Scene");
    char type[64] = {};
    view_conf.GetStr("Type", type, sizeof(type), "");
    scene_type = SceneType::PianoRoll3D;
    for (SceneType candidate : {SceneType::PianoRoll3D, SceneType::PianoRoll2D, SceneType::PianoRollRain,
                                SceneType::PianoRollRain2D, SceneType::PianoRollRing}) {
        if (std::strcmp(type, scene_name_of(candidate)) == 0) scene_type = candidate;
    }
    int value = 0;
    view_conf.GetInt("AutoSaveViewpoint", &value, 1); auto_save_viewpoint = value == 1;
    view_conf.GetInt("AutoSaveViewSettings", &value, 0); auto_save_view_settings = value == 1;
    if (auto_save_view_settings) {
        auto get = [this](const char* key, bool* flag) {
            int v = *flag ? 1 : 0;
            view_conf.GetInt(key, &v, v);
            *flag = v == 1;
        };
        get("EnablePianoKeyboard", &keyboard_visible);
        get("EnableRipple", &ripple_visible);
        get("EnableLyrics", &lyrics_visible);
        get("EnablePitchBend", &pitch_bend_enabled);
        get("EnablePitchBendAllNotes", &pitch_bend_all_notes);
        get("EnableStars", &stars_visible);
        get("EnableCounter", &dashboard_visible);
        get("EnableBackgroundImage", &background_image_visible);
        get("EnableTimeIndicator", &time_indicator_visible);
        get("EnableGridBox", &grid_visible);
        get("SingleKeyboard", &single_keyboard);
    }
    load_window_size_conf();
}

void LinuxDX11Context::Impl::save_view_conf() {
    view_conf.SetCurSection("Scene");
    view_conf.SetStr("Type", scene_name());
    view_conf.SetInt("AutoSaveViewpoint", auto_save_viewpoint ? 1 : 0);
    view_conf.SetInt("AutoSaveViewSettings", auto_save_view_settings ? 1 : 0);
    if (!auto_save_view_settings) return;
    view_conf.SetInt("EnablePianoKeyboard", keyboard_visible ? 1 : 0);
    view_conf.SetInt("EnableRipple", ripple_visible ? 1 : 0);
    view_conf.SetInt("EnableLyrics", lyrics_visible ? 1 : 0);
    view_conf.SetInt("EnablePitchBend", pitch_bend_enabled ? 1 : 0);
    view_conf.SetInt("EnablePitchBendAllNotes", pitch_bend_all_notes ? 1 : 0);
    view_conf.SetInt("EnableStars", stars_visible ? 1 : 0);
    view_conf.SetInt("EnableCounter", dashboard_visible ? 1 : 0);
    view_conf.SetInt("EnableBackgroundImage", background_image_visible ? 1 : 0);
    view_conf.SetInt("EnableTimeIndicator", time_indicator_visible ? 1 : 0);
    view_conf.SetInt("EnableGridBox", grid_visible ? 1 : 0);
    view_conf.SetInt("SingleKeyboard", single_keyboard ? 1 : 0);
}

void LinuxDX11Context::Impl::load_window_size_conf() {
    view_conf.SetCurSection("WindowSize");
    view_conf.GetInt("Width", &window_width, 0);
    view_conf.GetInt("Height", &window_height, 0);
    int apply = 0;
    view_conf.GetInt("ApplyToViewArea", &apply, 0);
    apply_to_view_area = apply != 0;
    if (window_width <= 0 || window_height <= 0) {
        window_width = 800;
        window_height = 600;
    }
}

void LinuxDX11Context::Impl::load_graphic_conf() {
    graphic_conf.Initialize(conf_path("Graphic").c_str());
    graphic_conf.SetCurSection("Anti-aliasing");
    int value = 0;
    graphic_conf.GetInt("MultiSampleType", &value, 0);
    multi_sample_type = (value >= 2 && value <= kMultiSampleMax) ? value : 0;
    graphic_conf.GetInt("SuperSample", &value, 1);
    super_sample = (value >= 1 && value <= 4) ? value : 1;
}

void LinuxDX11Context::Impl::load_player_conf() {
    MTConfFile conf;
    if (conf.Initialize("Player") != 0) return;
    int value = 0;
    conf.SetCurSection("PlayerControl");
    conf.GetInt("AutoPlaybackAfterOpenFile", &auto_playback_after_open, 0);
    conf.SetCurSection("ViewControl");
    conf.GetInt("ShowFileName", &value, 0);
    show_file_name = value > 0;
    conf.SetCurSection("SkipControl");
    conf.GetInt("SkipBackTimeSpanInMsec", &skip_back_ms, 10000);
    conf.GetInt("SkipForwardTimeSpanInMsec", &skip_forward_ms, 10000);
    conf.GetInt("MovingTimeSpanInMsec", &moving_time_span_ms, 400);
    conf.SetCurSection("PlaybackSpeedControl");
    int step = 1;
    int max_speed = 400;
    conf.GetInt("SpeedStepInPercent", &step, 1);
    conf.GetInt("MaxSpeedInPercent", &max_speed, 400);
    speed_step_percent = static_cast<unsigned long>(std::max(1, step));
    max_speed_percent = static_cast<unsigned long>(std::max(step, max_speed));
}

void LinuxDX11Context::Impl::load_midi_conf() {
    midi_conf.Initialize(conf_path("MIDI").c_str());
    midi_conf.SetCurSection("MIDIIN");
    int thru = 1;
    midi_conf.GetInt("MIDITHRU", &thru, 1);
    midi_thru = thru != 0;
    char device[256] = {};
    midi_conf.GetStr("PortA", device, sizeof(device), "");
    midi_in_device = device;
}

void LinuxDX11Context::Impl::update_title() {
    if (platform == nullptr || platform->sdl_window() == nullptr) return;
    std::string title = title_base;
    if (!song_path.empty()) title += " - " + base_name(song_path);
    char fps_text[32];
    std::snprintf(fps_text, sizeof(fps_text), " - FPS:%.1f", fps);
    title += fps_text;
    SDL_SetWindowTitle(platform->sdl_window(), title.c_str());
}

void LinuxDX11Context::Impl::show_message(const char* title, const std::string& text) {
    message.pending = true;
    message.title = title;
    message.text = text;
}

void LinuxDX11Context::Impl::show_error(const char* context) {
    std::string text = context;
    YNBaseLib::YNErrInfo* error = YNBaseLib::YNErrCtrl::GetErr();
    if (error != nullptr) {
        char detail[512];
        std::snprintf(detail, sizeof(detail), "\n\n%s\n%s (line %lu)\ninfo: %llu, %llu",
                      error->GetMessage(), error->GetFuncName(), error->GetLineNo(),
                      error->GetErrInfo1(), error->GetErrInfo2());
        text += detail;
    }
    std::fprintf(stderr, "MIDITrail: %s\n", text.c_str());
    show_message("ERROR", text);
}

//******************************************************************************
// DX11 scene setup (MIDITrailApp::_SetupDX11Scene)
//******************************************************************************
void LinuxDX11Context::Impl::detach_scene() {
    renderer.SetNoteBox11(nullptr);
    renderer.SetNoteRain11(nullptr);
    renderer.SetNoteBoxRing11(nullptr);
    renderer.SetNoteBoxLive11(nullptr);
    renderer.SetNoteRainLive11(nullptr);
    renderer.SetKeyboard11(nullptr);
    renderer.SetKeyboardRain11(nullptr);
    renderer.SetNoteRipple11(nullptr);
    renderer.SetNoteLyrics11(nullptr);
    renderer.SetGridBox11(nullptr);
    renderer.SetCamera11(nullptr);
    renderer.SetDashboard11(nullptr);
    renderer.SetTimeIndicator11(nullptr);
    renderer.SetPictBoard11(nullptr);
    renderer.SetGridRing11(nullptr);
    renderer.SetTimeIndicatorRing11(nullptr);
    renderer.SetPictBoardRing11(nullptr);
    renderer.SetBackgroundImage11(nullptr);
    renderer.SetStars11(nullptr);
}

void LinuxDX11Context::Impl::apply_scene_colors(const char* conf_scene) {
    // colour palette (1.4.1 port): the selected palette overrides the scene conf's
    // [Color] BackGroundRGB / CaptionRGBA (palette 0 = the scene conf itself)
    MTColorConf color_conf;
    if (color_conf.Initialize(conf_scene) == 0) {
        MTColorPalette palette;
        color_conf.GetSelectedColorPalette(&palette);
        D3DXCOLOR background;
        D3DXCOLOR counter;
        palette.GetBackgroundColor(&background);
        palette.GetCounterColor(&counter);
        char background_hex[16] = {};
        DXColorUtil::MakeHexRGBAFromColor(background, background_hex, sizeof(background_hex));
        renderer.SetBackgroundColor(DXColorUtil::MakeColorFromHexRGBA(background_hex));
        dashboard.SetTextColor(counter.r, counter.g, counter.b, counter.a);
        return;
    }
    MTConfFile conf;
    if (conf.Initialize(conf_scene) == 0 && conf.SetCurSection("Color") == 0) {
        char hex[16] = {};
        conf.GetStr("BackGroundRGB", hex, sizeof(hex), "000000");
        renderer.SetBackgroundColor(DXColorUtil::MakeColorFromHexRGBA(hex));
        conf.GetStr("CaptionRGBA", hex, sizeof(hex), "FFFFFFFF");
        D3DXCOLOR caption = DXColorUtil::MakeColorFromHexRGBA(hex);
        dashboard.SetTextColor(caption.r, caption.g, caption.b, caption.a);
    }
}

int LinuxDX11Context::Impl::setup_scene() {
    if (renderer.GetDevice() == nullptr) return 0;
    ID3D11Device* device = renderer.GetDevice();
    ID3D11DeviceContext* context = renderer.GetContext();

    dashboard.Create(device, context);
    {
        char path[MAX_PATH] = {};
        if (graphic_conf.SetCurSection("Background-image") == 0) {
            graphic_conf.GetStr("ImageFilePath", path, sizeof(path), "");
        }
        background_image.Release();
        background_image.Create(device, context, path);
    }

    const char* name = scene_name();
    Family scene_family = Family::Box;
    if (scene_type == SceneType::PianoRollRain || scene_type == SceneType::PianoRollRain2D) scene_family = Family::Rain;
    else if (scene_type == SceneType::PianoRollRing) scene_family = Family::Ring;

    if (play_status == PlayStatus::NoData) {
        family = Family::None;
        detach_scene();
        return 0;
    }
    family = scene_family;

    const bool live = is_monitor();
    const std::string live_name = std::string(name) + "Live";
    const char* conf_name = live ? live_name.c_str() : name;
    SMSeqData* seq = sequence.get();

    apply_scene_colors(conf_name);
    pitch_bend.Initialize();
    pitch_bend.SetEnable(true);

    // release every component first: only the active family is rebuilt
    note_box.Release(); note_rain.Release(); note_ring.Release();
    note_box_live.Release(); note_rain_live.Release();
    keyboard.Release(); keyboard_rain.Release(); grid_box.Release(); grid_ring.Release();
    time_indicator.Release(); time_indicator_ring.Release(); ripple.Release();
    pict_board_ring.Release(); lyrics.Release();
    detach_scene();

    unsigned long total_notes = 0;
    LoadProgress progress{&renderer, "Building note field...", 0.35f, 0.50f, 0};
    if (scene_family == Family::Box && live) {
        note_box_live.SetPitchBend(&pitch_bend);
        note_box_live.Create(device, context, live_name.c_str(), nullptr);
        renderer.SetNoteBoxLive11(&note_box_live);
        keyboard.Create(device, context, live_name.c_str(), nullptr, true);
        keyboard.SetPitchBend(&pitch_bend);
        ripple.SetPitchBend(&pitch_bend);
        ripple.Create(device, context, live_name.c_str(), nullptr);
    } else if (scene_family == Family::Box) {
        renderer.DrawLoadingScreen("Building note field...", 0.35f);
        DXNoteBox11::SetBuildProgressCallback(&parse_progress, &progress);
        note_box.Create(device, context, name, seq, single_keyboard);
        DXNoteBox11::SetBuildProgressCallback(nullptr, nullptr);
        note_box.SetPitchBend(&pitch_bend);
        renderer.SetNoteBox11(&note_box);
        total_notes = note_box.GetNoteCount();
        renderer.DrawLoadingScreen("Building keyboard...", 0.88f);
        keyboard.Create(device, context, name, seq, single_keyboard);
        keyboard.SetPitchBend(&pitch_bend);
        ripple.SetPitchBend(&pitch_bend);
        renderer.DrawLoadingScreen("Building ripple...", 0.93f);
        ripple.Create(device, context, name, seq);
        lyrics.SetPitchBend(&pitch_bend);
        lyrics.SetRingMode(false);
        lyrics.Create(device, context, name, seq);
        renderer.DrawLoadingScreen("Building grid...", 0.97f);
        grid_box.Create(device, context, name, seq);
        time_indicator.Create(device, context, name, seq);
    } else if (scene_family == Family::Rain && live) {
        note_rain_live.SetPitchBend(&pitch_bend);
        note_rain_live.Create(device, context, live_name.c_str(), nullptr);
        renderer.SetNoteRainLive11(&note_rain_live);
        keyboard_rain.Create(device, context, live_name.c_str(), nullptr);
        keyboard_rain.SetPitchBend(&pitch_bend);
    } else if (scene_family == Family::Rain) {
        renderer.DrawLoadingScreen("Building note field...", 0.35f);
        note_rain.Create(device, context, name, seq);
        note_rain.SetPitchBend(&pitch_bend);
        renderer.SetNoteRain11(&note_rain);
        total_notes = note_rain.GetNoteCount();
        keyboard_rain.Create(device, context, name, seq);
        keyboard_rain.SetPitchBend(&pitch_bend);
    } else if (live) {   // ring live
        note_box_live.SetPitchBend(&pitch_bend);
        note_box_live.Create(device, context, live_name.c_str(), nullptr, true);
        renderer.SetNoteBoxLive11(&note_box_live);
        pict_board_ring.Create(device, context, live_name.c_str(), nullptr);
        ripple.SetPitchBend(&pitch_bend);
        ripple.Create(device, context, live_name.c_str(), nullptr, true);
    } else {             // ring
        renderer.DrawLoadingScreen("Building note field...", 0.35f);
        note_ring.Create(device, context, name, seq);
        note_ring.SetPitchBend(&pitch_bend);
        renderer.SetNoteBoxRing11(&note_ring);
        total_notes = note_ring.GetNoteCount();
        grid_ring.Create(device, context, name, seq);
        time_indicator_ring.Create(device, context, name, seq);
        pict_board_ring.Create(device, context, name, seq);
        ripple.SetPitchBend(&pitch_bend);
        ripple.Create(device, context, name, seq, true);
        lyrics.SetPitchBend(&pitch_bend);
        lyrics.SetRingMode(true);
        lyrics.Create(device, context, name, seq);
    }

    // dashboard: file name, totals, counters
    {
        unsigned long total_bars = 0;
        SMBarList bar_list;
        if (seq != nullptr && !live && seq->GetBarList(&bar_list) == 0) total_bars = bar_list.GetSize();
        dashboard.Reset();
        if (!live) dashboard.SetMonitorMode(false, "");
        dashboard.SetFileName(show_file_name ? dash_file_name.c_str() : "");
        dashboard.SetTotals(seq != nullptr && !live ? seq->GetTotalPlayTime() : 0, total_bars, total_notes);
        dashboard.SetPlaySpeedRatio(play_speed_ratio);
    }

    // starfield: scene conf [Stars] NumberOfStars
    {
        int stars_count = 0;
        MTConfFile conf;
        if (conf.Initialize(conf_name) == 0 && conf.SetCurSection("Stars") == 0) {
            conf.GetInt("NumberOfStars", &stars_count, 2000);
        }
        stars.Release();
        stars.Create(device, context, stars_count);
    }

    // camera: keep the user's view when the same scene is set up again
    const bool keep_view = has_prev_view && prev_view_scene == scene_type && prev_view_live == live;
    float kx = 0, ky = 0, kz = 0, kphi = 0, ktheta = 0, kroll = 0, kauto = 0;
    if (keep_view) {
        camera.GetViewpointParam(&kx, &ky, &kz, &kphi, &ktheta, &kroll);
        kauto = auto_roll ? camera.GetAutoRollVelocity() : 0.0f;
    }
    const HWND hwnd = reinterpret_cast<HWND>(platform->native_window_handle());
    if (live) camera.Initialize(hwnd, live_name.c_str(), nullptr);
    else camera.Initialize(hwnd, name, seq);
    camera.Reset();
    cur_song_bpm = (seq != nullptr && seq->GetTempoBPM() > 0) ? seq->GetTempoBPM() : 120;
    camera.SetProgressDirection(scene_family == Family::Rain ? MTFirstPersonCam::DirY : MTFirstPersonCam::DirX);
    restore_saved_viewpoint();
    if (keep_view) {
        camera.SetViewpointParam(kx, ky, kz, kphi, ktheta, kroll, kauto);
        auto_roll = kauto != 0.0f;
    }
    camera.SetMouseCamMode(mouse_look);
    prev_view_scene = scene_type;
    prev_view_live = live;
    has_prev_view = true;
    renderer.SetCamera11(&camera);

    apply_visibility();
    if (seq != nullptr) seq->ReleaseMergedNoteList();
    return 0;
}

void LinuxDX11Context::Impl::apply_visibility() {
    pitch_bend.SetEnable(pitch_bend_enabled);
    note_box.SetPitchBendAllNotes(pitch_bend_all_notes);
    note_rain.SetPitchBendAllNotes(pitch_bend_all_notes);
    note_ring.SetPitchBendAllNotes(pitch_bend_all_notes);
    if (family == Family::None) return;
    renderer.SetBackgroundImage11(background_image_visible && background_image.IsReady() ? &background_image : nullptr);
    renderer.SetStars11(stars_visible ? &stars : nullptr);
    renderer.SetDashboard11(dashboard_visible ? &dashboard : nullptr);
    const bool live = is_monitor();
    if (family == Family::Box) {
        renderer.SetKeyboard11(keyboard_visible ? &keyboard : nullptr);
        renderer.SetNoteRipple11(ripple_visible ? &ripple : nullptr);
        renderer.SetNoteLyrics11(!live && lyrics_visible ? &lyrics : nullptr);
        renderer.SetGridBox11(!live && grid_visible ? &grid_box : nullptr);
        renderer.SetTimeIndicator11(!live && time_indicator_visible ? &time_indicator : nullptr);
    } else if (family == Family::Rain) {
        renderer.SetKeyboard11(nullptr);
        renderer.SetKeyboardRain11(keyboard_visible ? &keyboard_rain : nullptr);
        renderer.SetNoteLyrics11(nullptr);
    } else if (family == Family::Ring) {
        renderer.SetGridRing11(!live && grid_visible ? &grid_ring : nullptr);
        renderer.SetTimeIndicatorRing11(!live && time_indicator_visible ? &time_indicator_ring : nullptr);
        renderer.SetNoteRipple11(ripple_visible ? &ripple : nullptr);
        renderer.SetPictBoardRing11(keyboard_visible ? &pict_board_ring : nullptr);
        renderer.SetNoteLyrics11(!live && lyrics_visible ? &lyrics : nullptr);
    }
}

// MIDITrailApp::_FeedDX11Tick
void LinuxDX11Context::Impl::feed_tick(unsigned long tick, unsigned long play_ms) {
    camera.SetCurTickTime(tick);
    dashboard.SetPlayTimeMSec(play_ms);
    const bool live = is_monitor();
    if (family == Family::Rain) {
        note_rain.SetCurTickTime(tick);
        keyboard_rain.SetCurTickTime(tick);
        if (!live) dashboard.SetCurNotes(note_rain.GetPlayedNoteCount(tick));
    } else if (family == Family::Ring) {
        note_ring.SetCurTickTime(tick);
        time_indicator_ring.SetCurTickTime(tick);
        pict_board_ring.SetCurTickTime(tick);
        ripple.SetCurTickTime(tick);
        ripple.SetPlayTimeMSec(play_ms);
        lyrics.SetCurTickTime(tick);
        lyrics.SetPlayTimeMSec(play_ms);
        if (!live) dashboard.SetCurNotes(note_ring.GetPlayedNoteCount(tick));
    } else if (family == Family::Box) {
        note_box.SetCurTickTime(tick);
        if (sequence) {
            const unsigned long tpqn = sequence->GetTimeDivision();
            if (tpqn > 0) {
                const double tick_per_ms = static_cast<double>(tpqn) * static_cast<double>(cur_song_bpm) / 60000.0;
                keyboard.SetSongTickPerMs(tick_per_ms);
                note_box.SetSongTickPerMs(tick_per_ms);
            }
        }
        keyboard.SetCurTickTime(tick);
        ripple.SetCurTickTime(tick);
        ripple.SetPlayTimeMSec(play_ms);
        lyrics.SetCurTickTime(tick);
        lyrics.SetPlayTimeMSec(play_ms);
        time_indicator.SetCurTickTime(tick);
        if (!live) dashboard.SetCurNotes(note_box.GetPlayedNoteCount(tick));
    }
}

// MsgSkipStart / MsgSkipEnd handling
void LinuxDX11Context::Impl::reset_components_after_skip() {
    if (family == Family::Rain) {
        keyboard_rain.Reset();
        note_rain.Reset();
    } else if (family == Family::Ring) {
        note_ring.Reset();
        ripple.Reset();
        lyrics.Reset();
    } else if (family == Family::Box) {
        keyboard.Reset();
        ripple.Reset();
        lyrics.Reset();
        time_indicator.Reset();
    }
    pitch_bend.Reset();
    bend_timeline.Reset();
    bend_last_tick = 0;
}

//******************************************************************************
// viewpoints
//******************************************************************************
static void write_viewpoint(YNBaseLib::YNConfFile& conf, MTFirstPersonCam& camera, bool auto_roll) {
    float x, y, z, phi, theta, roll;
    camera.GetViewpointParam(&x, &y, &z, &phi, &theta, &roll);
    conf.SetFloat("X", x); conf.SetFloat("Y", y); conf.SetFloat("Z", z);
    conf.SetFloat("Phi", phi); conf.SetFloat("Theta", theta);
    conf.SetFloat("ManualRollAngle", roll);
    conf.SetFloat("AutoRollVelocity", auto_roll ? camera.GetAutoRollVelocity() : 0.0f);
}

static bool read_viewpoint(YNBaseLib::YNConfFile& conf, MTFirstPersonCam& camera, bool* auto_roll) {
    const float missing = -1.0e30f;
    float x = missing, y = 0, z = 0, phi = 0, theta = 0, roll = 0, velocity = 0;
    conf.GetFloat("X", &x, missing);
    if (x == missing) return false;
    conf.GetFloat("Y", &y, 0.0f); conf.GetFloat("Z", &z, 0.0f);
    conf.GetFloat("Phi", &phi, 0.0f); conf.GetFloat("Theta", &theta, 0.0f);
    conf.GetFloat("ManualRollAngle", &roll, 0.0f);
    conf.GetFloat("AutoRollVelocity", &velocity, 0.0f);
    camera.SetViewpointParam(x, y, z, phi, theta, roll, velocity);
    *auto_roll = velocity != 0.0f;
    return true;
}

int LinuxDX11Context::Impl::save_viewpoint() {
    if (family == Family::None) return 0;
    std::string section = std::string("Viewpoint-") + scene_name() + (is_monitor() ? "Live" : "");
    if (view_conf.SetCurSection(section.c_str()) == 0) write_viewpoint(view_conf, camera, auto_roll);
    return 0;
}

void LinuxDX11Context::Impl::restore_saved_viewpoint() {
    if (scene_type == SceneType::PianoRollRing) camera.SetDefaultViewpointRing();
    else camera.SetDefaultViewpoint();
    std::string section = std::string("Viewpoint-") + scene_name() + (is_monitor() ? "Live" : "");
    if (view_conf.SetCurSection(section.c_str()) == 0) read_viewpoint(view_conf, camera, &auto_roll);
    camera.SetAutoRollMode(auto_roll);
}

void LinuxDX11Context::Impl::reset_viewpoint_to_saved() {
    if (family == Family::None) return;
    restore_saved_viewpoint();
}

void LinuxDX11Context::Impl::save_my_viewpoint(unsigned long slot) {
    if (family == Family::None) return;
    std::string section = "MyViewpoint-" + std::to_string(slot) + "-" + scene_name() + (is_monitor() ? "Live" : "");
    if (view_conf.SetCurSection(section.c_str()) == 0) write_viewpoint(view_conf, camera, auto_roll);
}

void LinuxDX11Context::Impl::restore_my_viewpoint(unsigned long slot) {
    if (family == Family::None) return;
    std::string section = "MyViewpoint-" + std::to_string(slot) + "-" + scene_name() + (is_monitor() ? "Live" : "");
    if (view_conf.SetCurSection(section.c_str()) == 0 && read_viewpoint(view_conf, camera, &auto_roll)) {
        camera.SetAutoRollMode(auto_roll);
    }
}

//******************************************************************************
// loading files (MIDITrailApp::_LoadMIDIFile / _AddMIDIFile / _FileOpenProc)
//******************************************************************************
int LinuxDX11Context::Impl::load_midi_file(const std::string& path) {
    renderer.DrawLoadingScreen("Opening MIDI file...", 0.0f);
    stop_worker();

    auto loaded = std::make_unique<SMSeqData>();
    SMFileReader reader;
    if (debug_dump) reader.SetLogPath((path + ".dump.txt").c_str());
    LoadProgress progress{&renderer, "Reading track", 0.0f, 0.30f, 0};
    SMSimpleList::ResetTruncatedFlag();
    SMFileReader::SetLoadProgressCallback(&parse_progress, &progress);
    const int result = reader.Load(path.c_str(), loaded.get());
    SMFileReader::SetLoadProgressCallback(nullptr, nullptr);
    if (result != 0) {
        start_worker();
        show_error(("Could not load the MIDI file.\n" + path).c_str());
        return result;
    }
    if (SMSimpleList::WasTruncated()) {
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
        };
        const SDL_MessageBoxData data = {
            SDL_MESSAGEBOX_WARNING, platform->sdl_window(), "MIDITrail - File too large",
            "This file is too large to load completely (over the 32-bit limit of\n"
            "~2.1 billion notes); some notes could not be read.\n\n"
            "Display the portion that was loaded anyway?",
            2, buttons, nullptr};
        int answer = 1;
        if (SDL_ShowMessageBox(&data, &answer) && answer != 1) {
            unload_song();
            start_worker();
            return 0;
        }
    }

    sequence = std::move(loaded);
    song_path = path;
    dash_file_name = base_name(path);
    play_speed_ratio = 100;
    position_ms = 0.0;
    rewind_on_play = false;
    play_status = PlayStatus::Stop;
    renderer.DrawLoadingScreen("Reading MIDI events...", 0.30f);
    tempo_map.Build(sequence.get());
    bend_timeline.Build(sequence.get());
    bend_last_tick = 0;
    build_midi_events();
    setup_scene();
    feed_tick(0, 0);
    start_worker();
    update_title();
    renderer.DrawLoadingScreen("Ready", 1.0f);
    std::fprintf(stderr, "MIDITrail: loaded %s (%s)\n", path.c_str(), scene_name());
    return 0;
}

int LinuxDX11Context::Impl::add_midi_file(const std::string& path) {
    if (!(play_status == PlayStatus::NoData || play_status == PlayStatus::Stop
          || play_status == PlayStatus::MonitorOFF)) return 0;
    renderer.DrawLoadingScreen("Opening MIDI file...", 0.0f);
    SMSeqData added;
    SMFileReader reader;
    if (debug_dump) reader.SetLogPath((path + ".dump.txt").c_str());
    if (reader.Load(path.c_str(), &added) != 0) {
        show_error(("Could not load the MIDI file.\n" + path).c_str());
        return -1;
    }
    // port / channel from the file name ("portB", "ch10")
    const std::string name = base_name(path);
    short port = -1;
    short channel = -1;
    const char* port_text = std::strstr(name.c_str(), "port");
    if (port_text != nullptr && port_text[4] != '\0') port = static_cast<short>(std::tolower(port_text[4]) - 'a');
    const char* ch_text = std::strstr(name.c_str(), "ch");
    if (ch_text != nullptr) {
        char number[3] = {ch_text[2], ch_text[2] ? ch_text[3] : '\0', '\0'};
        channel = static_cast<short>(std::atoi(number) - 1);
    }

    stop_worker();
    if (!sequence) sequence = std::make_unique<SMSeqData>();
    sequence->AddSequence(added, port, channel);
    if (song_path.empty()) {
        song_path = path;
        dash_file_name = name;
    }
    play_speed_ratio = 100;
    position_ms = 0.0;
    rewind_on_play = false;
    play_status = PlayStatus::Stop;
    tempo_map.Build(sequence.get());
    bend_timeline.Build(sequence.get());
    bend_last_tick = 0;
    build_midi_events();
    setup_scene();
    feed_tick(0, 0);
    start_worker();
    update_title();
    return 0;
}

int LinuxDX11Context::Impl::file_open_proc(const std::string& path) {
    const int result = load_midi_file(path);
    if (result != 0 || play_status == PlayStatus::NoData) return result;
    disp_how_to_view_first_time();
    if (auto_playback_after_open > 0) play_pause();
    return 0;
}

int LinuxDX11Context::Impl::stop_playback_and_open_file(const std::string& path) {
    if (auto_save_viewpoint) save_viewpoint();
    if (play_status == PlayStatus::MonitorON) stop_monitoring();
    if (is_playing()) {
        notify_worker(false, true, 0.0);
        play_status = PlayStatus::Stop;
    }
    return file_open_proc(path);
}

int LinuxDX11Context::Impl::open_folder(const std::string& path) {
    std::vector<std::string> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (!entry.is_regular_file(error)) continue;
        const std::string name = entry.path().filename().string();
        if (is_midi_file_name(name)) files.push_back(entry.path().string());
    }
    if (files.empty()) {
        show_message("WARNING", "MIDI data file is not found in the folder.");
        return 0;
    }
    std::sort(files.begin(), files.end());
    file_list = std::move(files);
    file_list_index = 0;
    return open_file_from_list();
}

int LinuxDX11Context::Impl::open_file_from_list() {
    if (file_list_index >= file_list.size()) return 0;
    return stop_playback_and_open_file(file_list[file_list_index]);
}

void LinuxDX11Context::Impl::unload_song() {
    stop_worker();
    play_status = PlayStatus::NoData;
    sequence.reset();
    song_path.clear();
    dash_file_name.clear();
    {
        std::lock_guard<std::recursive_mutex> lock(midi_output_mutex);
        midi_events.clear();
        sysex_data.clear();
        midi_notes.clear();
    }
    setup_scene();
    start_worker();
    update_title();
}

//******************************************************************************
// playback control
//******************************************************************************
int LinuxDX11Context::Impl::play_pause() {
    if (play_status == PlayStatus::Stop) {
        if (rewind_on_play) {
            rewind_on_play = false;
            position_ms = 0.0;
            reset_components_after_skip();
        }
        play_status = PlayStatus::Play;
        notify_worker(true, true, position_ms);
    } else if (play_status == PlayStatus::Play) {
        position_ms = snapshot_ms.load();
        play_status = PlayStatus::Pause;
        notify_worker(false, true, position_ms);
    } else if (play_status == PlayStatus::Pause) {
        play_status = PlayStatus::Play;
        notify_worker(true, true, position_ms);
    }
    return 0;
}

void LinuxDX11Context::Impl::stop() {
    if (!is_playing()) return;
    play_status = PlayStatus::Stop;
    if (auto_save_viewpoint) save_viewpoint();
    position_ms = 0.0;
    rewind_on_play = false;
    notify_worker(false, true, 0.0);
    snapshot_tick = 0;
    snapshot_ms = 0;
    reset_components_after_skip();
    reset_viewpoint_to_saved();
}

void LinuxDX11Context::Impl::skip(int delta_ms) {
    if (play_status != PlayStatus::Play || !sequence) return;
    const double total_ms = static_cast<double>(sequence->GetTotalPlayTime());
    const double current = static_cast<double>(snapshot_ms.load());
    const double target = std::clamp(current + delta_ms, 0.0, total_ms);
    position_ms = target;
    const unsigned long from = snapshot_tick.load();
    const unsigned long to = tempo_map.MsToTick(target);
    reset_components_after_skip();
    notify_worker(play_status == PlayStatus::Play, true, target, moving_time_span_ms > 0, from, to);
}

void LinuxDX11Context::Impl::change_speed(int direction) {
    if (direction < 0) {
        play_speed_ratio = play_speed_ratio > speed_step_percent * 2
            ? play_speed_ratio - speed_step_percent : speed_step_percent;
    } else {
        play_speed_ratio = std::min(max_speed_percent, play_speed_ratio + speed_step_percent);
    }
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_speed = (play_speed_ratio / 100.0) * (boost_2x ? 2.0 : 1.0);
    }
    dashboard.SetPlaySpeedRatio(play_speed_ratio);
}

// MsgPlayStatus(Stop) after a natural end of the song
void LinuxDX11Context::Impl::on_playback_end() {
    if (play_status != PlayStatus::Play) return;
    play_status = PlayStatus::Stop;
    position_ms = static_cast<double>(snapshot_ms.load());
    if (auto_save_viewpoint) save_viewpoint();
    rewind_on_play = true;
    if (folder_playback && !file_list.empty()) {
        if (file_list_index + 1 < file_list.size()) {
            ++file_list_index;
            if (load_midi_file(file_list[file_list_index]) == 0 && play_status == PlayStatus::Stop) play_pause();
        }
    } else if (repeat) {
        play_pause();
    }
    if (quit_at_end && platform != nullptr) platform->request_quit();
}

void LinuxDX11Context::Impl::select_scene(SceneType type) {
    if (!(play_status == PlayStatus::NoData || play_status == PlayStatus::Stop
          || play_status == PlayStatus::MonitorOFF)) return;
    if (play_status != PlayStatus::NoData && auto_save_viewpoint) save_viewpoint();
    scene_type = type;
    save_view_conf();
    if (play_status == PlayStatus::Stop || play_status == PlayStatus::MonitorOFF) {
        setup_scene();
        feed_tick(is_monitor() ? 0 : snapshot_tick.load(), is_monitor() ? 0 : snapshot_ms.load());
    }
}

//******************************************************************************
// live monitor (MIDITrailApp::_OnMenuStartMonitoring / _OnMenuStopMonitoring)
//******************************************************************************
int LinuxDX11Context::Impl::start_monitoring() {
    if (!(play_status == PlayStatus::NoData || play_status == PlayStatus::Stop
          || play_status == PlayStatus::MonitorOFF)) return 0;
    notify_worker(false, true, 0.0);
    const bool rebuild = play_status != PlayStatus::MonitorOFF;
    if (rebuild && play_status != PlayStatus::NoData && auto_save_viewpoint) save_viewpoint();

    // input port: [MIDIIN] PortA (errors are ignored like on Windows; the scene
    // still switches to the live view)
    load_midi_conf();
    midi_in.close_input();
    if (!midi_in_device.empty()) {
        std::vector<AlsaMidiPort> inputs;
        midi_in.enumerate(&inputs, nullptr);
        for (const AlsaMidiPort& input : inputs) {
            if (input.label != midi_in_device) continue;
            std::string error;
            if (!midi_in.open_input(input, &error)) std::fprintf(stderr, "MIDI IN: %s\n", error.c_str());
            break;
        }
    }
    for (int channel = 0; channel < 16; ++channel) {
        live_rpn_msb[channel] = 0x7f;
        live_rpn_lsb[channel] = 0x7f;
        live_bend_sens[channel] = SM_DEFAULT_PITCHBEND_SENSITIVITY;
    }
    play_status = PlayStatus::MonitorON;
    note_box_live.Reset();
    note_rain_live.Reset();
    if (rebuild || family == Family::None) setup_scene();
    live_note_count = 0;
    dashboard.SetMonitorMode(true, midi_in_device.c_str());
    dashboard.SetCurNotes(0);
    return 0;
}

void LinuxDX11Context::Impl::stop_monitoring() {
    if (play_status != PlayStatus::MonitorON) return;
    midi_in.close_input();
    midi_out.all_notes_off();
    play_status = PlayStatus::MonitorOFF;
}

void LinuxDX11Context::Impl::poll_midi_in() {
    if (play_status != PlayStatus::MonitorON || !midi_in.input_open()) return;
    midi_in.poll([this](const snd_seq_event_t& event) {
        // MIDI THRU: forward the raw message to MIDI OUT port A
        if (midi_thru && midi_decoder != nullptr && midi_out.port_open(0)) {
            unsigned char bytes[256];
            snd_midi_event_reset_decode(midi_decoder);
            const long size = snd_midi_event_decode(midi_decoder, bytes, sizeof(bytes), &event);
            if (size > 0) midi_out.send(0, bytes, static_cast<std::size_t>(size));
        }
        const unsigned char port = 0;
        switch (event.type) {
        case SND_SEQ_EVENT_NOTEON:
            if (event.data.note.velocity > 0) {
                const unsigned char ch = event.data.note.channel & 0x0f;
                const unsigned char note = event.data.note.note & 0x7f;
                note_box_live.SetNoteOn(port, ch, note, event.data.note.velocity & 0x7f);
                note_rain_live.SetNoteOn(port, ch, note, event.data.note.velocity & 0x7f);
                ripple.SetNoteOnLive(port, ch, note);
                keyboard.SetNoteOnLive(port, ch, note);
                keyboard_rain.SetNoteOnLive(port, ch, note);
                dashboard.SetCurNotes(++live_note_count);
                break;
            }
            [[fallthrough]];
        case SND_SEQ_EVENT_NOTEOFF: {
            const unsigned char ch = event.data.note.channel & 0x0f;
            const unsigned char note = event.data.note.note & 0x7f;
            note_box_live.SetNoteOff(port, ch, note);
            note_rain_live.SetNoteOff(port, ch, note);
            keyboard.SetNoteOffLive(note);
            keyboard_rain.SetNoteOffLive(port, ch, note);
            break;
        }
        case SND_SEQ_EVENT_CONTROLLER: {
            const unsigned char ch = event.data.control.channel & 0x0f;
            const unsigned int cc = event.data.control.param;
            const unsigned char value = event.data.control.value & 0x7f;
            if (cc == 101) live_rpn_msb[ch] = value;
            else if (cc == 100) live_rpn_lsb[ch] = value;
            else if (cc == 6 && live_rpn_msb[ch] == 0 && live_rpn_lsb[ch] == 0) live_bend_sens[ch] = value;
            else if (cc == 120 || cc == 123) {
                note_box_live.AllNoteOff();
                note_rain_live.AllNoteOff();
                keyboard.AllNoteOffLive();
                keyboard_rain.AllNoteOffLive();
            }
            break;
        }
        case SND_SEQ_EVENT_PITCHBEND: {
            const unsigned char ch = event.data.control.channel & 0x0f;
            const short value = static_cast<short>(std::clamp(event.data.control.value, -8192, 8191));
            pitch_bend.SetPitchBend(port, ch, value, live_bend_sens[ch]);
            break;
        }
        default:
            break;
        }
    });
}

//******************************************************************************
// MIDI OUT stream (replaces SMSequencer's output side)
//******************************************************************************
void LinuxDX11Context::Impl::build_midi_events() {
    std::lock_guard<std::recursive_mutex> lock(midi_output_mutex);
    midi_events.clear();
    sysex_data.clear();
    midi_notes.clear();
    midi_event_cursor = 0;
    midi_output_tick = 0;
    midi_output_sync_pending = true;
    if (!sequence) return;

    SMTrack track;
    if (sequence->GetMergedTrack(&track) == 0) {
        unsigned long tick = 0;
        SMEvent event;
        midi_events.reserve(track.GetSize());
        for (unsigned long index = 0; index < track.GetSize(); ++index) {
            unsigned long delta = 0;
            unsigned char port = 0;
            if (track.GetDataSet(index, &delta, &event, &port) != 0) continue;
            tick += delta;
            const unsigned char* data = event.GetDataPtr();
            const unsigned long size = event.GetDataSize();
            MidiEvent out = {};
            out.tick = tick;
            out.port = port;
            if (event.GetType() == SMEvent::EventMIDI) {
                out.data[0] = event.GetStatus();
                out.size = static_cast<unsigned char>(1 + std::min(2UL, size));
                for (unsigned long i = 0; i + 1 < out.size; ++i) out.data[i + 1] = data[i];
                midi_events.push_back(out);
            } else if (event.GetType() == SMEvent::EventSysEx) {
                std::vector<unsigned char> bytes;
                if (event.GetStatus() == 0xf0) bytes.push_back(0xf0);
                bytes.insert(bytes.end(), data, data + size);
                if (bytes.size() < 2 || bytes.front() != 0xf0 || bytes.back() != 0xf7) continue;
                out.size = 0;
                out.sysex = static_cast<std::uint32_t>(sysex_data.size());
                sysex_data.push_back(std::move(bytes));
                midi_events.push_back(out);
            }
        }
    }

    SMNoteList* note_list = nullptr;
    if (sequence->GetMergedNoteList(&note_list) == 0 && note_list != nullptr) {
        midi_notes.reserve(note_list->GetSize());
        for (unsigned long index = 0; index < note_list->GetSize(); ++index) {
            SMNote note = {};
            if (note_list->GetNote(index, &note) != 0) continue;
            midi_notes.push_back({note.startTime, note.endTime, note.portNo, note.chNo,
                                  note.noteNo, note.velocity});
        }
        std::sort(midi_notes.begin(), midi_notes.end(),
                  [](const SoundingNote& a, const SoundingNote& b) { return a.start < b.start; });
    }
}

// restore the controller state at `tick` and re-strike the sounding notes
// (SMSequencer::_ProcSkip equivalent)
void LinuxDX11Context::Impl::midi_output_reset(unsigned long tick) {
    std::lock_guard<std::recursive_mutex> lock(midi_output_mutex);
    midi_output_sync_pending = false;
    midi_output_tick = tick;
    midi_out.all_notes_off();
    struct ChannelState {
        short program = -1;
        short bend_lsb = -1, bend_msb = -1;
        short pressure = -1;
        short cc[128];
        ChannelState() { std::fill(std::begin(cc), std::end(cc), static_cast<short>(-1)); }
    };
    std::vector<ChannelState> state(LinuxMidiOut::kPortCount * 16);
    std::size_t cursor = 0;
    for (; cursor < midi_events.size() && midi_events[cursor].tick <= tick; ++cursor) {
        const MidiEvent& event = midi_events[cursor];
        if (event.port >= LinuxMidiOut::kPortCount) continue;
        if (event.size == 0) {   // SysEx in order (resets / setup)
            midi_out.send(event.port, sysex_data[event.sysex]);
            continue;
        }
        ChannelState& channel = state[event.port * 16 + (event.data[0] & 0x0f)];
        switch (event.data[0] & 0xf0) {
        case 0xb0: channel.cc[event.data[1] & 0x7f] = event.data[2]; break;
        case 0xc0: channel.program = event.data[1]; break;
        case 0xd0: channel.pressure = event.data[1]; break;
        case 0xe0: channel.bend_lsb = event.data[1]; channel.bend_msb = event.data[2]; break;
        default: break;
        }
    }
    midi_event_cursor = cursor;
    for (int port = 0; port < LinuxMidiOut::kPortCount; ++port) {
        if (!midi_out.port_open(port)) continue;
        for (int ch = 0; ch < 16; ++ch) {
            const ChannelState& channel = state[port * 16 + ch];
            const unsigned char status = static_cast<unsigned char>(ch);
            if (channel.program >= 0) {
                const unsigned char bytes[] = {static_cast<unsigned char>(0xc0 | status),
                                               static_cast<unsigned char>(channel.program)};
                midi_out.send(port, bytes, 2);
            }
            for (int cc = 0; cc < 128; ++cc) {
                if (channel.cc[cc] < 0 || cc == 120 || cc == 121 || cc == 123) continue;
                const unsigned char bytes[] = {static_cast<unsigned char>(0xb0 | status),
                                               static_cast<unsigned char>(cc),
                                               static_cast<unsigned char>(channel.cc[cc])};
                midi_out.send(port, bytes, 3);
            }
            if (channel.pressure >= 0) {
                const unsigned char bytes[] = {static_cast<unsigned char>(0xd0 | status),
                                               static_cast<unsigned char>(channel.pressure)};
                midi_out.send(port, bytes, 2);
            }
            if (channel.bend_msb >= 0) {
                const unsigned char bytes[] = {static_cast<unsigned char>(0xe0 | status),
                                               static_cast<unsigned char>(channel.bend_lsb),
                                               static_cast<unsigned char>(channel.bend_msb)};
                midi_out.send(port, bytes, 3);
            }
        }
    }
    for (const SoundingNote& note : midi_notes) {
        if (note.start > tick) break;
        if (tick < note.end && note.start < tick) {
            const unsigned char bytes[] = {static_cast<unsigned char>(0x90 | (note.ch & 0x0f)),
                                           note.note, note.velocity};
            midi_out.send(note.port, bytes, 3);
        }
    }
}

void LinuxDX11Context::Impl::midi_output_until(unsigned long tick) {
    std::lock_guard<std::recursive_mutex> lock(midi_output_mutex);
    if (!midi_out.any_open()) return;
    if (midi_output_sync_pending || tick < midi_output_tick) {
        midi_output_reset(tick);
        return;
    }
    while (midi_event_cursor < midi_events.size() && midi_events[midi_event_cursor].tick <= tick) {
        const MidiEvent& event = midi_events[midi_event_cursor++];
        if (event.size == 0) midi_out.send(event.port, sysex_data[event.sysex]);
        else midi_out.send(event.port, event.data, event.size);
    }
    midi_output_tick = tick;
}

// first run: pick a synthesizer for port A (MIDITrailApp::_AutoConfigMIDIOUT picks
// the GS Wavetable synth); FluidSynth / TiMidity are the usual Linux equivalents
void LinuxDX11Context::Impl::auto_config_midi_out() {
    midi_conf.SetCurSection("MIDIOUT");
    int confirmed = 0;
    midi_conf.GetInt("AutoConfigConfirm", &confirmed, 0);
    char port_a[256] = {};
    midi_conf.GetStr("PortA", port_a, sizeof(port_a), "");
    if (confirmed != 0 || port_a[0] != '\0') return;
    for (const std::string& device : midi_out.enumerate_devices()) {
        if (device.find("FluidSynth") != std::string::npos || device.find("TiMidity") != std::string::npos) {
            midi_conf.SetStr("PortA", device.c_str());
            break;
        }
    }
    midi_conf.SetInt("AutoConfigConfirm", 1);
}

//******************************************************************************
// playback worker
//******************************************************************************
void LinuxDX11Context::Impl::notify_worker(bool playing, bool sync, double target_ms, bool slide,
                                           unsigned long slide_from, unsigned long slide_to) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_playing = playing;
        worker_speed = (play_speed_ratio / 100.0) * (boost_2x ? 2.0 : 1.0);
        if (sync) {
            worker_position_ms = target_ms;
            worker_clock = std::chrono::steady_clock::now();
            worker_sync_pending = true;
        }
        worker_slide = slide;
        if (slide) {
            worker_slide_first = true;
            worker_slide_from = slide_from;
            worker_slide_to = slide_to;
            worker_slide_start = std::chrono::steady_clock::now();
        }
    }
    if (!playing && sync) {
        snapshot_ms = static_cast<unsigned long>(std::max(0.0, target_ms));
        if (!slide) snapshot_tick = tempo_map.MsToTick(target_ms);
    }
    worker_cv.notify_one();
}

void LinuxDX11Context::Impl::start_worker() {
    if (worker.joinable()) return;
    worker_stop = false;
    worker = std::thread([this] { worker_loop(); });
}

void LinuxDX11Context::Impl::stop_worker() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_stop = true;
        worker_playing = false;
    }
    worker_cv.notify_one();
    if (worker.joinable()) worker.join();
}

void LinuxDX11Context::Impl::worker_loop() {
    using clock = std::chrono::steady_clock;
    std::unique_lock<std::mutex> lock(worker_mutex);
    while (!worker_stop) {
        worker_cv.wait_for(lock, std::chrono::milliseconds(2), [this] { return worker_stop; });
        if (worker_stop) break;
        const auto now = clock::now();

        // skip slide (SMSequencer::_SlidePlaybackTime): sweep the tick from the old
        // to the new position over MovingTimeSpanInMsec, silent, then resume
        if (worker_slide) {
            const double elapsed = std::chrono::duration<double, std::milli>(now - worker_slide_start).count();
            if (elapsed < moving_time_span_ms) {
                const double t = elapsed / moving_time_span_ms;
                const double from = static_cast<double>(worker_slide_from);
                const double to = static_cast<double>(worker_slide_to);
                snapshot_tick = static_cast<unsigned long>(from + (to - from) * t);
                snapshot_ms = static_cast<unsigned long>(worker_position_ms);
                const bool first = worker_slide_first;
                worker_slide_first = false;
                if (first) {
                    lock.unlock();
                    midi_out.all_notes_off();
                    lock.lock();
                }
                continue;
            }
            worker_slide = false;
            worker_clock = now;
            worker_sync_pending = true;
            playback_skipped = true;
        }

        const bool sync = worker_sync_pending;
        worker_sync_pending = false;
        bool playing = worker_playing;
        bool ended = false;
        if (playing && sequence) {
            const double delta = std::chrono::duration<double, std::milli>(now - worker_clock).count();
            worker_clock = now;
            worker_position_ms += std::max(0.0, delta) * worker_speed;
            const double total_ms = static_cast<double>(sequence->GetTotalPlayTime());
            if (worker_position_ms >= total_ms) {
                worker_position_ms = total_ms;
                worker_playing = false;
                playing = false;
                ended = true;
            }
        }
        unsigned long tick = sequence ? tempo_map.MsToTick(worker_position_ms) : 0;
        if (ended) tick = sequence->GetTotalTickTime();
        if (playing || ended || sync) {
            snapshot_ms = static_cast<unsigned long>(std::max(0.0, worker_position_ms));
            snapshot_tick = tick;
        }
        lock.unlock();
        if (sync) {
            midi_output_sync_pending = true;
            if (!playing) midi_out.all_notes_off();
        }
        if (playing || ended) midi_output_until(tick);
        if (ended) {
            midi_out.all_notes_off();
            playback_ended = true;
        }
        lock.lock();
    }
}

//******************************************************************************
// renderer re-init (AA / window size change: MIDITrailApp::_ChangeWindowSize)
//******************************************************************************
int LinuxDX11Context::Impl::rebuild_renderer() {
    release_howto_textures();
    detach_scene();
    note_box.Release(); note_rain.Release(); note_ring.Release();
    note_box_live.Release(); note_rain_live.Release();
    keyboard.Release(); keyboard_rain.Release(); grid_box.Release(); grid_ring.Release();
    stars.Release(); time_indicator.Release(); time_indicator_ring.Release();
    ripple.Release(); pict_board_ring.Release(); lyrics.Release();
    dashboard.Release(); background_image.Release();
    renderer.Terminate();
    const int result = renderer.Initialize(reinterpret_cast<HWND>(platform->native_window_handle()),
                                           static_cast<unsigned long>(multi_sample_type), false,
                                           static_cast<unsigned long>(super_sample));
    if (result != 0) {
        show_error("Could not initialize the renderer.");
        return result;
    }
    platform->get_drawable_size(&width, &height);
    setup_scene();
    feed_tick(is_monitor() ? 0 : snapshot_tick.load(), is_monitor() ? 0 : snapshot_ms.load());
    return 0;
}

int LinuxDX11Context::Impl::change_window_size() {
    if (!fullscreen) {
        int width_px = window_width;
        int height_px = window_height;
        // the menu bar sits inside the SDL window: add its height so the view area
        // gets the configured size (ImGui may not exist yet at startup: 15px font + padding)
        if (apply_to_view_area && menu.enabled()) {
            height_px += ImGui::GetCurrentContext() != nullptr ? static_cast<int>(ImGui::GetFrameHeight()) : 21;
        }
        platform->set_window_size(width_px, height_px);
    }
    return 0;
}

//******************************************************************************
// SDL file dialogs (results are applied on the render thread)
//******************************************************************************
void LinuxDX11Context::Impl::show_open_dialog(int kind) {
    struct Request { Impl* impl; int kind; };
    static const SDL_DialogFileFilter midi_filters[] = {{"Standard MIDI File", "mid;midi"}};
    static const SDL_DialogFileFilter image_filters[] = {{"Image", "png;jpg;jpeg;bmp;tga;gif"}};
    auto* request = new Request{this, kind};
    auto callback = [](void* user, const char* const* files, int) {
        std::unique_ptr<Request> owned(static_cast<Request*>(user));
        if (files == nullptr) {
            std::fprintf(stderr, "File dialog failed: %s\n", SDL_GetError());
            return;
        }
        if (files[0] == nullptr) {
            // cancelled: an export that already loaded its song goes back to the title
            if (owned->kind == PendingExportSave) {
                std::lock_guard<std::mutex> lock(owned->impl->pending_mutex);
                owned->impl->pending_paths.emplace_back(owned->kind, std::string());
            }
            return;
        }
        std::lock_guard<std::mutex> lock(owned->impl->pending_mutex);
        owned->impl->pending_paths.emplace_back(owned->kind, files[0]);
    };
    SDL_Window* window = platform->sdl_window();
    switch (kind) {
    case PendingFolder:
        SDL_ShowOpenFolderDialog(callback, request, window, nullptr, false);
        break;
    case PendingBackgroundImage:
        SDL_ShowOpenFileDialog(callback, request, window, image_filters, 1, nullptr, false);
        break;
    case PendingExportSave: {
        static char default_name[MAX_PATH];
        std::string name = base_name(video_dlg.midi_path);
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name.resize(dot);
        name += MTVideoCodecExt(video_dlg.params.codec);
        const std::size_t slash = video_dlg.midi_path.find_last_of('/');
        if (slash != std::string::npos) name = video_dlg.midi_path.substr(0, slash + 1) + name;
        std::snprintf(default_name, sizeof(default_name), "%s", name.c_str());
        SDL_ShowSaveFileDialog(callback, request, window, nullptr, 0, default_name);
        break;
    }
    default:
        SDL_ShowOpenFileDialog(callback, request, window, midi_filters, 1, nullptr, false);
        break;
    }
}

void LinuxDX11Context::Impl::process_pending_paths() {
    std::vector<std::pair<int, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending.swap(pending_paths);
    }
    for (const auto& [kind, path] : pending) {
        switch (kind) {
        case PendingOpen: stop_playback_and_open_file(path); break;
        case PendingAdd: add_midi_file(path); break;
        case PendingFolder: open_folder(path); break;
        case PendingExport:
            video_dlg.midi_path = path;
            begin_export_video();
            break;
        case PendingExportSave:
            if (path.empty()) unload_song();
            else {
                std::snprintf(video_dlg.params.outPath, sizeof(video_dlg.params.outPath), "%s", path.c_str());
                export_video_after_dialog();
            }
            break;
        case PendingBackgroundImage:
            std::snprintf(graphic_dlg.background_path, sizeof(graphic_dlg.background_path), "%s", path.c_str());
            break;
        default: break;
        }
    }
}

//******************************************************************************
// gamepad (MIDITrailApp::_GamePadProc): the camera reads the sticks through
// MTGamePadCtrl; the app maps the buttons to playback commands
//******************************************************************************
void LinuxDX11Context::Impl::poll_gamepad() {
    if (platform == nullptr) return;
    const std::uint32_t buttons = platform->consume_gamepad_buttons();
    if (buttons == 0 || config_manager.IsVisible()) return;
    if (buttons & (LinuxGamepadStart | LinuxGamepadA)) handle_command(IDM_PLAY);
    if (buttons & LinuxGamepadB) handle_command(IDM_STOP);
    if (buttons & (LinuxGamepadLeftShoulder | LinuxGamepadRightShoulder)) {
        if (buttons & LinuxGamepadLeftShoulder) gamepad_viewpoint = gamepad_viewpoint <= 1 ? 3 : gamepad_viewpoint - 1;
        else gamepad_viewpoint = gamepad_viewpoint >= 3 ? 1 : gamepad_viewpoint + 1;
        handle_command(gamepad_viewpoint == 1 ? IDM_RESET_VIEWPOINT
                       : gamepad_viewpoint == 2 ? IDM_VIEWPOINT2 : IDM_VIEWPOINT3);
    }
    if (buttons & LinuxGamepadLeftTrigger) handle_command(IDM_SKIP_BACK);
    if (buttons & LinuxGamepadRightTrigger) handle_command(IDM_SKIP_FORWARD);
}

//******************************************************************************
// keyboard / mouse actions from the platform layer
//******************************************************************************
void LinuxDX11Context::Impl::handle_actions(std::uint32_t actions) {
    if (actions == 0) return;
    if (actions & LinuxActionToggleMouseLook) {
        if (family != Family::None) {
            mouse_look = !mouse_look;
            camera.SetMouseCamMode(mouse_look);
            platform->set_relative_mouse_mode(mouse_look);
        }
    }
    if (actions & LinuxActionToggleAutoRoll) {
        if (family != Family::None) {
            auto_roll = !auto_roll;
            camera.SetAutoRollMode(auto_roll);
            if (auto_roll) camera.SwitchAutoRllDirecton();
        }
    }
    if (actions & LinuxActionViewpoint1) handle_command(IDM_RESET_VIEWPOINT);
    if (actions & LinuxActionViewpoint2) handle_command(IDM_VIEWPOINT2);
    if (actions & LinuxActionViewpoint3) handle_command(IDM_VIEWPOINT3);
    if (actions & LinuxActionPlayPause) handle_command(IDM_PLAY);
    if (actions & LinuxActionStop) {
        if (play_status == PlayStatus::MonitorON) handle_command(IDM_STOP_MONITORING);
        else handle_command(IDM_STOP);
    }
    if (actions & LinuxActionStopOnly) handle_command(IDM_STOP);
    if (actions & LinuxActionSkipBack) handle_command(IDM_SKIP_BACK);
    if (actions & LinuxActionSkipForward) handle_command(IDM_SKIP_FORWARD);
    if (actions & LinuxActionSpeedDown) handle_command(IDM_PLAY_SPEED_DOWN);
    if (actions & LinuxActionSpeedUp) handle_command(IDM_PLAY_SPEED_UP);
    if (actions & LinuxActionToggleMenuBar) handle_command(IDM_MENUBAR);
    if (actions & LinuxActionStartMonitoring) handle_command(IDM_START_MONITORING);
    if (actions & LinuxActionOpenFile) handle_command(IDM_OPEN_FILE);
    if (actions & LinuxActionFullscreen) handle_command(IDM_FULLSCREEN);
}

//******************************************************************************
// menu commands (MIDITrailApp::_OnMenuXXX)
//******************************************************************************
void LinuxDX11Context::Impl::handle_command(std::int32_t command_id) {
    if (command_id == 0 || exporting) return;
    const bool loaded = family != Family::None;
    auto toggle = [this](bool* flag) {
        *flag = !*flag;
        apply_visibility();
        save_view_conf();
    };
    switch (command_id) {
    case IDM_OPEN_FILE: show_open_dialog(PendingOpen); break;
    case IDM_ADD_FILE:
        if (play_status == PlayStatus::NoData || play_status == PlayStatus::Stop
            || play_status == PlayStatus::MonitorOFF) show_open_dialog(PendingAdd);
        break;
    case IDM_OPEN_FOLDER: show_open_dialog(PendingFolder); break;
    case IDM_PREVIOUS_FILE:
        if (!file_list.empty() && file_list_index > 0) {
            --file_list_index;
            open_file_from_list();
        }
        break;
    case IDM_NEXT_FILE:
        if (!file_list.empty() && file_list_index + 1 < file_list.size()) {
            ++file_list_index;
            open_file_from_list();
        }
        break;
    case IDM_FOLDER_PLAYBACK: folder_playback = !folder_playback; break;
    case IDM_EXPORT_VIDEO: show_open_dialog(PendingExport); break;
    case IDM_EXIT: platform->request_quit(); break;

    case IDM_PLAY: play_pause(); break;
    case IDM_STOP: stop(); break;
    case IDM_REPEAT: repeat = !repeat; break;
    case IDM_SKIP_BACK: skip(-skip_back_ms); break;
    case IDM_SKIP_FORWARD: skip(skip_forward_ms); break;
    case IDM_PLAY_SPEED_DOWN: if (is_playing() || play_status == PlayStatus::Stop) change_speed(-1); break;
    case IDM_PLAY_SPEED_UP: if (is_playing() || play_status == PlayStatus::Stop) change_speed(+1); break;
    case IDM_START_MONITORING: start_monitoring(); break;
    case IDM_STOP_MONITORING: stop_monitoring(); break;

    case IDM_VIEW_3DPIANOROLL: select_scene(SceneType::PianoRoll3D); break;
    case IDM_VIEW_2DPIANOROLL: select_scene(SceneType::PianoRoll2D); break;
    case IDM_VIEW_PIANOROLLRAIN: select_scene(SceneType::PianoRollRain); break;
    case IDM_VIEW_PIANOROLLRAIN2D: select_scene(SceneType::PianoRollRain2D); break;
    case IDM_VIEW_PIANOROLLRING: select_scene(SceneType::PianoRollRing); break;
    case IDM_VIEW_SINGLEKEYBOARD:
        if (!(play_status == PlayStatus::NoData || play_status == PlayStatus::Stop
              || play_status == PlayStatus::MonitorOFF)) break;
        single_keyboard = !single_keyboard;
        save_view_conf();
        if (loaded) {
            setup_scene();
            feed_tick(is_monitor() ? 0 : snapshot_tick.load(), is_monitor() ? 0 : snapshot_ms.load());
        }
        break;
    case IDM_ENABLE_PIANOKEYBOARD: toggle(&keyboard_visible); break;
    case IDM_ENABLE_RIPPLE: toggle(&ripple_visible); break;
    case IDM_ENABLE_LYRICS: toggle(&lyrics_visible); break;
    case IDM_ENABLE_PITCHBEND: toggle(&pitch_bend_enabled); break;
    case IDM_ENABLE_PITCHBEND_ALLNOTES: toggle(&pitch_bend_all_notes); break;
    case IDM_ENABLE_STARS: toggle(&stars_visible); break;
    case IDM_ENABLE_COUNTER: toggle(&dashboard_visible); break;
    case IDM_ENABLE_BACKGROUNDIMAGE: toggle(&background_image_visible); break;
    case IDM_ENABLE_TIMEINDICATOR: toggle(&time_indicator_visible); break;
    case IDM_ENABLE_GRIDBOX: toggle(&grid_visible); break;
    case IDM_RESET_VIEWPOINT:
        if (!loaded) break;
        if (scene_type == SceneType::PianoRollRing) camera.SetDefaultViewpointRing();
        else camera.SetDefaultViewpoint();
        save_viewpoint();
        break;
    case IDM_VIEWPOINT2:
    case IDM_VIEWPOINT3:
        if (loaded) camera.SetViewpointFromConf(scene_name(), command_id == IDM_VIEWPOINT2 ? 2 : 3);
        break;
    case IDM_SAVE_VIEWPOINT: if (loaded) save_viewpoint(); break;
    case IDM_AUTO_SAVE_VIEWPOINT:
        auto_save_viewpoint = !auto_save_viewpoint;
        save_view_conf();
        break;
    case IDM_AUTO_SAVE_VIEWSETTINGS:
        auto_save_view_settings = !auto_save_view_settings;
        save_view_conf();
        break;
    case IDM_MYVIEWPOINT1: case IDM_MYVIEWPOINT2: case IDM_MYVIEWPOINT3:
        restore_my_viewpoint(static_cast<unsigned long>(command_id - IDM_MYVIEWPOINT1 + 1));
        break;
    case IDM_SAVE_MYVIEWPOINT1: case IDM_SAVE_MYVIEWPOINT2: case IDM_SAVE_MYVIEWPOINT3:
        save_my_viewpoint(static_cast<unsigned long>(command_id - IDM_SAVE_MYVIEWPOINT1 + 1));
        break;
    case IDM_WINDOWSIZE: open_window_size_dialog(); break;
    case IDM_FULLSCREEN:
        fullscreen = !fullscreen;
        platform->set_fullscreen(fullscreen);
        if (!fullscreen) change_window_size();
        break;
    case IDM_MENUBAR:
        menu.set_enabled(!menu.enabled());
        if (!fullscreen) change_window_size();
        break;

    case IDM_OPTION_MIDIOUT: open_midi_out_dialog(); break;
    case IDM_OPTION_MIDIIN: open_midi_in_dialog(); break;
    case IDM_OPTION_GRAPHIC: open_graphic_dialog(); break;
    case IDM_OPTION_COLOR: open_color_dialog(); break;
    case IDM_OPTION_CONFIGMANAGER: config_manager.Toggle(); break;

    case IDM_HOWTOVIEW: open_howto_dialog(); break;
    case IDM_MANUAL: {
        char dir[MAX_PATH] = {};
        YNBaseLib::YNPathUtil::GetModuleDirPath(dir, sizeof(dir));
        const std::string url = std::string("file://") + dir + "doc/index.html";
        if (!SDL_OpenURL(url.c_str())) show_message("Manual", "Could not open " + url);
        break;
    }
    case IDM_ABOUT: about_visible = true; break;
    default: break;
    }
}

//******************************************************************************
// LinuxDX11Context
//******************************************************************************
LinuxDX11Context::LinuxDX11Context()
    : impl_(std::make_unique<Impl>()) {
}

LinuxDX11Context::~LinuxDX11Context() = default;

bool LinuxDX11Context::initialize(LinuxPlatform& platform, const LinuxCommandLine& command_line) {
    Impl& app = *impl_;
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);
    app.platform = &platform;
    app.quit_at_end = command_line.quit_at_end && command_line.play;
    app.debug_dump = command_line.debug_dump;
    app.version_string = MIDITRAIL_VERSION_STRING_X64;
    {
        // "... Mod Mod (Linux Native Build) | Based ..." (title bar and About)
        const std::size_t mod = app.version_string.find("Mod Mod");
        if (mod != std::string::npos) app.version_string.insert(mod + 7, " (Linux Native Build)");
        else app.version_string += " (Linux Native Build)";
    }
    app.title_base = "MIDITrail " + app.version_string;

    app.load_view_conf();
    app.load_graphic_conf();
    app.load_player_conf();
    app.load_midi_conf();
    app.change_window_size();

    app.renderer.SetImGuiPlatformWindow(platform.sdl_window());
    platform.get_drawable_size(&app.width, &app.height);
    const int renderer_result = app.renderer.Initialize(
        reinterpret_cast<HWND>(platform.native_window_handle()),
        static_cast<unsigned long>(app.multi_sample_type), false,
        static_cast<unsigned long>(app.super_sample));
    if (renderer_result != 0) {
        app.last_result = renderer_result;
        return false;
    }
    if (app.config_manager.Initialize() != 0) {
        app.last_result = -19;
        return false;
    }
    app.renderer.SetConfigManager11(&app.config_manager);
    app.renderer.SetImGuiDrawCallback([](void* user) { static_cast<Impl*>(user)->draw_ui(); }, impl_.get());

    // MIDI OUT ports A-F from MIDI.ini
    snd_midi_event_new(256, &app.midi_decoder);
    if (app.midi_decoder != nullptr) snd_midi_event_no_status(app.midi_decoder, 1);
    app.auto_config_midi_out();
    app.midi_conf.SetCurSection("MIDIOUT");
    for (int port = 0; port < LinuxMidiOut::kPortCount; ++port) {
        char key[8];
        std::snprintf(key, sizeof(key), "Port%c", 'A' + port);
        char device[256] = {};
        app.midi_conf.GetStr(key, device, sizeof(device), "");
        if (device[0] == '\0') continue;
        std::string error;
        if (!app.midi_out.open_port(port, device, &error)) {
            std::fprintf(stderr, "MIDI OUT %s: %s\n", key, error.c_str());
        }
    }

    app.setup_scene();
    app.start_worker();
    app.fps_last_ticks = SDL_GetTicks();
    app.update_title();

    if (!command_line.midi_path.empty()) {
        if (app.file_open_proc(command_line.midi_path) == 0 && command_line.play
            && app.play_status == Impl::PlayStatus::Stop) {
            app.play_pause();
        }
    }
    return true;
}

void LinuxDX11Context::handle_actions(LinuxPlatform&, std::uint32_t actions) {
    impl_->handle_actions(actions);
}

void LinuxDX11Context::open_dropped_file(const std::string& path) {
    if (!is_midi_file_name(base_name(path))) return;
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    impl_->pending_paths.emplace_back(Impl::PendingOpen, path);
}

bool LinuxDX11Context::render(LinuxPlatform& platform) {
    Impl& app = *impl_;
    app.process_pending_paths();
    app.poll_midi_in();
    app.poll_gamepad();

    // F2 held: 2x playback (MIDITrailApp::_OnTimer MIDITRAIL_TIMER_CHECK_KEY)
    const bool boost = platform.key_down_f2() && !ImGui::GetIO().WantCaptureKeyboard;
    if (boost != app.boost_2x) {
        app.boost_2x = boost;
        std::lock_guard<std::mutex> lock(app.worker_mutex);
        app.worker_speed = (app.play_speed_ratio / 100.0) * (boost ? 2.0 : 1.0);
    }

    std::uint32_t width = 1;
    std::uint32_t height = 1;
    platform.get_drawable_size(&width, &height);
    if (width != app.width || height != app.height) {
        const int resize_result = app.renderer.Resize(width, height);
        if (resize_result != 0) {
            app.last_result = resize_result;
            return false;
        }
        app.width = width;
        app.height = height;
    }

    if (app.playback_skipped.exchange(false)) app.reset_components_after_skip();
    if (app.playback_ended.exchange(false)) app.on_playback_end();

    // menu bar: hidden in full screen / when disabled, shown while the mouse is at the top edge
    {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        const bool hover = !app.mouse_look && SDL_GetMouseFocus() == platform.sdl_window()
            && (mouse_y <= 5.0f || (app.menu.visible() && (mouse_y <= ImGui::GetFrameHeight()
                                                           || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))));
        app.menu.set_visible((app.menu.enabled() && !app.fullscreen) || hover);
    }

    if (app.family != Impl::Family::None) {
        const bool live = app.is_monitor();
        const unsigned long tick = live ? 0 : app.snapshot_tick.load();
        const unsigned long play_ms = live ? 0 : app.snapshot_ms.load();
        if (!live && app.sequence) {
            // pitch bend + tempo/bar/beat at the tick (fed by the sequencer
            // message queue on Windows)
            if (tick < app.bend_last_tick) {
                app.bend_timeline.Reset();
                app.pitch_bend.Reset();
            }
            app.bend_timeline.Apply(tick, &app.pitch_bend);
            app.bend_last_tick = tick;
            MTPlaybackState state;
            app.tempo_map.GetStateAtTick(tick, &state);
            if (state.tempoBPM > 0) app.cur_song_bpm = state.tempoBPM;
            app.dashboard.SetTempo(state.tempoBPM);
            app.dashboard.SetCurBar(static_cast<long>(state.barNo));
            app.dashboard.SetBeat(static_cast<int>(state.beatNum), static_cast<int>(state.beatDenom));
        }
        app.feed_tick(tick, play_ms);
    }

    // Config Manager: suspend camera input, restore mouse look when it closes
    {
        const bool cfg_visible = app.config_manager.IsVisible();
        app.camera.SetInputEnabled(!cfg_visible && !ImGui::GetIO().WantCaptureKeyboard);
        if (cfg_visible && !app.cfg_was_visible) {
            app.mouse_look_before_cfg = app.mouse_look;
            if (app.mouse_look) {
                app.mouse_look = false;
                app.camera.SetMouseCamMode(false);
                platform.set_relative_mouse_mode(false);
            }
        } else if (!cfg_visible && app.cfg_was_visible && app.mouse_look_before_cfg
                   && app.family != Impl::Family::None) {
            app.mouse_look = true;
            app.camera.SetMouseCamMode(true);
            platform.set_relative_mouse_mode(true);
        }
        app.cfg_was_visible = cfg_visible;
    }

    const int render_result = app.renderer.RenderScene(nullptr);
    if (app.config_manager.ConsumeApplyRequest() && app.family != Impl::Family::None) {
        app.setup_scene();
    }
    app.handle_command(app.menu.consume_action());

    // FPS in the title bar (once a second)
    ++app.fps_frames;
    const std::uint64_t now = SDL_GetTicks();
    if (now - app.fps_last_ticks >= 1000) {
        app.fps = app.fps_frames * 1000.0 / static_cast<double>(now - app.fps_last_ticks);
        app.fps_frames = 0;
        app.fps_last_ticks = now;
        app.update_title();
    }

    app.last_result = render_result;
    return render_result == 0;
}

long LinuxDX11Context::last_result() const {
    return impl_->last_result;
}

} // namespace miditrail::linux_native
