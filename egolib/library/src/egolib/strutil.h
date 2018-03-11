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

/// @file  egolib/strutil.h
/// @brief String manipulation functions.

#pragma once

#include "egolib/typedef.h"

//--------------------------------------------------------------------------------------------
// MACROS
//--------------------------------------------------------------------------------------------

/// end-of-string character. assume standard null terminated string
#   define CSTR_END '\0'
#   define EMPTY_CSTR { CSTR_END }

#   define VALID_CSTR(PSTR)   ((NULL!=PSTR) && (CSTR_END != PSTR[0]))
#   define INVALID_CSTR(PSTR) ((NULL==PSTR) || (CSTR_END == PSTR[0]))

//--------------------------------------------------------------------------------------------
// GLOBAL FUNCTION PROTOTYPES
//--------------------------------------------------------------------------------------------

    std::string add_linebreak_cpp(const std::string& text, size_t lineLength);

    std::string str_decode(const std::string& source);
    std::string str_encode(const std::string& source);
	/**
	 * @brief
	 *	Turn an entity name into a pathname for loading and saving files.
	 * @param objectName
	 *	the object name
	 * @return
	 *	the pathname
	 * @remark
	 *	Replaces space by underscore,
	 *	replaces uppercase alphabetic character by the corresponding lowercase alphabetic character, and
	 *	appends <tt>".obj"</tt> to the resulting string.  
	 */
    std::string str_encode_path(const std::string& objectName);

    /// @brief Replace consecutive multiple slashes by a single slash.
    /// @param pathname the input pathname
    /// @return the converted pathname
    std::string str_clean_path(const std::string& pathname);
    
    /// @brief Convert all slashes to system-specific format.
    /// @param pathname the input pathname
    /// @return the converted pathname
    std::string str_convert_slash_sys(const std::string& pathname);

	/// Append a operating system slash to a filename
	/// if there is no slash or backslash.
    std::string str_append_slash(const std::string& filename);
	/// Append a network slash to a filename
	/// if there is no slash or backslash.
    std::string str_append_slash_net(const std::string& filename);
