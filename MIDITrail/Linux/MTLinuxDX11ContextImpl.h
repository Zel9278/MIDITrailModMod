#pragma once

// Private state of LinuxDX11Context. It mirrors MIDITrailApp (the Windows app):
// the same play-status machine, the same single set of DX11 scene components set
// up by setup_scene() (= _SetupDX11Scene), and the same conf files. Only the
// platform layer differs (SDL3 window/input, ImGui dialogs, ALSA/KDMAPI MIDI).

#include "MTLinuxCompat.h"
#include "../../YNBaseLib/YNConfFile.h"
#include "../../YNBaseLib/YNPathUtil.h"
#include "MTLinuxDX11Context.h"
#include "MTLinuxPlatform.h"
#include "MTLinuxMenu.h"
#include "MTLinuxMidiOut.h"
#include "MTNativeAlsaMidi.h"

#include "../DXNoteBox11.h"
#include "../DXNoteRain11.h"
#include "../DXNoteBoxRing11.h"
#include "../DXRenderer11.h"
#include "../MTFirstPersonCam.h"
#include "../MTKeyboard11.h"
#include "../MTGridBox11.h"
#include "../MTGridRing11.h"
#include "../MTStars11.h"
#include "../MTTimeIndicator11.h"
#include "../MTTimeIndicatorRing11.h"
#include "../MTNoteRipple11.h"
#include "../MTPictBoardRing11.h"
#include "../MTKeyboardRain11.h"
#include "../MTNoteLyrics11.h"
#include "../MTNoteBoxLive11.h"
#include "../MTNoteRainLive11.h"
#include "../MTDashboard11.h"
#include "../MTBackgroundImage11.h"
#include "../MTNotePitchBend.h"
#include "../MTConfigManager11.h"
#include "../MTColorConf.h"
#include "../MTColorPalette.h"
#include "../MTVideoExporter.h"
#include "../../YNBaseLib/YNConfFile.h"
#include "../../YNBaseLib/YNPathUtil.h"
#include "../../SMIDILib/SMSeqData.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SDL_Gamepad;

namespace miditrail::linux_native {

struct LinuxDX11Context::Impl {
    //--------------------------------------------------------------------------
    // state (MIDITrailApp::PlayStatus / SceneType)
    //--------------------------------------------------------------------------
    enum class PlayStatus { NoData, Stop, Play, Pause, MonitorOFF, MonitorON };
    enum class SceneType { PianoRoll3D, PianoRoll2D, PianoRollRain, PianoRollRain2D, PianoRollRing };
    enum class Family { None, Box, Rain, Ring };

    LinuxPlatform* platform = nullptr;
    PlayStatus play_status = PlayStatus::NoData;
    SceneType scene_type = SceneType::PianoRoll3D;
    Family family = Family::None;
    long last_result = 0;

    //--------------------------------------------------------------------------
    // renderer + scene components (one set, created for the active family)
    //--------------------------------------------------------------------------
    DXRenderer11 renderer;
    std::unique_ptr<SMIDILib::SMSeqData> sequence;
    std::string song_path;
    std::string dash_file_name;
    DXNoteBox11 note_box;
    DXNoteRain11 note_rain;
    DXNoteBoxRing11 note_ring;
    MTNoteBoxLive11 note_box_live;
    MTNoteRainLive11 note_rain_live;
    MTKeyboard11 keyboard;
    MTKeyboardRain11 keyboard_rain;
    MTGridBox11 grid_box;
    MTGridRing11 grid_ring;
    MTStars11 stars;
    MTTimeIndicator11 time_indicator;
    MTTimeIndicatorRing11 time_indicator_ring;
    MTNoteRipple11 ripple;
    MTPictBoardRing11 pict_board_ring;
    MTNoteLyrics11 lyrics;
    MTDashboard11 dashboard;
    MTBackgroundImage11 background_image;
    MTNotePitchBend pitch_bend;
    MTFirstPersonCam camera;
    MTConfigManager11 config_manager;
    LinuxMenu menu;

    MTTempoMap tempo_map;
    MTPitchBendTimeline bend_timeline;
    unsigned long bend_last_tick = 0;
    unsigned long cur_song_bpm = 120;
    unsigned long live_note_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    //--------------------------------------------------------------------------
    // view settings (View.ini) / graphic (Graphic.ini) / MIDI (MIDI.ini)
    //--------------------------------------------------------------------------
    YNBaseLib::YNConfFile view_conf;
    YNBaseLib::YNConfFile graphic_conf;
    YNBaseLib::YNConfFile midi_conf;
    std::string user_conf_dir;
    bool keyboard_visible = true;
    bool grid_visible = true;
    bool stars_visible = true;
    bool time_indicator_visible = true;
    bool ripple_visible = true;
    bool lyrics_visible = true;
    bool single_keyboard = true;
    bool dashboard_visible = true;
    bool pitch_bend_enabled = true;
    bool pitch_bend_all_notes = false;
    bool background_image_visible = true;
    bool auto_save_viewpoint = true;
    bool auto_save_view_settings = false;
    bool mouse_look = false;
    bool auto_roll = false;
    bool mouse_look_before_cfg = false;
    bool cfg_was_visible = false;
    bool has_prev_view = false;
    SceneType prev_view_scene = SceneType::PianoRoll3D;
    bool prev_view_live = false;
    int multi_sample_type = 0;
    int super_sample = 1;
    bool fullscreen = false;
    int window_width = 0;
    int window_height = 0;
    bool apply_to_view_area = false;

