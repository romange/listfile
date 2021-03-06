// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
// Modified LevelDB implementation of log_format.
#ifndef _LIST_FILE_H_
#define _LIST_FILE_H_

#include <map>

#include "base/logging.h"   // For CHECK.
#include "file/file.h"
#include "file/list_file_format.h"
#include "strings/slice.h"
#include "strings/strcat.h"
#include "util/sinksource.h"



namespace file {

extern const char kProtoSetKey[], kProtoTypeKey[];

class ListWriter {
 public:
  struct Options {
    uint8 block_size_multiplier = 1;  // the block size is 64KB * multiplier
    bool use_compression = true;
    uint8 compress_method = list_file::kCompressionLZ4;
    uint8 compress_level = 1;
    bool append = false;

    Options() {}
  };

  // Takes ownership over sink.
  ListWriter(util::Sink* sink, const Options& options = Options());

  // Create a writer that will overwrite filename with data.
  ListWriter(StringPiece filename, const Options& options = Options());
  ~ListWriter();

  // Adds user provided meta information about the file. Must be called before Init.
  void AddMeta(StringPiece key, StringPiece value);

  base::Status Init();
  base::Status AddRecord(StringPiece slice);
  base::Status Flush();

  uint32 records_added() const { return records_added_;}
  uint64 bytes_added() const { return bytes_added_;}
  uint64 compression_savings() const { return compression_savings_;}
 private:

  std::unique_ptr<util::Sink> dest_;
  std::unique_ptr<uint8[]> array_store_;
  std::unique_ptr<uint8[]> compress_buf_;
  std::map<std::string, std::string> meta_;

  uint8* array_next_ = nullptr, *array_end_ = nullptr;  // wraps array_store_
  bool init_called_ = false;

  Options options_;
  uint32 array_records_ = 0;
  uint32 block_offset_ = 0;      // Current offset in block

  uint32 block_size_ = 0;
  uint32 block_leftover_ = 0;
  size_t compress_buf_size_ = 0;

  uint32 records_added_ = 0;
  uint64 bytes_added_ = 0, compression_savings_ = 0;

  void Construct();

  base::Status EmitPhysicalRecord(list_file::RecordType type, const uint8* ptr,
                                  size_t length);

  uint32 block_leftover() const { return block_leftover_; }

  void AddRecordToArray(strings::Slice size_enc, strings::Slice record);
  base::Status FlushArray();

  base::Status (*compress_func_)(int level, const void* src, size_t len, void* dest,
                                 size_t* compress_size) = nullptr;
  // No copying allowed
  ListWriter(const ListWriter&) = delete;
  void operator=(const ListWriter&) = delete;
};

class ListReader {
 public:
  // Create a Listreader that will return log records from "*file".
  // "*file" must remain live while this ListReader is in use.
  //
  // If "reporter" is non-NULL, it is notified whenever some data is
  // dropped due to a detected corruption.  "*reporter" must remain
  // live while this ListReader is in use.
  //
  // If "checksum" is true, verify checksums if available.
  //
  // The ListReader will start reading at the first record located at position >= initial_offset
  // relative to start of list records. In afact all positions mentioned in the API are
  // relative to list start position in the file (i.e. file header is read internally and its size
  // is not relevant for the API).
  typedef std::function<void(size_t bytes, const base::Status& status)> CorruptionReporter;

  // initial_offset - file offset AFTER the file header, i.e. offset 0 does not skip anything.
  // File header is read in any case.
  explicit ListReader(ReadonlyFile* file, Ownership ownership, bool checksum = false,
                      CorruptionReporter = nullptr);

  // This version reads the file and owns it.
  explicit ListReader(StringPiece filename, bool checksum = false,
                      CorruptionReporter = nullptr);

  ~ListReader();

  bool GetMetaData(std::map<std::string, std::string>* meta);

  // Read the next record into *record.  Returns true if read
  // successfully, false if we hit end of file. May use
  // "*scratch" as temporary storage.  The contents filled in *record
  // will only be valid until the next mutating operation on this
  // reader or the next mutation to *scratch.
  // If invalid record is encountered, read will continue to the next record and
  // will notify reporter about the corruption.
  bool ReadRecord(strings::Slice* record, std::string* scratch);

  // Returns the offset of the last record read by ReadRecord relative to list start position
  // in the file.
  // Undefined before the first call to ReadRecord.
  //size_t LastRecordOffset() const { return last_record_offset_; }

  void Reset() {
    block_size_ = file_offset_ = array_records_ = 0;
    eof_ = false;
  }

  uint32 read_header_bytes() const { return read_header_bytes_;}
  uint32 read_data_bytes() const { return read_data_bytes_; }
private:
  bool ReadHeader();

