#pragma once
#include <string>
#include <vector>

namespace pf {

// Opens a native file dialog.
// filters: A string where pairs are separated by \0. E.g. "Point Clouds\0*.las;*.laz;*.e57;*.ply\0All Files\0*.*\0"
std::string openFileDialog(const char* filters);

// Opens a native file dialog with multi-select; returns all chosen paths
// (empty on cancel). Same filter format as openFileDialog.
std::vector<std::string> openFileDialogMulti(const char* filters);

// Opens a native folder selection dialog.
std::string openFolderDialog();

// Opens a native "Save As" dialog. Same filter format as openFileDialog;
// defaultName pre-fills the file name field, defaultExt (no dot, e.g. "mp4")
// is appended when the user types a bare name. Empty string on cancel.
std::string saveFileDialog(const char* filters, const char* defaultName,
                           const char* defaultExt);

} // namespace pf
