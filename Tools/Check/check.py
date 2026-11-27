# 仕様書 10 章の確かめ（WAV の処理ツールに決まった音を通して、数値で確かめる）。
# 必要なもの: Python 3、numpy、scipy、soundfile。先に vcablv_render をビルドしておく。
# 使い方: python Tools/Check/check.py [vcablv_render.exe] [作業フォルダ]
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import butter, resample_poly, sosfiltfilt

HERE = os.path.dirname(os.path.abspath(__file__))
RENDER = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'build', 'vcablv_render_artefacts', 'Release', 'vcablv_render.exe'))
WORK = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix='vcablv_check_')
os.makedirs(WORK, exist_ok=True)

ALL_OFF = ['gate_on=0', 'comp_on=0', 'gain_on=0', 'limiter_on=0']
failures = []


def render(x, fs, args, name, trace=False):
    src = os.path.join(WORK, name + '_in.wav')
    dst = os.path.join(WORK, name + '_out.wav')
    sf.write(src, x.astype(np.float32), fs, subtype='FLOAT')
    cmd = [RENDER, src, dst] + args
    if trace:
        cmd += ['--trace', os.path.join(WORK, name + '.csv')]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    latency = int([l for l in out.splitlines() if l.startswith('latency')][0].split()[1])
    y, _ = sf.read(dst, dtype='float64')
    return y, latency, out


def check(ok, text):
    print(('OK   ' if ok else 'NG   ') + text)
    if not ok:
        failures.append(text)


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-12))


def true_peak_db(y):
    # 4 倍に補間した山。ファイルの両端は、補間で切れ目の波打ちが出るので 10ms ずつ除く
    return db(np.max(np.abs(resample_poly(y, 4, 1)[4 * 480:-4 * 480])))


rng = np.random.default_rng(1)

# 1. 遅延と、全部の段をオフにしたときの素通し
for fs in (44100, 48000, 96000):
    x = (rng.standard_normal(fs) * 0.1).astype(np.float32).astype(np.float64)  # WAV（32 ビット）に書く値と同じにする
    y, lat, _ = render(x, fs, ALL_OFF, f'pass_{fs}')
    expect = int(round(0.001 * fs))
    check(lat == expect, f'{fs}Hz: 遅延の報告 {lat} 標本（1ms = {expect}）')
    diff = np.max(np.abs(y[lat:] - x[:len(x) - lat]))
    check(diff == 0.0 and np.all(y[:lat] == 0.0), f'{fs}Hz: 全部オフで {lat} 標本遅れるだけ（最大の差 {diff:.3g}）')

fs = 48000
t = np.arange(fs * 2) / fs

# 2. リミッター: 点と点の間の山も含めて上限を超えない
# 白色雑音は 20kHz より上を削る（補間の精度は 20kHz まで。声は前段のハイカットで 16kHz より上がほとんどない）
noise = sosfiltfilt(butter(8, 20000, fs=fs, output='sos'), rng.standard_normal(len(t)) * 2.0)
cases = {
    'noise_20kHz_+6dBFS': noise,
    'sine_997Hz_0dBFS': np.sin(2 * np.pi * 997 * t),
    'sine_12kHz_isp': np.sin(2 * np.pi * 12000 * t + np.pi / 4) * 1.0,  # 標本の間に山が来る
    'bursts': np.sin(2 * np.pi * 300 * t) * (np.sin(2 * np.pi * 3 * t) > 0.6) * 1.5,
    'clicks': np.zeros(len(t)),
}
cases['clicks'][::4801] = 1.0
cases['clicks'][1::4801] = -1.0
for ceiling in (-3.0, -0.1):
    for name, x in cases.items():
        y, lat, _ = render(x, fs, [f'limiter_ceiling={ceiling}', 'gain=12', 'gate_on=0'], f'lim_{name}')
        tp = true_peak_db(y)
        check(tp <= ceiling + 0.05, f'リミッター（上限 {ceiling}、ゲイン +12）{name}: 点の間も含めた山 {tp:.2f}dBFS')
# 参考（合否に入れない）: 20kHz より上まである白色雑音
y, _, _ = render(rng.standard_normal(len(t)) * 2.0, fs, ['gain=12', 'gate_on=0'], 'lim_fullband')
print(f'参考 リミッター（上限 -3.0）20kHz より上まである白色雑音: 点の間も含めた山 {true_peak_db(y):.2f}dBFS')

# 3. コンプ: 一定の大きさの正弦波で、下げる量が 4 章の式どおり
def comp_reduction(x_db, t_db, ratio, knee):
    slope = 1 / ratio - 1
    if knee > 0 and abs(x_db - t_db) <= knee / 2:
        d = x_db - t_db + knee / 2
        return -slope * d * d / (2 * knee)
    return -slope * (x_db - t_db) if x_db > t_db else 0.0


