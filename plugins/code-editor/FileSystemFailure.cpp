#include "FileSystemFailure.h"

#include <QHash>

namespace workpane::plugins::codeeditor {

QString FileSystemFailures::fileSystemFailureMessage(const Error& error, PluginHost& host) {
    static const QHash<QString, QString> keys{
        {QStringLiteral("filesystem_copy_failed"), QStringLiteral("code-editor.error.copy-failed")}, {QStringLiteral("filesystem_create_directory_failed"), QStringLiteral("code-editor.error.create-directory-failed")}, {QStringLiteral("filesystem_create_file_failed"), QStringLiteral("code-editor.error.create-file-failed")}, {QStringLiteral("filesystem_destination_exists"), QStringLiteral("code-editor.error.destination-exists")}, {QStringLiteral("filesystem_directory_missing"), QStringLiteral("code-editor.error.directory-missing")}, {QStringLiteral("filesystem_directory_unavailable"), QStringLiteral("code-editor.error.directory-unavailable")}, {QStringLiteral("filesystem_file_missing"), QStringLiteral("code-editor.error.file-missing")}, {QStringLiteral("filesystem_file_too_large"), QStringLiteral("code-editor.error.file-too-large")}, {QStringLiteral("filesystem_file_unavailable"), QStringLiteral("code-editor.error.file-unavailable")}, {QStringLiteral("filesystem_move_failed"), QStringLiteral("code-editor.error.move-failed")}, {QStringLiteral("filesystem_move_invalid"), QStringLiteral("code-editor.error.move-invalid")}, {QStringLiteral("filesystem_parent_unavailable"), QStringLiteral("code-editor.error.parent-unavailable")}, {QStringLiteral("filesystem_path_invalid"), QStringLiteral("code-editor.error.path-invalid")}, {QStringLiteral("filesystem_path_unsafe"), QStringLiteral("code-editor.error.path-unsafe")}, {QStringLiteral("filesystem_read_failed"), QStringLiteral("code-editor.error.read-failed")}, {QStringLiteral("filesystem_remove_directory_failed"), QStringLiteral("code-editor.error.remove-directory-failed")}, {QStringLiteral("filesystem_remove_file_failed"), QStringLiteral("code-editor.error.remove-file-failed")}, {QStringLiteral("filesystem_source_unavailable"), QStringLiteral("code-editor.error.source-unavailable")}, {QStringLiteral("filesystem_write_failed"), QStringLiteral("code-editor.error.write-failed")}, {QStringLiteral("code_editor_workspace_invalid"), QStringLiteral("code-editor.error.folder-unavailable")},
    };
    const QString key = keys.value(error.code);

    // A limit the interface never asks for keeps its diagnostic, which is written for the log rather than for the reader.
    const QString sentence = key.isEmpty() ? error.message : host.translate(key);

    // The path a failure happened to names what the sentence is about, so the reader knows which file refused.
    return error.detail.isEmpty() ? sentence : sentence + QStringLiteral("\n") + error.detail;
}

} // namespace workpane::plugins::codeeditor
