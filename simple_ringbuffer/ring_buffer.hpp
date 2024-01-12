#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define ALIGN(x, a) (((x) + (a)-1) & ~((a)-1))
#define PAGE_ALIGN(x) ALIGN(x, 4096)
#define ALIGN_8(x) ALIGN(x, 8)

// Buffer types
template<typename T>
class Buffer {
 public:
  void add(const T& value) { buffer.push_back(value); }
  size_t serializedSize() const { return ALIGN_8(buffer.size() * sizeof(T)); }
  const T* data() const { return buffer.data(); }
  void clear() { buffer.clear(); }
  size_t size() const { return buffer.size(); }

 private:
  std::vector<T> buffer;
};

// Specialization for std::string and std::string_view
template<>
class Buffer<std::string> {
 public:
  void add(const std::string& value) { addStringData(value); }
  void add(const std::string_view& value) { addStringData(value); }
  size_t serializedSize() const { return ALIGN_8(buffer.size()); }
  const char* data() const { return buffer.data(); }
  void clear() { buffer.clear(); }
  size_t size() const { return n_elem; }

 private:
  std::vector<char> buffer;
  size_t n_elem = 0;

  template<typename StringType>
  void addStringData(const StringType& value) {
    buffer.reserve(buffer.size() + value.size() + 1);
    std::copy(value.begin(), value.end(), std::back_inserter(buffer));
    buffer.push_back('\0');
    n_elem++;
  }
};

struct RBIn {
  Buffer<int64_t> lines;
  Buffer<std::string> filenames;
  Buffer<std::string> names;
  std::vector<int64_t> values;

  size_t serializedSize() const {
    size_t total_size = sizeof(size_t) * 7;  // FIXME make this computed
    total_size += lines.serializedSize();
    total_size += filenames.serializedSize();
    total_size += names.serializedSize();
    total_size += values.size() * sizeof(int64_t);
    return total_size;
  }

  void push(std::string_view filename, std::string_view name, int64_t line) {
    filenames.add(filename);
    names.add(name);
    lines.add(line);
  }

  size_t get_size() const { return lines.size(); }

  void clear() {
    lines.clear();
    filenames.clear();
    names.clear();
    values.clear();
  }

  void serialize(const size_t val, unsigned char *&write_ptr) {
    memcpy(write_ptr, &val, sizeof(val));
    write_ptr += sizeof(val);
  }

  template<typename T>
  void serialize_buffer(const Buffer<T> &buffer, unsigned char *&write_ptr) {
    size_t sz = buffer.serializedSize();
    memcpy(write_ptr, buffer.data(), sz);
    write_ptr += sz;
  }

  template<typename T>
  void serialize_vector(const std::vector<T> &buffer, unsigned char *&write_ptr) {
    size_t sz = buffer.size() * sizeof(T);
    memcpy(write_ptr, buffer.data(), sz);
    write_ptr += sz;
  }

  void serialize(unsigned char *write_ptr) {
    size_t serialized_sz = serializedSize();
    size_t sz = sizeof(size_t);
    serialize(serialized_sz, write_ptr);
    serialize(get_size(), write_ptr);
    serialize(values.size(), write_ptr);

    // Store the offsets
    size_t lines_offset = 7 * sizeof(size_t);  // FIXME make this computed
    size_t filenames_offset = lines_offset + lines.serializedSize();
    size_t names_offset = filenames_offset + filenames.serializedSize();
    size_t values_offset = names_offset + names.serializedSize();

    // Write down the offsets
    serialize(lines_offset, write_ptr);
    serialize(filenames_offset, write_ptr);
    serialize(names_offset, write_ptr);
    serialize(values_offset, write_ptr);

    // Next, write the lines
    serialize_buffer<int64_t>(lines, write_ptr);
    serialize_buffer<std::string>(filenames, write_ptr);
    serialize_buffer<std::string>(names, write_ptr);
    serialize_vector<int64_t>(values, write_ptr);
  }
};

using IteratorFunction = std::function<void(std::string_view, std::string_view, int64_t)>;
class RBOut {
 public:
  RBOut(unsigned char *_buffer, IteratorFunction _iterate) : buffer_start{_buffer}, iterate{_iterate} { deserialize(); }

  size_t get_size() const { return buffer_size; }

 private:
  const unsigned char *buffer_start;
  size_t buffer_size;
  IteratorFunction iterate;

  template<typename T>
  T read(unsigned char *&buffer) {
    T val = *(T *)buffer;
    buffer += sizeof(T);
    return val;
  }
  size_t read(unsigned char *&buffer) { return read<size_t>(buffer); }


