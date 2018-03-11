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

/// @file  egolib/font_bmp.h
/// @brief Bitmapped font.

#pragma once

#include "egolib/typedef.h"

namespace Ego {
class Texture;
} // namespace Ego

#define NUMFONTX            16          ///< Number of fonts in the bitmap
#define NUMFONTY            6
#define NUMFONT             (NUMFONTX*NUMFONTY)
#define FONTADD             4               ///< Gap between letters

#define TABADD              (1<<5)
#define TABAND              (~(TABADD-1))                      ///< Tab size

extern int      fontoffset;                 ///< Line up fonts from top of screen
extern SDL_Rect fontrect[NUMFONT];          ///< The font rectangles
extern uint8_t  fontxspacing[NUMFONT];      ///< The spacing stuff
extern uint8_t  fontyspacing;
extern uint8_t  asciitofont[256];           ///< Conversion table

void font_bmp_init();
void font_bmp_load_vfs(const std::string& szBitmap, const char* szSpacing);
int  font_bmp_length_of_word(const std::string& szText);
