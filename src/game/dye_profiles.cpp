#include "dye_profiles.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace trinity::game
{
    namespace
    {
        constexpr uint8_t magic[8] = {'T','R','D','Y','P','R','0','1'};
        constexpr size_t recordBytes = 300; // explicit encoding, no compiler struct padding
        std::atomic<uint64_t> tempSequence{0};
        uint32_t Hash(const uint8_t* data, size_t size)
        {
            uint32_t value = 2166136261u;
            for (size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 16777619u;
            return value;
        }
        void Put(std::vector<uint8_t>& out, uint32_t value, unsigned bytes = 4)
        {
            for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
        uint32_t Take(const std::vector<uint8_t>& data, size_t& at, unsigned bytes = 4)
        {
            uint32_t value = 0;
            for (unsigned i = 0; i < bytes; ++i) value |= uint32_t{data[at++]} << (8 * i);
            return value;
        }
        bool TextValid(const char (&text)[64], bool required)
        {
            const char* end = static_cast<const char*>(std::memchr(text, 0, 64));
            if (!end) return false;
            bool nonSpace = false;
            for (const char* c = text; c != end; ++c)
            {
                if (static_cast<unsigned char>(*c) < 32 || *c == 127) return false;
                nonSpace |= *c != ' ';
            }
            return !required || nonSpace;
        }
        bool Valid(const DyeProfile& p)
        {
            if (!p.id || !p.revision || !p.typeId || p.typeId == 0xFFFF || p.mode > 1 ||
                (p.mask & ~0xFFFu) || !TextValid(p.name, true) || !TextValid(p.itemName, false)) return false;
            for (unsigned ch = 0; ch < 12; ++ch)
                if ((p.mask & (1u << ch)) && p.records[ch][6] != ch) return false;
            return true;
        }
        std::vector<uint8_t> Encode(const std::vector<DyeProfile>& profiles)
        {
            std::vector<uint8_t> out(magic, magic + 8);
            Put(out, static_cast<uint32_t>(profiles.size()));
            for (const auto& p : profiles)
            {
                Put(out, p.id); Put(out, p.revision); Put(out, p.typeId, 2); Put(out, p.mode, 1); Put(out, 0, 1); Put(out, p.mask);
                out.insert(out.end(), p.name, p.name + 64);
                out.insert(out.end(), p.itemName, p.itemName + 64);
                for (unsigned ch = 0; ch < 12; ++ch)
                    for (unsigned b = 0; b < 13; ++b) out.push_back((p.mask & (1u << ch)) ? p.records[ch][b] : 0);
            }
            Put(out, Hash(out.data(), out.size()));
            return out;
        }
        bool Decode(const std::vector<uint8_t>& data, std::vector<DyeProfile>& out)
        {
            if (data.size() < 16 || std::memcmp(data.data(), magic, 8)) return false;
            size_t at = 8;
            const uint32_t count = Take(data, at);
            if (count > DyeProfileStore::kLimit || data.size() != 16 + count * recordBytes) return false;
            size_t hashAt = data.size() - 4;
            const uint32_t computed = Hash(data.data(), hashAt);
            if (computed != Take(data, hashAt)) return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                DyeProfile p{};
                p.id = Take(data, at); p.revision = Take(data, at); p.typeId = static_cast<uint16_t>(Take(data, at, 2));
                p.mode = static_cast<uint8_t>(Take(data, at, 1));
                if (Take(data, at, 1)) return false;
                p.mask = Take(data, at);
                std::memcpy(p.name, data.data() + at, 64); at += 64;
                std::memcpy(p.itemName, data.data() + at, 64); at += 64;
                for (auto& rec : p.records) { std::memcpy(rec, data.data() + at, 13); at += 13; }
                if (!Valid(p) || std::any_of(out.begin(), out.end(), [&](const auto& old) { return old.id == p.id; })) return false;
                out.push_back(p);
            }
            return true;
        }
    }

    bool DyeProfileStore::Load(const std::wstring& path)
    {
        std::lock_guard<std::mutex> io(ioMutex_);
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = path; ready_ = false; profiles_.clear(); nextId_ = 1;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) { ready_ = true; error_.clear(); return true; }
            error_ = "Could not read the dye profile file"; return false;
        }
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 16 && size.QuadPart <= 16 + kLimit * recordBytes;
        std::vector<uint8_t> data(ok ? static_cast<size_t>(size.QuadPart) : 0);
        DWORD read = 0;
        if (ok) ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
        CloseHandle(file);
        std::vector<DyeProfile> parsed;
        if (!ok || !Decode(data, parsed)) { error_ = "Invalid or unsupported dye profile file"; return false; }
        profiles_ = std::move(parsed);
        for (const auto& p : profiles_) nextId_ = std::max(nextId_, p.id);
        if (!profiles_.empty()) ++nextId_;
        ready_ = true; error_.clear(); return true;
    }
    std::vector<DyeProfile> DyeProfileStore::List() const
    {
        std::lock_guard<std::mutex> lock(mutex_); return profiles_;
    }
    bool DyeProfileStore::Get(uint32_t id, DyeProfile& out) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& p : profiles_) if (p.id == id) { out = p; return true; }
        return false;
    }
    bool DyeProfileStore::Commit(const std::vector<DyeProfile>& profiles, std::unique_lock<std::mutex>& stateLock)
    {
        if (!ready_) return false;
        const auto data = Encode(profiles);
        const std::wstring temp = path_ + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(++tempSequence);
        // Mutators remain serialized by ioMutex_; menu reads keep seeing the
        // previous committed snapshot while the worker flushes to disk.
        stateLock.unlock();
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            stateLock.lock(); error_ = "Could not create the dye profile temporary file (Windows " + std::to_string(error) + ")"; return false;
        }
        DWORD written = 0;
        bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) ok = MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok) DeleteFileW(temp.c_str());
        stateLock.lock();
        if (!ok) { error_ = "Could not save dye profiles; previous file retained (Windows " + std::to_string(error) + ")"; return false; }
        profiles_ = profiles; error_.clear(); return true;
    }
    bool DyeProfileStore::Save(DyeProfile p, uint32_t replaceId, uint32_t* savedId)
    {
        std::lock_guard<std::mutex> io(ioMutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_) return false;
        p.id = replaceId ? replaceId : nextId_;
        if (!Valid(p)) { error_ = "Enter a profile name (1-63 bytes) and valid dye data"; return false; }
        auto next = profiles_;
        if (replaceId)
        {
            auto at = std::find_if(next.begin(), next.end(), [&](const auto& old) { return old.id == replaceId; });
            if (at == next.end() || !DyeProfileCompatible(*at, p.revision, p.typeId, p.mode))
            { error_ = "Profile does not match this equipment"; return false; }
            *at = p;
        }
        else
        {
            if (next.size() >= kLimit || !nextId_ || nextId_ == UINT32_MAX) { error_ = "Dye profile library is full"; return false; }
            next.push_back(p);
        }
        if (!Commit(next, lock)) return false;
        if (!replaceId) ++nextId_;
        if (savedId) *savedId = p.id;
        return true;
    }
    bool DyeProfileStore::Rename(uint32_t id, const char* name)
    {
        std::lock_guard<std::mutex> io(ioMutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        auto next = profiles_;
        for (auto& p : next) if (p.id == id)
        {
            if (!name || strnlen_s(name, 64) >= 64) { error_ = "Profile name is too long"; return false; }
            strcpy_s(p.name, name);
            if (!TextValid(p.name, true)) { error_ = "Enter a non-empty profile name"; return false; }
            return Commit(next, lock);
        }
        error_ = "Profile no longer exists"; return false;
    }
    bool DyeProfileStore::Erase(uint32_t id)
    {
        std::lock_guard<std::mutex> io(ioMutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        auto next = profiles_;
        next.erase(std::remove_if(next.begin(), next.end(), [&](const auto& p) { return p.id == id; }), next.end());
        if (next.size() == profiles_.size()) { error_ = "Profile no longer exists"; return false; }
        return Commit(next, lock);
    }
    std::string DyeProfileStore::Error() const
    {
        std::lock_guard<std::mutex> lock(mutex_); return error_;
    }
}
