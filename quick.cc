#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

// Next work to do:
// Create some sort of on-disk database of all function definitions, indexed by
// name.
// because of overloading, single name can map to multiple functions
// produce function definition strings, which are just the range from the
// start of the return type to the closing brace
// the full function definition string is what gets compiled. if it ever
// changes, or if its dependent prototypes change, it gets recompiled.
// Otherwise, it doesn't need to get recompiled


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
  operator Str() const { return {data, size}; }
};

struct CStr {
  String storage;
  operator char const*() const { return &storage[0]; }
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

  CStr take_null_terminated() {
    storage.resize(size + 1);
    storage[size] = '\0';
    size = 0;
    return {move(storage)};
  }
};

CStr read_file(int fd) {
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
  return out.take_null_terminated();
}

struct Token {
  unsigned start;
  unsigned size;
};

unsigned memsize(char const&) { return 1; }
unsigned memsize(Str const& s) { return s.size; }
template <unsigned N>
unsigned memsize(char const (&)[N]) {
  return N;
}

void put(char*& cursor, char const& c) { *cursor++ = c; }
void put(char*& cursor, Str const& s) { memcpy(cursor, s.data, s.size); cursor += s.size; }
template <unsigned N>
void put(char*& cursor, char const (&s)[N]) { memcpy(cursor, s, N); cursor += N; }

template <class... T>
void sprint(BytesOut& out, T const&... args) {
  auto needed = (... + memsize(args));
  auto cursor = out.reserve(needed);
  (put(cursor, args),...);
  out.size = static_cast<unsigned>(cursor - out.storage.data);
}

using u8 = unsigned char;

bool is_alphanum(char const& c) {
  return u8(c - 'A') < 26 || u8(c - 'a') < 26 || u8(c - '0') < 10;
}

bool is_identifier(char const& c) {
  return u8(c - 'A') < 26 || u8(c - 'a') < 26 || u8(c - '0') < 10 || c == '_';
}

void whitespace(char const*& it) {
  while (*it == ' ' || *it == '\n')
    ++it;
}

void parse_one_function(char const*& it, BytesOut& prototype) {
  auto return_type = it;
  while (is_alphanum(*it))
    ++it;
  sprint(prototype, Str {return_type, static_cast<unsigned>(it - return_type)}, ' ');

  whitespace(it);

  auto name = it;
  while (is_identifier(*it))
    ++it;
  sprint(prototype, Str {name, static_cast<unsigned>(it - name)}, '(');

  whitespace(it);
  check(*it == '(');
  ++it;

  whitespace(it);

  while (*it != ')') {
    auto type = it;
    while (is_identifier(*it))
      ++it;
    sprint(prototype, Str {type, static_cast<unsigned>(it - type)});
    whitespace(it);

    if (*it == ',') {
      ++it;
      whitespace(it);
      sprint(prototype, ", ");
    }
  }
  ++it;
  whitespace(it);

  sprint(prototype, ");\n");

  check(*it == '{');
  unsigned level = 1;
  while (true) {
    ++it;
    if (*it == '{') {
      ++level;
    } else if (*it == '}') {
      --level;
      if (!level)
        break;
    }
  }
  ++it;  // '}'
  whitespace(it);
}

void parse_prototypes(char const* source) {
  char const* it = source;

  BytesOut prototype;

  whitespace(it);
  while (*it)
    parse_one_function(it, prototype);

  write(STDERR_FILENO, prototype.storage.data, prototype.size);
}

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

void read_directory(char const* path, long last_build_time) {
  int dir_fd = open(path, O_RDONLY);
  DIR* d = fdopendir(dir_fd);
  check(d);

  struct dirent* dir;
  while ((dir = readdir(d))) {
    if (ends_with(Str::from(dir->d_name), ".cc"_s)) {
      int file_fd = openat(dir_fd, dir->d_name, O_RDONLY);
      struct stat file_stat;
      check(fstat(file_fd, &file_stat) == 0);

      long file_time = file_stat.st_mtime;
      if (last_build_time < file_time) {
        printf("%s changed\n", dir->d_name);
        CStr contents = read_file(file_fd);
        parse_prototypes(contents);
      }
    }
  }
  closedir(d);
}

}  // namespace

#ifdef __APPLE__
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif

int main() {
  struct stat info;

  int stat_result = stat(".quick", &info);
  printf("%d\n", stat_result);

  long last_build_time = info.st_mtime;
  printf("last build time was %ld\n", last_build_time);

  read_directory("demo", last_build_time);

  int index_fd = open(".quick", O_RDWR | O_CREAT | O_TRUNC, 644);
  check(index_fd >= 0);
  char c = 'x';
  printf("%d\n", write(index_fd, &c, 1));
}
