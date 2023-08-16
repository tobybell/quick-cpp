#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace {

using std::exchange;
using std::move;

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

Str operator""_s(char const* data, unsigned long size) {
  return {data, static_cast<unsigned>(size)};
}

bool ends_with(Str str, Str suffix) {
  if (str.size < suffix.size)
    return false;
  return memcmp(
             suffix.data, str.data + (str.size - suffix.size), suffix.size) ==
      0;
}

struct String {
  char* data;
  unsigned size;
  String(): data(nullptr), size(0) {}
  explicit String(unsigned size):
    data(reinterpret_cast<char*>(malloc(size))), size(size) {}
  String(String const&) = delete;
  String(String&& rhs):
    data(exchange(rhs.data, nullptr)), size(exchange(rhs.size, 0u)) {}
  ~String() { free(data); }
  void resize(unsigned new_size) {
    data = reinterpret_cast<char*>(realloc(data, new_size));
    size = new_size;
  }
  char& operator[](unsigned index) { return data[index]; }
  char const& operator[](unsigned index) const { return data[index]; }
};

struct BytesOut {
  String storage;
  unsigned size = 0;

  char* reserve(unsigned runway) {
    unsigned needed = size + runway;
    unsigned capacity = storage.size;
    if (capacity < needed) {
      if (!capacity)
        capacity = needed;
      else
        do {
          capacity *= 2;
        } while (capacity < needed);
      storage.resize(capacity);
    }
    return &storage[size];
  }

  String take() {
    storage.resize(size);
    size = 0;
    return move(storage);
  }
};

String read_file(char const* path) {
  int fd = open(path, 0, O_RDONLY);
  check(fd >= 0);

  BytesOut out;
  while (true) {
    static constexpr unsigned ChunkSize = 16 * 1024 * 1024;
    char* chunk = out.reserve(ChunkSize);
    ssize_t n_read = read(fd, chunk, ChunkSize);
    check(n_read >= 0);
    if (!n_read)
      break;
    out.size += n_read;
  }

  close(fd);
  return out.take();
}

struct CStr {
  String storage;
  operator char const*() const { return &storage[0]; }
};

CStr path_join(char const* first, char const* second) {
  unsigned n_first = strlen(first);
  unsigned n_second = strlen(second);
  unsigned needed = n_first + n_second + 2;
  String storage {needed};
  memcpy(&storage[0], first, n_first);
  storage[n_first] = '/';
  memcpy(&storage[n_first + 1], second, n_second);
  storage[needed - 1] = '\0';
  return {move(storage)};
}

void read_directory(char const* path) {
  DIR* d = opendir(path);
  check(d);

  struct dirent* dir;
  while ((dir = readdir(d))) {
    if (ends_with(Str::from(dir->d_name), ".cc"_s)) {
      String contents = read_file(path_join(path, dir->d_name));
      write(STDERR_FILENO, contents.data, contents.size);
    }
  }
  closedir(d);
}

}  // namespace

int main() { read_directory("demo"); }
