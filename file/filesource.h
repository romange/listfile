// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#ifndef FILESOURCE_H
#define FILESOURCE_H

#include "base/integral_types.h"
#include "strings/stringpiece.h"
#include "util/sinksource.h"

#include <functional>
#include <memory>

namespace file {
class ReadonlyFile;
class File;

class Source : public util::BufferredSource {
 public:
  // file must be open for reading.
  Source(ReadonlyFile* file, Ownership ownership,
         uint32 buffer_size = BufferredSource::kDefaultBufferSize);
  ~Source();

  // Moves the current file position relative to the current one.
  // Does not change the contents of the buffer.
  base::Status SkipPos(uint64 offset);

  // Returns the source wrapping the file. If the file is compressed, than the stream
  // automatically inflates the compressed data. The returned source owns the file object.
  static util::Source* Uncompressed(ReadonlyFile* file,
                                    uint32 buffer_size = BufferredSource::kDefaultBufferSize);
 private:
  bool RefillInternal();

  ReadonlyFile* file_;
  uint64 offset_ = 0;
  Ownership ownership_;
};

class Sink : public util::Sink {
public:
  // file must be open for writing.
  Sink(File* file, Ownership ownership) : file_(file), ownership_(ownership) {}
  ~Sink();
  base::Status Append(strings::Slice slice);

private:
  File* file_;
  Ownership ownership_;
};

// Assumes that source provides stream of text characters.
// Will break the stream into lines ending with EOL (either \r\n\ or \n).
class LineReader {
public:
  LineReader(util::Source* source, Ownership ownership) : source_(source),
    ownership_(ownership) {
  }

  LineReader();
  bool Open(const std::string& filename);

  explicit LineReader(const std::string& filename);

  ~LineReader();

  uint64 line_num() const { return line_num_;}

  // Overrides the result with the new null-terminated line.
  // Empty lines are also returned.
  // Returns true if new line was found or false if end of stream was reached.
  bool Next(std::string* result);

  void TEST_set_ignore_newline_at_begin(bool value = true);
private:
  util::Source* source_ = nullptr;
  Ownership ownership_;
  uint64 line_num_ = 0;
  bool ignore_newline_at_begin_ = false;
};

class CsvReader {
  LineReader reader_;
  std::function<void(const std::vector<StringPiece>&)> row_cb_;
public:
  explicit CsvReader(const std::string& filename,
    std::function<void(const std::vector<StringPiece>&)> row_cb, char delimiter = ',');

  // Indicates to skip lines starts with '#' (Default: false).
  // Must be called before SkipHeader if you want to skip hashed lines before header!
  void SetSkipHashMark() { skip_hash_mark_ = true; }

  void SkipHeader(unsigned rows = 1);
  void Run();

  bool IsValid() const { return is_valid_; }
private:
  bool is_valid_ = false;
  char delimiter_;
  bool skip_hash_mark_ = false;
};

}  // namespace file

#endif  // FILESOURCE_H
