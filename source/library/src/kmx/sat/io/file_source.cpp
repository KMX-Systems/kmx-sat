/// @file library/src/kmx/sat/io/file_source.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/file_source.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/file_source.hpp>

namespace kmx::sat::io
{
    bool file_source::open(const std::string_view path) noexcept
    {
        close();
        path_ = std::string {path};
        file_ = std::fopen(path_.c_str(), "rb");
        compressed_ = false;
        return file_ != nullptr;
    }

    std::size_t file_source::read(char* buffer, const std::size_t buffer_size) noexcept
    {
        if ((file_ == nullptr) || (buffer == nullptr) || (buffer_size == 0u))
            return 0u;
        return std::fread(buffer, 1u, buffer_size, file_);
    }

    int file_source::getc() noexcept
    {
        if (file_ == nullptr)
            return -1;
        return std::fgetc(file_);
    }

    void file_source::close() noexcept
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    file_source& file_source::operator=(file_source&& other) noexcept
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
}
