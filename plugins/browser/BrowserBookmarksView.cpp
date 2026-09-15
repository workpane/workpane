#include "BrowserBookmarksView.h"

#include "BrowserPlugin.h"
#include "ui/Components.h"
#include "ui/Icons.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace workpane::plugins::browser {

constexpr int itemKindRole = Qt::UserRole;
constexpr int itemIdRole = Qt::UserRole + 1;
constexpr int ungroupedItemKind = 1;
constexpr int groupItemKind = 2;
constexpr int bookmarkItemKind = 3;

struct BookmarkEditorValue final {
    QString name;
    QString address;
    QString groupId;
};

class BrowserBookmarksViewHelper final {
  public:
    static const BrowserBookmarkGroup* findGroup(const QVector<BrowserBookmarkGroup>& groups, const QString& groupId);
    static const BrowserBookmark* findBookmark(const QVector<BrowserBookmark>& bookmarks, const QString& bookmarkId);
    static QDialogButtonBox* editorButtons(BrowserPlugin& plugin, QDialog& dialog);
    static std::optional<QString> editGroupValue(BrowserPlugin& plugin, QWidget* parent, const QString& currentName);
    static std::optional<BookmarkEditorValue> editBookmarkValue(BrowserPlugin& plugin, QWidget* parent, const QString& currentName, const QString& currentAddress, const QString& currentGroupId);
};

const BrowserBookmarkGroup* BrowserBookmarksViewHelper::findGroup(const QVector<BrowserBookmarkGroup>& groups, const QString& groupId) {
    for (const auto& group : groups) {
        if (group.id == groupId) {
            return &group;
        }
    }

    return nullptr;
}

const BrowserBookmark* BrowserBookmarksViewHelper::findBookmark(const QVector<BrowserBookmark>& bookmarks, const QString& bookmarkId) {
    for (const auto& bookmark : bookmarks) {
        if (bookmark.id == bookmarkId) {
            return &bookmark;
        }
    }

    return nullptr;
}

