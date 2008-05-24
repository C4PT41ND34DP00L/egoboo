/* Egoboo - network.c
 * Shuttles bits across the network, using Enet.  Networked play doesn't
 * really work at the moment.
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

#include "egoboo.h"
#include "Log.h"
#include <enet/enet.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#ifdef WIN32
#define vsnprintf _vsnprintf
#endif

// Networking constants
enum NetworkConstant {
	NET_UNRELIABLE_CHANNEL		= 0,
	NET_GUARANTEED_CHANNEL		= 1,
	NET_EGOBOO_NUM_CHANNELS,
	NET_EGOBOO_PORT				= 34626,
	NET_MAX_FILE_NAME			= 128,
	NET_MAX_FILE_TRANSFERS		= 1024,	// Maximum files queued up at once
};

// Network messages
enum NetworkMessage {
	NET_TRANSFER_FILE			= 10001,	// Packet contains a file.
	NET_TRANSFER_OK				= 10002,	// Acknowledgement packet for a file send
	NET_CREATE_DIRECTORY		= 10003,	// Tell the peer to create the named directory
	NET_DONE_SENDING_FILES		= 10009,	// Sent when there are no more files to send.
	NET_NUM_FILES_TO_SEND		= 10010,	// Let the other person know how many files you're sending
};

// Network players information
typedef struct NetPlayerInfo
{
	int playerSlot;
}NetPlayerInfo;

// ENet host & client identifiers
ENetHost* net_myHost = NULL;
ENetPeer* net_gameHost = NULL;
ENetPeer* net_playerPeers[MAXPLAYER];
NetPlayerInfo net_playerInfo[MAXNETPLAYER];

bool_t net_amHost = bfalse;

// Packet reading
ENetPacket*		net_readPacket = NULL;
size_t			net_readLocation = 0;

// Packet writing
unsigned int	packethead;                             // The write head
unsigned int	packetsize;                             // The size of the packet
unsigned char	packetbuffer[MAXSENDSIZE];              // The data packet
unsigned int	nexttimestamp;                          // Expected timestamp

// File transfer variables & structures
typedef struct NetFileTransfer
{
	char sourceName[NET_MAX_FILE_NAME];
	char destName[NET_MAX_FILE_NAME];
	ENetPeer *target;
}NetFileTransfer;

// File transfer queue
NetFileTransfer net_transferStates[NET_MAX_FILE_TRANSFERS];
int net_numFileTransfers = 0;
int net_fileTransferHead = 0;	// Queue indices
int net_fileTransferTail = 0;
int net_waitingForXferAck = 0;

// Receiving files
NetFileTransfer net_receiveState;

//--------------------------------------------------------------------------------------------
void close_session()
{
	size_t i, numPeers;
	ENetEvent event;

	// ZZ> This function gets the computer out of a network game
	if(networkon)
	{
		if(net_amHost)
		{
			// Disconnect the peers
			numPeers = net_myHost->peerCount;
			for(i = 0;i < numPeers;i++)
			{
#ifdef ENET11
				enet_peer_disconnect(&net_myHost->peers[i], 0);
#else
				enet_peer_disconnect(&net_myHost->peers[i]);
#endif
			}

			// Allow up to 5 seconds for peers to drop
			while(enet_host_service(net_myHost, &event, 5000))
			{
				switch(event.type)
				{
				case ENET_EVENT_TYPE_RECEIVE:
					enet_packet_destroy(event.packet);
					break;

				case ENET_EVENT_TYPE_DISCONNECT:
					log_info("close_session: Peer id %d disconnected gracefully.\n", event.peer->address.host);
					numPeers--;
					break;
				}
			}

			// Forcefully disconnect any peers leftover
			for(i = 0;i < net_myHost->peerCount;i++)
			{
				enet_peer_reset(&net_myHost->peers[i]);
			}
		}

		log_info("close_session: Disconnecting from network.\n");
		enet_host_destroy(net_myHost);
		net_myHost = NULL;
		net_gameHost = NULL;
	}
}

//--------------------------------------------------------------------------------------------
int add_player(unsigned short character, unsigned short player, unsigned char device)
{
	// ZZ> This function adds a player, returning bfalse if it fails, btrue otherwise
	int cnt;

	if(plavalid[player] == bfalse)
	{
		chrisplayer[character] = btrue;
		plaindex[player] = character;
		plavalid[player] = btrue;
		pladevice[player] = device;
		if(device != INPUTNONE)  nolocalplayers = bfalse;
		plalatchx[player] = 0;
		plalatchy[player] = 0;
		plalatchbutton[player] = 0;
		cnt = 0;
		while(cnt < MAXLAG)
		{
			platimelatchx[player][cnt] = 0;
			platimelatchy[player][cnt] = 0;
			platimelatchbutton[player][cnt] = 0;
			cnt++;
		}
		if(device != INPUTNONE)
		{
			chrislocalplayer[character] = btrue;
			numlocalpla++;
		}
		numpla++;
		return btrue;
	}
	return bfalse;
}

//--------------------------------------------------------------------------------------------
void clear_messages()
{
	// ZZ> This function empties the message buffer
	int cnt;

	cnt = 0;
	while(cnt < MAXMESSAGE)
	{
		msgtime[cnt] = 0;
		cnt++;
	}
}

//--------------------------------------------------------------------------------------------
void check_add(unsigned char key, char bigletter, char littleletter)
{
	// ZZ> This function adds letters to the net message
	/*PORT
	if(KEYDOWN(key))
	{
	if(keypress[key]==bfalse)
	{
	keypress[key] = btrue;
	if(netmessagewrite < MESSAGESIZE-2)
	{
	if(KEYDOWN(DIK_LSHIFT) || KEYDOWN(DIK_RSHIFT))
	{
	netmessage[netmessagewrite] = bigletter;
	}
	else
	{
	netmessage[netmessagewrite] = littleletter;
	}
	netmessagewrite++;
	netmessage[netmessagewrite] = '?'; // The flashing input cursor
	netmessage[netmessagewrite+1] = 0;
	}
	}
	}
	else
	{
	keypress[key] = bfalse;
	}
	*/
}

//--------------------------------------------------------------------------------------------
void net_startNewPacket()
{
	// ZZ> This function starts building a network packet
	packethead = 0;
	packetsize = 0;
}

//--------------------------------------------------------------------------------------------
void packet_addUnsignedByte(unsigned char uc)
{
	// ZZ> This function appends an unsigned char to the packet
	unsigned char* ucp;
	ucp = (unsigned char*) (&packetbuffer[packethead]);
	*ucp = uc;
	packethead+=1;
	packetsize+=1;
}

//--------------------------------------------------------------------------------------------
void packet_addSignedByte(signed char sc)
{
	// ZZ> This function appends a signed char to the packet
	signed char* scp;
	scp = (signed char*) (&packetbuffer[packethead]);
	*scp = sc;
	packethead+=1;
	packetsize+=1;
}

