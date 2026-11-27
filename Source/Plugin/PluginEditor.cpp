#include "PluginEditor.h"
#include "EditorStyle.h"

using namespace editor;

namespace
{
constexpr int kHeader = 44;      // 上の段: 名前、バイパス、（右から）設定値初期化、オーディオ設定、入力チャンネル
constexpr int kRow = 30;         // 操作の帯の 1 行
constexpr int kButtonWidth = 120;
constexpr int kInputComboWidth = 176; // ほかの 3 つの入力チャンネルの選択欄と同じ幅
constexpr int kLabelInset = 5;        // Label の文字の左の余白（JUCE の標準）
constexpr int kLabelGap = 8;          // 名前と部品の隙間
constexpr int kStageWidth = 118;      // 段の名前（オン/オフ）の列
constexpr int kPanelGap = 6;
constexpr int kColumnGap = 14;
constexpr int kTimerHz = 30;

// 段ごとのつまみの並び（列ごとに上から。count は空き）
const vcab::P kGateGrid[3][2] = { { vcab::P::gateThreshold, vcab::P::gateHysteresis },
                                  { vcab::P::gateAttack, vcab::P::gateHold },
                                  { vcab::P::gateRelease, vcab::P::count } };
const vcab::P kCompGrid[3][2] = { { vcab::P::compThreshold, vcab::P::compRatio },
                                  { vcab::P::compAttack, vcab::P::compKnee },
                                  { vcab::P::compRelease, vcab::P::count } };
const vcab::P kReverbGrid[3][2] = { { vcab::P::reverbLevel, vcab::P::count },
                                    { vcab::P::reverbTime, vcab::P::reverbPredelay },
                                    { vcab::P::reverbHighCut, vcab::P::reverbLowCut } };
} // namespace

VcabLevelEditor::Control& VcabLevelEditor::add (vcab::P id, bool withLabel)
{
    const auto& info = vcab::paramInfo (id);
    auto& apvts = proc.state();
    auto c = std::make_unique<Control>();
    c->id = id;
    const auto help = utf8 (info.help);
    if (withLabel)
    {
        c->label.setText (utf8 (info.name), juce::dontSendNotification);
        c->label.setColour (juce::Label::textColourId, kText);
        c->label.setTooltip (help);
        addAndMakeVisible (c->label);
    }
    switch (info.type)
    {
        case vcab::ParamType::choice:
            c->combo = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < info.choices.size(); ++i)
                c->combo->addItem (utf8 (info.choices[i]), static_cast<int> (i) + 1);
            c->combo->setTooltip (help);
            addAndMakeVisible (*c->combo);
            if (id != vcab::P::inputChannel) // 入力チャンネルは「自動」の文字が変わるので、自前でつなぐ（timerCallback）
                c->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, info.key, *c->combo);
            break;
        case vcab::ParamType::toggle:
            c->toggle = std::make_unique<juce::ToggleButton> (utf8 (info.name));
            c->toggle->setTooltip (help);
            addAndMakeVisible (*c->toggle);
            c->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, info.key, *c->toggle);
            break;
        case vcab::ParamType::real:
            c->slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
            c->slider->setTooltip (help);
            addAndMakeVisible (*c->slider);
            c->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, info.key, *c->slider);
            break;
    }
    controls.push_back (std::move (c));
    return *controls.back();
}

