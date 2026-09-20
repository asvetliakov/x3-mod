#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace x3m::media {
constexpr std::size_t package_document_limit = 128 * 1024;
enum class PackageStatus { ready, disabled, error };
enum class PackageErrorCode {
    none, io, journal, too_large, malformed_json, invalid_schema, invalid_path,
    missing_file, reparse_point, identity, allocation
};
struct PackageError { PackageErrorCode code = PackageErrorCode::none; std::uint32_t system = 0; };
struct PackageSource { std::uint32_t id = 0, effective_flags = 0; std::wstring path; };
class PackageInput;
struct PackageConfig {
    std::wstring provider_manifest;
    std::vector<PackageSource> sources;
private:
    // Keeps the adapter's directory/file identity pins alive as long as config.
    std::shared_ptr<PackageInput> input_;
    friend PackageStatus read_package_config(std::shared_ptr<PackageInput>,
        std::shared_ptr<const PackageConfig>&, PackageError&) noexcept;
};
// Narrow I/O seam shared by production and the parser fixture. Paths are validated
// UTF-8, slash-separated, game-root-relative. Adapters must refuse reparse points
// and retain identity pins for resolved files; no COM/provider calls are permitted.
class PackageInput {
public:
    virtual ~PackageInput() = default;
    virtual bool journal_absent(PackageError&) = 0;
    // Only the installation manifest may be absent. Missing is returned separately.
    virtual bool read_small(const std::string&, std::string&, bool& missing, PackageError&) = 0;
    virtual bool resolve_file(const std::string&, std::wstring&, PackageError&) = 0;
};
// Preparation-thread only. Never call from an engine constructor/pump. Success
// publishes one immutable snapshot; disabled clears output; failure leaves it intact.
// All public entry points contain C++ exceptions. Errors never require allocation.
PackageStatus read_package_config(std::shared_ptr<PackageInput>,
    std::shared_ptr<const PackageConfig>&, PackageError&) noexcept;
#ifdef _WIN32
// The caller already owns a pinned reference to the proxy HMODULE. Discovery is
// relative to that module, never CWD/environment. The returned config owns pins.
PackageStatus load_package_config(HMODULE pinned_proxy,
    std::shared_ptr<const PackageConfig>&, PackageError&) noexcept;
#endif
} // namespace x3m::media
