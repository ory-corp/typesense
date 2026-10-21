#include <gtest/gtest.h>
#include <vector>
#include <store.h>
#include <string_utils.h>
#include <filesystem>
#include <map>
#include <thread>

namespace {
// Observe actual RocksDB WAL syncs, and inject a disk sync failure without changing Store's
// scheduler or substituting a mock database. SST and manifest writes use the normal Env.
class WalSyncEnv : public rocksdb::EnvWrapper {
    // Completed appends to one WAL file, how many of them the last successful sync covered,
    // and whether RocksDB deleted the file after retiring it.
    struct WalState {
        std::atomic<size_t> appends{0};
        std::atomic<size_t> synced{0};
        std::atomic<bool> deleted{false};
    };

    class WalFile : public rocksdb::WritableFileWrapper {
        std::unique_ptr<rocksdb::WritableFile> file;
        std::shared_ptr<WalState> state;
        WalSyncEnv& env;
    public:
        WalFile(std::unique_ptr<rocksdb::WritableFile> file, std::shared_ptr<WalState> state, WalSyncEnv& env):
            rocksdb::WritableFileWrapper(file.get()), file(std::move(file)), state(std::move(state)), env(env) {}

        rocksdb::Status Append(const rocksdb::Slice& data) override {
            const auto status = file->Append(data);
            ++state->appends;
            return status;
        }

        rocksdb::Status Append(const rocksdb::Slice& data, const rocksdb::DataVerificationInfo& info) override {
            const auto status = file->Append(data, info);
            ++state->appends;
            return status;
        }

        rocksdb::Status Sync() override {
            // Only appends that completed before the sync started are guaranteed to be covered.
            const size_t covered = state->appends;
            const auto status = env.fail_sync ? rocksdb::Status::IOError("injected WAL sync failure") : file->Sync();
            if(status.ok()) {
                state->synced = covered;
            }
            ++env.syncs;
            return status;
        }

        rocksdb::Status Fsync() override { return Sync(); }
    };

    std::mutex mutex;
    std::map<std::string, std::shared_ptr<WalState>> wal_states;

    static bool is_wal(const std::string& name) {
        return name.size() >= 4 && name.compare(name.size() - 4, 4, ".log") == 0;
    }

    static std::string file_name(const std::string& path) {
        return path.substr(path.find_last_of('/') + 1);
    }

public:
    std::atomic<size_t> syncs{0};
    std::atomic<bool> fail_sync{false};

    WalSyncEnv(): rocksdb::EnvWrapper(rocksdb::Env::Default()) {}

    rocksdb::Status NewWritableFile(const std::string& name, std::unique_ptr<rocksdb::WritableFile>* result,
                                    const rocksdb::EnvOptions& options) override {
        auto status = target()->NewWritableFile(name, result, options);
        if(status.ok() && is_wal(name)) {
            auto state = std::make_shared<WalState>();
            {
                std::lock_guard lock(mutex);
                wal_states[file_name(name)] = state;
            }
            *result = std::make_unique<WalFile>(std::move(*result), std::move(state), *this);
        }
        return status;
    }

    rocksdb::Status DeleteFile(const std::string& name) override {
        const auto status = target()->DeleteFile(name);
        std::lock_guard lock(mutex);
        const auto it = wal_states.find(file_name(name));
        if(status.ok() && it != wal_states.end()) {
            it->second->deleted = true;
        }
        return status;
    }

    size_t wal_files() {
        std::lock_guard lock(mutex);
        return wal_states.size();
    }

    // Whether every completed WAL append is covered by a successful sync, or its file was retired.
    bool wal_synced() {
        std::lock_guard lock(mutex);
        for(const auto& [name, state] : wal_states) {
            if(!state->deleted && state->synced < state->appends) {
                return false;
            }
        }
        return true;
    }

    // RocksDB deletes retired WAL files in the background, so allow it some time.
    bool wait_until_wal_synced() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while(!wal_synced()) {
            if(std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }
};

class WalSyncTest : public ::testing::Test {
protected:
    WalSyncEnv env;
    std::string path;
    std::unique_ptr<Store> store;

