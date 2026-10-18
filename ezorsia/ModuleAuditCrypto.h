#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

namespace ModuleAuditCrypto {
inline void Check(NTSTATUS result) {
    if (result < 0) throw std::runtime_error("module audit crypto failed");
}

// BDMAUD01 | SHA256(key)[0:8] | random file ID[16]. Each frame:
// plaintext length LE32 | sequence LE32 | random nonce[12] | ciphertext | tag[16].
// AAD = file header + length + sequence. No plaintext fallback.
class Writer {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    unsigned char header_[32]{};
    unsigned long sequence_ = 0;
    unsigned long bytes_ = 0;
    bool dirty_ = false;
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
        memcpy(header_, "BDMAUD01", 8);
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
        if (json.empty() || json.size() > 65536 || sequence_ == MAXDWORD)
            throw std::runtime_error("module audit frame too large");
        const DWORD length = static_cast<DWORD>(json.size());
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
        Check(BCryptEncrypt(key_, reinterpret_cast<PUCHAR>(const_cast<char*>(json.data())), length, &auth,
            nullptr, 0, frame.data() + 20, length, &encrypted, 0));
        if (encrypted != length) throw std::runtime_error("module audit encryption length mismatch");
        WriteBytes(frame.data(), static_cast<DWORD>(frame.size()));
    }
    void Flush() {
        if (file_ != INVALID_HANDLE_VALUE && dirty_) {
            if (!FlushFileBuffers(file_)) throw std::runtime_error("module audit flush failed");
            dirty_ = false;
        }
    }
    unsigned long Bytes() const { return bytes_; }
};
}