    //--------------------------------------------------------------------------
    // player control (conf/Player.ini)
    //--------------------------------------------------------------------------
    int auto_playback_after_open = 0;
    bool show_file_name = false;
    int skip_back_ms = 10000;
    int skip_forward_ms = 10000;
    int moving_time_span_ms = 400;
    unsigned long speed_step_percent = 1;
    unsigned long max_speed_percent = 400;
    unsigned long play_speed_ratio = 100;
    bool repeat = false;
    bool rewind_on_play = false;
    double position_ms = 0.0;
    bool boost_2x = false;

    //--------------------------------------------------------------------------
    // file list (Open Folder / Previous / Next / Folder Playback)
    //--------------------------------------------------------------------------
    std::vector<std::string> file_list;
    std::size_t file_list_index = 0;
    bool folder_playback = false;
    std::mutex pending_mutex;
    std::vector<std::pair<int, std::string>> pending_paths;   // (kind, path) from SDL dialogs / drops
    enum PendingKind { PendingOpen = 0, PendingAdd, PendingFolder, PendingExport, PendingExportSave,
                       PendingBackgroundImage };

    //--------------------------------------------------------------------------
    // command line
    //--------------------------------------------------------------------------
    bool quit_at_end = false;
    bool debug_dump = false;

    //--------------------------------------------------------------------------
    // MIDI OUT (ports A-F) / MIDI IN (port A, live monitor)
    //--------------------------------------------------------------------------
    struct MidiEvent {
        unsigned long tick;
        unsigned char port;
        unsigned char size;        // bytes in data (<= 3), or 0 for a SysEx stored in sysex
        unsigned char data[3];
        std::uint32_t sysex = 0;   // index into sysex_data when size == 0
    };
    struct SoundingNote {
        unsigned long start;
        unsigned long end;
        unsigned char port, ch, note, velocity;
    };
    LinuxMidiOut midi_out;
    std::vector<MidiEvent> midi_events;
    std::vector<std::vector<unsigned char>> sysex_data;
    std::vector<SoundingNote> midi_notes;
    std::size_t midi_event_cursor = 0;
    unsigned long midi_output_tick = 0;
    std::recursive_mutex midi_output_mutex;
    std::atomic<bool> midi_output_sync_pending{true};

    NativeAlsaMidi midi_in;
    std::string midi_in_device;
    bool midi_thru = true;
    unsigned char live_rpn_msb[16] = {};
    unsigned char live_rpn_lsb[16] = {};
    unsigned char live_bend_sens[16] = {};
    snd_midi_event_t* midi_decoder = nullptr;

    //--------------------------------------------------------------------------
    // playback worker (replaces SMSequencer): advances the play position on a
    // steady clock, streams MIDI OUT and publishes the tick for the renderer.
    //--------------------------------------------------------------------------
    std::mutex worker_mutex;
    std::condition_variable worker_cv;
    std::thread worker;
    bool worker_stop = false;
    bool worker_playing = false;
    bool worker_sync_pending = true;
    double worker_position_ms = 0.0;
    double worker_speed = 1.0;
    std::chrono::steady_clock::time_point worker_clock = {};
    bool worker_slide = false;
    bool worker_slide_first = false;
    unsigned long worker_slide_from = 0;
    unsigned long worker_slide_to = 0;
    std::chrono::steady_clock::time_point worker_slide_start = {};
    std::atomic<unsigned long> snapshot_tick{0};
    std::atomic<unsigned long> snapshot_ms{0};
    std::atomic<bool> playback_ended{false};
    std::atomic<bool> playback_skipped{false};

