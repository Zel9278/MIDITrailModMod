// Offline video export (MIDITrailApp::_OnMenuExportVideo / _ExportVideo):
// renders every frame off-screen on a fixed-FPS timeline and streams raw BGRA
// frames to ffmpeg through a pipe (MTFFmpegPipe).

#include "MTLinuxDX11ContextImpl.h"

#include "../MTConfFile.h"

#include <cstdio>
#include <cstring>

namespace miditrail::linux_native {

// 1) the MIDI file was picked: load it, then show the settings dialog
void LinuxDX11Context::Impl::begin_export_video() {
    if (exporting) return;
    if (stop_playback_and_open_file(video_dlg.midi_path) != 0 || family == Family::None) {
        show_message("Export Video", "Could not build a scene for this MIDI.");
        return;
    }
    if (play_status == PlayStatus::Play) {   // AutoPlaybackAfterOpenFile
        notify_worker(false, true, 0.0);
        play_status = PlayStatus::Stop;
    }

    // defaults: window size (even), 60 fps, H.264 CPU; then conf/Video.ini
    MTVideoExportParams& params = video_dlg.params;
    params = {};
    params.codec = MTVC_H264_CPU;
    params.width = static_cast<int>(width) & ~1;
    params.height = static_cast<int>(height) & ~1;
    if (params.width < 16) params.width = 1280;
    if (params.height < 16) params.height = 720;
    params.fps = 60;
    MTConfFile conf;
    if (conf.Initialize("Video") == 0 && conf.SetCurSection("Video") == 0) {
        int value = static_cast<int>(params.codec);
        conf.GetInt("Codec", &value, value);
        if (value >= 0 && value <= static_cast<int>(MTVC_HEVC_AMF)) params.codec = static_cast<MTVideoCodec>(value);
        conf.GetInt("Width", &params.width, params.width);
        conf.GetInt("Height", &params.height, params.height);
        conf.GetInt("Fps", &params.fps, params.fps);
        conf.GetInt("Quality", &params.quality, params.quality);
        value = 0;
        conf.GetInt("Transparent", &value, 0);
        params.transparent = value != 0;
        value = 0;
        conf.GetInt("Equirect360", &value, 0);
        params.equirect360 = value != 0;
    }
    video_dlg.visible = true;
}

// 2) settings accepted and the output path chosen: export, then back to the title
void LinuxDX11Context::Impl::export_video_after_dialog() {
    std::string error_tail;
    const int result = export_video(video_dlg.params, &error_tail);
    unload_song();
    if (result == 0) {
        show_message("Export Video", "Video export finished.");
    } else if (!error_tail.empty()) {
        show_message("Export Video", "Video export failed.\n\nffmpeg:\n" + error_tail);
    } else {
        show_message("Export Video", "Video export failed. Make sure ffmpeg is installed and on PATH.");
    }
}

int LinuxDX11Context::Impl::export_video(const MTVideoExportParams& params, std::string* error_tail) {
    if (renderer.GetDevice() == nullptr || family == Family::None || !sequence) return -1;
    if (params.width <= 0 || params.height <= 0 || params.fps <= 0) return -1;

    notify_worker(false, true, 0.0);
    play_status = PlayStatus::Stop;

    MTTempoMap tempo;
    if (tempo.Build(sequence.get()) != 0) return -1;
    MTPitchBendTimeline bends;
    bends.Build(sequence.get());
    pitch_bend.Reset();
    reset_components_after_skip();

    unsigned long end_tick = 0;
    if (family == Family::Box) end_tick = note_box.GetMaxEndTick();
    else if (family == Family::Rain) end_tick = note_rain.GetMaxEndTick();
    else if (family == Family::Ring) end_tick = note_ring.GetMaxEndTick();
    const double end_ms = end_tick > 0 ? tempo.TickToMs(end_tick) : static_cast<double>(sequence->GetTotalPlayTime());
    const double tail_ms = 700.0;
    const int fps = params.fps;
    int total_frames = static_cast<int>((end_ms + tail_ms) * fps / 1000.0) + 1;
    if (total_frames < 1) total_frames = 1;

    int ss = 1;
    int ow = params.width;
    int oh = params.height;
    if (params.equirect360) {
        oh = ow / 2;
        if (renderer.BeginOffscreen(ow, oh, true) != 0) return -1;
    } else {
        ss = (params.width <= 1920 && params.height <= 1080) ? 2 : 1;
        ow = params.width * ss;
        oh = params.height * ss;
        int begin = renderer.BeginOffscreen(ow, oh);
        if (begin != 0 && ss > 1) {
            ss = 1;
            ow = params.width;
            oh = params.height;
            begin = renderer.BeginOffscreen(ow, oh);
        }
        if (begin != 0) return -1;
    }

    MTVideoExportParams effective = params;
    effective.superSample = ss;
    // overlay text: render it ss x larger so it keeps its on-screen size after the downscale
    renderer.SetOffscreenUiScale(static_cast<float>(ss));
    if (params.equirect360) {
        effective.width = ow;
        effective.height = oh;
    }
    MTFFmpegPipe pipe;
    if (pipe.Open(effective) != 0) {
        renderer.EndOffscreen();
        renderer.SetOffscreenUiScale(1.0f);
        return -1;
    }

    const int row_bytes = ow * 4;
    int result = 0;
    exporting = true;
    for (int frame = 0; frame < total_frames; ++frame) {
        const double ms = static_cast<double>(frame) * 1000.0 / fps;
        const unsigned long tick = tempo.MsToTick(ms);
        feed_tick(tick, static_cast<unsigned long>(ms + 0.5));
        bends.Apply(tick, &pitch_bend);
        MTPlaybackState state;
        tempo.GetStateAtTick(tick, &state);
        if (state.tempoBPM > 0) cur_song_bpm = state.tempoBPM;
        dashboard.SetTempo(state.tempoBPM);
        dashboard.SetCurBar(static_cast<long>(state.barNo));
        dashboard.SetBeat(static_cast<int>(state.beatNum), static_cast<int>(state.beatDenom));

        // render(f) || readback(f-1) || encode(f-2)
        if (renderer.RenderOffscreenFrame(params.transparent, frame & 1) != 0) { result = -1; break; }
        if (frame >= 1) {
            const int slot = pipe.AcquireBuffer();
            if (slot < 0) { result = -1; break; }
            if (renderer.ReadOffscreenBGRA((frame - 1) & 1, pipe.BufferPtr(slot), row_bytes) != 0) { result = -1; break; }
            pipe.SubmitBuffer(slot);
        }
        if ((frame % 8) == 0) {
            char text[128];
            std::snprintf(text, sizeof(text), "Exporting video...  %d / %d", frame + 1, total_frames);
            renderer.DrawLoadingScreen(text, static_cast<float>(frame) / static_cast<float>(total_frames));
            if (platform != nullptr && !platform->poll_events()) { result = -1; break; }
        }
    }
    if (result == 0) {
        const int slot = pipe.AcquireBuffer();
        if (slot >= 0 && renderer.ReadOffscreenBGRA((total_frames - 1) & 1, pipe.BufferPtr(slot), row_bytes) == 0) {
            pipe.SubmitBuffer(slot);
        }
    }
    exporting = false;

    const int code = pipe.Close();
    renderer.EndOffscreen();
    renderer.SetOffscreenUiScale(1.0f);
    if (result == 0 && code != 0) result = -1;
    if (result != 0 && error_tail != nullptr) {
        char tail[1600] = {};
        if (pipe.GetErrorTail(tail, sizeof(tail))) *error_tail = tail;
    }
    return result;
}

} // namespace miditrail::linux_native
