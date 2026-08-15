#include "pak.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>

#include "../core/logger.h"

namespace trinity::game::pak
{
    namespace
    {
        // --- LZ4 block decompression (no frame header) ----------------------
        // Standard LZ4 block: token nibble = literal length / match length,
        // 2-byte little-endian match offset, matches may overlap the output.
        bool Lz4Decompress(const uint8_t* src, size_t srcLen,
                           uint8_t* dst, size_t dstLen)
        {
            size_t s = 0, d = 0;
            while (s < srcLen)
            {
                const uint8_t token = src[s++];

                size_t lit = token >> 4;
                if (lit == 15)
                {
                    uint8_t b;
                    do { if (s >= srcLen) return false; b = src[s++]; lit += b; } while (b == 255);
                }
                if (lit)
                {
                    if (s + lit > srcLen || d + lit > dstLen) return false;
                    memcpy(dst + d, src + s, lit);
                    s += lit; d += lit;
                }
                if (s >= srcLen) break; // literals-only final sequence

                if (s + 2 > srcLen) return false;
                const size_t off = static_cast<size_t>(src[s]) | (static_cast<size_t>(src[s + 1]) << 8);
                s += 2;
                if (off == 0 || off > d) return false;

                size_t mlen = token & 0x0F;
                if (mlen == 15)
                {
                    uint8_t b;
                    do { if (s >= srcLen) return false; b = src[s++]; mlen += b; } while (b == 255);
                }
                mlen += 4;
                if (d + mlen > dstLen) return false;

                const size_t start = d - off;
                for (size_t k = 0; k < mlen; ++k) // byte-by-byte: overlap is legal
                    dst[d + k] = dst[start + k];
                d += mlen;
            }
            return d == dstLen;
        }

        // FileEntry::comp packing - see the decode switch in ReadFile().
        constexpr uint8_t kCompMethodMask = 0x0F;
        constexpr uint8_t kCompEncrypted  = 0x30;

        // --- Parsed manifest ------------------------------------------------
        #pragma pack(push, 1)
        struct FileEntry
        {
            uint32_t nameOff;
            uint32_t offset;
            uint32_t csize;
            uint32_t usize;
            uint16_t pazIdx;
            uint8_t  comp;
            uint8_t  flag;
        };
        struct DirEntry
        {
            uint32_t nameHash;
            uint32_t nameOff;
            uint32_t firstFile;
            uint32_t fileCount;
        };
        #pragma pack(pop)

        struct Manifest
        {
            bool                    valid = false;
            std::vector<uint8_t>    dirBlob;
            std::vector<uint8_t>    fileBlob;
            std::vector<DirEntry>   dirs;
            std::vector<FileEntry>  files;
            // Per-directory name -> file index, built on a directory's first
            // lookup. Reconstructing every name in a big directory per read
            // is wasteful once callers stream many files from one place
            // (lazy item icons pull from a ~7800-file directory).
            std::unordered_map<std::string,
                               std::unordered_map<std::string, uint32_t>> dirIndex;
        };

        // Reconstruct a '/'-joined path from parent-linked fragments in `blob`.
        std::string NameFromBlob(const std::vector<uint8_t>& blob, uint32_t off)
        {
            std::string parts[64];
            int n = 0;
            while (off != 0xFFFFFFFFu && n < 64)
            {
                if (off + 5 > blob.size()) break;
                uint32_t parent;
                memcpy(&parent, &blob[off], 4);
                const uint8_t len = blob[off + 4];
                if (off + 5 + len > blob.size()) break;
                parts[n++].assign(reinterpret_cast<const char*>(&blob[off + 5]), len);
                off = parent;
            }
            // Fragments are concatenated with NO separator - any '/' is already
            // part of a fragment (e.g. "ui" + "/texture" -> "ui/texture").
            std::string out;
            for (int i = n - 1; i >= 0; --i)
                out += parts[i];
            return out;
        }

        // --- Game-root resolution + manifest cache --------------------------
        std::wstring g_root;   // "<root>\" (contains the NNNN chunk folders)
        bool         g_rootResolved = false;
        bool         g_rootOk       = false;
        std::unordered_map<int, Manifest> g_cache;