VcabLevelEditor::VcabLevelEditor (VcabLevelProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (p)
{
    setLookAndFeel (&lookAndFeel);
    lookAndFeel.setTooltipAvoid (&display, this); // グラフの説明は、グラフの外に出す

    // 上の段
    add (vcab::P::bypass, false);
    {
        auto& input = add (vcab::P::inputChannel, true);
        input.combo->onChange = [this, combo = input.combo.get()]
        {
            const int index = combo->getSelectedItemIndex();
            auto* param = proc.param (vcab::P::inputChannel);
            if (index < 0 || index == static_cast<int> (std::lround (proc.value (vcab::P::inputChannel))))
                return;
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
            param->endChangeGesture();
        };
    }

    // 段（処理の順）
    add (vcab::P::gateOn, false);
    for (const auto& column : kGateGrid)
        for (auto id : column)
            if (id != vcab::P::count)
                add (id, true);
    add (vcab::P::compOn, false);
    for (const auto& column : kCompGrid)
        for (auto id : column)
            if (id != vcab::P::count)
                add (id, true);
    add (vcab::P::reverbOn, false);
    for (const auto& column : kReverbGrid)
        for (auto id : column)
            if (id != vcab::P::count)
                add (id, true);
    add (vcab::P::gainOn, false);
    add (vcab::P::gain, false); // 名前はオン/オフのチェックボックスと同じなので出さない
    add (vcab::P::limiterOn, false);
    add (vcab::P::limiterCeiling, true);
    add (vcab::P::limiterRelease, true);

    resetButton.setButtonText (utf8 ("設定値初期化"));
    resetButton.setTooltip (utf8 ("すべての設定を初期値に戻します（入力チャンネルは戻しません）。押すと確認が出ます。"));
    resetButton.onClick = [this]
    {
        const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::QuestionIcon, utf8 ("設定値初期化"),
            utf8 ("すべての設定を初期値に戻します。よろしいですか？（入力チャンネルとオーディオデバイスの設定は変わりません）"),
            utf8 ("初期化"), utf8 ("キャンセル"), this);
        juce::Component::SafePointer<VcabLevelEditor> safe (this);
        juce::AlertWindow::showAsync (options, [safe] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->proc.resetAllParameters();
        });
    };
    addAndMakeVisible (resetButton);

    // EXE 版だけ、オーディオの設定画面を開くボタン（「オプション」の中と同じ画面）
    audioSettingsButton.setButtonText (utf8 ("オーディオ設定"));
    audioSettingsButton.setTooltip (utf8 ("オーディオの設定画面（入出力の機器、サンプルレート、バッファの大きさ）を開きます。左上の「オプション」の中と同じ画面です。"));
    audioSettingsButton.onClick = [this]
    {
        if (proc.openAudioSettings)
            proc.openAudioSettings();
    };
    if (proc.openAudioSettings)
        addAndMakeVisible (audioSettingsButton);

    addAndMakeVisible (display);
    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (kTimerHz);
}

VcabLevelEditor::~VcabLevelEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

VcabLevelEditor::Control* VcabLevelEditor::control (vcab::P id)
{
    for (auto& c : controls)
        if (c->id == id)
            return c.get();
    return nullptr;
}

void VcabLevelEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kText);
    g.setFont (juce::FontOptions (kFont + 3.0f, juce::Font::bold));
    g.drawText ("VcabLevel", juce::Rectangle<int> (kMargin, 0, 120, kHeader), juce::Justification::centredLeft);
    g.setColour (kPanel);
    for (auto a : { gateArea, compArea, reverbArea, gainArea, limiterArea })
        g.fillRoundedRectangle (a.toFloat(), 4.0f);
}

