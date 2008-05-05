/* Egoboo - Server.h
 * Basic skeleton for the server portion of a client-server architecture, 
 * this is totally not in use yet.
 */

/*
    This file is part of Egoboo.

    Egoboo is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Egoboo is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Egoboo.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef egoboo_Server_h
#define egoboo_Server_h

typedef struct GameState GameState;

typedef struct ServerState_t
{
	int dummy;
//	GameState gameState;
}ServerState_t;

// Globally accessible server state
extern ServerState_t ServerState;

int  sv_init();
void sv_shutDown();
void sv_frameStep();

// More to come...
// int  sv_beginSinglePlayer(...)
// int  sv_beginMultiPlayer(...)
// int  sv_loadModule(...)

#endif // include guard
