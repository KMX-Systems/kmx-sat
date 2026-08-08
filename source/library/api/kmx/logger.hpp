/// @file api/kmx/logger.hpp
/// @brief Minimal, header-only, source-location-aware logging facade shared across KMX components.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstring>
    #include <format>
    #include <print>
    #include <source_location>
#endif

namespace kmx::logger
{
    /// @brief Severity level for one log message.
    enum class level
    {
        /// @brief Verbose diagnostic detail, typically suppressed in release builds.
        debug,
        /// @brief Routine informational message.
        info,
        /// @brief Recoverable but noteworthy condition.
        warn,
        /// @brief Failure condition; routed to stderr.
        error
    };

    namespace detail
    {
        /// @brief Maps a log level to its single-character tag used in the log line prefix.
        /// @param l Log level to format.
        /// @return 'D'/'I'/'W'/'E' for the corresponding level, or '?' for an out-of-range value.
        /// @throws None (noexcept).
        constexpr char level_to_char(const level l) noexcept
        {
            static constexpr std::array<char, static_cast<std::size_t>(level::error) + 2u> chars {
                'D', // debug
                'I', // info
                'W', // warn
                'E', // error
                '?'  // unknown
            };
            const auto index = static_cast<std::size_t>(l);
            return index < chars.size() ? chars[index] : chars.back();
        }
    }

    /// @brief Logs a formatted message tagged with severity level and call-site source location.
    ///
    /// Formats `fmt`/`args` using `std::format`, prefixes it with the level tag and a trimmed `file:line` call-site
    /// (derived from `loc`, with the directory portion stripped so log lines stay short), and writes the result as
    /// one line to stderr for `level::error` (unbuffered, immediately flushed) or stdout otherwise (also immediately
    /// flushed), so output ordering stays predictable even when mixed with other unbuffered output.
    /// @tparam Args Types of the positional arguments forwarded to `std::format`.
    /// @param lvl Severity level for this message, also selecting the output stream.
    /// @param loc Call-site location whose file name and line number are embedded in the log line; typically left at
    /// its default (`std::source_location::current()`) so it reflects the caller.
    /// @param fmt Compile-time-checked `std::format` format string for the message body.
    /// @param args Positional arguments substituted into `fmt`.
    /// @note Guaranteed not to throw; any exception raised while formatting or writing is swallowed to prevent a
    /// crash during logging.
    template <typename... Args>
    void log(const level lvl, const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try
        {
            // Extract only the file name from the full path
            auto full = loc.file_name();
            const char* file = full;
            if (const char* last_slash = std::strrchr(full, '/'))
                file = last_slash + 1;
            // Route error messages to stderr (unbuffered), others to stdout
            // Manually flush to ensure immediate output
            if (lvl == level::error)
            {
                std::println(stderr, "[{0}] [{1}:{2}] {3}", detail::level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stderr);
            }
            else
            {
                std::println(stdout, "[{0}] [{1}:{2}] {3}", detail::level_to_char(lvl), file, loc.line(),
                             std::format(fmt, std::forward<Args>(args)...));
                std::fflush(stdout);
            }
        }
        catch (...)
        {
            // Swallow exceptions
        }
    }

} // namespace logger