void VcabLevelEditor::resized()
{
    auto r = getLocalBounds().reduced (kMargin, 0);
    const juce::Font font { juce::FontOptions (kFont) };
    auto textWidth = [&font] (const juce::String& t) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, t)); };
    auto labelWidth = [&] (std::initializer_list<vcab::P> ids)
    {
        int w = 0;
        for (auto id : ids)
            if (id != vcab::P::count)
                w = std::max (w, textWidth (control (id)->label.getText()));
        return w + kLabelInset + kLabelGap;
    };
    // 名前（幅 lw。0 なら名前なし）とバーを 1 行に置く
    auto place = [this] (vcab::P id, juce::Rectangle<int> line, int lw)
    {
        if (id == vcab::P::count)
            return;
        auto* c = control (id);
        line = line.reduced (0, 3);
        if (lw > 0)
            c->label.setBounds (line.removeFromLeft (lw));
        c->slider->setBounds (line);
    };

    // 上の段: 名前（左）、バイパス。右端に寄せて、入力チャンネル、オーディオ設定、設定値初期化
    {
        auto h = r.removeFromTop (kHeader).reduced (0, 5);
        h.removeFromLeft (118);
        control (vcab::P::bypass)->toggle->setBounds (h.removeFromLeft (110));
        resetButton.setBounds (h.removeFromRight (kButtonWidth));
        h.removeFromRight (6);
        if (audioSettingsButton.isVisible())
        {
            audioSettingsButton.setBounds (h.removeFromRight (kButtonWidth));
            h.removeFromRight (10);
        }
        else
            h.removeFromRight (4);
        auto* input = control (vcab::P::inputChannel);
        input->combo->setBounds (h.removeFromRight (kInputComboWidth));
        input->label.setBounds (h.removeFromRight (textWidth (input->label.getText()) + kLabelInset + kLabelGap));
    }

    // 下から: ゲインとリミッターの帯（1 行）、リバーブの帯（2 行）、コンプの帯（2 行）、ゲートの帯（2 行）。残りがグラフ
    r.removeFromBottom (kMargin);
    auto lastRow = r.removeFromBottom (kRow + 12);
    r.removeFromBottom (kPanelGap);
    reverbArea = r.removeFromBottom (kRow * 2 + 12);
    r.removeFromBottom (kPanelGap);
    compArea = r.removeFromBottom (kRow * 2 + 12);
    r.removeFromBottom (kPanelGap);
    gateArea = r.removeFromBottom (kRow * 2 + 12);
    r.removeFromBottom (kPanelGap);
    display.setBounds (r);

    // ゲートとコンプ: 左にオン/オフ、右に 3 列 × 2 行。名前の幅は列ごとに 2 つの帯でそろえ（列の位置が上下でそろう）、
    // バーの幅は 3 列で同じにする
    int lw[3];
    int labels = 0;
    for (int c = 0; c < 3; ++c)
    {
        lw[c] = std::max ({ labelWidth ({ kGateGrid[c][0], kGateGrid[c][1] }), labelWidth ({ kCompGrid[c][0], kCompGrid[c][1] }),
                            labelWidth ({ kReverbGrid[c][0], kReverbGrid[c][1] }) });
        labels += lw[c];
    }
    auto layoutGrid = [&] (juce::Rectangle<int> area, vcab::P onId, const vcab::P (&grid)[3][2])
    {
        auto s = area.reduced (8, 6);
        control (onId)->toggle->setBounds (s.removeFromLeft (kStageWidth).removeFromTop (kRow).reduced (0, 3));
        const int bar = (s.getWidth() - labels - kColumnGap * 2) / 3;
        for (int c = 0; c < 3; ++c)
        {
            auto col = s.removeFromLeft (lw[c] + bar);
            s.removeFromLeft (kColumnGap);
            place (grid[c][0], col.removeFromTop (kRow), lw[c]);
            place (grid[c][1], col.removeFromTop (kRow), lw[c]);
        }
    };
    layoutGrid (gateArea, vcab::P::gateOn, kGateGrid);
    layoutGrid (compArea, vcab::P::compOn, kCompGrid);
    layoutGrid (reverbArea, vcab::P::reverbOn, kReverbGrid);

    // ゲイン ｜ リミッター
    {
        gainArea = lastRow.removeFromLeft (lastRow.getWidth() * 2 / 5);
        lastRow.removeFromLeft (kPanelGap);
        limiterArea = lastRow;

        auto s = gainArea.reduced (8, 6);
        control (vcab::P::gainOn)->toggle->setBounds (s.removeFromLeft (kStageWidth).reduced (0, 3));
        place (vcab::P::gain, s, 0);

        auto t = limiterArea.reduced (8, 6);
        control (vcab::P::limiterOn)->toggle->setBounds (t.removeFromLeft (kStageWidth).reduced (0, 3));
        const int lw1 = labelWidth ({ vcab::P::limiterCeiling }), lw2 = labelWidth ({ vcab::P::limiterRelease });
        const int bar = (t.getWidth() - lw1 - lw2 - kColumnGap) / 2;
        place (vcab::P::limiterCeiling, t.removeFromLeft (lw1 + bar), lw1);
        t.removeFromLeft (kColumnGap);
        place (vcab::P::limiterRelease, t, lw2);
    }
}

void VcabLevelEditor::timerCallback()
{
    const auto s = proc.currentSettings();
    auto dim = [this] (vcab::P id, bool on)
    {
        if (auto* c = control (id))
            for (juce::Component* comp : { static_cast<juce::Component*> (c->slider.get()), static_cast<juce::Component*> (&c->label) })
                if (comp != nullptr)
                    comp->setAlpha (on ? 1.0f : 0.45f);
    };
    // オフにした段の部品は薄くする（操作はできる）
    for (const auto& column : kGateGrid)
        for (auto id : column)
            dim (id, s.gate.on);
    for (const auto& column : kCompGrid)
        for (auto id : column)
            dim (id, s.comp.on);
    for (const auto& column : kReverbGrid)
        for (auto id : column)
            dim (id, s.reverb.on);
    dim (vcab::P::gain, s.gain.on);
    dim (vcab::P::limiterCeiling, s.limiter.on);
    dim (vcab::P::limiterRelease, s.limiter.on);

    // 入力チャンネルの「自動」に、いま使っているチャンネルを添える
    if (auto* c = control (vcab::P::inputChannel))
    {
        const auto text = utf8 ("自動（") + (proc.usedInputChannel.load() == 1 ? "2" : "1") + utf8 ("ch を使用中）");
        const auto& info = vcab::paramInfo (vcab::P::inputChannel);
        auto* combo = c->combo.get();
        if (combo->getItemText (0) != text && ! combo->isPopupActive())
        {
            combo->clear (juce::dontSendNotification);
            combo->addItem (text, 1);
            for (size_t i = 1; i < info.choices.size(); ++i)
                combo->addItem (utf8 (info.choices[i]), static_cast<int> (i) + 1);
        }
        const int index = static_cast<int> (std::lround (proc.value (vcab::P::inputChannel)));
        if (combo->getSelectedItemIndex() != index)
            combo->setSelectedItemIndex (index, juce::dontSendNotification);
    }
}
