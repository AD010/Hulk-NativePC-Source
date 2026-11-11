#include "../src/native_pc_save_path.h"

#include <cassert>
#include <filesystem>

int main() {
  namespace fs = std::filesystem;
  using hulk::save::FolderUnderDocuments;
  assert(!FolderUnderDocuments({}).has_value());
  const fs::path documents = fs::path("example_user") / "OneDrive" / "Documents";
  const auto target = FolderUnderDocuments(documents);
  assert(target.has_value());
  assert(*target == documents / "SEGA" / "The Incredible Hulk");
  assert(target->filename() == "The Incredible Hulk");
  assert(target->parent_path().filename() == "SEGA");
  // No write or directory creation. Guest save serialization is separate.
}