  // 'size' is size of the compressed blob.
  // Returns true if succeeded. In that case uncompress_buf_ will contain the uncompressed data
  // and size will be updated to the uncompressed size.
  bool Uncompress(const uint8* data_ptr, uint32* size);

  ReadonlyFile* file_;
  size_t file_offset_ = 0;
  size_t read_header_bytes_ = 0;  // how much headers bytes were read so far.
  size_t read_data_bytes_ = 0;  // how much data bytes were read so far.

  Ownership ownership_;
  CorruptionReporter const reporter_;
  bool const checksum_;
  std::unique_ptr<uint8[]> backing_store_;
  std::unique_ptr<uint8[]> uncompress_buf_;
  strings::Slice block_buffer_;
  std::map<std::string, std::string> meta_;

  bool eof_ = false;   // Last Read() indicated EOF by returning < kBlockSize

  // Offset of the last record returned by ReadRecord.
  // size_t last_record_offset_;
  // Offset of the first location past the end of buffer_.
  // size_t end_of_buffer_offset_ = 0;

  // Offset at which to start looking for the first record to return
  // size_t const initial_offset_;
  uint32 block_size_ = 0;
  uint32 array_records_ = 0;
  strings::Slice array_store_;

  // Extend record types with the following special values
  enum {
    kEof = list_file::kMaxRecordType + 1,
    // Returned whenever we find an invalid physical record.
    // Currently there are three situations in which this happens:
    // * The record has an invalid CRC (ReadPhysicalRecord reports a drop)
    // * The record is a 0-length record (No drop is reported)
    // * The record is below constructor's initial_offset (No drop is reported)
    kBadRecord = list_file::kMaxRecordType + 2
  };

  // Skips all blocks that are completely before "initial_offset_".
  // base::Status SkipToInitialBlock();

  // Return type, or one of the preceding special values
  unsigned int ReadPhysicalRecord(strings::Slice* result);

  // Reports dropped bytes to the reporter.
  // buffer_ must be updated to remove the dropped bytes prior to invocation.
  void ReportCorruption(size_t bytes, const std::string& reason);
  void ReportDrop(size_t bytes, const base::Status& reason);

  // No copying allowed
  ListReader(const ListReader&) = delete;
  void operator=(const ListReader&) = delete;
};

template<typename T> void ReadProtoRecords(StringPiece name,
                                           std::function<void(T&&)> cb,
                                           bool need_metadata = false) {
  ListReader reader(name);
  ReadProtoRecords(&reader, cb, need_metadata, &name);
}

template<typename T> void ReadProtoRecords(ReadonlyFile* file,
                                           std::function<void(T&&)> cb,
                                           bool need_metadata = false) {
  ListReader reader(file, TAKE_OWNERSHIP);
  ReadProtoRecords(&reader, cb, need_metadata);
}

namespace internal {
void ReadProtoRecordsImpl(ListReader *reader_p,
                          bool(*parse_and_cb)(strings::Slice&&, void *cb2),
                          void *cb2,
                          const google::protobuf::Descriptor *desc,
                          bool need_metadata,
                          const StringPiece *name);
}

template<typename T> void ReadProtoRecords(ListReader *reader_p,
                                           std::function<void(T&&)> cb,
                                           bool need_metadata,
                                           const StringPiece *name = nullptr) {
  bool(*parse_and_cb)(strings::Slice&&, void *)([](strings::Slice &&record, void *cb2) {
    typename std::remove_const<T>::type item;
    if (!item.ParseFromArray(record.data(), record.size()))
      return false;
    (*(decltype(cb)*)cb2)(std::move(item));
    return true;
  });
  internal::ReadProtoRecordsImpl(reader_p, parse_and_cb, &cb, T::descriptor(), need_metadata, name);
}

template<typename T> base::Status SafeReadProtoRecords(StringPiece name,
                                                      std::function<void(T&&)> cb) {
  ReadonlyFile::Options options;
  options.use_mmap = false;
  auto res = file::ReadonlyFile::Open(name, options);
  if (!res.ok()) {
    return res.status;
  }
  CHECK(res.obj);   // Fatal error. If status ok, must contains file pointer.

  file::ListReader reader(res.obj, TAKE_OWNERSHIP);
  std::string record_buf;
  strings::Slice record;
  while (reader.ReadRecord(&record, &record_buf)) {
    T item;
    if (!item.ParseFromArray(record.data(), record.size())) {
      return base::Status("Invalid record");
    }
    cb(std::move(item));
  }

  return base::Status::OK;
}

}  // namespace file

#endif  // _LIST_FILE_H_
