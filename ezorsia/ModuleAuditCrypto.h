#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include "third_party/miniz/miniz_tdef.h"

#pragma comment(lib, "bcrypt.lib")

namespace ModuleAuditCrypto {
inline void Check(NTSTATUS result) {
    if (result < 0) throw std::runtime_error("module audit crypto failed");
}

// BDMAUD02 | SHA256(key)[0:8] | random file ID[16]. Each frame:
// plaintext length LE32 | sequence LE32 | random nonce[12] | ciphertext | tag[16].
// Authenticated plaintext = codec (0=raw, 1=raw DEFLATE) | expanded length LE32 | JSONL.
// Batch at most 65537 bytes, flush every 5 seconds or at rotation/shutdown.
// AAD = file header + length + sequence. No plaintext disk fallback.
class Writer {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    unsigned char header_[32]{};
    unsigned long sequence_ = 0;
    unsigned long bytes_ = 0;
    bool dirty_ = false;
    std::string pending_;
    void WriteBytes(const void* data, DWORD length) {
        DWORD written = 0;
        if (!WriteFile(file_, data, length, &written, nullptr) || written != length)
            throw std::runtime_error("module audit write failed");
        bytes_ += length;
        dirty_ = true;
    }
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() {
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (key_) BCryptDestroyKey(key_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    void Initialize(const unsigned char (&secret)[32], const unsigned char (&keyId)[8]) {
        Check(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_AES_ALGORITHM, nullptr, 0));
        Check(BCryptSetProperty(algorithm_, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
        Check(BCryptGenerateSymmetricKey(algorithm_, &key_, nullptr, 0, const_cast<PUCHAR>(secret), 32, 0));
        memcpy(header_, "BDMAUD02", 8);
        memcpy(header_ + 8, keyId, 8);
    }
    void Open(const std::wstring& path) {
        Flush();
        if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
        Check(BCryptGenRandom(nullptr, header_ + 16, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        file_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("module audit open failed");
        sequence_ = 0;
        bytes_ = 0;
        WriteBytes(header_, sizeof(header_));
    }
    void Append(const std::string& json) {
        if (json.empty() || json.size() > 65536)
            throw std::runtime_error("module audit frame too large");
        if (pending_.size() + json.size() + 1 > 65537) WriteBatch();
        pending_ += json;
        pending_ += '\n';
    }
private:
    void WriteBatch() {
        if (pending_.empty()) return;
        if (sequence_ == MAXDWORD) throw std::runtime_error("module audit sequence exhausted");
        const DWORD expanded = static_cast<DWORD>(pending_.size());
        std::vector<unsigned char> plain(expanded + 5);
        // A bounded output buffer: incompressible data uses the raw codec without growth.
        const size_t compressed = tdefl_compress_mem_to_mem(plain.data() + 5, expanded,
            pending_.data(), expanded, TDEFL_DEFAULT_MAX_PROBES);
        plain[0] = compressed && compressed < expanded ? 1 : 0;
        memcpy(plain.data() + 1, &expanded, 4);
        if (plain[0]) plain.resize(compressed + 5);
        else memcpy(plain.data() + 5, pending_.data(), expanded);
        const DWORD length = static_cast<DWORD>(plain.size());
        std::vector<unsigned char> frame(length + 36);
        const DWORD seq = ++sequence_;
        memcpy(frame.data(), &length, 4);
        memcpy(frame.data() + 4, &seq, 4);
        Check(BCryptGenRandom(nullptr, frame.data() + 8, 12, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        unsigned char aad[40];
        memcpy(aad, header_, 32);
        memcpy(aad + 32, frame.data(), 8);
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce = frame.data() + 8; auth.cbNonce = 12;
        auth.pbTag = frame.data() + 20 + length; auth.cbTag = 16;
        auth.pbAuthData = aad; auth.cbAuthData = sizeof(aad);
        ULONG encrypted = 0;
        Check(BCryptEncrypt(key_, plain.data(), length, &auth,
            nullptr, 0, frame.data() + 20, length, &encrypted, 0));
        if (encrypted != length) throw std::runtime_error("module audit encryption length mismatch");
        WriteBytes(frame.data(), static_cast<DWORD>(frame.size()));
        pending_.clear();
    }
public:
    // Make evidence visible to the OS/uploader without forcing a synchronous disk flush.
    void Commit() { WriteBatch(); }
    void Flush() {
        WriteBatch();
        if (file_ != INVALID_HANDLE_VALUE && dirty_) {
            if (!FlushFileBuffers(file_)) throw std::runtime_error("module audit flush failed");
            dirty_ = false;
        }
    }
    // Conservative rotation accounting includes a still-uncompressed pending batch.
    unsigned long Bytes() const { return bytes_ + (pending_.empty() ? 0 : static_cast<DWORD>(pending_.size()) + 41); }
};
}
