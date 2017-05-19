//********************************************************************************************
//*
//*    This file is part of Egoboo.
//*
//*    Egoboo is free software: you can redistribute it and/or modify it
//*    under the terms of the GNU General Public License as published by
//*    the Free Software Foundation, either version 3 of the License, or
//*    (at your option) any later version.
//*
//*    Egoboo is distributed in the hope that it will be useful, but
//*    WITHOUT ANY WARRANTY; without even the implied warranty of
//*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//*    General Public License for more details.
//*
//*    You should have received a copy of the GNU General Public License
//*    along with Egoboo.  If not, see <http://www.gnu.org/licenses/>.
//*
//********************************************************************************************

/// @file egolib/Renderer/CompareFunction.hpp
/// @brief Enumeration of compare functions used for depth/stencil buffer operations and others.
/// @author Michael Heilmann

#pragma once

namespace Ego {

/// @brief Compare functions used for the depth/stencil buffer operations and others.
enum class CompareFunction
{
    /// @brief Always reject.
    AlwaysFail,

    /// @brief Always pass.
    AlwaysPass,

    /// @brief Pass if less.
    Less,

    /// @brief Pass if less or equal.
    LessOrEqual,

    /// @brief Pass if equal.
    Equal,

    /// @brief Pass if not equal.
    NotEqual,

    /// @brief Pass if greater or equal.
    GreaterOrEqual,

    /// @brief Pass if greater.
    Greater,
    
    _COUNT, ///< @todo Remove this.

}; // enum class CompareFunction

} // namespace Ego
