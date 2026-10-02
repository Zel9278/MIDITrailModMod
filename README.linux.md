# MIDITrail Mod Mod Linux port

Linux版は `MIDITrailLinux` を本線とする。SDL3でウィンドウと入力を扱い、ImGuiでメニュー/ダイアログを表示し、DXVK Native経由のD3D11描画をVulkan上で実行する。Qt版は使用しない。

アプリの挙動はWindows版の `MIDITrailApp` に合わせている(再生状態の遷移、シーン構築、設定ファイル、メニューの有効/無効)。プラットフォーム依存部分だけを置き換えている。

| Windows | Linux |
|---|---|
| Win32 ウィンドウ / DirectInput / XInput | SDL3(キーボード、マウス、ゲームパッド) |
| Win32 ダイアログ | ImGui ウィンドウ |
| SMSequencer / winmm MIDI OUT・IN | 再生ワーカースレッド + ALSA シーケンサ / KDMAPI (`libOmniMIDI.so`) |
| CreateProcess による ffmpeg パイプ | fork/exec による ffmpeg パイプ |
| 名前付きミューテックス + メールスロット | 抽象名前空間の Unix ソケット |

## 機能

- 5シーン(Piano Roll 3D / 2D / Rain / Rain 2D / Ring)と、各シーンのライブモニター表示(`*Live.ini`)
- Open / Add / Open Folder / Previous・Next File / Folder Playback / ドラッグ&ドロップ
- 再生、一時停止、停止、リピート、スキップ(スライド移動)、速度変更、F2 長押しで2倍速(`conf/Player.ini`)
- テンポ、拍子、小節の表示と、ピッチベンドの演奏中反映
- MIDI OUT Port A〜F(ALSA または KDMAPI、SMF のポート番号で振り分け)
- MIDI IN Port A によるライブモニター、MIDITHRU
- 動画書き出し(ffmpeg が必要。NVENC / QSV / libx264 / libx265 / VAAPI / 透過 qtrle・ProRes 4444・FFV1 / 360度 equirectangular)
- カラーパレット(グラデーション、既定色、パラメータのエクスポート/インポート)、グラフィック(MSAA/SSAA、背景画像、四分音符長)、ウィンドウサイズ、How to view、Manual、About、Config Manager
- 視点の保存と復元(自動保存、My Viewpoint 1〜3)、全画面表示、メニューバーの非表示(画面上端にマウスを置くと表示)
- 多重起動の防止(`Player.ini` の `AllowMultipleInstances=0` のとき、2つ目の起動はファイルを既存のウィンドウに渡して終了する)

RCP 形式(`rcpcv.dll`)は Windows 専用の DLL に依存するため対象外。

## 設定ファイル

- アプリ側: 実行ファイルと同じ場所の `conf/*.ini`(Windows版と共通)
- ユーザー側: `~/.config/MIDITrail/yknk/MIDITrail/{View,Graphic,MIDI,Color}.ini`

## ビルド

```sh
xmake f -m release --dxvk_native_root=/path/to/dxvk-native/usr
xmake build MIDITrailLinux
```

`dxvk_native_root` には、`include/dxvk/d3d11.h` と DXVK Native のライブラリを含む prefix を指定する。

Fedora では `dxvk-native-devel` をインストールすれば、既定値(`/usr`)のままビルドできる。システムにインストールしない場合は、RPM をビルドディレクトリへ展開して使う。

```sh
mkdir -p build/deps/rpms && cd build/deps/rpms
dnf download --arch x86_64 dxvk-native dxvk-native-devel
mkdir -p ../dxvk-native && cd ../dxvk-native
for r in ../rpms/*.rpm; do rpm2cpio "$r" | cpio -idm; done
cd ../../..
xmake f -m release --dxvk_native_root=$PWD/build/deps/dxvk-native/usr
```

その他の依存パッケージ(Fedora): `SDL3-devel`, `libvkd3d-utils-devel`, `libvkd3d-shader-devel`, `vulkan-loader-devel`, `alsa-lib-devel`, `freetype-devel`, `fontconfig-devel`。動画書き出しには `ffmpeg` が必要。

## 起動

```sh
./build/linux/x86_64/release/MIDITrailLinux [-p] [-q] [-d] [path/to/file.mid]
```

- `-p`: 起動後に再生する
- `-q`: 再生が終わったら終了する(`-p` と併用)
- `-d`: SMF の解析結果を `<file>.dump.txt` に出力する

テスト用に `--autoplay`(`-p` と同じ)と `--smoke-frames N`(N フレーム描画して終了する。多重起動チェックはしない)も使える。

VAAPI のデバイスは既定で `/dev/dri/renderD128` を使う。環境変数 `MIDITRAIL_VAAPI_DEVICE` で変更できる。

## キー操作

| キー | 動作 |
|---|---|
| Space / テンキー0 | 再生 / 一時停止 |
| Shift+Space | ライブモニター開始 |
| Esc | 停止(モニター中はモニター停止) |
| テンキー Enter | 停止 |
| 1 / 2 | スキップ戻し / 送り |
| 4 / 5 | 速度ダウン / アップ |
| 7 / 8 / 9 | 視点 1(リセット・保存) / 2 / 3 |
| F2(押している間) | 2倍速 |
| Ctrl+O | ファイルを開く |
| F10 | メニューバー表示の切替 |
| F11 | 全画面表示 |
| W A S D / 矢印 / Q E / PgUp PgDn / Z C , . | カメラ移動(Shift で加速、Ctrl で回転) |
| 左クリック / 中クリック / ホイール | マウスルック / 自動ロール / ロール |

ゲームパッドの割り当て:

- Start / A: 再生
- B: 停止
- LB / RB: 視点の切替
- LT / RT: スキップ
- スティック / 十字キー / X / Y: カメラ移動
