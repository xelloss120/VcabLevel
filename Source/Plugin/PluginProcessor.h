#pragma once

#include "Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>

class VcabLevelProcessor : public juce::AudioProcessor
{
public:
    VcabLevelProcessor();
    ~VcabLevelProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // 入力が止まった後に出る音の長さ。遅延（1ms）の分と、リバーブがオンなら響きの尾（プリディレイ + 長さ）
    double getTailLengthSeconds() const override;
    // ホストのバイパスを、画面の「バイパス」と同じものにする（ホスト側で切っても中の判定が続き、10ms でなめらかに移る。VcabDenoise と同じ）
    juce::AudioProcessorParameter* getBypassParameter() const override { return param (vcab::P::bypass); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }
    juce::RangedAudioParameter* param (vcab::P id) const { return params[static_cast<size_t> (id)]; }
    float value (vcab::P id) const { return params[static_cast<size_t> (id)]->convertFrom0to1 (params[static_cast<size_t> (id)]->getValue()); }
    vcab::Settings currentSettings() const;
    void resetAllParameters(); // すべての設定値を初期値に戻す（入力チャンネルは戻さない）

    // 画面用
    vcab::MeterRing& meters() { return engine.meters; }
    double sampleRate() const { return currentSampleRate.load(); }
    std::atomic<int> usedInputChannel { 0 }; // 自動のときに使っている入力チャンネル（0 = 1ch、1 = 2ch）

    // EXE 版だけ、外枠が入れる。音声の設定画面を開く（VST3 では空のまま）
    std::function<void()> openAudioSettings;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void mixToMono (const juce::AudioBuffer<float>& buffer, int start, int n, int choice); // 入力チャンネルの設定に従って mono に書く

    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, vcab::kNumParams> params {};
    std::atomic<double> currentSampleRate { 48000.0 };

    vcab::Engine engine;
    std::vector<float> mono;

    // 入力チャンネルの自動選択（VcabDenoise・VcabShift と同じ形）
    static constexpr double kAutoSmoothSeconds = 0.3;  // 各チャンネルの音量のならし
    static constexpr double kAutoSwitchSeconds = 2.0;  // もう片方が大きい状態がこれだけ続いたら切り替える
    static constexpr float kAutoSwitchRatio = 3.98f;   // 6dB（パワー）
    static constexpr double kAutoFadeSeconds = 0.010;  // 切り替えのつなぎ
    float autoPower[2] { 0.0f, 0.0f };
    int autoChannel = 0, autoPrev = 0;
    int otherLouderFor = 0;
    float autoFade = 1.0f; // 前のチャンネルから今のチャンネルへのつなぎ（1 で今だけ）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VcabLevelProcessor)
};
