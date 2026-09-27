#pragma once
#include "clipboard/history.h"
#include "clipboard/preview_data.h"
#include <filesystem>
#include <chrono>
namespace lumashot::clipboard {
constexpr UINT SessionStoreReady=WM_APP+94;
enum class StoreResultKind { Restored, Saved, Loaded, Thumbnail, Preview, Search, Error };
struct StoreResult {
    StoreResultKind kind{StoreResultKind::Error};
    uint64_t token{};bool foreground_read{},preview_read{},thumbnail_read{};Entry entry;std::shared_ptr<const Frame> image;
    PreviewData preview;
    std::vector<Entry> entries;std::vector<Group> groups;std::vector<uint64_t> matches;std::wstring message;
};
struct StoreStats {size_t pending_bytes{},result_bytes{},disk_bytes{},jobs{};};
// All payload I/O, encryption, decryption and image decoding run on this worker.
// The caller retains only short summaries and shared leases on encrypted files.
class SessionStore {
public:
    static constexpr size_t PendingLimit=32*1024*1024,DiskLimit=192*1024*1024,SummaryChars=512;
    SessionStore(std::filesystem::path root,HWND notify=nullptr,bool persistent=false);
    ~SessionStore();
    bool Save(Entry entry);
    // Persists the index (and group table) in persistent mode; no-op for session-only stores.
    void Commit(std::vector<Entry> entries,std::vector<Group> groups={});
    bool Load(const Entry& entry,uint64_t token,bool thumbnail=false);
    bool LoadPreview(const Entry& entry,uint64_t token);
    void CancelPreview();
    void Search(std::vector<Entry> entries,std::wstring query,uint64_t token);
    void CancelReads();
    void CancelSaves();
    std::vector<StoreResult> Take();
    bool WaitIdle(unsigned milliseconds=15000);
    StoreStats Stats();
    std::filesystem::path Directory();
    // Deletes the cross-restart history under root unless a live store holds its lease.
    static bool PurgePersistent(const std::filesystem::path& root);
private:
    struct State;std::shared_ptr<State> state_;
};
}