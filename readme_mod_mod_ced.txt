MIDITrail DirectX 11 移植・ライブモニタ全シーン対応・360度動画出力 ほか

MIDITrail 1.3.1 Mod Mod ced_20260621
GitHub: https://github.com/Zel9278/MIDITrailModMod
（yossiepon 版 mod をベースに改造）

────────────────────────────────────────────────────────
ビルド方法 / Build

ImGui は同梱せずパッケージから取得します（どちらの方法でも自動）。

■ xmake  ( https://xmake.io )
    xmake f -p windows -a x64 -m release
    xmake
  → 出力: build/windows/x64/release/MIDITrail.exe
    ImGui は xrepo から自動取得します。

■ MSBuild + vcpkg  ( https://github.com/microsoft/vcpkg )
    1. vcpkg を用意し、一度だけ:  vcpkg integrate install
    2. MIDITrail.sln を Visual Studio で開いてビルド（構成: Release / x64）
       またはコマンドライン:
       MSBuild MIDITrail.sln -t:MIDITrail -p:Configuration=Release -p:Platform=x64
  → 出力: x64/Release/MIDITrail.exe
    ルートの vcpkg.json により ImGui を自動インストールします（静的リンク）。

In short: install xmake ( https://xmake.io ) and run `xmake`, OR install vcpkg
( https://github.com/microsoft/vcpkg ), run `vcpkg integrate install` once, and
build MIDITrail.sln. ImGui is fetched from the package manager automatically.

────────────────────────────────────────────────────────
改造点 20260623：

・[FIX] 鍵盤アニメーションを DX9 と同等に修正
　→押下を先読みし、音が鳴る瞬間にちょうど押し切る（KeyDownDuration）。
　　離鍵は KeyUpDuration でゆっくり戻す。テンポ・再生速度に追従
　→色は押し切った時だけ付与（音の開始でパッと付き、終了で消える）
・[FIX] 鍵盤の押下色を ini の [PianoKeyboard] ActiveKeyColor 設定に対応
　→専用パレット（Ch-NN-ActiveKeyColor）＋ActiveKeyColorType を反映（従来はノート色固定）
　→色はノートオンの瞬間に即フル色で付与（フェードなし）
　→CHANNELTRACK（トラック別色）モード時はトラック色を維持
・[FIX] アクティブノートに [ActiveNote] EmissiveRGBA を反映（DX9 の emissive 加算）
・[NEW] ロード画面を改善（進捗バーに％表示、全体を 0→100％で単調表示）
　→重いノートフィールド構築中も件数とバーが動くように進捗を配線
　→「Reading MIDI file」がトラック毎にリセットしていた問題を修正（通し表示）
・[FIX] 巨大ファイル（32bit 上限＝約42.9億イベント／約21億ノート 超）読み込み時の
　　データ破壊・誤カウントを防止
　→上限到達で安全に打ち切り、「読めた分を表示しますか？」を Yes/No で確認
・[FIX] 再生/モニタリング中でないのに左クリックでマウスがグラブされる不具合を修正
　→マウスカメラのトグルを再生/一時停止/モニタ中のみに制限。停止・曲アンロード時は
　　グラブを自動解除（カーソルが戻る）
・[FIX] シーク（1／2 キー）を DX9 と同等の滑らかなスクロールへ修正
　→移動先まで MovingTimeSpanInMsec かけてスライド（瞬間移動を解消）。
　　本家の _SlidePlaybackTime をそのまま使う形に
・[FIX] 再生終了後にビューが先頭へ戻ってしまう不具合を修正
　→自然終了は末尾に残す（DX9 と同じ）。停止ボタン／ファイル読み込み時のみ
　　先頭へ巻き戻す（自然終了は次回再生時に巻き戻る）
・[FIX] 再生途中で停止して別 MIDI を読み込むと変な位置で表示される不具合を修正
・[FIX] ダッシュボードの文字色を DX9 と同等に修正（[Color] CaptionRGBA を反映）
・[FIX] 再生速度変更（4／5 キー）時に「SPEED:NNN%」をダッシュボードへ表示

────────────────────────────────────────────────────────
Mod 20260623:

*[FIX] Keyboard key animation now matches DX9: a key presses down ahead of the
       note so it is fully pressed exactly when the note sounds (KeyDownDuration)
       and releases over KeyUpDuration after note-off (follows tempo / play
       speed). The note colour is applied only while fully pressed - it snaps on
       at the note onset and off at the note end.
*[FIX] Pressed-key colour now honours the [PianoKeyboard] ActiveKeyColor settings
       (the dedicated Ch-NN-ActiveKeyColor palette + ActiveKeyColorType) instead
       of reusing the note colour. The colour appears at full immediately on
       note-on (no fade-in). CHANNELTRACK (per-track colour) mode keeps the track
       colours on the keys.
*[FIX] Active notes now honour [ActiveNote] EmissiveRGBA (DX9's active-note
       emissive), added on top of the existing white-flash.
*[NEW] Reworked the loading screen: the progress bar shows a percentage and now
       climbs monotonically 0->100%. The (long) note-field build reports live
       progress so the bar/count keep moving for black MIDI, and "Reading MIDI
       file" no longer restarts on every track (whole-file progress).
*[FIX] Guard against the 32-bit item limit (~4.29 billion events / ~2.1 billion
       notes): loading a file past it used to silently wrap and corrupt the data
       (wrong note count). It now stops safely at the limit and asks whether to
       display the portion that was loaded (Yes/No).
*[FIX] The mouse was grabbed (cursor hidden + clipped) on a left click even when
       not playing/monitoring, because the DX11 scene is always NULL. Mouse-look
       now toggles only while playing/paused/monitoring, and the grab is released
       on stop / when the song is unloaded.
*[FIX] Seeking (keys 1/2) now scrolls smoothly to the target over
       MovingTimeSpanInMsec instead of teleporting - it reuses DX9's own
       _SlidePlaybackTime slide rather than an approximation.
*[FIX] After a song finishes the view now stays at the end (DX9 behaviour); it
       rewinds to the start only on the Stop button or when loading a file (a
       natural end rewinds on the next Play).
*[FIX] Loading another MIDI after stopping mid-playback no longer shows the new
       song at a wrong scroll position.
*[FIX] Dashboard text colour now matches DX9 (reads [Color] CaptionRGBA instead
       of forcing solid white).
*[FIX] Changing the playback speed (keys 4/5) now shows "SPEED:NNN%" on the
       dashboard.

────────────────────────────────────────────────────────
改造点 20260622-2：

・[NEW] メニュー「View > Auto save viewpoint」を復活（ON/OFF 切替・状態を保存）
　→旧バージョンにあった自動視点保存をメニューから切り替え可能に
・[FIX] DX11 のライティングを DX9 と同等の 2 灯モデルへ修正（鍵盤等が暗く／
　　灰色がかって見える問題を解消）
　→DX9 の 3D シーンは対向 2 灯（diffuse 1.2）。1 灯だと光と逆向きの面が
　　アンビエントのみで暗くなっていたため、対向フィルライトを追加

────────────────────────────────────────────────────────
Mod 20260622-2:

*[NEW] Restored the "View > Auto save viewpoint" menu item (toggle on/off, the
       state is saved). Brings back the old auto-save-viewpoint as a menu toggle.
*[FIX] Matched the DX11 lighting to DX9's two-light model (fixes the keyboard and
       other meshes looking darker / greyer than DX9). DX9's 3D scene used two
       opposing lights (diffuse 1.2); with a single light, faces turned away from
       it were lit by ambient only, so an opposing fill light was added.

────────────────────────────────────────────────────────
改造点 20260622：

・[FIX] 次の MIDI に切り替えた後、スキップするまで波紋が表示されない不具合を修正
・[FIX] MIDI 再生後にモニタリングへ移行するとライブノートが表示されない不具合を修正
・[NEW] モニタリング（ライブ）でも波紋を表示（リアルタイムのノートオン駆動）

────────────────────────────────────────────────────────
Mod 20260622:

*[FIX] Fixed ripples not loading after switching to the next MIDI (until you skip
       back/forward).
*[FIX] Fixed live notes not showing when entering monitoring after playing a MIDI.
*[NEW] Ripples now show in monitoring (live), driven by real-time note-ons.

────────────────────────────────────────────────────────
改造点 20260621：

・[NEW] 描画エンジンを Direct3D 9（固定機能＋d3dx9）から Direct3D 11 へ全面移植
　→全シーン（PianoRoll 2D/3D/Rain/Rain2D/Ring）をノート・鍵盤・波紋・歌詞・
　　グリッド・タイムインジケータ・ボード・背景画像・星・ダッシュボードまで再実装
　→ノートは GPU インスタンシング描画。Black MIDI（数百万ノート）でも高速
・[NEW] ImGui を Direct3D 11 バックエンドへ移行
・[FIX] DXSDK (June 2010) / d3dx9 への依存を排除し、最新 Windows SDK のみでビルド可能に

・[NEW] オフライン動画出力（ffmpeg へ直接パイプ）
　→コーデック選択、解像度・FPS・品質指定、透過(alpha)出力に対応
・[NEW] 360 度動画出力（エクイレクタングラー 2:1、YouTube 用）
　→現在の視点位置・向きを中心にキューブマップ 6 面を描画して変換
　→出力ダイアログの「360 equirectangular」チェックで有効化
　　※YouTube に 360 と認識させるには球面メタデータの注入が別途必要

・[NEW] ライブモニタ（リアルタイム MIDI 入力）を全シーンで動的描画
　→PianoRoll 2D/3D：流れるノートボックス＋鍵盤反応
　→Rain/Rain2D：落下ノート＋鍵盤反応
　→Ring：円形ノート＋テクスチャボード（Ring に鍵盤は無し）
・[NEW] ライブと再生で設定・視点を完全分離
　→ライブは各シーンの *Live.ini と Viewpoint-*Live セクションを参照
・[NEW] 歌詞（Lyrics メタイベント）表示を DX11 へ移植

・[NEW] ピッチベンド対応を拡充（鍵盤・ノートの音程シフト、チャンネル全体/発音中のみ）
・[NEW] チャンネル×トラック配色、波紋のアンチエイリアス 等の描画調整
・[CHG] ソースコードを Shift-JIS から UTF-8(BOM) へ変換（GitHub 上での文字化け解消）
・[CHG] 配布リポジトリから個人用 conf/data を除外し、動画設定を Video.ini に統一
・[NEW] xmake ビルドを追加（MSBuild ソリューションに加えて）
・[CHG] ImGui をパッケージ管理に移行し最新版(1.92)へ更新。in-tree のソースを廃止し、
　xmake は xrepo、MSBuild は vcpkg(vcpkg.json) から取得（共に静的リンク）

────────────────────────────────────────────────────────
Mod 20260621:

*[NEW] Full port of the renderer from Direct3D 9 (fixed-function + d3dx9) to
       Direct3D 11. Every scene (PianoRoll 2D/3D/Rain/Rain2D/Ring) is
       reimplemented: notes, keyboards, ripple, lyrics, grid, time indicator,
       board, background image, stars and dashboard. Notes use GPU instancing
       and stay fast even for Black MIDI (millions of notes).
*[NEW] ImGui moved to the Direct3D 11 backend.
*[FIX] Dropped the DXSDK (June 2010) / d3dx9 dependency; builds with the modern
       Windows SDK only.

*[NEW] Offline video export (piped straight to ffmpeg): codec selection,
       resolution / FPS / quality, and transparent (alpha) output.
*[NEW] 360-degree video export (equirectangular 2:1, for YouTube). Renders the
       scene into a 6-face cubemap from the current viewpoint, then remaps it so
       the panorama centre is the current view direction. Enable via the
       "360 equirectangular" checkbox in the export dialog. (Spherical metadata
       still has to be injected separately for YouTube to detect it as 360.)

*[NEW] Live monitor (real-time MIDI input) now draws in every scene:
       2D/3D = flowing note boxes + reacting keyboard; Rain/Rain2D = falling
       notes + reacting keyboard; Ring = circular notes + textured board
       (Ring has no piano keyboard).
*[NEW] Live and playback are fully separated: live reads each scene's *Live.ini
       and its own Viewpoint-*Live section.
*[NEW] Ported the lyrics (Lyrics meta event) display to DX11.

*[NEW] Expanded pitch-bend support (keyboard + note pitch shift, whole-channel
       or sounding-notes-only).
*[NEW] Channel x track colouring, ripple anti-aliasing and other rendering
       tweaks.
*[CHG] Converted the source from Shift-JIS to UTF-8 (BOM) so it is not garbled
       on GitHub.
*[CHG] Removed personal conf/data from the published repo and unified the video
       settings into Video.ini.
*[NEW] Added an xmake build (xmake.lua) alongside the MSBuild solution.
*[CHG] ImGui is now package-managed and upgraded to the latest (1.92): the
       in-tree sources are dropped and pulled from xrepo (xmake) / vcpkg
       (MSBuild, vcpkg.json); both link it statically.