//--------------------------------------------------------------------------------------------
void packet_addUnsignedShort(unsigned short us)
{
	// ZZ> This function appends an unsigned short to the packet
	unsigned short* usp;
	usp = (unsigned short*) (&packetbuffer[packethead]);
	
	*usp = ENET_HOST_TO_NET_16(us);
	packethead+=2;
	packetsize+=2;
}

//--------------------------------------------------------------------------------------------
void packet_addSignedShort(signed short ss)
{
	// ZZ> This function appends a signed short to the packet
	signed short* ssp;
	ssp = (signed short*) (&packetbuffer[packethead]);
	
	*ssp = ENET_HOST_TO_NET_16(ss);
	
	packethead+=2;
	packetsize+=2;
}

//--------------------------------------------------------------------------------------------
void packet_addUnsignedInt(unsigned int ui)
{
	// ZZ> This function appends an unsigned int to the packet
	unsigned int* uip;
	uip = (unsigned int*) (&packetbuffer[packethead]);
	
	*uip = ENET_HOST_TO_NET_32(ui);
	
	packethead+=4;
	packetsize+=4;
}

//--------------------------------------------------------------------------------------------
void packet_addSignedInt(signed int si)
{
	// ZZ> This function appends a signed int to the packet
	signed int* sip;
	sip = (signed int*) (&packetbuffer[packethead]);
	
	*sip = ENET_HOST_TO_NET_32(si);
	
	packethead+=4;
	packetsize+=4;
}

//--------------------------------------------------------------------------------------------
void packet_addString(char *string)
{
	// ZZ> This function appends a null terminated string to the packet
	char* cp;
	char cTmp;
	int cnt;

	cnt = 0;
	cTmp = 1;
	cp = (char*) (&packetbuffer[packethead]);
	while(cTmp != 0)
	{
		cTmp = string[cnt];
		*cp = cTmp;
		cp+=1;
		packethead+=1;
		packetsize+=1;
		cnt++;
	}
}

//--------------------------------------------------------------------------------------------
void packet_startReading(ENetPacket *packet)
{
	net_readPacket = packet;
	net_readLocation = 0;
}

//--------------------------------------------------------------------------------------------
void packet_doneReading()
{
	net_readPacket = NULL;
	net_readLocation = 0;
}

//--------------------------------------------------------------------------------------------
void packet_readString(char *buffer, int maxLen)
{
	// ZZ> This function reads a null terminated string from the packet
	unsigned char uc;
	unsigned short outindex;

	outindex = 0;
	uc = net_readPacket->data[net_readLocation];
	net_readLocation++;
	while(uc != 0 && outindex < maxLen)
	{
		buffer[outindex] = uc;
		outindex++;
		uc = net_readPacket->data[net_readLocation];
		net_readLocation++;
	}
	buffer[outindex] = 0;
}

//--------------------------------------------------------------------------------------------
unsigned char packet_readUnsignedByte()
{
	// ZZ> This function reads an unsigned char from the packet
	unsigned char uc;
	uc = (unsigned char)net_readPacket->data[net_readLocation];
	net_readLocation++;
	return uc;
}

//--------------------------------------------------------------------------------------------
signed char packet_readSignedByte()
{
	// ZZ> This function reads a signed char from the packet
	signed char sc;
	sc = (signed char)net_readPacket->data[net_readLocation];
	net_readLocation++;
	return sc;
}

//--------------------------------------------------------------------------------------------
unsigned short packet_readUnsignedShort()
{
	// ZZ> This function reads an unsigned short from the packet
	unsigned short us;
	unsigned short* usp;
	usp = (unsigned short*) (&net_readPacket->data[net_readLocation]);
	
	us = ENET_NET_TO_HOST_16(*usp);
	
	net_readLocation+=2;
	return us;
}

//--------------------------------------------------------------------------------------------
signed short packet_readSignedShort()
{
	// ZZ> This function reads a signed short from the packet
	signed short ss;
	signed short* ssp;
	ssp = (signed short*) (&net_readPacket->data[net_readLocation]);
	
	ss = ENET_NET_TO_HOST_16(*ssp);
	
	net_readLocation+=2;
	return ss;
}

//--------------------------------------------------------------------------------------------
unsigned int packet_readUnsignedInt()
{
	// ZZ> This function reads an unsigned int from the packet
	unsigned int ui;
	unsigned int* uip;
	uip = (unsigned int*) (&net_readPacket->data[net_readLocation]);
	
	ui = ENET_NET_TO_HOST_32(*uip);
	
	net_readLocation+=4;
	return ui;
}

//--------------------------------------------------------------------------------------------
signed int packet_readSignedInt()
{
	// ZZ> This function reads a signed int from the packet
	signed int si;
	signed int* sip;
	sip = (signed int*) (&net_readPacket->data[net_readLocation]);
	
	si = ENET_NET_TO_HOST_32(*sip);
	
	net_readLocation+=4;
	return si;
}

