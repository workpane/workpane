#include "ui/components/indicators/Tones.h"

namespace workpane::ui {

void Tones::read(json::ObjectReader& reader, std::string_view key, Tone& out) {
    reader.readChoice(key, out, {{"neutral", Tone::Neutral}, {"accent", Tone::Accent}, {"success", Tone::Success}, {"warning", Tone::Warning}, {"danger", Tone::Danger}, {"information", Tone::Information}}, json::Presence::Optional);
}

ThemeColor Tones::color(Tone tone) {
    switch (tone) {
    case Tone::Neutral:
        return ThemeColor::TextMuted;
    case Tone::Accent:
        return ThemeColor::Accent;
    case Tone::Success:
        return ThemeColor::Success;
    case Tone::Warning:
        return ThemeColor::Warning;
    case Tone::Danger:
        return ThemeColor::Danger;
    case Tone::Information:
        return ThemeColor::Information;
    }

    return ThemeColor::TextMuted;
}

ThemeColor Tones::ink(Tone tone) {
    switch (tone) {
    case Tone::Neutral:
        return ThemeColor::Window;
    case Tone::Accent:
        return ThemeColor::OnAccent;
    case Tone::Success:
        return ThemeColor::OnSuccess;
    case Tone::Warning:
        return ThemeColor::OnWarning;
    case Tone::Danger:
        return ThemeColor::OnDanger;
    case Tone::Information:
        return ThemeColor::OnInformation;
    }

    return ThemeColor::Window;
}

ThemeColor Tones::background(Tone tone) {
    switch (tone) {
    case Tone::Neutral:
        return ThemeColor::Pressed;
    case Tone::Accent:
        return ThemeColor::AccentBackground;
    case Tone::Success:
        return ThemeColor::SuccessBackground;
    case Tone::Warning:
        return ThemeColor::WarningBackground;
    case Tone::Danger:
        return ThemeColor::DangerBackground;
    case Tone::Information:
        return ThemeColor::InformationBackground;
    }

    return ThemeColor::Pressed;
}

ThemeColor Tones::text(Tone tone) {
    switch (tone) {
    case Tone::Neutral:
        return ThemeColor::Text;
    case Tone::Accent:
        return ThemeColor::AccentText;
    case Tone::Success:
        return ThemeColor::SuccessText;
    case Tone::Warning:
        return ThemeColor::WarningText;
    case Tone::Danger:
        return ThemeColor::DangerText;
    case Tone::Information:
        return ThemeColor::InformationText;
    }

    return ThemeColor::Text;
}

} // namespace workpane::ui
