#pragma once

#include "egolib/Log/_Include.hpp"

namespace Ego {

/// @brief A display mode.
class DisplayMode
{
protected:
    /// @brief Construct this display mode.
    /// @remark Intentionally protected.
    DisplayMode();

    /// @brief Destruct this display mode.
    /// @remark Intentionally protected.
    virtual ~DisplayMode();

protected:
    /// @brief Display modes can not be copy-constructed.
    /// @remark Intentionally protected.
    DisplayMode(const DisplayMode&) = delete;

    /// @brief Display modes can not be assigned.
    /// @remark Intentionally protected.
    const DisplayMode& operator=(const DisplayMode&) = delete;

public:
    /// @brief Compare this display mode with another display mode for equality.
    /// @param other the other display mode
    /// @return @a true if this display mode is equal to the other display mode, @a false otherwise
    bool operator==(const DisplayMode& other) const;

    /// @brief Compare this display mode with another display mode for inequality.
    /// @param other the other display mode
    /// @return @a true if this display mode is not equal to the other display mode, @a false otherwise
    bool operator!=(const DisplayMode& other) const;

protected:
    virtual bool compare(const DisplayMode&) const = 0;

public:
    /// @brief Get a pointer to the backend display mode.
    /// @return a pointer to the backend display mode
    virtual void *get() const = 0;

    /// @brief Get the horizontal resolution, in pixels, of this display mode.
    /// @return the horizontal resolution, in pixels, of this display mode.
    virtual int getHorizontalResolution() const = 0;

    /// @brief Get the vertical resolution, in pixels, of this display mode.
    /// @return the vertical resolution, in pixels, of this display mode.
    virtual int getVerticalResolution() const = 0;

    /// @brief Get the refresh rate, in Hertz, of this display mode.
    /// @return the refresh rate, in Hertz, of this display mode
    virtual int getRefreshRate() const = 0;
};

} // namespace Ego

Log::Entry& operator<<(Log::Entry& logEntry, const Ego::DisplayMode& displayMode);