//--------------------------------------------------------------------------------------------
size_t packet_remainingSize()
{
	// ZZ> This function tells if there's still data left in the packet
	return net_readPacket->dataLength - net_readLocation;
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToHost()
{
	// ZZ> This function sends a packet to the host
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, 0);
	enet_peer_send(net_gameHost, NET_UNRELIABLE_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToAllPlayers()
{
	// ZZ> This function sends a packet to all the players
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, 0);
	enet_host_broadcast(net_myHost, NET_UNRELIABLE_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToHostGuaranteed()
{
	// ZZ> This function sends a packet to the host
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(net_gameHost, NET_UNRELIABLE_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToAllPlayersGuaranteed()
{
	// ZZ> This function sends a packet to all the players
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(net_myHost, NET_GUARANTEED_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToOnePlayerGuaranteed(int player)
{
	// ZZ> This function sends a packet to one of the players
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, ENET_PACKET_FLAG_RELIABLE);

	if(player < numplayer)
	{
		enet_peer_send(&net_myHost->peers[player], NET_GUARANTEED_CHANNEL, packet);
	}
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToPeer(ENetPeer *peer)
{
	// JF> This function sends a packet to a given peer
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(peer, NET_UNRELIABLE_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void net_sendPacketToPeerGuaranteed(ENetPeer *peer)
{
	// JF> This funciton sends a packet to a given peer, with guaranteed delivery
	ENetPacket *packet = enet_packet_create(packetbuffer, packetsize, 0);
	enet_peer_send(peer, NET_GUARANTEED_CHANNEL, packet);
}

//--------------------------------------------------------------------------------------------
void input_net_message()
{
	// ZZ> This function lets players communicate over network by hitting return, then
	//     typing text, then return again
	/*PORT
	int cnt;
	char cTmp;


	if(netmessagemode)
	{
	// Add new letters
	check_add(DIK_A, 'A', 'a');
	check_add(DIK_B, 'B', 'b');
	check_add(DIK_C, 'C', 'c');
	check_add(DIK_D, 'D', 'd');
	check_add(DIK_E, 'E', 'e');
	check_add(DIK_F, 'F', 'f');
	check_add(DIK_G, 'G', 'g');
	check_add(DIK_H, 'H', 'h');
	check_add(DIK_I, 'I', 'i');
	check_add(DIK_J, 'J', 'j');
	check_add(DIK_K, 'K', 'k');
	check_add(DIK_L, 'L', 'l');
	check_add(DIK_M, 'M', 'm');
	check_add(DIK_N, 'N', 'n');
	check_add(DIK_O, 'O', 'o');
	check_add(DIK_P, 'P', 'p');
	check_add(DIK_Q, 'Q', 'q');
	check_add(DIK_R, 'R', 'r');
	check_add(DIK_S, 'S', 's');
	check_add(DIK_T, 'T', 't');
	check_add(DIK_U, 'U', 'u');
	check_add(DIK_V, 'V', 'v');
	check_add(DIK_W, 'W', 'w');
	check_add(DIK_X, 'X', 'x');
	check_add(DIK_Y, 'Y', 'y');
	check_add(DIK_Z, 'Z', 'z');


	check_add(DIK_1, '!', '1');
	check_add(DIK_2, '@', '2');
	check_add(DIK_3, '#', '3');
	check_add(DIK_4, '$', '4');
	check_add(DIK_5, '%', '5');
	check_add(DIK_6, '^', '6');
	check_add(DIK_7, '&', '7');
	check_add(DIK_8, '*', '8');
	check_add(DIK_9, '(', '9');
	check_add(DIK_0, ')', '0');


	check_add(DIK_APOSTROPHE, 34, 39);
	check_add(DIK_SPACE,      ' ', ' ');
	check_add(DIK_SEMICOLON,  ':', ';');
	check_add(DIK_PERIOD,     '>', '.');
	check_add(DIK_COMMA,      '<', ',');
	check_add(DIK_GRAVE,      '`', '`');
	check_add(DIK_MINUS,      '_', '-');
	check_add(DIK_EQUALS,     '+', '=');
	check_add(DIK_LBRACKET,   '{', '[');
	check_add(DIK_RBRACKET,   '}', ']');
	check_add(DIK_BACKSLASH,  '|', '\\');
	check_add(DIK_SLASH,      '?', '/');



	// Make cursor flash
	if(netmessagewrite < MESSAGESIZE-1)
	{
	if((wldframe & 8) == 0)
	{
	netmessage[netmessagewrite] = '#';
	}
	else
	{
	netmessage[netmessagewrite] = '+';
	}
	}


	// Check backspace and return
	if(netmessagedelay == 0)
	{
	if(KEYDOWN(DIK_BACK))
	{
	if(netmessagewrite < MESSAGESIZE)  netmessage[netmessagewrite] = 0;
	if(netmessagewrite > netmessagewritemin) netmessagewrite--;
	netmessagedelay = 3;
	}


	// Ship out the message
	if(KEYDOWN(DIK_RETURN))
	{
	// Is it long enough to bother?
	if(netmessagewrite > 0)
	{
	// Yes, so send it
	netmessage[netmessagewrite] = 0;
	if(networkon)
	{
	start_building_packet();
	add_packet_us(TO_ANY_TEXT);
	add_packet_sz(netmessage);
	send_packet_to_all_players();
	}
	}
	netmessagemode = bfalse;
	netmessagedelay = 20;
	}
	}
	else
	{
	netmessagedelay--;
	}
	}
	else
	{
	// Input a new message?
	if(netmessagedelay == 0)
	{
	if(KEYDOWN(DIK_RETURN))
	{
	// Copy the name
	cnt = 0;
	cTmp = netmessagename[cnt];
	while(cTmp != 0 && cnt < 64)
	{
	netmessage[cnt] = cTmp;
	cnt++;
	cTmp = netmessagename[cnt];
	}
	netmessage[cnt] = '>';  cnt++;
	netmessage[cnt] = ' ';  cnt++;
	netmessage[cnt] = '?';
	netmessage[cnt+1] = 0;
	netmessagewrite = cnt;
	netmessagewritemin = cnt;

	netmessagemode = btrue;
	netmessagedelay = 20;
	}
	}
	else
	{
	netmessagedelay--;
	}
	}
	*/
}

//------------------------------------------------------------------------------
void net_copyFileToAllPlayers(char *source, char *dest)
{
	// JF> This function queues up files to send to all the hosts.
	//     TODO: Deal with having to send to up to MAXPLAYER players...
	NetFileTransfer *state;

	if(net_numFileTransfers < NET_MAX_FILE_TRANSFERS)
	{
		// net_fileTransferTail should already be pointed at an open
		// slot in the queue.
		state = &(net_transferStates[net_fileTransferTail]);
		assert(state->sourceName[0] == 0);

		// Just do the first player for now
		state->target = &net_myHost->peers[0];
		strncpy(state->sourceName, source, NET_MAX_FILE_NAME);
		strncpy(state->destName, dest, NET_MAX_FILE_NAME);

		// advance the tail index
		net_numFileTransfers++;
		net_fileTransferTail++;
		if(net_fileTransferTail >= NET_MAX_FILE_TRANSFERS)
		{
			net_fileTransferTail = 0;
		}

		if(net_fileTransferTail == net_fileTransferHead)
		{
			log_warning("net_copyFileToAllPlayers: Warning!  Queue tail caught up with the head!\n");
		}
	}
}

//------------------------------------------------------------------------------
void net_copyFileToAllPlayersOld(char *source, char *dest)
{
	// ZZ> This function copies a file on the host to every remote computer.
	//     Packets are sent in chunks of COPYSIZE bytes.  The max file size
	//     that can be sent is 2 Megs ( TOTALSIZE ).
	FILE* fileread;
	int packetsize, packetstart;
	int filesize;
	int fileisdir;
	char cTmp;

	log_info("net_copyFileToAllPlayers: %s, %s\n", source, dest);
	if(networkon && hostactive)
	{
		fileisdir = fs_fileIsDirectory(source);
		if(fileisdir)
		{
			net_startNewPacket();
			packet_addUnsignedShort(TO_REMOTE_DIR);
			packet_addString(dest);
			net_sendPacketToAllPlayersGuaranteed();
		}
		else
		{
			fileread = fopen(source, "rb");
			if(fileread)
			{
				fseek(fileread, 0, SEEK_END);
				filesize = ftell(fileread);
				fseek(fileread, 0, SEEK_SET);
				if(filesize > 0 && filesize < TOTALSIZE)
				{
					packetsize = 0;
					packetstart = 0;
					numfilesent++;

					net_startNewPacket();
					packet_addUnsignedShort(TO_REMOTE_FILE);
					packet_addString(dest);
					packet_addUnsignedInt(filesize);
					packet_addUnsignedInt(packetstart);
					while(packetstart < filesize)
					{
						//	This will probably work...
						//fread((packetbuffer + packethead), COPYSIZE, 1, fileread);

						// But I'll leave it alone for now
						fscanf(fileread, "%c", &cTmp);

						packet_addUnsignedByte(cTmp);
						packetsize++;
						packetstart++;
						if(packetsize >= COPYSIZE)
						{
							// Send off the packet
							net_sendPacketToAllPlayersGuaranteed();
							enet_host_flush(net_myHost);

							// Start on the next 4K
							packetsize = 0;
							net_startNewPacket();
							packet_addUnsignedShort(TO_REMOTE_FILE);
							packet_addString(dest);
							packet_addUnsignedInt(filesize);
							packet_addUnsignedInt(packetstart);
						}
					}
					// Send off the packet
					net_sendPacketToAllPlayersGuaranteed();
				}
				fclose(fileread);
			}
		}
	}
}

//------------------------------------------------------------------------------
void net_copyFileToHost(char *source, char *dest)
{
	NetFileTransfer *state;

	// JF> New function merely queues up a new file to be sent

	// If this is the host, just copy the file locally
	if(hostactive)
	{
		// Simulate a network transfer
		if(fs_fileIsDirectory(source))
		{
			fs_createDirectory(dest);
		}
		else
		{
			fs_copyFile(source, dest);
		}
		return;
	}

	if(net_numFileTransfers < NET_MAX_FILE_TRANSFERS)
	{
		// net_fileTransferTail should already be pointed at an open
		// slot in the queue.
		state = &(net_transferStates[net_fileTransferTail]);
		assert(state->sourceName[0] == 0);

		state->target = net_gameHost;
		strncpy(state->sourceName, source, NET_MAX_FILE_NAME);
		strncpy(state->destName, dest, NET_MAX_FILE_NAME);

		// advance the tail index
		net_numFileTransfers++;
		net_fileTransferTail++;
		if(net_fileTransferTail >= NET_MAX_FILE_TRANSFERS)
		{
			net_fileTransferTail = 0;
		}

		if(net_fileTransferTail == net_fileTransferHead)
		{
			log_warning("net_copyFileToHost: Warning!  Queue tail caught up with the head!\n");
		}
	}
}

//------------------------------------------------------------------------------
void net_copyFileToHostOld(char *source, char *dest)
{
	// ZZ> This function copies a file on the remote to the host computer.
	//     Packets are sent in chunks of COPYSIZE bytes.  The max file size
	//     that can be sent is 2 Megs ( TOTALSIZE ).
	FILE* fileread;
	int packetsize, packetstart;
	int filesize;
	int fileisdir;
	char cTmp;

	log_info("net_copyFileToHost: ");
	fileisdir = fs_fileIsDirectory(source);
	if(hostactive)
	{
		// Simulate a network transfer
		if(fileisdir)
		{
			log_info("Creating local directory %s\n", dest);
			fs_createDirectory(dest);
		}
		else
		{
			log_info("Copying local file %s --> %s\n", source, dest);
			fs_copyFile(source, dest);
		}
	}
	else
	{
		if(fileisdir)
		{
			log_info("Creating directory on host: %s\n", dest);
			net_startNewPacket();
			packet_addUnsignedShort(TO_HOST_DIR);
			packet_addString(dest);
//			net_sendPacketToAllPlayersGuaranteed();
			net_sendPacketToHost();
		}
		else
		{
			log_info("Copying local file to host file: %s --> %s\n", source, dest);
			fileread = fopen(source, "rb");
			if(fileread)
			{
				fseek(fileread, 0, SEEK_END);
				filesize = ftell(fileread);
				fseek(fileread, 0, SEEK_SET);
				if(filesize > 0 && filesize < TOTALSIZE)
				{
					numfilesent++;
					packetsize = 0;
					packetstart = 0;
					net_startNewPacket();
					packet_addUnsignedShort(TO_HOST_FILE);
					packet_addString(dest);
					packet_addUnsignedInt(filesize);
					packet_addUnsignedInt(packetstart);
					while(packetstart < filesize)
					{
						fscanf(fileread, "%c", &cTmp);
						packet_addUnsignedByte(cTmp);
						packetsize++;
						packetstart++;
						if(packetsize >= COPYSIZE)
						{
							// Send off the packet
							net_sendPacketToHostGuaranteed();
							enet_host_flush(net_myHost);

							// Start on the next 4K
							packetsize = 0;
							net_startNewPacket();
							packet_addUnsignedShort(TO_HOST_FILE);
							packet_addString(dest);
							packet_addUnsignedInt(filesize);
							packet_addUnsignedInt(packetstart);
						}
					}
					// Send off the packet
					net_sendPacketToHostGuaranteed();
				}
				fclose(fileread);
			}
		}
	}
}

//--------------------------------------------------------------------------------------------
void net_copyDirectoryToHost(char *dirname, char *todirname)
{
	// ZZ> This function copies all files in a directory
	char searchname[128];
	char fromname[128];
	char toname[128];
	const char *searchResult;

	log_info("net_copyDirectoryToHost: %s, %s\n", dirname, todirname);
	// Search for all files
	sprintf(searchname, "%s/*", dirname);
	searchResult = fs_findFirstFile(dirname, NULL);
	if(searchResult != NULL)
	{
		// Make the new directory
		net_copyFileToHost(dirname, todirname);

		// Copy each file
		while(searchResult != NULL)
		{
			// If a file begins with a dot, assume it's something
			// that we don't want to copy.  This keeps repository
			// directories, /., and /.. from being copied
			// Also avoid copying directories in general.
			sprintf(fromname, "%s/%s", dirname, searchResult);
			if(searchResult[0] == '.' || fs_fileIsDirectory(fromname)) 
			{
				searchResult = fs_findNextFile();
				continue;
			}

			sprintf(fromname, "%s/%s", dirname, searchResult);
			sprintf(toname, "%s/%s", todirname, searchResult);

			net_copyFileToHost(fromname, toname);
			searchResult = fs_findNextFile();
		}
	}

	fs_findClose();
}

//--------------------------------------------------------------------------------------------
void net_copyDirectoryToAllPlayers(char *dirname, char *todirname)
{
	// ZZ> This function copies all files in a directory
	char searchname[128];
	char fromname[128];
	char toname[128];
	const char *searchResult;

	log_info("net_copyDirectoryToAllPlayers: %s, %s\n", dirname, todirname);
	// Search for all files
	sprintf(searchname, "%s/*.*", dirname);
	searchResult = fs_findFirstFile(dirname, NULL);
	if(searchResult != NULL)
	{
		// Make the new directory
		net_copyFileToAllPlayers(dirname, todirname);

		// Copy each file
		while(searchResult != NULL)
		{
			// If a file begins with a dot, assume it's something
			// that we don't want to copy.  This keeps repository
			// directories, /., and /.. from being copied
			if(searchResult[0] == '.')
			{
				searchResult = fs_findNextFile();
				continue;
			}

			sprintf(fromname, "%s/%s", dirname, searchResult);
			sprintf(toname, "%s/%s", todirname, searchResult);
			net_copyFileToAllPlayers(fromname, toname);

			searchResult = fs_findNextFile();
		}
	}
	fs_findClose();
}

//--------------------------------------------------------------------------------------------
void net_sayHello()
{
	// ZZ> This function lets everyone know we're here
	if(networkon)
	{
		if(hostactive)
		{
			log_info("net_sayHello: Server saying hello.\n");
			playersloaded++;
			if(playersloaded >= numplayer)
			{
				waitingforplayers = bfalse;
			}
		}
		else
		{
			log_info("net_sayHello: Client saying hello.\n");
			net_startNewPacket();
			packet_addUnsignedShort(TO_HOST_IM_LOADED);
			net_sendPacketToHostGuaranteed();
		}
	}
	else
	{
		waitingforplayers = bfalse;
	}
}

//--------------------------------------------------------------------------------------------
void cl_talkToHost()
{
	// ZZ> This function sends the latch packets to the host machine
	unsigned char player;

	// Let the players respawn
	if(sdlkeybuffer[SDLK_SPACE]
	&& ( alllocalpladead || respawnanytime )
		&& respawnvalid
		&& rtscontrol == bfalse
		&& netmessagemode == bfalse)
	{
		player = 0;
		while(player < MAXPLAYER)
		{
			if(plavalid[player] && pladevice[player] != INPUTNONE)
			{
				plalatchbutton[player]|=LATCHBUTTONRESPAWN;  // Press the respawn button...
			}
			player++;
		}
	}

	// Start talkin'
	if(networkon && hostactive==bfalse && rtscontrol == bfalse)
	{
		net_startNewPacket();
		packet_addUnsignedShort(TO_HOST_LATCH);					// The message header
		player = 0;
		while(player < MAXPLAYER)
		{
			// Find the local players
			if(plavalid[player] && pladevice[player] != INPUTNONE)
			{
				packet_addUnsignedByte(player);                          // The player index
				packet_addUnsignedByte(plalatchbutton[player]);          // Player button states
				packet_addSignedShort(plalatchx[player]*SHORTLATCH);    // Player motion
				packet_addSignedShort(plalatchy[player]*SHORTLATCH);    // Player motion
			}
			player++;
		}

		// Send it to the host        
		net_sendPacketToHost();
	}
}

//--------------------------------------------------------------------------------------------
void sv_talkToRemotes()
{
	// ZZ> This function sends the character data to all the remote machines
	int player, time;
	signed short sTmp;

	if(wldframe > STARTTALK)
	{
		if(hostactive && rtscontrol == bfalse)
		{
			time = wldframe+lag;

			if (networkon)
			{
				// Send a message to all players
				net_startNewPacket();
				packet_addUnsignedShort(TO_REMOTE_LATCH);                         // The message header
				packet_addUnsignedInt(time);                                    // The stamp


				// Send all player latches...
				player = 0;
				while(player < MAXPLAYER)
				{
					if(plavalid[player])
					{
						packet_addUnsignedByte(player);                          // The player index
						packet_addUnsignedByte(plalatchbutton[player]);          // Player button states
						packet_addSignedShort(plalatchx[player]*SHORTLATCH);    // Player motion
						packet_addSignedShort(plalatchy[player]*SHORTLATCH);    // Player motion
					}
					player++;
				}


				// Send the packet
				net_sendPacketToAllPlayers();
			}
			else
			{
				time = wldframe+1;
			}


			// Now pretend the host got the packet...
			time = time&LAGAND;
			player = 0;
			while(player < MAXPLAYER)
			{
				if(plavalid[player])
				{
					platimelatchbutton[player][time] = plalatchbutton[player];
					sTmp = plalatchx[player]*SHORTLATCH;
					platimelatchx[player][time] = sTmp/SHORTLATCH;
					sTmp = plalatchy[player]*SHORTLATCH;
					platimelatchy[player][time] = sTmp/SHORTLATCH;
				}
				player++;
			}
			numplatimes++;
		}
	}
}

//--------------------------------------------------------------------------------------------
void net_handlePacket(ENetEvent *event)
{
	unsigned short header;
	char filename[256];			// also used for reading various strings
	int filesize, newfilesize, fileposition;
	char newfile;
	unsigned short player;
	unsigned int stamp;
	int time;
	FILE *file;
	size_t fileSize;

	log_info("net_handlePacket: Received ");
	
	packet_startReading(event->packet);
	header = packet_readUnsignedShort();
	switch(header)
	{
	case TO_ANY_TEXT:
		log_info("TO_ANY_TEXT\n");
		packet_readString(filename, 255);
		debug_message(filename);
		break;

	case TO_HOST_MODULEOK:
		log_info("TO_HOSTMODULEOK\n");
		if(hostactive)
		{
			playersready++;
			if(playersready >= numplayer)
			{
				readytostart = btrue;
			}
		}
		break;

	case TO_HOST_LATCH:
		log_info("TO_HOST_LATCH\n");
		if(hostactive)
		{
			while(packet_remainingSize() > 0)
			{
				player = packet_readUnsignedByte();
				plalatchbutton[player] = packet_readUnsignedByte();
				plalatchx[player] = packet_readSignedShort() / SHORTLATCH;
				plalatchy[player] = packet_readSignedShort() / SHORTLATCH;
			}

		}
		break;

	case TO_HOST_IM_LOADED:
		log_info("TO_HOST_IMLOADED\n");
		if(hostactive)
		{
			playersloaded++;
			if(playersloaded == numplayer)
			{
				// Let the games begin...
				waitingforplayers = bfalse;
				net_startNewPacket();
				packet_addUnsignedShort(TO_REMOTE_START);
				net_sendPacketToAllPlayersGuaranteed();
			}
		}
		break;

	case TO_HOST_RTS:
		log_info("TO_HOST_RTS\n");
		if(hostactive)
		{
			/*whichorder = get_empty_order();
			if(whichorder < MAXORDER)
			{
				// Add the order on the host machine
				cnt = 0;
				while(cnt < MAXSELECT)
				{
					who = packet_readUnsignedByte();
					orderwho[whichorder][cnt] = who;
					cnt++;
				}
				what = packet_readUnsignedInt();
				when = wldframe + orderlag;
				orderwhat[whichorder] = what;
				orderwhen[whichorder] = when;


				// Send the order off to everyone else
				net_startNewPacket();
				packet_addUnsignedShort(TO_REMOTE_RTS);
				cnt = 0;
				while(cnt < MAXSELECT)
				{
					packet_addUnsignedByte(orderwho[whichorder][cnt]);
					cnt++;
				}
				packet_addUnsignedInt(what);
				packet_addUnsignedInt(when);
				net_sendPacketToAllPlayersGuaranteed();
			}*/
		}
		break;

	case NET_TRANSFER_FILE:
		packet_readString(filename, 256);
		fileSize = packet_readUnsignedInt();

		log_info("NET_TRANSFER_FILE: %s with size %d.\n", filename, fileSize);

		// Try and save the file
		file = fopen(filename, "wb");
		if(file != NULL)
		{
			fwrite(net_readPacket->data + net_readLocation, 1, fileSize, file);
			fclose(file);
		} else
		{
			log_warning("net_handlePacket: Couldn't write new file!\n");
		}

		// Acknowledge that we got this file
		net_startNewPacket();
		packet_addUnsignedShort(NET_TRANSFER_OK);
		net_sendPacketToPeer(event->peer);

		// And note that we've gotten another one
		numfile++;
		break;

	case NET_TRANSFER_OK:
		log_info("NET_TRANSFER_OK. The last file sent was successful.\n");
		net_waitingForXferAck = 0;
		net_numFileTransfers--;

		break;

	case NET_CREATE_DIRECTORY:
		packet_readString(filename, 256);
		log_info("NET_CREATE_DIRECTORY: %s\n", filename);
		
		fs_createDirectory(filename);
		
		// Acknowledge that we got this file
		net_startNewPacket();
		packet_addUnsignedShort(NET_TRANSFER_OK);
		net_sendPacketToPeer(event->peer);
		
		numfile++;	// The client considers directories it sends to be files, so ya.
		break;

	case NET_DONE_SENDING_FILES:
		log_info("NET_DONE_SENDING_FILES\n");
		numplayerrespond++;
		break;

	case NET_NUM_FILES_TO_SEND:
		log_info("NET_NUM_FILES_TO_SEND\n");
		numfileexpected = (int)packet_readUnsignedShort();
		break;

	case TO_HOST_FILE:
		log_info("TO_HOST_FILE\n");
		packet_readString(filename, 255);
		newfilesize = packet_readUnsignedInt();

		// Change the size of the file if need be
		newfile = 0;
		file = fopen(filename, "rb");
		if(file)
		{
			fseek(file, 0, SEEK_END);
			filesize = ftell(file);
			fclose(file);

			if(filesize != newfilesize)
			{
				// Destroy the old file
				newfile = 1;
			}
		} else
		{
			newfile = 1;
		}
		
		if(newfile)
		{
			// file must be created.  Write zeroes to the file to do it
			numfile++;
			file = fopen(filename, "wb");
			if(file)
			{
				filesize = 0;
				while(filesize < newfilesize)
				{
					fputc(0, file);
					filesize++;
				}
				fclose(file);
			}
		}

		// Go to the position in the file and copy data
		fileposition = packet_readUnsignedInt();
		file = fopen(filename, "r+b");
		if(file)
		{
			if(fseek(file, fileposition, SEEK_SET) == 0)
			{
				while(packet_remainingSize() > 0)
				{
					fputc(packet_readUnsignedByte(), file);
				}
			}
			fclose(file);
		}
		break;

	case TO_HOST_DIR:
		log_info("TO_HOST_DIR\n");
		if(hostactive)
		{
			packet_readString(filename, 255);
			fs_createDirectory(filename);
		}
		break;

	case TO_HOST_FILESENT:
		log_info("TO_HOST_FILESENT\n");
		if(hostactive)
		{
			numfileexpected += packet_readUnsignedInt();
			numplayerrespond++;
		}
		break;

	case TO_REMOTE_FILESENT:
		log_info("TO_REMOTE_FILESENT\n");
		if(!hostactive)
		{
			numfileexpected += packet_readUnsignedInt();
			numplayerrespond++;
		}
		break;

	case TO_REMOTE_MODULE:
		log_info("TO_REMOTE_MODULE\n");
		if(!hostactive && !readytostart)
		{
			seed = packet_readUnsignedInt();
			packet_readString(filename, 255);
			strcpy(pickedmodule, filename);

			// Check to see if the module exists
			pickedindex = find_module(pickedmodule);
			if(pickedindex == -1)
			{
				// The module doesn't exist locally
				// !!!BAD!!!  Copy the data from the host
				pickedindex = 0;
			}

			// Make ourselves ready
			readytostart = btrue;

			// Tell the host we're ready
			net_startNewPacket();
			packet_addUnsignedShort(TO_HOST_MODULEOK);
			net_sendPacketToHostGuaranteed();
		}
		break;

	case TO_REMOTE_START:
		log_info("TO_REMOTE_START\n");
		if(!hostactive)
		{
			waitingforplayers = bfalse;
		}
		break;

	case TO_REMOTE_RTS:
		log_info("TO_REMOTE_RTS\n");
		if(!hostactive)
		{
	/*		whichorder = get_empty_order();
			if(whichorder < MAXORDER)
			{
				// Add the order on the remote machine
				cnt = 0;
				while(cnt < MAXSELECT)
				{
					who = packet_readUnsignedByte();
					orderwho[whichorder][cnt] = who;
					cnt++;
				}
				what = packet_readUnsignedInt();
				when = packet_readUnsignedInt();
				orderwhat[whichorder] = what;
				orderwhen[whichorder] = when;
			}*/
		}
		break;

	case TO_REMOTE_FILE:
		log_info("TO_REMOTE_FILE\n");
		if(!hostactive)
		{
			packet_readString(filename, 255);
			newfilesize = packet_readUnsignedInt();

			// Change the size of the file if need be
			newfile = 0;
			file = fopen(filename, "rb");
			if(file)
			{
				fseek(file, 0, SEEK_END);
				filesize = ftell(file);
				fclose(file);

				if(filesize != newfilesize)
				{
					// Destroy the old file
					newfile = 1;
				}
			} else
			{
				newfile = 1;
			}

			if(newfile)
			{
				// file must be created.  Write zeroes to the file to do it
				numfile++;
				file = fopen(filename, "wb");
				if(file)
				{
					filesize = 0;
					while(filesize < newfilesize)
					{
						fputc(0, file);
						filesize++;
					}
					fclose(file);
				}
			}

			// Go to the position in the file and copy data
			fileposition = packet_readUnsignedInt();
			file = fopen(filename, "r+b");
			if(file)
			{
				if(fseek(file, fileposition, SEEK_SET) == 0)
				{
					while(packet_remainingSize() > 0)
					{
						fputc(packet_readUnsignedByte(), file);
					}
				}
				fclose(file);
			}
		}
		break;

	case TO_REMOTE_DIR:
		log_info("TO_REMOTE_DIR\n");
		if(!hostactive)
		{
			packet_readString(filename, 255);
			fs_createDirectory(filename);
		}
		break;

	case TO_REMOTE_LATCH:
		log_info("TO_REMOTE_LATCH\n");
		if(!hostactive)
		{
			stamp = packet_readUnsignedInt();
			time = stamp&LAGAND;
			if(nexttimestamp == -1)
			{
				nexttimestamp = stamp;
			}
			if(stamp < nexttimestamp)
			{
				log_warning("net_handlePacket: OUT OF ORDER PACKET\n");
				outofsync = btrue;
			}
			if(stamp <= wldframe)
			{
				log_warning("net_handlePacket: LATE PACKET\n");
				outofsync = btrue;
			}
			if(stamp > nexttimestamp)
			{
				log_warning("net_handlePacket: MISSED PACKET\n");
				nexttimestamp = stamp;  // Still use it
				outofsync = btrue;
			}
			if(stamp == nexttimestamp)
			{
				// Remember that we got it
				numplatimes++;


				// Read latches for each player sent
				while(packet_remainingSize() > 0)
				{
					player = packet_readUnsignedByte();
					platimelatchbutton[player][time] = packet_readUnsignedByte();
					platimelatchx[player][time] = packet_readSignedShort() / SHORTLATCH;
					platimelatchy[player][time] = packet_readSignedShort() / SHORTLATCH;
				}
				nexttimestamp = stamp+1;
			}
		}
		break;
	}
}

//--------------------------------------------------------------------------------------------
void listen_for_packets()
{
	// ZZ> This function reads any new messages and sets the player latch and matrix needed
	//     lists...
	ENetEvent event;

	if(networkon)
	{
		// Listen for new messages
		while(enet_host_service(net_myHost, &event, 0) != 0)
		{
			switch(event.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				net_handlePacket(&event);
				enet_packet_destroy(event.packet);
				break;

			case ENET_EVENT_TYPE_CONNECT:
				// don't allow anyone to connect during the game session
				log_warning("listen_for_packets: Client tried to connect during the game: %x:%u\n",
					event.peer->address.host, event.peer->address.port);
#ifdef ENET11
				enet_peer_disconnect(event.peer, 0);
#else
				enet_peer_disconnect(event.peer);
#endif
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				// Is this a player disconnecting, or just a rejected connection
				// from above?
				if(event.peer->data != 0)
				{
					NetPlayerInfo *info = event.peer->data;

					// uh oh, how do we handle losing a player?
					log_warning("listen_for_packets: Player %d disconnected!\n",
						info->playerSlot);
				}
				break;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------
void unbuffer_player_latches()
{
	// ZZ> This function sets character latches based on player input to the host
	int cnt, time, character;


	// Copy the latches
	time = wldframe&LAGAND;
	cnt = 0;
	while(cnt < MAXPLAYER)
	{
		if(plavalid[cnt] && !rtscontrol)
		{
			character = plaindex[cnt];

			chrlatchx[character] = platimelatchx[cnt][time];
			chrlatchy[character] = platimelatchy[cnt][time];
			chrlatchbutton[character] = platimelatchbutton[cnt][time];

			// Let players respawn
			if((chrlatchbutton[character] & LATCHBUTTONRESPAWN) && respawnvalid)
			{
				if(chralive[character] == bfalse)
				{
					respawn_character(character);
					teamleader[chrteam[character]] = character;
					chralert[character] |= ALERTIFCLEANEDUP;
					// Cost some experience for doing this...  Never lose a level
					chrexperience[character] = chrexperience[character] * EXPKEEP;
				}
				chrlatchbutton[character] &= 127;
			}
		}
		cnt++;
	}
	numplatimes--;
}

//--------------------------------------------------------------------------------------------
void send_rts_order(int x, int y, unsigned char order, unsigned char target)
{
	// ZZ> This function asks the host to order the selected characters
/*	unsigned int what, when, whichorder, cnt;

	if(numrtsselect > 0)
	{
		x = (x >> 6) & 1023;
		y = (y >> 6) & 1023;
		what = (target << 24) | (x << 14) | (y << 4) | (order&15); 
		if(hostactive)
		{
			when = wldframe + orderlag;
			whichorder = get_empty_order();
			if(whichorder != MAXORDER)
			{
				// Add a new order on own machine
				orderwhen[whichorder] = when;
				orderwhat[whichorder] = what;
				cnt = 0;
				while(cnt < numrtsselect)
				{
					orderwho[whichorder][cnt] = rtsselect[cnt];
					cnt++;
				}
				while(cnt < MAXSELECT)
				{
					orderwho[whichorder][cnt] = MAXCHR;
					cnt++;
				}


				// Send the order off to everyone else
				if(networkon)
				{
					net_startNewPacket();
					packet_addUnsignedShort(TO_REMOTE_RTS);
					cnt = 0;
					while(cnt < MAXSELECT)
					{
						packet_addUnsignedByte(orderwho[whichorder][cnt]);
						cnt++;
					}
					packet_addUnsignedInt(what);
					packet_addUnsignedInt(when);
					net_sendPacketToAllPlayersGuaranteed();
				}
			}
		}
		else
		{
			// Send the order off to the host
			net_startNewPacket();
			packet_addUnsignedShort(TO_HOST_RTS);
			cnt = 0;
			while(cnt < numrtsselect)
			{
				packet_addUnsignedByte(rtsselect[cnt]);
				cnt++;
			}
			while(cnt < MAXSELECT)
			{
				packet_addUnsignedByte(MAXCHR);
				cnt++;
			}
			packet_addUnsignedInt(what);
			net_sendPacketToHostGuaranteed();
		}
	}*/
}

//--------------------------------------------------------------------------------------------
void net_initialize()
{
	// ZZ> This starts up the network and logs whatever goes on
	serviceon = bfalse;
	numsession = 0;
	numservice = 0;

	// Clear all the state variables to 0 to start.
	memset(net_playerPeers, 0, sizeof(ENetPeer*) * MAXPLAYER);
	memset(net_playerInfo, 0, sizeof(NetPlayerInfo) * MAXPLAYER);
	memset(packetbuffer, 0, MAXSENDSIZE);
	memset(net_transferStates, 0, sizeof(NetFileTransfer) * NET_MAX_FILE_TRANSFERS);
	memset(&net_receiveState, 0, sizeof(NetFileTransfer));

	if(networkon)
	{
		// initialize enet
		log_info("net_initialize: Initializing enet...");
		if(enet_initialize() != 0)
		{
			log_info("Failed!\n");
			networkon = bfalse;
			serviceon = 0;
		} else
		{
			log_info("Done\n");
			serviceon = btrue;
			numservice = 1;
		}
	} else
	{
		// We're not doing networking this time...
		log_info("net_initialize: Networking not enabled.\n");
	}
}

//--------------------------------------------------------------------------------------------
void net_shutDown()
{
	log_info("net_shutDown: Turning off networking.\n");
	enet_deinitialize();
}

//--------------------------------------------------------------------------------------------
void find_open_sessions()
{
	/*PORT
	// ZZ> This function finds some open games to join
	DPSESSIONDESC2      sessionDesc;
	HRESULT             hr;

	if(networkon)
	{
	numsession = 0;
	if(globalnetworkerr)  fprintf(globalnetworkerr, "  Looking for open games...\n");
	ZeroMemory(&sessionDesc, sizeof(DPSESSIONDESC2));
	sessionDesc.dwSize = sizeof(DPSESSIONDESC2);
	sessionDesc.guidApplication = NETWORKID;
	hr = lpDirectPlay3A->EnumSessions(&sessionDesc, 0, SessionsCallback, hGlobalWindow, DPENUMSESSIONS_AVAILABLE);
	if(globalnetworkerr)  fprintf(globalnetworkerr, "    %d sessions found\n", numsession);
	}
	*/
}

//--------------------------------------------------------------------------------------------
void sv_letPlayersJoin()
{
	// ZZ> This function finds all the players in the game
	ENetEvent event;
	char hostName[64];

	// Check all pending events for players joining
	while(enet_host_service(net_myHost, &event, 0) > 0)
	{
		switch(event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			// Look up the hostname the player is connecting from
			enet_address_get_host(&event.peer->address, hostName, 64);

			log_info("sv_letPlayersJoin: A new player connected from %s:%u\n",
				hostName, event.peer->address.port);

			// save the player data here.
			enet_address_get_host(&event.peer->address, hostName, 64);
			strncpy(netplayername[numplayer], hostName, 16);

			event.peer->data = &(net_playerInfo[numplayer]);
			numplayer++;

			break;
		
		case ENET_EVENT_TYPE_RECEIVE:
			log_info("sv_letPlayersJoin: Recieved a packet when we weren't expecting it...\n");
			log_info("\tIt came from %x:%u\n", event.peer->address.host, event.peer->address.port);

			// clean up the packet
			enet_packet_destroy(event.packet);
			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			log_info("sv_letPlayersJoin: A client disconnected!  Address %x:%u\n",
				event.peer->address.host, event.peer->address.port);

			// Reset that peer's data
			event.peer->data = NULL;
		}
	}
}

//--------------------------------------------------------------------------------------------
int cl_joinGame(const char* hostname)
{
	// ZZ> This function tries to join one of the sessions we found
	ENetAddress address;
	ENetEvent event;

	if(networkon)
	{
		log_info("cl_joinGame: Creating client network connection...");
		// Create my host thingamabober
		// TODO: Should I limit client bandwidth here?
		net_myHost = enet_host_create(NULL, 1, 0, 0);
		if(net_myHost == NULL)
		{
			// can't create a network connection at all
			log_info("Failed!\n");
			return bfalse;
		}
		log_info("Succeeded\n");

		// Now connect to the remote host
		log_info("cl_joinGame: Attempting to connect to %s:%d\n", hostname, NET_EGOBOO_PORT);
		enet_address_set_host(&address, hostname);
		address.port = NET_EGOBOO_PORT;
		net_gameHost = enet_host_connect(net_myHost, &address, NET_EGOBOO_NUM_CHANNELS);
		if(net_gameHost == NULL)
		{
			log_info("cl_joinGame: No available peers to create a connection!\n");
			return bfalse;
		}

		// Wait for up to 5 seconds for the connection attempt to succeed
		if(enet_host_service(net_myHost, &event, 5000) > 0 &&
			event.type == ENET_EVENT_TYPE_CONNECT)
		{
			log_info("cl_joinGame: Connected to %s:%d\n", hostname, NET_EGOBOO_PORT);
			return btrue;
			// return create_player(bfalse);
		} else
		{
			log_info("cl_joinGame: Could not connect to %s:%d!\n", hostname, NET_EGOBOO_PORT);
		}
	}
	return bfalse;
}

//--------------------------------------------------------------------------------------------
void stop_players_from_joining()
{
	// ZZ> This function stops players from joining a game
}

//--------------------------------------------------------------------------------------------
int sv_hostGame()
{
	// ZZ> This function tries to host a new session
	ENetAddress address;

	if(networkon)
	{
		// Try to create a new session
		address.host = ENET_HOST_ANY;
		address.port = NET_EGOBOO_PORT;

		log_info("sv_hostGame: Creating game on port %d\n", NET_EGOBOO_PORT);
		net_myHost = enet_host_create(&address, MAXPLAYER, 0, 0);
		if(net_myHost == NULL)
		{
			log_info("sv_hostGame: Could not create network connection!\n");
			return bfalse;
		}

		// Try to create a host player
//		return create_player(btrue);
		net_amHost = btrue;

		// Moved from net_sayHello because there they cause a race issue
		waitingforplayers = btrue;
		playersloaded = 0;
	} 
	// Run in solo mode
	return btrue;
}

//--------------------------------------------------------------------------------------------
void turn_on_service(int service)
{
	// This function turns on a network service ( IPX, TCP, serial, modem )
}

int  net_pendingFileTransfers()
{
	return net_numFileTransfers;
}

unsigned char	*transferBuffer = NULL;
size_t			transferSize = 0;

void net_updateFileTransfers()
{
	NetFileTransfer *state;
	ENetPacket *packet;
	size_t nameLen, fileSize;
	unsigned int networkSize;
	FILE *file;
	char *p;

	// Are there any pending file sends?
	if(net_numFileTransfers > 0)
	{
		if(!net_waitingForXferAck)
		{
			state = &net_transferStates[net_fileTransferHead];

			// Check and see if this is a directory, instead of a file
			if(fs_fileIsDirectory(state->sourceName))
			{
				// Tell the target to create a directory
				log_info("net_updateFileTranfers: Creating directory %s on target\n", state->destName);
				net_startNewPacket();
				packet_addUnsignedShort(NET_CREATE_DIRECTORY);
				packet_addString(state->destName);
				net_sendPacketToPeerGuaranteed(state->target);
				
				net_waitingForXferAck = 1;
			} else
			{
				file = fopen(state->sourceName, "rb");
				if(file)
				{
					log_info("net_updateFileTransfers: Attempting to send %s to %s\n", state->sourceName, state->destName);

					fseek(file, 0, SEEK_END);
					fileSize = ftell(file);
					fseek(file, 0, SEEK_SET);

					// Make room for the file's name
					nameLen = strlen(state->destName) + 1;
					transferSize = nameLen;

					// And for the file's size
					transferSize += 6;	// unsigned int size, and unsigned short message type
					transferSize += fileSize;

					transferBuffer = malloc(transferSize);
					*(unsigned short*)transferBuffer = ENET_HOST_TO_NET_16(NET_TRANSFER_FILE);

					// Add the string and file length to the buffer
					p = transferBuffer + 2;
					strcpy(p, state->destName);
					p += nameLen;

					networkSize = ENET_HOST_TO_NET_32((unsigned long)fileSize);
					*(size_t*)p = networkSize;
					p += 4;
					
					fread(p, 1, fileSize, file);
					fclose(file);

					packet = enet_packet_create(transferBuffer, transferSize, ENET_PACKET_FLAG_RELIABLE);
					enet_peer_send(state->target, NET_GUARANTEED_CHANNEL, packet);

					free(transferBuffer);
					transferBuffer = NULL;
					transferSize = 0;

					net_waitingForXferAck = 1;
				} else
				{
					log_warning("net_updateFileTransfers: Could not open file %s to send it!\n", state->sourceName);
				}
			}

			// update transfer queue state
			memset(state, 0, sizeof(NetFileTransfer));
			net_fileTransferHead++;
			if(net_fileTransferHead >= NET_MAX_FILE_TRANSFERS)
			{
				net_fileTransferHead = 0;
			}

		} // end if waiting for ack
	} // end if net_numFileTransfers > 0

	// Let the recieve loop run at least once
	listen_for_packets();
}
