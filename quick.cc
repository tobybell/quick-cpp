#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

namespace {

void check(bool condition) {
  if (!condition)
    abort();
}

unsigned strlen(char const* str) {
  unsigned i = 0;
  while (str[i])
    ++i;
  return i;
}

struct Str {
  char const* data;
  unsigned size;
  static Str from(char const* str) { return {str, strlen(str)}; }
};

[[maybe_unused]] Str operator""_s(char const* data, unsigned long size) {
  return {data, static_cast<unsigned>(size)};
}

bool ends_with(Str str, Str suffix) {
  if (str.size < suffix.size)
    return false;
  return memcmp(
             suffix.data, str.data + (str.size - suffix.size), suffix.size) ==
      0;
}

void read_directory(char const* path) {
  DIR* d = opendir(path);
  check(d);

  struct dirent* dir;
  while ((dir = readdir(d))) {
    if (ends_with(Str::from(dir->d_name), ".cc"_s))
      printf("%s\n", dir->d_name);
  }
  closedir(d);
}

}  // namespace

int main() {
  read_directory("demo");
}
