#include "FileFinder.h"

#include "LanguageRegistry.h"
#include "plugins/PluginInterface.h"
#include "ui/Components.h"

#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace workpane::plugins::codeeditor {

class FileFinderHelper final {
  public:
    static int scoreOf(const QString& candidate, const QString& query);
};

// A run of characters found together is worth more than the same characters found apart, and a match in the file name is worth more than one in a directory above it.
int FileFinderHelper::scoreOf(const QString& candidate, const QString& query) {
    qsizetype position = 0;
    int score = 0;
    int run = 0;

    for (const QChar wanted : query) {
        const qsizetype found = candidate.indexOf(wanted, position, Qt::CaseInsensitive);
        if (found < 0) {
            return -1;
        }
        run = found == position ? run + 1 : 0;
        score += 1 + run;
        position = found + 1;
    }

    const qsizetype separator = candidate.lastIndexOf(QLatin1Char('/'));
    return position > separator ? score * 2 : score;
}

int FileMatches::fileMatchScore(const QString& path, const QString& query) {
    return query.isEmpty() ? 0 : FileFinderHelper::scoreOf(path, query);
}

QStringList FileMatches::rankedFileMatches(const QStringList& paths, const QString& query, int maximumResults) {
    QVector<std::pair<int, QString>> scored;

    for (const auto& path : paths) {
        const int score = FileMatches::fileMatchScore(path, query);
        if (score >= 0) {
            scored.append({score, path});
        }
    }

    // clang-format off
    std::stable_sort(scored.begin(), scored.end(), [](const auto& first, const auto& second) { return first.first > second.first; });
    // clang-format on
    QStringList ranked;

    for (const auto& entry : scored) {
        if (ranked.size() >= maximumResults) {
            break;
        }
        ranked.append(entry.second);
    }

    return ranked;
}

FileFinder::FileFinder(const QString& rootPath, QStringList paths, bool complete, PluginHost& host, QWidget* parent) : QDialog(parent), m_rootPath(rootPath), m_paths(std::move(paths)), m_host(host) {
    setObjectName(QStringLiteral("codeEditorFileFinder"));
    setWindowTitle(m_host.translate(QStringLiteral("code-editor.finder.title")));
    setModal(true);
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    m_query = new QLineEdit(this);
    m_query->setObjectName(QStringLiteral("codeEditorFileFinderQuery"));
    m_query->setPlaceholderText(m_host.translate(QStringLiteral("code-editor.finder.placeholder")));
    m_matches = new QListWidget(this);
    m_matches->setObjectName(QStringLiteral("codeEditorFileFinderMatches"));
    m_matches->setFrameShape(QFrame::NoFrame);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("mutedLabel"));
    layout->addWidget(m_query);
    layout->addWidget(m_matches, 1);
    layout->addWidget(m_summary);

    // clang-format off
    connect(m_query, &QLineEdit::textChanged, this, [this]() { refreshMatches(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this]() { chooseCurrent(); });
    connect(m_matches, &QListWidget::itemActivated, this, [this]() { chooseCurrent(); });
    // clang-format on

    // A workspace larger than the bound is searched only as far as it was read, and the count says so.
    m_summary->setText(complete ? m_host.translate(QStringLiteral("code-editor.finder.count")).arg(m_paths.size()) : m_host.translate(QStringLiteral("code-editor.finder.count-capped")).arg(m_paths.size()));
    refreshMatches();
}

void FileFinder::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
        const int row = m_matches->currentRow() + (event->key() == Qt::Key_Down ? 1 : -1);
        m_matches->setCurrentRow(std::clamp(row, 0, m_matches->count() - 1));
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

QString FileFinder::chosenPath() const {
    auto* item = m_matches->currentItem();
    return item == nullptr ? QString{} : QDir(m_rootPath).filePath(item->text());
}

void FileFinder::refreshMatches() {
    m_matches->clear();
    m_matches->addItems(FileMatches::rankedFileMatches(m_paths, m_query->text(), LanguageRegistry::limits().maximumReferences));

    if (m_matches->count() > 0) {
        m_matches->setCurrentRow(0);
    }
}

void FileFinder::chooseCurrent() {
    const QString path = chosenPath();

    if (path.isEmpty()) {
        return;
    }

    emit pathChosen(path);
    accept();
}

} // namespace workpane::plugins::codeeditor
