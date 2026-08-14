/// @file inc/kmx/sat/io/file_source.hpp
/// @brief File abstraction inspired by CaDiCaL/Kissat, with support for compressed files when enabled.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdio>
    #include <string>
    #include <string_view>
    #include <utility>
#endif

namespace kmx::sat::io
{
    /// @brief File abstraction inspired by CaDiCaL/Kissat, with support for compressed files when enabled.
    /// @details
    /// `file_source` is the single abstraction `dimacs_parser` and `io::fixture::binary::reader` read through,
    /// covering plain files, optionally compressed streams (gzip/xz-style, when built with that support enabled, in
    /// the CaDiCaL/Kissat tradition of piping through an external decompressor or a compression library), and
    /// in-memory buffers for tests and fuzzing. `open` establishes the source; `read`/`getc` provide buffered and
    /// single-character access respectively (`getc` matching the DIMACS parser's character-at-a-time grammar);
    /// `close` releases the underlying resource; `is_compressed` reports whether decompression is active, which the
    /// parser can use to adjust progress reporting or buffering strategy.
    class file_source final
    {
    public:
        /// @brief Constructs a file source with no open resource.
        /// @throws None (noexcept).
        file_source() noexcept = default;

        /// @brief Opens a file or stream at the given path, transparently detecting compression if enabled.
        /// @param path Path or identifier of the source to open.
        /// @return True if the source was opened successfully.
        /// @throws None (noexcept).
        bool open(const std::string_view path) noexcept
        {
            close();
            path_ = std::string {path};
            file_ = std::fopen(path_.c_str(), "rb");
            compressed_ = false;
            return file_ != nullptr;
        }

        /// @brief Reads up to `buffer_size` bytes into `buffer`.
        /// @param buffer Destination buffer to read into.
        /// @param buffer_size Maximum number of bytes to read.
        /// @return Number of bytes actually read, which may be less than `buffer_size` at end of stream.
        /// @throws None (noexcept).
        std::size_t read(char* buffer, const std::size_t buffer_size) noexcept
        {
            if (file_ == nullptr || buffer == nullptr || buffer_size == 0)
                return 0;
            return std::fread(buffer, 1, buffer_size, file_);
        }

        /// @brief Reads a single character from the source.
        /// @return The character read, or a negative value at end of stream.
        /// @throws None (noexcept).
        int getc() noexcept
        {
            if (file_ == nullptr)
                return -1;
            return std::fgetc(file_);
        }

        /// @brief Closes the underlying resource.
        /// @throws None (noexcept).
        void close() noexcept
        {
            if (file_ != nullptr)
            {
                std::fclose(file_);
                file_ = nullptr;
            }
        }

        /// @brief Checks whether this source is being transparently decompressed.
        /// @return True if decompression is active for this source.
        /// @throws None (noexcept).
        bool is_compressed() const noexcept { return compressed_; }

        /// @brief Destroys the file source and closes any open handle.
        /// @throws None (noexcept).
        ~file_source() noexcept { close(); }

        file_source(const file_source&) = delete;
        file_source& operator=(const file_source&) = delete;

        file_source(file_source&& other) noexcept: file_ {other.file_}, compressed_ {other.compressed_}, path_ {std::move(other.path_)}
        {
            other.file_ = nullptr;
            other.compressed_ = false;
        }

        file_source& operator=(file_source&& other) noexcept
        {
            if (this != &other)
            {
                close();
                file_ = other.file_;
                compressed_ = other.compressed_;
                path_ = std::move(other.path_);
                other.file_ = nullptr;
                other.compressed_ = false;
            }
            return *this;
        }

    private:
        std::FILE* file_ {};
        bool compressed_ {};
        std::string path_ {};
    };
}
