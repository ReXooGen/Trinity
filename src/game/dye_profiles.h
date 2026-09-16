#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace trinity::game
{
    struct DyeProfile
    {
        uint32_t id = 0;
        uint32_t revision = 0;
        uint16_t typeId = 0;
        uint8_t mode = 0; // player / mount; slot namespaces are different
        uint32_t mask = 0;
        char name[64]{};
        char itemName[64]{};
        uint8_t records[12][16]{};
    };

    inline bool DyeProfileCompatible(const DyeProfile& p, uint32_t revision, uint16_t type, int mode)
    {
        return p.revision == revision && p.typeId == type && p.mode == mode;
    }

    // Full appearance replacement: missing saved channels explicitly clear dye
    // introduced by subsequent edits. Preserve raw material/condition payloads.
    inline void DyeProfileRecord(const DyeProfile& profile, unsigned channel, uint8_t (&record)[16])
    {
        std::memset(record, 0, sizeof(record));
        if (channel >= 12) return;
        if (profile.mask & (1u << channel)) std::memcpy(record, profile.records[channel], 13);
        else
        {
            record[4] = record[5] = 0xFF;
            record[6] = static_cast<uint8_t>(channel);
            record[11] = 0xFF;
        }
    }

    // Explicit user presets; separate from the automatic dye restore cache.
    // No game pointers, native calls or automatic replay. Commits are atomic;
    // an unreadable/corrupt file is never silently replaced with an empty library.
    class DyeProfileStore
    {
    public:
        static constexpr size_t kLimit = 64;
        bool Load(const std::wstring& path);
        std::vector<DyeProfile> List() const;
        bool Get(uint32_t id, DyeProfile& out) const;
        bool Save(DyeProfile profile, uint32_t replaceId, uint32_t* savedId = nullptr);
        bool Rename(uint32_t id, const char* name);
        bool Erase(uint32_t id);
        std::string Error() const;
    private:
        bool Commit(const std::vector<DyeProfile>& profiles, std::unique_lock<std::mutex>& stateLock);
        std::mutex ioMutex_;
        mutable std::mutex mutex_;
        std::wstring path_;
        std::vector<DyeProfile> profiles_;
        std::string error_;
        uint32_t nextId_ = 1;
        bool ready_ = false;
    };
}