        bool ChunkPamtExists(const std::wstring& root, int chunk)
        {
            wchar_t p[MAX_PATH];
            swprintf_s(p, L"%s%04d\\0.pamt", root.c_str(), chunk);
            return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
        }

        void ResolveRoot()
        {
            if (g_rootResolved) return;
            g_rootResolved = true;

            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring dir(exe);
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return;
            dir.resize(slash + 1); // "<exeDir>\"

            // Chunk folders sit either beside the exe or one level up (the exe
            // lives in <root>\bin64\). Probe both using chunk 0012 (UI assets).
            const std::wstring parent = [&] {
                std::wstring d = dir; d.pop_back(); // drop trailing slash
                const size_t s = d.find_last_of(L"\\/");
                return s == std::wstring::npos ? dir : d.substr(0, s + 1);
            }();

            if (ChunkPamtExists(dir, 12))         { g_root = dir;    g_rootOk = true; }
            else if (ChunkPamtExists(parent, 12)) { g_root = parent; g_rootOk = true; }

            if (!g_rootOk)
                LOG_ERR("pak: could not locate chunk folders near the game exe.");
        }

        bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out)
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER sz{};
            GetFileSizeEx(h, &sz);
            out.resize(static_cast<size_t>(sz.QuadPart));
            DWORD got = 0;
            const bool ok = out.empty() ||
                (::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
            CloseHandle(h);
            return ok;
        }

        // Read a range out of chunk's N.paz.
        bool ReadPazRange(int chunk, uint16_t pazIdx, uint32_t offset, uint32_t len,
                          std::vector<uint8_t>& out)
        {
            wchar_t p[MAX_PATH];
            swprintf_s(p, L"%s%04d\\%u.paz", g_root.c_str(), chunk, static_cast<unsigned>(pazIdx));
            HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER li; li.QuadPart = offset;
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            out.resize(len);
            DWORD got = 0;
            const bool ok = ::ReadFile(h, out.data(), len, &got, nullptr) && got == len;
            CloseHandle(h);
            return ok;
        }

