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

/// @file egolib/game/GameStates/GameState.hpp
/// @details Abstract interface class for a GameState
/// @author Johan Jansen

#include "egolib/game/GameStates/GameState.hpp"

GameState::GameState() :
	_terminateStateRequested(false)
{
	//ctor
}

void GameState::endState()
{
	_terminateStateRequested = true;
}

bool GameState::isEnded() const
{
	return _terminateStateRequested;
}

void GameState::beginState()
{
	//default does nothing
}
