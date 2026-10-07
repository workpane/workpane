#include "ui/components/buttons/ButtonVariants.h"

namespace workpane::ui {

void ButtonVariants::read(json::ObjectReader& reader, ButtonVariant& variant) {
    reader.readChoice("variant", variant, {{"default", ButtonVariant::Default}, {"primary", ButtonVariant::Primary}, {"destructive", ButtonVariant::Destructive}, {"toolbar", ButtonVariant::Toolbar}, {"icon", ButtonVariant::Icon}, {"link", ButtonVariant::Link}}, json::Presence::Optional);
}

} // namespace workpane::ui
