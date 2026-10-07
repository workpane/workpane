#include "ui/components/StandardComponents.h"

#include "ui/components/buttons/Button.h"
#include "ui/components/buttons/Chip.h"
#include "ui/components/buttons/MenuButton.h"
#include "ui/components/buttons/Popover.h"
#include "ui/components/choices/Checkbox.h"
#include "ui/components/choices/Combo.h"
#include "ui/components/choices/LayoutSwatch.h"
#include "ui/components/choices/RadioGroup.h"
#include "ui/components/choices/Toggle.h"
#include "ui/components/collections/List.h"
#include "ui/components/collections/Table.h"
#include "ui/components/collections/Tree.h"
#include "ui/components/containers/Card.h"
#include "ui/components/containers/Column.h"
#include "ui/components/containers/Divider.h"
#include "ui/components/containers/FormField.h"
#include "ui/components/containers/Grid.h"
#include "ui/components/containers/Row.h"
#include "ui/components/containers/Scroll.h"
#include "ui/components/containers/Spacer.h"
#include "ui/components/containers/Splitter.h"
#include "ui/components/containers/Stack.h"
#include "ui/components/containers/Tabs.h"
#include "ui/components/indicators/Avatar.h"
#include "ui/components/indicators/Badge.h"
#include "ui/components/indicators/BusyIndicator.h"
#include "ui/components/indicators/IconGraphic.h"
#include "ui/components/indicators/Image.h"
#include "ui/components/indicators/Progress.h"
#include "ui/components/indicators/StatusIndicator.h"
#include "ui/components/inputs/FilterField.h"
#include "ui/components/inputs/NumberField.h"
#include "ui/components/inputs/SecretField.h"
#include "ui/components/inputs/Slider.h"
#include "ui/components/inputs/TextArea.h"
#include "ui/components/inputs/TextField.h"
#include "ui/components/pickers/ColorField.h"
#include "ui/components/pickers/DateTimeField.h"
#include "ui/components/settings/SettingsActions.h"
#include "ui/components/settings/SettingsForm.h"
#include "ui/components/settings/SettingsRow.h"
#include "ui/components/terminal/Terminal.h"
#include "ui/components/text/Alert.h"
#include "ui/components/text/EmptyState.h"
#include "ui/components/text/Label.h"
#include "ui/components/text/Markdown.h"
#include "ui/components/text/PageHeader.h"
#include "ui/components/text/SectionTitle.h"
#include "ui/components/views/Canvas.h"
#include "ui/components/views/CodeEditor.h"
#include "ui/components/views/WebView.h"
#include "ui/model/NodeId.h"

#include <memory>
#include <string>
#include <utility>

namespace workpane::ui {

template <typename T> void StandardComponents::add(ComponentRegistry& registry, std::string kind) {
    // clang-format off
    registry.add(std::move(kind), [](NodeId id) { return std::make_unique<T>(id); });
    // clang-format on
}

void StandardComponents::registerAll(ComponentRegistry& registry) {
    StandardComponents::add<Column>(registry, "column");
    StandardComponents::add<Row>(registry, "row");
    StandardComponents::add<Card>(registry, "card");
    StandardComponents::add<Grid>(registry, "grid");
    StandardComponents::add<Stack>(registry, "stack");
    StandardComponents::add<Scroll>(registry, "scroll");
    StandardComponents::add<Splitter>(registry, "splitter");
    StandardComponents::add<Spacer>(registry, "spacer");
    StandardComponents::add<Divider>(registry, "divider");
    StandardComponents::add<FormField>(registry, "formField");
    StandardComponents::add<Label>(registry, "label");
    StandardComponents::add<PageHeader>(registry, "pageHeader");
    StandardComponents::add<SectionTitle>(registry, "sectionTitle");
    StandardComponents::add<EmptyState>(registry, "emptyState");
    StandardComponents::add<Alert>(registry, "alert");
    StandardComponents::add<Button>(registry, "button");
    StandardComponents::add<Chip>(registry, "chip");
    StandardComponents::add<MenuButton>(registry, "menuButton");
    StandardComponents::add<Popover>(registry, "popover");
    StandardComponents::add<TextField>(registry, "textField");
    StandardComponents::add<SecretField>(registry, "secretField");
    StandardComponents::add<TextArea>(registry, "textArea");
    StandardComponents::add<FilterField>(registry, "filterField");
    StandardComponents::add<NumberField>(registry, "numberField");
    StandardComponents::add<Slider>(registry, "slider");
    StandardComponents::add<Checkbox>(registry, "checkbox");
    StandardComponents::add<Toggle>(registry, "toggle");
    StandardComponents::add<RadioGroup>(registry, "radioGroup");
    StandardComponents::add<Combo>(registry, "combo");
    StandardComponents::add<LayoutSwatch>(registry, "layoutSwatch");
    StandardComponents::add<DateTimeField>(registry, "dateTimeField");
    StandardComponents::add<ColorField>(registry, "colorField");
    StandardComponents::add<Badge>(registry, "badge");
    StandardComponents::add<StatusIndicator>(registry, "statusIndicator");
    StandardComponents::add<BusyIndicator>(registry, "busyIndicator");
    StandardComponents::add<Progress>(registry, "progress");
    StandardComponents::add<IconGraphic>(registry, "icon");
    StandardComponents::add<Image>(registry, "image");
    StandardComponents::add<Avatar>(registry, "avatar");
    StandardComponents::add<List>(registry, "list");
    StandardComponents::add<Tree>(registry, "tree");
    StandardComponents::add<Table>(registry, "table");
    StandardComponents::add<Tabs>(registry, "tabs");
    StandardComponents::add<SettingsForm>(registry, "settingsForm");
    StandardComponents::add<SettingsRow>(registry, "settingsRow");
    StandardComponents::add<SettingsActions>(registry, "settingsActions");
    StandardComponents::add<Canvas>(registry, "canvas");
    StandardComponents::add<CodeEditor>(registry, "codeEditor");
    StandardComponents::add<WebView>(registry, "webView");
    StandardComponents::add<Terminal>(registry, "terminal");
    StandardComponents::add<Markdown>(registry, "markdown");
}

} // namespace workpane::ui