for knee in (0.0, 6.0):
    worst = 0.0
    for level in (-30, -21, -19, -18, -17, -15, -12, -6):
        amp = 10 ** (level / 20) * np.sqrt(2)  # RMS が level dBFS の正弦波
        x = amp * np.sin(2 * np.pi * 1000 * t)
        y, lat, _ = render(x, fs, ['gate_on=0', 'gain_on=0', 'limiter_on=0', f'comp_knee={knee}', 'comp_attack=1', 'comp_release=10'],
                           f'comp_{knee}_{level}')
        seg = slice(fs, 2 * fs - lat)  # 1 秒たってから
        measured = db(np.sqrt(np.mean(y[seg.start + lat:seg.stop + lat] ** 2))) - level
        expect = -comp_reduction(level, -18.0, 2.0, knee)
        worst = max(worst, abs(measured - expect))
    check(worst < 0.1, f'コンプ（ニー {knee}dB、-30〜-6dBFS）: 式との差の最大 {worst:.3f}dB')

# 4. ゲート: 開く線・閉じる線・ホールド・アタック・リリース
def gate_run(levels_db, args, name):
    # 0.5 秒ずつ、指定の大きさの正弦波（1kHz）
    seg = int(0.5 * fs)
    x = np.concatenate([10 ** (l / 20) * np.sin(2 * np.pi * 1000 * np.arange(seg) / fs) for l in levels_db])
    y, lat, _ = render(x, fs, ['comp_on=0', 'gain_on=0', 'limiter_on=0'] + args, name)
    y = y[lat:]
    env = np.abs(y)
    # 1ms ごとの山（正弦波の山を包絡にする）
    hop = 48
    frames = env[:len(env) // hop * hop].reshape(-1, hop).max(axis=1) / np.repeat([10 ** (l / 20) for l in levels_db], seg // hop)[: len(env) // hop]
    return frames  # 1ms ごとの倍率（1 = 開いている）


f = gate_run([-60, -40, -60], ['gate_threshold=-48', 'gate_hysteresis=3', 'gate_hold=100', 'gate_release=300', 'gate_attack=3'], 'gate_basic')
check(f[100:490].max() < 1e-3, f'ゲート: 開く線（-48）より下（-60）では閉じたまま（最大の倍率 {f[100:490].max():.2g}）')
opened = np.argmax(f[480:] > 0.99) + 480
check(502 <= opened <= 505, f'ゲート: -40 になってから開ききるまで {opened - 500}ms（アタック 3ms。1.1.0 から先読みなし）')
start_close = np.argmax(f[1000:] < 0.999) + 1000
check(1099 <= start_close <= 1103, f'ゲート: -60 に下がってから閉じ始めるまで {start_close - 1000}ms（ホールド 100ms、判定のピークの戻りを含む）')
half = np.argmax(db(f[start_close:]) < -40) + start_close
check(140 <= half - start_close <= 160, f'ゲート: 閉じ始めてから -40dB まで {half - start_close}ms（リリース 300ms で -80dB まで dB で直線 → 150ms）')

f = gate_run([-60, -40, -49.5, -40, -60], ['gate_threshold=-48', 'gate_hysteresis=3', 'gate_hold=100'], 'gate_hys3')
check(f[1010:1490].min() > 0.99, f'ゲート: ヒステリシス 3dB で、開く線と閉じる線の間（-49.5）は開いたまま（最小の倍率 {f[1010:1490].min():.3f}）')
f = gate_run([-60, -40, -49.5, -40, -60], ['gate_threshold=-48', 'gate_hysteresis=0', 'gate_hold=100'], 'gate_hys0')
check(f[1200:1490].max() < 0.5, f'ゲート: ヒステリシス 0 では、開く線より下（-49.5）でホールドの後に閉じる（倍率 {f[1200:1490].max():.3f}）')
f = gate_run([-60, -40, -60], ['gate_threshold=-48', 'gate_release=50'], 'gate_closed')
check(f[1300:1490].max() == 0.0, f'ゲート: 閉じきると無音（倍率の最大 {f[1300:1490].max():.2g}）')

# 5. リバーブ（12 章）
REV = ALL_OFF + ['reverb_on=1', 'reverb_level=0', 'reverb_predelay=0', 'reverb_high_cut=16000', 'reverb_low_cut=50']


def wet_of(x, args, name):
    # 全部の段をオフにして、出力から元の音（遅延の分ずらす）を引いた残りが響き
    y, lat, _ = render(x, fs, args, name)
    return y[lat:] - x[:len(x) - lat]


def t60_of(w):
    # 響きのエネルギーの減り方（後ろから積み上げた曲線）の -5〜-35dB を直線で当てはめて、60dB 下がる時間にする
    e = np.cumsum((w ** 2)[::-1])[::-1]
    edc = db(e / e[0]) / 2  # パワーの dB
    i5, i35 = np.argmax(edc < -5), np.argmax(edc < -35)
    slope = np.polyfit(np.arange(i5, i35) / fs, edc[i5:i35], 1)[0]
    return -60 / slope


for t60 in (0.5, 2.0, 4.0):
    x = np.zeros(int(fs * (t60 * 1.5 + 0.5)))
    x[100] = 0.5
    w = wet_of(x, REV + [f'reverb_time={t60}'], f'rev_t60_{t60}')
    got = t60_of(w[100:])
    check(abs(got / t60 - 1) < 0.1, f'リバーブ: 長さ {t60} 秒で、測った長さ {got:.2f} 秒')

x = np.zeros(fs)
x[100] = 0.5
onset = {}
for pre in (0, 50):
    w = wet_of(x, REV + ['reverb_time=1', f'reverb_predelay={pre}'], f'rev_pre_{pre}')
    onset[pre] = np.argmax(np.abs(w) > 1e-4 * np.max(np.abs(w)))
d = (onset[50] - onset[0]) / fs * 1000
check(abs(d - 50) < 0.5, f'リバーブ: プリディレイ 50ms で、響きの始まりが {d:.2f}ms 遅れる')

# 響きを足しても、上限を超えない
burst = np.sin(2 * np.pi * 300 * t) * (np.sin(2 * np.pi * 3 * t) > 0.6) * 1.5
y, _, _ = render(burst, fs, ['gain=12', 'gate_on=0', 'reverb_on=1', 'reverb_level=0', 'reverb_time=5'], 'rev_ceiling')
tp = true_peak_db(y)
check(tp <= -3.0 + 0.05, f'リバーブ（量 0dB、長さ 5 秒、ゲイン +12）: 点の間も含めた山 {tp:.2f}dBFS（上限 -3）')

# オンからオフに切り替えると、10ms でつないだ後は素通しに戻る（残っていた響きも出ない）
xb = (burst * 0.3).astype(np.float32).astype(np.float64)
y, lat, _ = render(xb, fs, REV + ['reverb_time=5', '--set-at', '1.0', 'reverb_on=0'], 'rev_off')
a = int(1.02 * fs)
diff = np.max(np.abs(y[lat + a:] - xb[a:len(xb) - lat]))
check(diff == 0.0, f'リバーブ: オフに切り替えて 20ms 後からは素通し（最大の差 {diff:.3g}）')

# 処理の塊の大きさで出力が変わらない（リバーブを含む）
outs = []
for block in (1, 256, 4096):
    y, _, _ = render(x, fs, ['reverb_on=1', '--block', str(block)], f'rev_block_{block}')
    outs.append(y)
diff = max(np.max(np.abs(outs[0] - outs[1])), np.max(np.abs(outs[0] - outs[2])))
check(diff == 0.0, f'リバーブ: 処理の塊（1 / 256 / 4096 標本）で出力が変わらない（最大の差 {diff:.3g}）')

# プリディレイを 0 へ動かしても、輪の外を読まない（1.1.0 の作りかけで、ならしの途中に輪の外を読んでいた。デバッグ版では止まる）
y, _, _ = render(xb, fs, ['reverb_on=1', '--set-at', '0.5', 'reverb_predelay=0', '--set-at', '1.5', 'reverb_predelay=100'], 'rev_pre_to0')
check(bool(np.all(np.isfinite(y))), 'リバーブ: プリディレイを 0 へ動かしても、出力はすべて有限の値')

# 6. 処理の塊の大きさで出力が変わらない（全部の段と、途中の設定の変更を含む）
mix = (burst * 0.5 + 0.02 * rng.standard_normal(len(t))) * (np.sin(2 * np.pi * 0.5 * t) > -0.3)
mix = mix.astype(np.float32).astype(np.float64)
args = ['reverb_on=1', 'gain=12', '--set-at', '0.6', 'limiter_ceiling=-9', '--set-at', '1.1', 'comp_on=0',
        '--set-at', '1.4', 'bypass=1', '--set-at', '1.6', 'bypass=0']
outs = [render(mix, fs, args + ['--block', str(b)], f'block_{b}')[0] for b in (1, 7, 256, 4096)]
diff = max(np.max(np.abs(outs[0] - o)) for o in outs[1:])
check(diff == 0.0, f'処理の塊（1 / 7 / 256 / 4096 標本）で出力が変わらない（全部の段、途中の変更あり。最大の差 {diff:.3g}）')

# 参考（合否に入れない。6 章の「切り替えの直後」）: 再生中にリミッターをオンにした直後と、上限を下げた直後の 10ms の山
sine = np.sin(2 * np.pi * 997 * t)
y, lat, _ = render(sine, fs, ['gate_on=0', 'gain=12', 'limiter_on=0', '--set-at', '1.0', 'limiter_on=1'], 'ref_lim_on')
print(f'参考 リミッターをオンにした直後 10ms（上限 -3）: 点の間も含めた山 {db(np.max(np.abs(resample_poly(y[fs + lat:fs + lat + 480], 4, 1)))):.2f}dBFS')
y, lat, _ = render(sine, fs, ['gate_on=0', 'gain=12', '--set-at', '1.0', 'limiter_ceiling=-12'], 'ref_lim_down')
print(f'参考 上限を -3 から -12 に下げた直後 2ms: 点の間も含めた山 {db(np.max(np.abs(resample_poly(y[fs + lat:fs + lat + 96], 4, 1)))):.2f}dBFS')

print()
print('NG: ' + str(len(failures)) if failures else 'すべて OK')
print('作業フォルダ: ' + WORK)
sys.exit(1 if failures else 0)
