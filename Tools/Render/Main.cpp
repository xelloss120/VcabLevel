// 開発用の WAV の処理ツール。
//   vcablv_render 入力.wav 出力.wav [キー=値 ...] [--block n] [--trace 出力.csv]
// キーは Params.cpp の key（例: gate_threshold=-50 comp_ratio=3 gain=4）。値は表の範囲（つまみの範囲）に収める（収めたときは知らせる）。
// 入力が 2 チャンネル以上のときは、input_channel の 1ch（初期値の「自動」も 1ch）/ 2ch / 平均 で 1 チャンネルにする。
// --block n: 1 回に処理する標本の数（初期値 256）。
// --trace: 画面用の 10ms ごとの値 { 時刻, 入力の山, 出力の山, RMS, コンプの削り量, リミッターの削り量, ゲートの倍率 } を CSV に書く（dB）。
// 出力は遅延（1ms）を詰めずに書く（プラグインと同じ。遅延の標本数は終わりに出す）。
// --set-at 秒 キー=値: その時刻から設定を変える（複数可。つまみを動かしたときのなめらかさを確かめるため）。
// 出力は 1 チャンネル、32 ビット浮動小数（測るときに丸めの差が乗らないように）。
#include "Engine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

static int run (int argc, char** argv);

static void usage()
{
    std::printf ("usage: vcablv_render in.wav out.wav [key=value ...] [--block n] [--trace out.csv] [--set-at sec key=value]\n");
}

// 数の読めない引数（std::stoi / std::stof の例外）は、使い方を出して終える
static int runChecked (int argc, char** argv)
{
    try
    {
        return run (argc, argv);
    }
    catch (const std::logic_error& e)
    {
        std::printf ("bad argument (%s)\n", e.what());
        usage();
        return 1;
    }
}

