#pragma once

#include <stdint.h>
#include <cstdlib>
#include <string>
#include <sstream>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <option.h>
#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/options.h>
#include <rocksdb/merge_operator.h>
#include <rocksdb/transaction_log.h>
#include <butil/file_util.h>
#include <mutex>
#include <rocksdb/utilities/checkpoint.h>
#include <rocksdb/utilities/table_properties_collectors.h>
#include "string_utils.h"
#include "logger.h"
#include "file_utils.h"
#include <rocksdb/utilities/db_ttl.h>

#define FOURWEEKS_SECS 2419200

class UInt64AddOperator : public rocksdb::AssociativeMergeOperator {
public:
    virtual bool Merge(const rocksdb::Slice& key, const rocksdb::Slice* existing_value, const rocksdb::Slice& value,
                       std::string* new_value, rocksdb::Logger* logger) const override {
        uint64_t existing = 0;
        if (existing_value) {
            existing = StringUtils::deserialize_uint32_t(existing_value->ToString());
        }
        *new_value = StringUtils::serialize_uint32_t(existing + StringUtils::deserialize_uint32_t(value.ToString()));
        return true;
    }

    virtual const char* Name() const override {
        return "UInt64AddOperator";
    }
};

enum StoreStatus {
    FOUND,
    NOT_FOUND,
    ERROR
};

/*
 *  Abstraction for underlying KV store (RocksDB)
 */
class Store {
private:

    const std::string state_dir_path;
    rocksdb::DB *db;
    rocksdb::Options options;
    rocksdb::WriteOptions write_options;

    // Serializes sync_wal(), so that no sync reports success after a concurrent one failed.
    std::mutex wal_sync_mutex;
    std::atomic<bool> wal_sync_failed{false};
    std::function<void()> wal_sync_error_handler;
    // The latest sequence number when the last successful sync_wal() started.
    std::optional<rocksdb::SequenceNumber> wal_synced_seq;

    // Used to protect assignment to DB handle, which is otherwise thread safe
    // So we use unique lock only for assignment, but shared locks for all other operations on DB
    mutable std::shared_mutex mutex;

    rocksdb::Status init_db(int32_t ttl);

public:

    Store() = delete;

    Store(const std::string & state_dir_path,
          const size_t wal_ttl_secs = 24*60*60,
          const size_t wal_size_mb = 1024,
          bool disable_wal = true,
          int32_t ttl=0,
          size_t write_buffer_size = 4*1048576,
          size_t max_write_buffer_number = 2,
          size_t max_log_file_size = 4*1048576,
          size_t keep_log_file_num = 5,
          rocksdb::Env* env = rocksdb::Env::Default());

    // Requires an open store with the WAL enabled. Skips the disk when nothing was written since the
    // last successful sync. A failed sync is sticky: the OS may have dropped the unwritten data, so
    // later syncs and all mutations fail. The handler, set before the first sync, runs once on the
    // thread whose sync failed.
    rocksdb::Status sync_wal();
    bool has_wal_sync_error() const { return wal_sync_failed.load(); }
    void set_wal_sync_error_handler(std::function<void()> handler);

    ~Store();

    bool insert(const std::string& key, const std::string& value);

    bool batch_write(rocksdb::WriteBatch& batch);

    bool contains(const std::string& key) const;

    StoreStatus get(const std::string& key, std::string& value) const;

    bool remove(const std::string& key);

    rocksdb::Iterator* scan(const std::string & prefix, const rocksdb::Slice* iterate_upper_bound);

    rocksdb::Iterator* get_iterator();

    void scan_fill(const std::string& prefix_start, const std::string& prefix_end, std::vector<std::string> & values);

    void increment(const std::string & key, uint32_t value);

    uint64_t get_latest_seq_number() const;

    Option<std::vector<std::string>*> get_updates_since(const uint64_t seq_number_org, const uint64_t max_updates) const;

    void close();

    int reload(bool clear_state_dir, const std::string& snapshot_path, int32_t ttl = 0);

    void flush();

    rocksdb::Status compact_all();

    rocksdb::Status create_check_point(rocksdb::Checkpoint** checkpoint_ptr, const std::string& db_snapshot_path);

    rocksdb::Status delete_range(const std::string& begin_key, const std::string& end_key);

    rocksdb::Status compact_range(const rocksdb::Slice& begin_key, const rocksdb::Slice& end_key);

    // Only for internal tests
    rocksdb::DB* _get_db_unsafe() const;

    const std::string& get_state_dir_path() const;

    const rocksdb::Options &get_db_options() const;

    void print_memory_usage();

    void get_last_N_values(const std::string& userid_prefix, uint32_t N, std::vector<std::string>& values);
};