    void SetUp() override {
        char temp[] = "/tmp/typesense-wal-sync-XXXXXX";
        ASSERT_NE(nullptr, mkdtemp(temp));
        path = temp;
        open();
    }

    // Like standalone mode: retired WAL files are deleted instead of archived.
    void open(bool disable_wal = false) {
        store = std::make_unique<Store>(path, 0, 0, disable_wal, 0, 4*1048576, 2, 4*1048576, 5, &env);
    }

    void TearDown() override {
        store.reset();
        std::filesystem::remove_all(path);
    }
};
}

TEST_F(WalSyncTest, SyncCoversPendingWrites) {
    ASSERT_TRUE(store->insert("key", "value"));
    ASSERT_FALSE(env.wal_synced());
    ASSERT_TRUE(store->sync_wal().ok());
    EXPECT_TRUE(env.wal_synced());
    EXPECT_FALSE(store->has_wal_sync_error());
    store.reset();
    open();
    std::string value;
    ASSERT_EQ(StoreStatus::FOUND, store->get("key", value));
    EXPECT_EQ("value", value);
}

TEST_F(WalSyncTest, SyncSkipsTheDiskWhenNothingWasWritten) {
    ASSERT_TRUE(store->insert("key", "value"));
    ASSERT_TRUE(store->sync_wal().ok());
    const size_t syncs = env.syncs;
    ASSERT_GT(syncs, 0);
    // Most requests leave some stores unchanged, e.g. the meta store.
    EXPECT_TRUE(store->sync_wal().ok());
    EXPECT_EQ(syncs, env.syncs.load());
    ASSERT_TRUE(store->insert("other", "value"));
    ASSERT_FALSE(env.wal_synced());
    EXPECT_TRUE(store->sync_wal().ok());
    EXPECT_GT(env.syncs.load(), syncs);
    EXPECT_TRUE(env.wal_synced());
}

TEST_F(WalSyncTest, SyncsConcurrentWritesAcrossWalRotation) {
    const std::string value(4096, 'x');
    std::atomic<bool> writes_ok{true};
    std::atomic<bool> writing{true};
    std::atomic<bool> syncs_ok{true};
    // Concurrent requests sync while others are still writing.
    std::thread syncer([&] {
        while(writing) {
            if(!store->sync_wal().ok()) {
                syncs_ok = false;
            }
        }
    });
    std::vector<std::thread> writers;
    for(size_t thread = 0; thread < 4; ++thread) {
        writers.emplace_back([&, thread] {
            for(size_t document = 0; document < 1024; ++document) {
                if(!store->insert(std::to_string(thread) + ":" + std::to_string(document), value)) {
                    writes_ok = false;
                }
            }
        });
    }
    for(auto& writer : writers) writer.join();
    writing = false;
    syncer.join();
    ASSERT_TRUE(writes_ok);
    ASSERT_TRUE(syncs_ok);
    ASSERT_GT(env.wal_files(), 1);
    ASSERT_TRUE(store->sync_wal().ok());
    // Every live WAL file, including ones closed by a rotation, must have been synced.
    EXPECT_TRUE(env.wait_until_wal_synced());
    store.reset();
    open();
    std::unique_ptr<rocksdb::Iterator> iterator(store->get_iterator());
    size_t count = 0;
    for(iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        ASSERT_EQ(value, iterator->value().ToString());
        ++count;
    }
    EXPECT_TRUE(iterator->status().ok());
    EXPECT_EQ(4096, count);
}

TEST_F(WalSyncTest, SyncFailureRejectsFurtherMutationsAndNotifiesOwner) {
    std::atomic<bool> notified{false};
    store->set_wal_sync_error_handler([&] { notified = true; });
    ASSERT_TRUE(store->insert("before_failure", "value"));
    env.fail_sync = true;
    EXPECT_FALSE(store->sync_wal().ok());
    EXPECT_TRUE(notified);
    EXPECT_TRUE(store->has_wal_sync_error());
    EXPECT_FALSE(store->insert("after_failure", "value"));
    EXPECT_FALSE(store->remove("before_failure"));
    rocksdb::WriteBatch batch;
    batch.Put("batch", "value");
    EXPECT_FALSE(store->batch_write(batch));
    EXPECT_FALSE(store->delete_range("a", "z").ok());
}

TEST_F(WalSyncTest, FailedSyncIsStickyAndNotifiesOnce) {
    std::atomic<size_t> notifications{0};
    store->set_wal_sync_error_handler([&] { ++notifications; });
    ASSERT_TRUE(store->insert("key", "value"));
    ASSERT_TRUE(store->sync_wal().ok());
    ASSERT_TRUE(store->insert("unsynced", "value"));
    env.fail_sync = true;
    EXPECT_FALSE(store->sync_wal().ok());
    // The disk recovers, but data dropped by the failed sync stays lost: never report success again.
    env.fail_sync = false;
    const size_t syncs = env.syncs;
    EXPECT_FALSE(store->sync_wal().ok());
    EXPECT_EQ(syncs, env.syncs.load());
    EXPECT_TRUE(store->has_wal_sync_error());
    EXPECT_EQ(1, notifications.load());
}

TEST_F(WalSyncTest, SyncRequiresAnOpenStoreWithTheWal) {
    store->close();
    EXPECT_FALSE(store->sync_wal().ok());
    store.reset();
    open(true);
    EXPECT_FALSE(store->sync_wal().ok());
    // Misuse is not a storage failure.
    EXPECT_FALSE(store->has_wal_sync_error());
}

TEST(StoreTest, WalWritesSurviveReopen) {
    const std::string path = "/tmp/typesense_test/wal_store";
    system(("rm -rf " + path + " && mkdir -p " + path).c_str());

    {
        Store store(path, 0, 0, false);
        ASSERT_TRUE(store.insert("key", "value"));
        ASSERT_TRUE(store.insert("remove", "value"));
        ASSERT_TRUE(store.delete_range("remove", "removf").ok());
    }

    Store reopened(path, 0, 0, false);
    std::string value;
    EXPECT_EQ(StoreStatus::FOUND, reopened.get("key", value));
    EXPECT_EQ("value", value);
    EXPECT_EQ(StoreStatus::NOT_FOUND, reopened.get("remove", value));
}

TEST(StoreTest, GetUpdatesSince) {
    std::string primary_store_path = "/tmp/typesense_test/primary_store_test";
    LOG(INFO) << "Truncating and creating: " << primary_store_path;
    system(("rm -rf "+primary_store_path+" && mkdir -p "+primary_store_path).c_str());

    // add some records, get the updates and restore them in a new store

    Store primary_store(primary_store_path, 24*60*60, 1024, false);

    // on a fresh store, sequence number is 0
    Option<std::vector<std::string>*> updates_op = primary_store.get_updates_since(0, 10);
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(0, updates_op.get()->size());
    ASSERT_EQ(0, primary_store.get_latest_seq_number());
    delete updates_op.get();

    // get_updates_since(1) == get_updates_since(0)
    updates_op = primary_store.get_updates_since(1, 10);
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(0, updates_op.get()->size());
    ASSERT_EQ(0, primary_store.get_latest_seq_number());
    delete updates_op.get();

    // querying for a seq_num > 1 on a fresh store
    updates_op = primary_store.get_updates_since(2, 10);
    ASSERT_FALSE(updates_op.ok());
    ASSERT_EQ("Unable to fetch updates. Master's latest sequence number is 0 but "
              "requested sequence number is 2", updates_op.error());

    // get_updates_since(1) == get_updates_since(0) even after inserting a record
    primary_store.insert("foo1", "bar1");
    ASSERT_EQ(1, primary_store.get_latest_seq_number());
    updates_op = primary_store.get_updates_since(1, 10);
    std::cout << updates_op.error() << std::endl;
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(1, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(0, 10);
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(1, updates_op.get()->size());
    delete updates_op.get();

    // add more records
    primary_store.insert("foo2", "bar2");
    primary_store.insert("foo3", "bar3");
    ASSERT_EQ(3, primary_store.get_latest_seq_number());

    updates_op = primary_store.get_updates_since(0, 10);
    ASSERT_EQ(3, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(1, 10);
    ASSERT_EQ(3, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(3, 10);
    ASSERT_EQ(1, updates_op.get()->size());
    delete updates_op.get();

    std::string replica_store_path = "/tmp/typesense_test/replica_store_test";
    LOG(INFO) << "Truncating and creating: " << replica_store_path;
    system(("rm -rf "+replica_store_path+" && mkdir -p "+replica_store_path).c_str());

    Store replica_store(replica_store_path, 24*60*60, 1024, false);
    rocksdb::DB* replica_db = replica_store._get_db_unsafe();

    updates_op = primary_store.get_updates_since(0, 10);

    for(const std::string & update: *updates_op.get()) {
        // Do Base64 encoding and decoding as we would in the API layer
        const std::string update_encoded = StringUtils::base64_encode(update);
        const std::string update_decoded = StringUtils::base64_decode(update_encoded);
        rocksdb::WriteBatch write_batch(update_decoded);
        replica_db->Write(rocksdb::WriteOptions(), &write_batch);
    }

    delete updates_op.get();

    std::string value;
    for(auto i=1; i<=3; i++) {
        replica_store.get(std::string("foo")+std::to_string(i), value);
        ASSERT_EQ(std::string("bar")+std::to_string(i), value);
    }

    // Ensure that updates are limited to max_updates argument
    updates_op = primary_store.get_updates_since(0, 10);
    ASSERT_EQ(3, updates_op.get()->size());
    delete updates_op.get();

    // sequence numbers 0 and 1 are the same
    updates_op = primary_store.get_updates_since(0, 10);
    ASSERT_EQ(3, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(1, 10);
    ASSERT_EQ(3, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(3, 100);
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(1, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(4, 100);
    ASSERT_TRUE(updates_op.ok());
    ASSERT_EQ(0, updates_op.get()->size());
    delete updates_op.get();

    updates_op = primary_store.get_updates_since(50, 100);
    ASSERT_FALSE(updates_op.ok());
    ASSERT_EQ("Unable to fetch updates. Master's latest sequence number is 3 but "
              "requested sequence number is 50", updates_op.error());
}

TEST(StoreTest, GetUpdateSinceInvalidIterator) {
    std::string primary_store_path = "/tmp/typesense_test/primary_store_test";
    LOG(INFO) << "Truncating and creating: " << primary_store_path;
    system(("rm -rf "+primary_store_path+" && mkdir -p "+primary_store_path).c_str());

    // add some records, get the updates and restore them in a new store

    Store primary_store(primary_store_path, 0, 0, true);  // disable WAL
    primary_store.insert("foo1", "bar1");
    primary_store.insert("foo2", "bar2");
    primary_store.insert("foo3", "bar3");
    primary_store.insert("foo4", "bar4");

    primary_store.flush();

    Option<std::vector<std::string>*> updates_op = primary_store.get_updates_since(2, 10);
    ASSERT_FALSE(updates_op.ok());
    ASSERT_EQ("Invalid iterator. Master's latest sequence number is 4 but updates are requested from sequence number 2. "
                      "The master's WAL entries might have expired (they are kept only for 24 hours).", updates_op.error());
}

TEST(StoreTest, Contains) {
    std::string primary_store_path = "/tmp/typesense_test/primary_store_test";
    LOG(INFO) << "Truncating and creating: " << primary_store_path;
    system(("rm -rf "+primary_store_path+" && mkdir -p "+primary_store_path).c_str());

    // add some records, flush and try to query
    Store primary_store(primary_store_path, 0, 0, true);  // disable WAL
    primary_store.insert("foo1", "bar1");
    primary_store.insert("foo2", "bar2");
    primary_store.flush();

    ASSERT_EQ(true, primary_store.contains("foo1"));
    ASSERT_EQ(true, primary_store.contains("foo2"));
    ASSERT_EQ(false, primary_store.contains("foo"));
    ASSERT_EQ(false, primary_store.contains("foo3"));

    // add more records without flushing and query again
    primary_store.insert("foo3", "bar1");
    primary_store.insert("foo4", "bar2");
    primary_store.flush();

    ASSERT_EQ(true, primary_store.contains("foo1"));
    ASSERT_EQ(true, primary_store.contains("foo3"));
    ASSERT_EQ(true, primary_store.contains("foo4"));
    ASSERT_EQ(false, primary_store.contains("foo"));
    ASSERT_EQ(false, primary_store.contains("foo5"));
}