    //--------------------------------------------------------------------------
    // dialogs (ImGui windows replacing the Win32 dialogs)
    //--------------------------------------------------------------------------
    struct ColorDialog {
        bool visible = false;
        std::unique_ptr<MTColorConf> conf;
        std::string scene;
        unsigned long palette_no = 0;
        MTColorPalette palette;
        bool editing = false;
        int gradation_start = 0;
        int gradation_end = 15;
        bool export_visible = false;
        bool import_visible = false;
        std::string export_text;
        std::vector<char> import_text = std::vector<char>(4096, 0);
        std::string import_error;
    } color_dlg;
    struct GraphicDialog {
        bool visible = false;
        int msaa = 0;
        int ssaa = 1;
        char background_path[MAX_PATH] = {};
        int quarter_note_mag = 100;
        bool supported[17] = {};
    } graphic_dlg;
    struct MidiDialog {
        bool out_visible = false;
        bool in_visible = false;
        std::vector<std::string> out_devices;
        std::vector<AlsaMidiPort> in_devices;
        std::string out_selected[LinuxMidiOut::kPortCount];
        std::string in_selected;
        bool thru = true;
        std::string error;
    } midi_dlg;
    struct WindowSizeDialog {
        bool visible = false;
        std::vector<std::pair<int, int>> sizes;
        int selected = -1;
        int width = 800;
        int height = 600;
        bool apply_to_view = false;
    } window_dlg;
    struct VideoDialog {
        bool visible = false;
        MTVideoExportParams params = {};
        std::string midi_path;
    } video_dlg;
    struct HowToViewDialog {
        bool visible = false;
        int page = 0;
        ID3D11ShaderResourceView* pages[3] = {};
        int page_w[3] = {};
        int page_h[3] = {};
    } howto_dlg;
    bool about_visible = false;
    struct MessageBoxState {
        bool pending = false;
        std::string title;
        std::string text;
    } message;
    bool exporting = false;

    //--------------------------------------------------------------------------
    // title bar FPS (MIDITrailApp::_UpdateFPS)
    //--------------------------------------------------------------------------
    std::uint64_t fps_last_ticks = 0;
    unsigned long fps_frames = 0;
    double fps = 0.0;
    std::string title_base;
    std::string version_string;

    //--------------------------------------------------------------------------
    // gamepad (MIDITrailApp::_GamePadProc; camera uses MTGamePadCtrl)
    //--------------------------------------------------------------------------
    unsigned long gamepad_viewpoint = 1;

    //==========================================================================
    // core (MTLinuxDX11Context.cpp)
    //==========================================================================
    Impl();
    ~Impl();
    const char* scene_name() const;
    static const char* scene_name_of(SceneType type);
    bool is_monitor() const { return play_status == PlayStatus::MonitorON || play_status == PlayStatus::MonitorOFF; }
    bool is_playing() const { return play_status == PlayStatus::Play || play_status == PlayStatus::Pause; }
    std::string conf_path(const char* name) const;
    void load_view_conf();
    void save_view_conf();
    void load_graphic_conf();
    void load_player_conf();
    void load_midi_conf();
    void load_window_size_conf();
    void update_title();

    int setup_scene();
    void detach_scene();
    void apply_visibility();
    void apply_scene_colors(const char* conf_scene);
    void feed_tick(unsigned long tick, unsigned long play_ms);
    void reset_components_after_skip();

    int save_viewpoint();
    void reset_viewpoint_to_saved();
    void restore_saved_viewpoint();
    void save_my_viewpoint(unsigned long slot);
    void restore_my_viewpoint(unsigned long slot);

    int load_midi_file(const std::string& path);
    int add_midi_file(const std::string& path);
    int file_open_proc(const std::string& path);
    int stop_playback_and_open_file(const std::string& path);
    int open_folder(const std::string& path);
    int open_file_from_list();
    void unload_song();

    int play_pause();
    void stop();
    void skip(int delta_ms);
    void change_speed(int direction);
    void on_playback_end();
    int start_monitoring();
    void stop_monitoring();
    void select_scene(SceneType type);
    int change_window_size();
    int rebuild_renderer();

    void build_midi_events();
    void midi_output_reset(unsigned long tick);
    void midi_output_until(unsigned long tick);
    void notify_worker(bool playing, bool sync, double position_ms, bool slide = false,
                       unsigned long slide_from = 0, unsigned long slide_to = 0);
    void start_worker();
    void stop_worker();
    void worker_loop();
    void poll_midi_in();
    void auto_config_midi_out();

    void handle_command(std::int32_t command_id);
    void handle_actions(std::uint32_t actions);
    void show_open_dialog(int kind);
    void process_pending_paths();
    void poll_gamepad();
    void show_message(const char* title, const std::string& text);
    void show_error(const char* context);

    //==========================================================================
    // dialogs (MTLinuxDialogs.cpp)
    //==========================================================================
    void draw_ui();
    void draw_color_dialog();
    void draw_color_export_import();
    void draw_graphic_dialog();
    void draw_midi_out_dialog();
    void draw_midi_in_dialog();
    void draw_window_size_dialog();
    void draw_video_dialog();
    void draw_howto_dialog();
    void draw_about_dialog();
    void draw_message_boxes();
    void open_color_dialog();
    void open_graphic_dialog();
    void open_midi_out_dialog();
    void open_midi_in_dialog();
    void open_window_size_dialog();
    void open_howto_dialog();
    void release_howto_textures();
    void disp_how_to_view_first_time();

    //==========================================================================
    // video export (MTLinuxVideoExport.cpp)
    //==========================================================================
    void begin_export_video();
    void export_video_after_dialog();
    int export_video(const MTVideoExportParams& params, std::string* error_tail);
};

} // namespace miditrail::linux_native