  void deserialize() {
    unsigned char *buffer = const_cast<unsigned char *>(buffer_start);

    // Read the buffer size
    buffer_size = read(buffer);
    size_t num_entries = read(buffer);
    size_t num_values = read(buffer);

    // Read the offsets
    size_t lines_offset = read(buffer);
    size_t filenames_offset = read(buffer);
    size_t names_offset = read(buffer);
    size_t values_offset = read(buffer);

    // Read the lines
    const int64_t *lines = reinterpret_cast<const int64_t *>(buffer_start + lines_offset);
    buffer += lines_offset;

    // Read the filenames
    const char *filenames = reinterpret_cast<const char *>(buffer_start + filenames_offset);
    buffer += filenames_offset;

    // Read the names
    const char *names = reinterpret_cast<const char *>(buffer_start + names_offset);
    buffer += names_offset;

    // Read the values
    std::vector<int64_t> values;
    for (size_t i = 0; i < num_values; i++) {
      values.push_back(*(int64_t *)(buffer_start + values_offset + (i * sizeof(int64_t))));
    }

    // Iterate over the entries
    for (size_t i = 0; i < num_entries; i++) {
      iterate(filenames, names, lines[i]);
      filenames += strlen(filenames) + 1;
      names += strlen(names) + 1;
    }
  }
};

// A simple shared-memory ringbuffer
class RingBuffer {
 public:
  RingBuffer(size_t size) {
    this->size = PAGE_ALIGN(size);

    // We want a file-descriptor backed region, we try a few ways to get one.
    if (!buffer_from_memfd() && !buffer_from_tmpfile()) {
      throw std::runtime_error("Failed to create ring buffer");
    }
  }
  ~RingBuffer() { munmap(buffer, size * 2); }

  void *operator new(size_t sz) {
    void *ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
      throw std::bad_alloc();
    }
    return ptr;
  }

  void operator delete(void *ptr) { munmap(ptr, sizeof(RingBuffer)); }

  bool write(RBIn &entry) {
    size_t serialized_sz = entry.serializedSize();
    bool pending = true;
    size_t saved_write = 0;
    int tries = 3;

    while (pending) {
      saved_write = write_pos.load(std::memory_order_relaxed);
      size_t saved_read = read_pos.load(std::memory_order_acquire);
      size_t next_write = saved_write + serialized_sz;  // don't wraparound yet

      // Since the buffer is mirrors, we just add the size to the read position and
      // compare that to the new write position.
      if (next_write < saved_read + size) {
        // We have enough room.  Do a compare and swap.
        if (write_pos.compare_exchange_strong(saved_write, next_write % size)) {
          pending = false;
        } else {
          // CAS failed, try again
          if (--tries > 0) {
            std::this_thread::yield();
            continue;
          } else {
            // We've tried too many times, fail
            return false;
          }
        }
      } else {
        // There's not enough room, fail
        return false;
      }
    }

    // If we're here, then we've successfully reserved space in the buffer.
    // Write down the serialized size
    entry.serialize(buffer + saved_write);
    return true;
  }

  bool read(IteratorFunction fun) {
    // If there's no data to read, then return false
    if (read_pos.load(std::memory_order_relaxed) == write_pos.load(std::memory_order_acquire)) {
      return false;
    }
    RBOut out{read_pos.load(std::memory_order_relaxed) + buffer, fun};

    // Add the size of the entry to the read position and wraparound
    size_t serialized_sz = out.get_size();
    size_t saved_read = read_pos.load(std::memory_order_relaxed);
    size_t next_read = saved_read + serialized_sz;
    read_pos.store(next_read % size, std::memory_order_release);
    return true;
  }

 private:
  size_t size;
  std::atomic<size_t> read_pos{0};
  std::atomic<size_t> write_pos{0};
  unsigned char *buffer;
  unsigned char *buffer_mirror;

  bool try_map(int fd) {
    // Map the memfd into memory.  Map 2x what we need so we can mirror the buffer
    void *buffer = mmap(NULL, size * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
      close(fd);
      return false;
    }

    void *buffer_mirror =
      mmap((void *)((uintptr_t)buffer + size), size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (buffer_mirror == MAP_FAILED) {
      munmap(buffer, size * 2);
      close(fd);
      return false;
    }

    // Store the buffer and fd
    this->buffer = (unsigned char *)buffer;
    this->buffer_mirror = (unsigned char *)buffer_mirror;
    return true;
  }

  bool buffer_from_memfd() {
    // Create a memfd
    int fd = memfd_create("ring_buffer", MFD_CLOEXEC);
    if (fd == -1) {
      return false;
    }

    // Resize the memfd to the desired size
    if (ftruncate(fd, size) == -1) {
      close(fd);
      return false;
    }

    // Try to map the buffer
    bool ret = try_map(fd);
    close(fd);
    return ret;
  }
  bool buffer_from_tmpfile() {
    // Directories to try
    constexpr std::array<std::string_view, 4> dirs = {
      "/tmp"
      "/dev/shm",
      "/run/shm",
      "/var/tmp",
      "."};

    int fd = -1;
    for (const auto &dir : dirs) {
      fd = open(dir.data(), O_TMPFILE | O_RDWR | O_EXCL | O_CLOEXEC, 0600);
      if (fd != -1)
        break;
    }

    // If we failed to open a tmpfile, we're out of luck
    if (fd == -1) {
      return false;
    }

    // Resize the memfd to the desired size
    if (ftruncate(fd, size) == -1) {
      close(fd);
      return false;
    }

    // Try to map the buffer
    bool ret = try_map(fd);
    close(fd);
    return ret;
  }
};