        Manifest* GetManifest(int chunk)
        {
            auto it = g_cache.find(chunk);
            if (it != g_cache.end())
                return it->second.valid ? &it->second : nullptr;

            Manifest& m = g_cache[chunk];

            wchar_t p[MAX_PATH];
            swprintf_s(p, L"%s%04d\\0.pamt", g_root.c_str(), chunk);
            std::vector<uint8_t> d;
            if (!ReadWholeFile(p, d) || d.size() < 12)
                return nullptr;

            size_t o = 0;
            auto u32 = [&](size_t at) { uint32_t v; memcpy(&v, &d[at], 4); return v; };
            auto need = [&](size_t bytes) { return o + bytes <= d.size(); };

            if (!need(12)) return nullptr;
            const uint32_t pazCount = u32(4);
            o = 12 + static_cast<size_t>(pazCount) * 12; // skip paz table

            if (!need(4)) return nullptr;
            const uint32_t dirBlobSz = u32(o); o += 4;
            if (!need(dirBlobSz)) return nullptr;
            m.dirBlob.assign(d.begin() + o, d.begin() + o + dirBlobSz); o += dirBlobSz;

            if (!need(4)) return nullptr;
            const uint32_t fileBlobSz = u32(o); o += 4;
            if (!need(fileBlobSz)) return nullptr;
            m.fileBlob.assign(d.begin() + o, d.begin() + o + fileBlobSz); o += fileBlobSz;

            if (!need(4)) return nullptr;
            const uint32_t nDirs = u32(o); o += 4;
            if (!need(static_cast<size_t>(nDirs) * sizeof(DirEntry))) return nullptr;
            m.dirs.resize(nDirs);
            memcpy(m.dirs.data(), &d[o], static_cast<size_t>(nDirs) * sizeof(DirEntry));
            o += static_cast<size_t>(nDirs) * sizeof(DirEntry);

            if (!need(4)) return nullptr;
            const uint32_t nFiles = u32(o); o += 4;
            if (!need(static_cast<size_t>(nFiles) * sizeof(FileEntry))) return nullptr;
            m.files.resize(nFiles);
            memcpy(m.files.data(), &d[o], static_cast<size_t>(nFiles) * sizeof(FileEntry));

            m.valid = true;
            return &m;
        }
    } // namespace

    bool HaveGameRoot()
    {
        ResolveRoot();
        return g_rootOk;
    }

    bool ReadFile(int chunk, const char* dirPath, const char* fileName,
                  std::vector<uint8_t>& out, bool optional)
    {
        ResolveRoot();
        if (!g_rootOk) return false;

        Manifest* m = GetManifest(chunk);
        if (!m) { LOG_ERR("pak: chunk %d manifest parse failed.", chunk); return false; }

        // Find the directory whose reconstructed path matches, then look the
        // file up in that directory's name index (built once per directory).
        auto dirIt = m->dirIndex.find(dirPath);
        if (dirIt == m->dirIndex.end())
        {
            const DirEntry* dir = nullptr;
            for (const auto& e : m->dirs)
            {
                if (NameFromBlob(m->dirBlob, e.nameOff) == dirPath) { dir = &e; break; }
            }
            if (!dir) { LOG_ERR("pak: dir '%s' not found in chunk %d.", dirPath, chunk); return false; }

            auto& index = m->dirIndex[dirPath];
            const uint32_t end = dir->firstFile + dir->fileCount;
            index.reserve(dir->fileCount);
            for (uint32_t i = dir->firstFile; i < end && i < m->files.size(); ++i)
                index.emplace(NameFromBlob(m->fileBlob, m->files[i].nameOff), i);
            dirIt = m->dirIndex.find(dirPath);
        }

        const auto fileIt = dirIt->second.find(fileName);
        if (fileIt == dirIt->second.end())
        {
            // Not an error when the caller expects misses (see pak.h).
            if (!optional) LOG_ERR("pak: file '%s/%s' not found.", dirPath, fileName);
            return false;
        }
        const FileEntry* file = &m->files[fileIt->second];

        std::vector<uint8_t> raw;
        if (!ReadPazRange(chunk, file->pazIdx, file->offset, file->csize, raw))
        { LOG_ERR("pak: paz read failed for %s (paz %u off %u len %u).",
                  fileName, file->pazIdx, file->offset, file->csize); return false; }

        // `comp` is a bitfield, not an enum: the low nibble picks the method,
        // 0x30 marks the payload encrypted. Chunk 0012 carries comp 48 (0x30,
        // stored) and comp 50 (0x32, LZ4) alongside the plain 0/1/2 - and a
        // comp-48 entry with csize == usize still is not the bytes it claims
        // to be (cd_ui_imagefont_00.xml is not XML), so the flag transforms
        // even stored files. The UI's html/css/xml live behind it.
        if (file->comp & kCompEncrypted)
        {
            LOG_ERR("pak: %s/%s is encrypted (comp %u); no decryption support.",
                    dirPath, fileName, file->comp);
            return false;
        }

        switch (file->comp & kCompMethodMask)
        {
        case 0: // stored
            out = std::move(raw);
            return true;

        case 2: // whole-file LZ4
            out.resize(file->usize);
            return Lz4Decompress(raw.data(), raw.size(), out.data(), out.size());

        case 1: // DDS: plaintext 128B header + LZ4 mip payload (or stored)
        {
            if (file->csize == file->usize) { out = std::move(raw); return true; }
            if (raw.size() < 128 || file->usize < 128) return false;
            out.resize(file->usize);
            memcpy(out.data(), raw.data(), 128);
            const bool ok = Lz4Decompress(raw.data() + 128, raw.size() - 128,
                                          out.data() + 128, out.size() - 128);
            if (!ok) LOG_ERR("pak: LZ4 (comp1) failed for %s (csize %u usize %u).",
                             fileName, file->csize, file->usize);
            return ok;
        }

        default:
            LOG_ERR("pak: %s/%s uses unsupported compression method %u (comp %u).",
                    dirPath, fileName, file->comp & kCompMethodMask, file->comp);
            return false;
        }
    }
}