static int run (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 1;
    }

    auto values = vcab::defaultValues();
    int block = 256;
    std::string tracePath;
    struct Change { double at; int id; float value; };
    std::vector<Change> changes;
    // キー=値を読む（範囲に収める）。読めなければ -1
    auto parseKeyValue = [] (const std::string& a, float& out) -> int
    {
        const auto eq = a.find ('=');
        const int id = eq == std::string::npos ? -1 : vcab::findParamByKey (a.substr (0, eq));
        if (id < 0)
            return -1;
        const auto& info = vcab::paramInfo (static_cast<vcab::P> (id));
        float v = std::stof (a.substr (eq + 1));
        if (std::isnan (v))
            v = info.defaultValue;
        out = std::clamp (v, info.minValue, info.maxValue);
        if (out != v)
            std::printf ("%s: %g -> %g (range %g..%g)\n", info.key, v, out, info.minValue, info.maxValue);
        return id;
    };
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--block" && i + 1 < argc)
        {
            block = std::max (1, std::stoi (argv[++i]));
            continue;
        }
        if (a == "--trace" && i + 1 < argc)
        {
            tracePath = argv[++i];
            continue;
        }
        if (a == "--set-at" && i + 2 < argc)
        {
            const double at = std::stod (argv[++i]);
            float v = 0.0f;
            const int id = parseKeyValue (argv[++i], v);
            if (id < 0)
            {
                std::printf ("unknown argument: %s\n", argv[i]);
                usage();
                return 1;
            }
            changes.push_back ({ at, id, v });
            continue;
        }
        float v = 0.0f;
        const int id = parseKeyValue (a, v);
        if (id < 0)
        {
            std::printf ("unknown argument: %s\n", a.c_str());
            usage();
            return 1;
        }
        values[static_cast<size_t> (id)] = v;
    }
    const auto settings = vcab::Settings::fromValues (values);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const juce::File inFile { juce::String (juce::CharPointer_UTF8 (argv[1])) };
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("cannot read %s\n", argv[1]);
        return 1;
    }
    if (reader->lengthInSamples > std::numeric_limits<int>::max() - 1000000)
    {
        std::printf ("too long: %s\n", argv[1]);
        return 1;
    }
    const int numCh = static_cast<int> (reader->numChannels);
    const int len = static_cast<int> (reader->lengthInSamples);
    const double fs = reader->sampleRate;
    juce::AudioBuffer<float> input (numCh, len);
    reader->read (&input, 0, len, 0, true, true);

    // 1 チャンネルにする
    std::vector<float> data (static_cast<size_t> (len), 0.0f);
    {
        const auto ch = settings.inputChannel;
        const float* c0 = input.getReadPointer (0);
        const float* c1 = input.getReadPointer (numCh > 1 ? 1 : 0);
        for (int i = 0; i < len; ++i)
            data[static_cast<size_t> (i)] = ch == vcab::InputChannel::ch2       ? c1[i]
                                            : ch == vcab::InputChannel::average ? 0.5f * (c0[i] + c1[i])
                                                                                : c0[i];
    }

    vcab::Engine engine;
    std::vector<vcab::MeterFrame> trace;
    if (! tracePath.empty())
    {
        trace.reserve (static_cast<size_t> (len / (0.01 * fs)) + 16);
        engine.setFrameLog (&trace);
    }
    engine.setSettings (settings);
    engine.prepare (fs, block);

    std::vector<double> blockSecs;
    blockSecs.reserve (static_cast<size_t> (len / block) + 2);
    // プラグインと同じく、非正規化数を 0 として扱う
    juce::ScopedNoDenormals noDenormals;
    const auto start = std::chrono::steady_clock::now();
    std::sort (changes.begin(), changes.end(), [] (const Change& x, const Change& y) { return x.at < y.at; });
    size_t nextChange = 0;
    for (int pos = 0; pos < len;)
    {
        // 設定を変える時刻で区切る（プラグインと同じく、ブロックの頭で設定を渡す）
        int n = std::min (block, len - pos);
        bool changed = false;
        while (nextChange < changes.size() && changes[nextChange].at * fs <= pos)
        {
            values[static_cast<size_t> (changes[nextChange].id)] = changes[nextChange].value;
            ++nextChange;
            changed = true;
        }
        if (changed)
        {
            const auto s2 = vcab::Settings::fromValues (values);
            engine.setSettings (s2);
        }
        if (nextChange < changes.size())
            n = std::min (n, std::max (1, static_cast<int> (std::ceil (changes[nextChange].at * fs)) - pos));
        const auto t0 = std::chrono::steady_clock::now();
        engine.process (data.data() + pos, n);
        blockSecs.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        pos += n;
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    // 一時ファイルに書いてから置き換える（書けなかったときに、前の出力を消してしまわないように）
    const juce::File outFile { juce::String (juce::CharPointer_UTF8 (argv[2])) };
    juce::WavAudioFormat wav;
    {
        juce::TemporaryFile tmpOut (outFile);
        bool written = false;
        {
            std::unique_ptr<juce::OutputStream> stream = tmpOut.getFile().createOutputStream();
            const auto options = juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (1).withBitsPerSample (32)
                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream, options) : nullptr);
            if (writer != nullptr)
            {
                const float* p = data.data();
                written = writer->writeFromFloatArrays (&p, 1, len);
            }
        }
        if (! written || ! tmpOut.overwriteTargetFileWithTemporary())
        {
            std::printf ("cannot write %s\n", argv[2]);
            return 1;
        }
    }

    if (! tracePath.empty())
    {
        std::ofstream tl (tracePath);
        tl << "t,in,out,rms,comp,limiter,gate\n";
        for (size_t k = 0; k < trace.size(); ++k)
        {
            const auto& r = trace[k];
            tl << (k + 1) * 0.01 << ',' << r.inDb << ',' << r.outDb << ',' << r.rmsDb << ',' << r.compDb << ',' << r.limiterDb << ','
               << r.gateDb << '\n';
        }
    }

    std::printf ("latency %d samples\n", engine.latency());
    std::printf ("cpu %.3f%% of realtime\n", 100.0 * secs / (len / fs));
    if (! blockSecs.empty())
    {
        std::sort (blockSecs.begin(), blockSecs.end());
        const double blockLen = block / fs;
        std::printf ("block max %.1f us (%.1f%% of block), %zu blocks\n", blockSecs.back() * 1.0e6, 100.0 * blockSecs.back() / blockLen,
                     blockSecs.size());
    }
    return 0;
}

// Windows ではコマンドラインの引数を UTF-8 にして渡す（日本語のファイル名を読めるように）
#ifdef _WIN32
int wmain (int argc, wchar_t** wargv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back (juce::String (wargv[i]).toStdString());
    std::vector<char*> ptrs;
    for (auto& a : args)
        ptrs.push_back (a.data());
    return runChecked (argc, ptrs.data());
}
#else
int main (int argc, char** argv)
{
    return runChecked (argc, argv);
}
#endif
