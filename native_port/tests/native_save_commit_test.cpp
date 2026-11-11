#include "../src/native_save_commit.h"
#include <cassert>
#include <fstream>
#include <vector>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#endif
namespace fs = std::filesystem;
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
  assert(argc==2);
  const fs::path root = fs::path(argv[1]) / fs::u8path("Документы");
  fs::create_directories(root);
  const auto target = root / "slot.header";
  const std::string original("first\0version", 13), next("second\0version", 14);
  assert(hulk::save::CommitMetadata(target, original.data(), original.size()));
  assert(read(target)==original);
  assert(hulk::save::CommitMetadata(target, next.data(), next.size()));
  assert(read(target)==next);
  unsigned backups=0;
  for (const auto& item:fs::directory_iterator(root)) {
    if (item.is_directory() && fs::exists(item.path()/"previous")) {
      assert(read(item.path()/"previous")==original);
      assert(!fs::exists(item.path()/"new")); ++backups;
    }
  }
  assert(backups==1);
  assert(!hulk::save::CommitMetadata(target,nullptr,10));
  assert(!hulk::save::CommitMetadata(target,next.data(),0));
  assert(read(target)==next);
  fs::create_directory(root/"directory.header");
  assert(!hulk::save::CommitMetadata(root/"directory.header",next.data(),next.size()));
#ifndef _WIN32
  // Real short-write/flush failure in a child, without filling the host disk.
  const pid_t pid=fork(); assert(pid>=0);
  if(pid==0) {
    std::signal(SIGXFSZ,SIG_IGN);
    rlimit limit{1024,1024}; assert(setrlimit(RLIMIT_FSIZE,&limit)==0);
    std::vector<uint8_t> big(65536,7);
    const bool result=hulk::save::CommitMetadata(target,big.data(),big.size());
    _exit(result ? 2 : 0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
  assert(read(target)==next);
  // Refuse a symlink leaf instead of modifying a different file.
  fs::create_symlink(target,root/"link.header");
  assert(!hulk::save::CommitMetadata(root/"link.header",original.data(),original.size()));
  assert(read(target)==next);
#endif
}
