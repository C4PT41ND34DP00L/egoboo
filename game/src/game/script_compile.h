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

/// @file game/script_compile.h

#pragma once

#include "egolib/Script/PDLToken.hpp"
#include "game/egoboo.h"
#include "egolib/Script/Buffer.hpp"
#include "egolib/Script/script.h"

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

#define END_VALUE    (script_t::Instruction::FUNCTIONBIT | FEND)

inline int GetDataBits(int x) { return (x >> 27) & 0x0F; }
inline int SetDataBits(int x) { return (x & 0x0F ) << 27; }

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

extern bool debug_scripts;
extern vfs_FILE * debug_script_file;

/// temporary data describing a single egoscript opcode
struct opcode_data_t
{
    Ego::Script::PDLTokenKind _kind;
    uint32_t iValue;
    std::string cName;
};

/// The opcodes.
extern std::vector<opcode_data_t> Opcodes;

//--------------------------------------------------------------------------------------------

struct line_scanner_state_t
{
public:
    static int TabulatorSymbol() { return '\t'; }
    static int TildeSymbol() { return '~'; }
    static int UnderscoreSymbol() { return '_'; }
    static int DoubleQuoteSymbol() { return C_DOUBLE_QUOTE_CHAR; }
    static int EndOfInputSymbol() { return 255 + 2; }
    static int StartOfInputSymbol() { return 255 + 1; }
    static int LinefeedSymbol() { return ASCII_LINEFEED_CHAR; }
    static int CarriageReturnSymbol() { return C_CARRIAGE_RETURN_CHAR; }

private:
    void emit(const Ego::Script::PDLToken& token);
    void emit(Ego::Script::PDLTokenKind kind, const id::c::location& start, const id::c::location& end);
    void emit(Ego::Script::PDLTokenKind kind, const id::c::location& start, const id::c::location& end,
              const std::string& lexeme);
    void emit(Ego::Script::PDLTokenKind kind, const id::c::location& start, const id::c::location& end,
              int value);
    Ego::Script::PDLToken m_token;
    id::c::location m_location;
    size_t m_inputPosition;
    Ego::Script::Buffer *m_inputBuffer;
    Ego::Script::Buffer m_lexemeBuffer;

public:
    line_scanner_state_t(Ego::Script::Buffer *inputBuffer, const id::c::location& location);

public:
    line_scanner_state_t(const line_scanner_state_t& other) = delete;
    line_scanner_state_t& operator=(const line_scanner_state_t&) = delete;

public:
    id::c::location getLocation() const;

    void next();
    void write(int symbol);
    void writeAndNext(int symbol);
    void save();
    void saveAndNext();

    int getCurrent() const;

public:
    bool is(int symbol) const;
    bool isOneOf(int symbol1, int symbol2) const;
    template <typename ... Symbols>
    bool isOneOf(int symbol1, int symbol2, Symbols ... symbols) const
    {
        return is(symbol1)
            || isOneOf(symbol2, symbols ...);
    }
    bool isDoubleQuote() const;
    bool isEndOfInput() const;
    bool isStartOfInput() const;
    bool isWhiteSpace() const;
    bool isDigit() const;
    bool isAlphabetic() const;
    bool isNewLine() const;
    bool isOperator() const;
    bool isControl() const;
public:
    /// @code
    /// WhiteSpaces := WhiteSpace*
    /// @endcode
    Ego::Script::PDLToken scanWhiteSpaces();

    /// @code
    /// NewLines := NewLine*
    /// @endcode
    Ego::Script::PDLToken scanNewLines();

    /// @code
    /// NumericLiteral := Digit+
    /// @endcode
    Ego::Script::PDLToken scanNumericLiteral();

    /// @code
    /// Name := ('_'|Alphabetic)('_'|Alphabetic|Digit)
    /// @endcode
    Ego::Script::PDLToken scanName();

    /// @code
    /// IDSZ := '[' (Digit|Alphabetic)^4 ']'
    /// @endcode
    Ego::Script::PDLToken scanIDSZ();

