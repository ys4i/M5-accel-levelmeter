# M5StickC Plus2 加速度レベルメーター

M5StickC Plus2 の加速度を使い、NeoPixel（GPIO32接続）を双方向VUメーター風に点灯させるスケッチです。中心LED（偶数本のときは中心2個）をゼロ点とし、負側はインデックス先頭、正側は末尾方向へ伸びます。キャリブレーションは BtnA 長押しで実行し、初回完了までLEDは点灯しません。

## 必要なもの
- M5StickC Plus2 本体
- NeoPixel（Unit NeoHEX等）LED 30本（デフォルト）
- Arduino IDE 2.x + M5StickC Plus2 ボード設定
- ライブラリ: `M5Unified`, `Adafruit_NeoPixel`

## 配線
- データ線: GPIO32 → NeoPixel DIN（先頭LEDを負側終端として扱う）
- 電源/GND: LED本数に見合った電源をユーザー側で供給（本スケッチでは電源方式を規定しません）

## 主な仕様
- キャリブレーション: BtnA 長押し（約700ms）で実行。サンプル数200。結果はRAMのみ保持（電源断で消失）。
- 表示: 重力方向の増減を射影し、EMA平滑化(alpha=0.3)後にレベル化。未使用LEDはデフォルトで消灯（必要なら `dim_unused=true` で弱点灯）。
- 更新周期: 20Hz。
- 配色: 中心緑、正側は緑→黄→赤、負側は緑→青→紫。
- 肩掛けミラー表示: テープを2ブロックに分割し、前面・背面どちらから見ても「負→中心→正, 正→中心→負」で同じパターンを表示する `render_mode=RENDER_SHOULDER_MIRROR` を追加（デフォルトは従来の中心分割 `RENDER_CENTER`）。
- デバッグ表示: `ENABLE_DEBUG` をビルド時に1で定義するとLCDへdelta/levelを表示。

## 設定値（コード内で変更）
`M5-accel-levelmeter.ino` 冒頭の `config` 構造体で初期値を変更できます（NVS等への保存なし）。

```cpp
Config config = {
  .led_count = 60,       // LED本数（偶数時は中心2個をゼロ点扱い）
  .brightness_max = 128, // 最大輝度
  .g_range_pos = 1.0f,   // 正側のレンジ[g]
  .g_range_neg = 1.0f,   // 負側のレンジ[g]
  .alpha = 0.3f,         // EMA係数（小さくすると滑らか、大きいと速応答）
  .update_hz = 20,       // 更新周期[Hz]
  .dim_unused = false,   // 未使用LEDを消灯（弱点灯させたい場合は true に）
  .render_mode = RENDER_CENTER, // 肩掛け用にする場合は RENDER_SHOULDER_MIRROR
};
```

### 中心LEDの色を変える
`M5-accel-levelmeter.ino` 冒頭で `center_color` というグローバル変数を定義しています。

```cpp
uint32_t center_color = strip.Color(0, 255, 0);
```

この1行の RGB 値を書き換えるだけで、`renderLevelCenter()` と `renderHalf()` の両方で使われる中心LED（偶数本なら中心2発）の色を一括で変更できます。`strip.Color(r, g, b)` は NeoPixel の GRB 配列を意識せず設定できるヘルパーなので、必要な色に応じて `r/g/b` の値を調整してください。

その他の調整:
- キャリブレーション長押し時間: `CAL_HOLD_MS`（デフォルト700ms）
- キャリブレーションサンプル数: `CAL_SAMPLES`（デフォルト200）
- デバッグ表示: ビルド時に `-DENABLE_DEBUG=1` を追加

## 使い方
1. 配線を行いスケッチを書き込む。
2. 起動後、BtnA を長押ししてキャリブレーション開始。完了すると中心LEDが点灯する。
3. デバイスを傾けたり振ったりして、負側（先頭）・正側（末尾）にレベルが振れることを確認。
4. 再調整したい場合は再度 BtnA 長押しでキャリブレーション。

## 注意
- 電源断でキャリブレーション結果は失われます（毎回やり直し）。
- 高輝度・多本数では電流が大きくなるため、十分な電源を用意してください。