QDialogButtonBox* BrowserBookmarksViewHelper::editorButtons(BrowserPlugin& plugin, QDialog& dialog) {
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* cancel = new QPushButton(plugin.host().translate(QStringLiteral("browser.actions.cancel")), buttons);
    auto* save = new QPushButton(plugin.host().translate(QStringLiteral("browser.actions.save")), buttons);
    save->setObjectName(QStringLiteral("primaryButton"));
    buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    buttons->addButton(save, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}

std::optional<QString> BrowserBookmarksViewHelper::editGroupValue(BrowserPlugin& plugin, QWidget* parent, const QString& currentName) {
    QDialog dialog(parent);
    dialog.setWindowTitle(plugin.host().translate(currentName.isEmpty() ? QStringLiteral("browser.bookmarks.add-group") : QStringLiteral("browser.bookmarks.edit-group")));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* name = new QLineEdit(currentName, &dialog);
    name->setObjectName(QStringLiteral("browserBookmarkGroupName"));
    form->addRow(plugin.host().translate(QStringLiteral("browser.bookmarks.name")), name);
    layout->addLayout(form);
    layout->addWidget(editorButtons(plugin, dialog));
    name->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return name->text();
}

std::optional<BookmarkEditorValue> BrowserBookmarksViewHelper::editBookmarkValue(BrowserPlugin& plugin, QWidget* parent, const QString& currentName, const QString& currentAddress, const QString& currentGroupId) {
    QDialog dialog(parent);
    dialog.setWindowTitle(plugin.host().translate(currentName.isEmpty() ? QStringLiteral("browser.bookmarks.add-bookmark") : QStringLiteral("browser.bookmarks.edit-bookmark")));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* name = new QLineEdit(currentName, &dialog);
    auto* address = new QLineEdit(currentAddress, &dialog);
    auto* group = new ui::ComboBox(plugin.host().theme(), &dialog);
    name->setObjectName(QStringLiteral("browserBookmarkName"));
    address->setObjectName(QStringLiteral("browserBookmarkAddress"));
    group->setObjectName(QStringLiteral("browserBookmarkGroup"));

    for (const auto& candidate : plugin.bookmarkGroups()) {
        group->addItem(candidate.name, candidate.id);
    }

    ui::Components::sortComboBoxItems(group);
    // The ungrouped collection is the choice that means none, so it opens the list instead of being sorted into the names.
    group->insertItem(0, plugin.host().translate(QStringLiteral("browser.bookmarks.ungrouped")), QString());
    group->setCurrentIndex(std::max(0, group->findData(currentGroupId)));
    form->addRow(plugin.host().translate(QStringLiteral("browser.bookmarks.name")), name);
    form->addRow(plugin.host().translate(QStringLiteral("browser.bookmarks.address")), address);
    form->addRow(plugin.host().translate(QStringLiteral("browser.bookmarks.group")), group);
    layout->addLayout(form);
    layout->addWidget(editorButtons(plugin, dialog));
    name->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return BookmarkEditorValue{name->text(), address->text(), group->currentData().toString()};
}

BookmarkTree::BookmarkTree(QWidget* parent) : QTreeWidget(parent) {
    setAcceptDrops(true);
    setDefaultDropAction(Qt::MoveAction);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDragEnabled(true);
    setDropIndicatorShown(true);
}

void BookmarkTree::dropEvent(QDropEvent* event) {
    QTreeWidget::dropEvent(event);

    if (event->isAccepted()) {
        emit layoutChanged();
    }
}

BrowserBookmarksView::BrowserBookmarksView(BrowserPlugin& plugin, QWidget* parent) : QWidget(parent), m_plugin(plugin) {
    setObjectName(QStringLiteral("browserBookmarksPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);
    setMaximumWidth(420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* headerBand = new QWidget(this);
    headerBand->setObjectName(QStringLiteral("browserBookmarksHeader"));
    headerBand->setAttribute(Qt::WA_StyledBackground, true);
    auto* header = new QHBoxLayout(headerBand);
    header->setContentsMargins(10, 6, 6, 6);
    auto* title = ui::Components::sectionTitleLabel(m_plugin.host().translate(QStringLiteral("browser.bookmarks.title")), headerBand);
    auto* addGroupButton = ui::Components::toolButton(ui::IconName::Folder, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.bookmarks.add-group")), headerBand);
    auto* addBookmarkButton = ui::Components::toolButton(ui::IconName::Bookmark, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.bookmarks.add-bookmark")), headerBand);
    m_edit = ui::Components::toolButton(ui::IconName::Edit, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.edit")), headerBand);
    m_remove = ui::Components::toolButton(ui::IconName::Clear, m_plugin.host().theme(), m_plugin.host().translate(QStringLiteral("browser.actions.remove")), headerBand);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(addGroupButton);
    header->addWidget(addBookmarkButton);
    header->addWidget(m_edit);
    header->addWidget(m_remove);
    root->addWidget(headerBand);

    m_tree = new BookmarkTree(this);
    m_tree->setObjectName(QStringLiteral("browserBookmarksTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    root->addWidget(m_tree, 1);

    auto* actionBand = new QWidget(this);
    actionBand->setObjectName(QStringLiteral("browserBookmarksActions"));
    actionBand->setAttribute(Qt::WA_StyledBackground, true);
    auto* actions = new QHBoxLayout(actionBand);
    actions->setContentsMargins(10, 6, 10, 8);
    m_openCurrent = new QPushButton(m_plugin.host().translate(QStringLiteral("browser.bookmarks.open-current")), actionBand);
    m_openNew = new QPushButton(m_plugin.host().translate(QStringLiteral("browser.bookmarks.open-new")), actionBand);
    m_openCurrent->setObjectName(QStringLiteral("browserBookmarkOpenCurrent"));
    m_openNew->setObjectName(QStringLiteral("browserBookmarkOpenNew"));
    actions->addWidget(m_openCurrent);
    actions->addWidget(m_openNew);
    root->addWidget(actionBand);

    connect(addGroupButton, &QToolButton::clicked, this, &BrowserBookmarksView::addGroup);
    connect(addBookmarkButton, &QToolButton::clicked, this, &BrowserBookmarksView::addBookmarkRequested);
    connect(m_edit, &QToolButton::clicked, this, &BrowserBookmarksView::editSelected);
    connect(m_remove, &QToolButton::clicked, this, &BrowserBookmarksView::removeSelected);
    connect(m_openCurrent, &QPushButton::clicked, this, &BrowserBookmarksView::openSelectedInCurrentTab);
    connect(m_openNew, &QPushButton::clicked, this, &BrowserBookmarksView::openSelectedInNewTab);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &BrowserBookmarksView::openSelectedFromDoubleClick);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &BrowserBookmarksView::showContextMenu);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &BrowserBookmarksView::updateActions);
    // clang-format off
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() { ui::Components::repaintTreeGlyphs(m_tree, m_plugin.host().theme()); });
    // clang-format on
    connect(m_tree, &BookmarkTree::layoutChanged, this, &BrowserBookmarksView::applyTreeLayout);
    connect(&m_plugin, &BrowserPlugin::bookmarksChanged, this, &BrowserBookmarksView::rebuild);
    rebuild();
}

void BrowserBookmarksView::beginAddBookmark(const QString& suggestedName, const QUrl& suggestedUrl) {
    const QString groupId = selectedGroupId();
    const QString addressName = suggestedUrl.host().isEmpty() ? suggestedUrl.toString() : suggestedUrl.host();
    const QString name = suggestedName.trimmed().isEmpty() ? addressName : suggestedName.trimmed();
    const auto value = BrowserBookmarksViewHelper::editBookmarkValue(m_plugin, this, name, suggestedUrl.toString(), groupId);

    if (!value.has_value()) {
        return;
    }

    const auto result = m_plugin.createBookmark(value->name, value->address, value->groupId);

    if (!result.hasValue()) {
        notifyInvalidEditorValue();
        return;
    }

    if (QTreeWidgetItem* created = bookmarkItem(result.value()); created != nullptr) {
        m_tree->setCurrentItem(created);
    }
}

void BrowserBookmarksView::addGroup() {
    const auto value = BrowserBookmarksViewHelper::editGroupValue(m_plugin, this, {});

    if (!value.has_value()) {
        return;
    }

    const auto result = m_plugin.createBookmarkGroup(value.value());

    if (!result.hasValue()) {
        notifyInvalidEditorValue();
    }
}

void BrowserBookmarksView::editSelected() {
    const QString groupId = selectedGroupId();

    if (!groupId.isEmpty()) {
        editGroup(groupId);
        return;
    }

    const QString bookmarkId = selectedBookmarkId();

    if (!bookmarkId.isEmpty()) {
        editBookmark(bookmarkId);
    }
}

void BrowserBookmarksView::removeSelected() {
    const QString bookmarkId = selectedBookmarkId();

    if (!bookmarkId.isEmpty()) {
        const BrowserBookmark* bookmark = BrowserBookmarksViewHelper::findBookmark(m_plugin.bookmarks(), bookmarkId);
        if (bookmark == nullptr) {
            notifyInvalidEditorValue();
            return;
        }
        if (m_plugin.host().confirm(this, m_plugin.host().translate(QStringLiteral("browser.bookmarks.remove-bookmark")), m_plugin.host().translate(QStringLiteral("browser.bookmarks.remove-bookmark-question")), bookmark->name, m_plugin.host().translate(QStringLiteral("browser.actions.remove")), true)) {
            const auto result = m_plugin.removeBookmark(bookmarkId);
            if (!result.hasValue()) {
                notifyInvalidEditorValue();
            }
        }
        return;
    }

    const QString groupId = selectedGroupId();

    if (groupId.isEmpty()) {
        return;
    }

    const BrowserBookmarkGroup* group = BrowserBookmarksViewHelper::findGroup(m_plugin.bookmarkGroups(), groupId);

    if (group == nullptr) {
        notifyInvalidEditorValue();
        return;
    }

    if (m_plugin.host().confirm(this, m_plugin.host().translate(QStringLiteral("browser.bookmarks.remove-group")), m_plugin.host().translate(QStringLiteral("browser.bookmarks.remove-group-question")), group->name, m_plugin.host().translate(QStringLiteral("browser.actions.remove")), true)) {
        const auto result = m_plugin.removeBookmarkGroup(groupId);
        if (!result.hasValue()) {
            notifyInvalidEditorValue();
        }
    }
}

void BrowserBookmarksView::openSelectedInCurrentTab() {
    const QString bookmarkId = selectedBookmarkId();
    const BrowserBookmark* bookmark = BrowserBookmarksViewHelper::findBookmark(m_plugin.bookmarks(), bookmarkId);

    if (bookmark != nullptr) {
        emit openRequested(bookmark->url, false);
    }
}

void BrowserBookmarksView::openSelectedInNewTab() {
    const QString bookmarkId = selectedBookmarkId();
    const BrowserBookmark* bookmark = BrowserBookmarksViewHelper::findBookmark(m_plugin.bookmarks(), bookmarkId);

    if (bookmark != nullptr) {
        emit openRequested(bookmark->url, true);
    }
}

void BrowserBookmarksView::openSelectedFromDoubleClick(QTreeWidgetItem* item, int) {
    if (item != nullptr && item->data(0, itemKindRole).toInt() == bookmarkItemKind) {
        m_tree->setCurrentItem(item);
        openSelectedInCurrentTab();
    }
}

void BrowserBookmarksView::showContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = m_tree->itemAt(position);

    if (item == nullptr || item->data(0, itemKindRole).toInt() != bookmarkItemKind) {
        return;
    }

    m_tree->setCurrentItem(item);
    QMenu menu(this);
    QAction* openCurrent = menu.addAction(m_plugin.host().translate(QStringLiteral("browser.bookmarks.open-current")));
    QAction* openNew = menu.addAction(m_plugin.host().translate(QStringLiteral("browser.bookmarks.open-new")));
    menu.addSeparator();
    QAction* edit = menu.addAction(m_plugin.host().translate(QStringLiteral("browser.actions.edit")));
    QAction* remove = menu.addAction(m_plugin.host().translate(QStringLiteral("browser.actions.remove")));
    QAction* selected = menu.exec(m_tree->viewport()->mapToGlobal(position));

    if (selected == openCurrent) {
        openSelectedInCurrentTab();
        return;
    }

    if (selected == openNew) {
        openSelectedInNewTab();
        return;
    }

    if (selected == edit) {
        editSelected();
        return;
    }

    if (selected == remove) {
        removeSelected();
    }
}

void BrowserBookmarksView::applyTreeLayout() {
    if (m_rebuilding) {
        return;
    }

    QVector<QString> groupIds;
    QVector<BrowserBookmarkPlacement> bookmarks;

    for (int topIndex = 0; topIndex < m_tree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* top = m_tree->topLevelItem(topIndex);
        const int kind = top->data(0, itemKindRole).toInt();
        if (kind == groupItemKind) {
            const QString groupId = top->data(0, itemIdRole).toString();
            groupIds.append(groupId);
            for (int childIndex = 0; childIndex < top->childCount(); ++childIndex) {
                QTreeWidgetItem* child = top->child(childIndex);
                if (child->data(0, itemKindRole).toInt() == bookmarkItemKind) {
                    bookmarks.append({child->data(0, itemIdRole).toString(), groupId});
                }
            }
            continue;
        }
        if (kind == ungroupedItemKind) {
            for (int childIndex = 0; childIndex < top->childCount(); ++childIndex) {
                QTreeWidgetItem* child = top->child(childIndex);
                if (child->data(0, itemKindRole).toInt() == bookmarkItemKind) {
                    bookmarks.append({child->data(0, itemIdRole).toString(), {}});
                }
            }
            continue;
        }
        if (kind == bookmarkItemKind) {
            bookmarks.append({top->data(0, itemIdRole).toString(), {}});
        }
    }

    const auto result = m_plugin.applyBookmarkLayout(groupIds, bookmarks);

    if (!result.hasValue()) {
        rebuild();
        notifyInvalidEditorValue();
    }
}

void BrowserBookmarksView::rebuild() {
    const QString selectedId = selectedBookmarkId().isEmpty() ? selectedGroupId() : selectedBookmarkId();
    m_rebuilding = true;
    m_tree->clear();
    auto* ungrouped = new QTreeWidgetItem(m_tree, QStringList{m_plugin.host().translate(QStringLiteral("browser.bookmarks.ungrouped"))});
    ungrouped->setData(0, itemKindRole, ungroupedItemKind);
    ui::Components::setItemGlyph(ungrouped, 0, ui::IconName::Folder, ui::ThemeColor::Text, m_plugin.host().theme());
    ungrouped->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);

    for (const auto& bookmark : m_plugin.bookmarks()) {
        if (!bookmark.groupId.isEmpty()) {
            continue;
        }
        auto* item = new QTreeWidgetItem(ungrouped, QStringList{bookmark.name});
        item->setData(0, itemKindRole, bookmarkItemKind);
        item->setData(0, itemIdRole, bookmark.id);
        ui::Components::setItemGlyph(item, 0, ui::IconName::Bookmark, ui::ThemeColor::Text, m_plugin.host().theme());
        item->setToolTip(0, bookmark.url.toString());
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }

    ungrouped->setExpanded(true);

    for (const auto& group : m_plugin.bookmarkGroups()) {
        auto* groupItem = new QTreeWidgetItem(m_tree, QStringList{group.name});
        groupItem->setData(0, itemKindRole, groupItemKind);
        groupItem->setData(0, itemIdRole, group.id);
        ui::Components::setItemGlyph(groupItem, 0, ui::IconName::Folder, ui::ThemeColor::Text, m_plugin.host().theme());
        groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
        for (const auto& bookmark : m_plugin.bookmarks()) {
            if (bookmark.groupId != group.id) {
                continue;
            }
            auto* item = new QTreeWidgetItem(groupItem, QStringList{bookmark.name});
            item->setData(0, itemKindRole, bookmarkItemKind);
            item->setData(0, itemIdRole, bookmark.id);
            ui::Components::setItemGlyph(item, 0, ui::IconName::Bookmark, ui::ThemeColor::Text, m_plugin.host().theme());
            item->setToolTip(0, bookmark.url.toString());
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        groupItem->setExpanded(true);
    }

    if (!selectedId.isEmpty()) {
        QTreeWidgetItemIterator iterator(m_tree);
        while (*iterator != nullptr) {
            if ((*iterator)->data(0, itemIdRole).toString() == selectedId) {
                m_tree->setCurrentItem(*iterator);
                break;
            }
            ++iterator;
        }
    }

    m_rebuilding = false;
    ui::Components::repaintTreeGlyphs(m_tree, m_plugin.host().theme());
    updateActions();
}

void BrowserBookmarksView::updateActions() {
    const bool editable = !selectedBookmarkId().isEmpty() || !selectedGroupId().isEmpty();
    const bool openable = !selectedBookmarkId().isEmpty();
    m_edit->setEnabled(editable);
    m_remove->setEnabled(editable);
    m_openCurrent->setEnabled(openable);
    m_openNew->setEnabled(openable);
}

QString BrowserBookmarksView::selectedGroupId() const {
    QTreeWidgetItem* item = m_tree->currentItem();
    return item != nullptr && item->data(0, itemKindRole).toInt() == groupItemKind ? item->data(0, itemIdRole).toString() : QString{};
}

QString BrowserBookmarksView::selectedBookmarkId() const {
    QTreeWidgetItem* item = m_tree->currentItem();
    return item != nullptr && item->data(0, itemKindRole).toInt() == bookmarkItemKind ? item->data(0, itemIdRole).toString() : QString{};
}

QTreeWidgetItem* BrowserBookmarksView::bookmarkItem(const QString& bookmarkId) const {
    QTreeWidgetItemIterator iterator(m_tree);

    while (*iterator != nullptr) {
        if ((*iterator)->data(0, itemKindRole).toInt() == bookmarkItemKind && (*iterator)->data(0, itemIdRole).toString() == bookmarkId) {
            return *iterator;
        }
        ++iterator;
    }

    return nullptr;
}

void BrowserBookmarksView::editGroup(const QString& groupId) {
    const BrowserBookmarkGroup* group = BrowserBookmarksViewHelper::findGroup(m_plugin.bookmarkGroups(), groupId);

    if (group == nullptr) {
        notifyInvalidEditorValue();
        return;
    }

    const auto value = BrowserBookmarksViewHelper::editGroupValue(m_plugin, this, group->name);

    if (value.has_value() && !m_plugin.updateBookmarkGroup(groupId, value.value()).hasValue()) {
        notifyInvalidEditorValue();
    }
}

void BrowserBookmarksView::editBookmark(const QString& bookmarkId) {
    const BrowserBookmark* bookmark = BrowserBookmarksViewHelper::findBookmark(m_plugin.bookmarks(), bookmarkId);

    if (bookmark == nullptr) {
        notifyInvalidEditorValue();
        return;
    }

    const auto value = BrowserBookmarksViewHelper::editBookmarkValue(m_plugin, this, bookmark->name, bookmark->url.toString(), bookmark->groupId);

    if (value.has_value() && !m_plugin.updateBookmark(bookmarkId, value->name, value->address, value->groupId).hasValue()) {
        notifyInvalidEditorValue();
    }
}

void BrowserBookmarksView::notifyInvalidEditorValue() {
    m_plugin.host().notify(m_plugin.host().translate(QStringLiteral("browser.plugin.title")), m_plugin.host().translate(QStringLiteral("browser.error.invalid-bookmark")), AlertSeverity::Error);
}

} // namespace workpane::plugins::browser