    /// @code
    /// String := '"' !'#' StringInfix '"'
    /// Reference := '"' '#' StringInfix '"'
    /// StringInfix := (. - (Newline|'"')*
    /// @endcode
    /// @todo
    /// As the grammar already indicates,
    /// escape codes are not supported (yet).
    Ego::Script::PDLToken scanStringOrReference();

    /// @code
    /// Operator := '+' | '-'
    ///           | '*' | '/'
    ///           | '%'
    ///           | '>' | '<'
    ///           | '&'
    ///           | '='
    /// @endcode
    Ego::Script::PDLToken scanOperator();

    const Ego::Script::PDLToken& getToken() const { return m_token; }
};

// the current state of the parser
struct parser_state_t : public id::singleton<parser_state_t>
{
protected:
    friend id::default_new_functor<parser_state_t>;
    friend id::default_delete_functor<parser_state_t>;
	/// @brief Construct this parser.
	/// @remark Intentionally protected.
	parser_state_t();
	/// @brief Destruct this parser.
    /// @remark Intentionally protected.
	virtual ~parser_state_t();
public:
    bool _error;
    Ego::Script::PDLToken _token;
    int _line_count;

protected:
    // @brief Skip '\n', '\r', '\n\r' or '\r\n'.
    // @return @a true if input symbols were consumed, @a false otherwise
    // @post @a read was incremented by the number of input symbols consumed
    bool skipNewline(size_t& read, script_info_t& script);

    Ego::Script::Buffer _lineBuffer;

public:
    Ego::Script::Buffer _loadBuffer;

    /// @brief Get the error variable value.
    /// @return the error variable value
    bool get_error() const;

    /// @brief Clear the error variable.
    void clear_error();

private:
	void emit_opcode(const Ego::Script::PDLToken& token, const BIT_FIELD highbits, script_info_t& script);

	static uint32_t jump_goto(int index, int index_end, script_info_t& script);
public:
	static void parse_jumps(script_info_t& script);

private:
    /// @brief Write a syntactical error log entry. Optionally also raises syntactical error exception.
    /// @param raiseException if @a true, a syntactical error exception is raised 
    /// @param level the log level. If the log level is @a Log::Level::Error, then a syntactical error exception is raised
    /// @param received the received token
    /// @param expected the expected token types. Can be empty, however, the information
    /// content of the error log entry/error exception decreases.
    void raise(bool raiseException, Log::Level level, const Ego::Script::PDLToken& received, const std::vector<Ego::Script::PDLTokenKind>& expected);
	Ego::Script::PDLToken parse_token(ObjectProfile *ppro, script_info_t& script, line_scanner_state_t& state);
	size_t load_one_line(size_t read, script_info_t& script);
	/// @brief Compute the indention level of a line.
	/// @remark
	/// A line must begin with a number of spaces \f$2k\f$ where \f$k=0,1,\ldots$, otherwise an error shall be raised.
	/// The indention level $j$ of a line with \f$2k\f$ leading spaces for some $k=0,1,\ldots$ is given by \f$\frac{2k
	/// }{2}=k\f$. The indention level \f$j\f$ of a line may not exceed \f$15\f$.
    Ego::Script::PDLToken parse_indention(script_info_t& script, line_scanner_state_t& state);
public:
	void parse_line_by_line(ObjectProfile *ppro, script_info_t& script);

};

//--------------------------------------------------------------------------------------------

/**
 * @brief
 *	Load an AI script.
 * @param parser
 *	the parser
 * @param loadname
 *  the loadname
 * @param objectProfile
 *  the object profile
 * @param script
 * the script
 * @remark
 *  A call to this function tries to load the script.
 *		If this fails, then it tries to load the default script.
 *			If this fails, then the call to this function fails.
 */
egolib_rv load_ai_script_vfs(parser_state_t& ps, const std::string& loadname, ObjectProfile *ppro, script_info_t& script);
