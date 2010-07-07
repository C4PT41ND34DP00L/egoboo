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

/// @file char.c
/// @brief Implementation of character functions
/// @details

#include "char.inl"
#include "ChrList.h"

#include "mad.h"
#include "md2.inl"
#include "mesh.inl"

#include "log.h"
#include "script.h"
#include "menu.h"
#include "sound.h"
#include "camera.h"
#include "input.h"
#include "passage.h"
#include "graphic.h"
#include "game.h"
#include "texture.h"
#include "ui.h"

#include "egoboo_vfs.h"
#include "egoboo_setup.h"
#include "egoboo_fileutil.h"
#include "egoboo_strutil.h"
#include "egoboo_math.inl"
#include "egoboo.h"

#include "SDL_extensions.h"

#include <float.h>
#include "egoboo_mem.h"

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
static IDSZ    inventory_idsz[INVEN_COUNT];

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
INSTANTIATE_STACK( ACCESS_TYPE_NONE, cap_t, CapStack, MAX_PROFILE );
INSTANTIATE_STACK( ACCESS_TYPE_NONE, team_t, TeamStack, TEAM_MAX );

int chr_stoppedby_tests = 0;
int chr_pressure_tests = 0;

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
static chr_instance_t * chr_instance_ctor( chr_instance_t * pinst );
static chr_instance_t * chr_instance_dtor( chr_instance_t * pinst );
static bool_t           chr_instance_alloc( chr_instance_t * pinst, size_t vlst_size );
static bool_t           chr_instance_free( chr_instance_t * pinst );
static bool_t           chr_spawn_instance( chr_instance_t * pinst, const PRO_REF by_reference profile, Uint8 skin );
static bool_t           chr_instance_set_mad( chr_instance_t * pinst, const MAD_REF by_reference imad );

static CHR_REF chr_pack_has_a_stack( const CHR_REF by_reference item, const CHR_REF by_reference character );
static bool_t  chr_add_pack_item( const CHR_REF by_reference item, const CHR_REF by_reference character );
static CHR_REF chr_get_pack_item( const CHR_REF by_reference character, grip_offset_t grip_off, bool_t ignorekurse );

static bool_t set_weapongrip( const CHR_REF by_reference iitem, const CHR_REF by_reference iholder, Uint16 vrt_off );

static BBOARD_REF chr_add_billboard( const CHR_REF by_reference ichr, Uint32 lifetime_secs );

static chr_t * resize_one_character( chr_t * pchr );
//static void    resize_all_characters();

static bool_t  chr_free( chr_t * pchr );

static chr_t * chr_config_ctor( chr_t * pchr );
static chr_t * chr_config_init( chr_t * pchr );
static chr_t * chr_config_deinit( chr_t * pchr );
static chr_t * chr_config_active( chr_t * pchr );
static chr_t * chr_config_dtor( chr_t * pchr );

static int get_grip_verts( Uint16 grip_verts[], const CHR_REF by_reference imount, int vrt_offset );

bool_t apply_one_character_matrix( chr_t * pchr, matrix_cache_t * mcache );
bool_t apply_one_weapon_matrix( chr_t * pweap, matrix_cache_t * mcache );

int convert_grip_to_local_points( chr_t * pholder, Uint16 grip_verts[], fvec4_t   dst_point[] );
int convert_grip_to_global_points( const CHR_REF by_reference iholder, Uint16 grip_verts[], fvec4_t   dst_point[] );

// definition that is consistent with using it as a callback in qsort() or some similar function
static int  cmp_matrix_cache( const void * vlhs, const void * vrhs );

static bool_t chr_upload_cap( chr_t * pchr, cap_t * pcap );

void cleanup_one_character( chr_t * pchr );

static bool_t chr_instance_update_ref( chr_instance_t * pinst, float floor_level, bool_t need_matrix );

static void chr_log_script_time( const CHR_REF by_reference ichr );

static bool_t update_chr_darkvision( const CHR_REF by_reference character );

static fvec2_t chr_get_diff( chr_t * pchr, float test_pos[], float center_pressure );
float          chr_get_mesh_pressure( chr_t * pchr, float test_pos[] );

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
void character_system_begin()
{
	ChrList_init();
	init_all_cap();
}

//--------------------------------------------------------------------------------------------
void character_system_end()
{
	release_all_cap();
	ChrList_dtor();
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
bool_t chr_free( chr_t * pchr )
{
	/// Free all allocated memory

	if ( !ALLOCATED_PCHR( pchr ) )
	{
		EGOBOO_ASSERT( NULL == pchr->inst.vrt_lst );
		return bfalse;
	}

	// do some list clean-up
	disenchant_character( GET_REF_PCHR( pchr ) );

	// deallocate
	BillboardList_free_one( REF_TO_INT( pchr->ibillboard ) );

	LoopedList_remove( pchr->loopedsound_channel );

	chr_instance_dtor( &( pchr->inst ) );
	BSP_leaf_dtor( &( pchr->bsp_leaf ) );
	ai_state_dtor( &( pchr->ai ) );

	EGOBOO_ASSERT( NULL == pchr->inst.vrt_lst );

	return btrue;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_ctor( chr_t * pchr )
{
	/// @details BB@> initialize the character data to safe values
	///     since we use memset(..., 0, ...), everything == 0 == false == 0.0f
	///     statements are redundant

	int cnt;
	ego_object_base_t save_base;
	ego_object_base_t * pbase;

	if( NULL == pchr ) return pchr;

	// grab the base object
	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase ) return NULL;

	//---- construct the character object

	// save the base object data
	memcpy( &save_base, pbase, sizeof( ego_object_base_t ) );

	if ( ALLOCATED_PCHR( pchr ) )
	{
		// deallocate any existing data
		chr_free( pchr );

		EGOBOO_ASSERT( NULL == pchr->inst.vrt_lst );
	}

	// clear out all data
	memset( pchr, 0, sizeof( *pchr ) );

	// restore the base object data
	memcpy( pbase, &save_base, sizeof( ego_object_base_t ) );

	// IMPORTANT!!!
	pchr->ibillboard = INVALID_BILLBOARD;
	pchr->sparkle = NOSPARKLE;
	pchr->loopedsound_channel = INVALID_SOUND_CHANNEL;

	// Set up model stuff
	pchr->inwhich_slot = SLOT_LEFT;
	pchr->hitready = btrue;
	pchr->boretime = BORETIME;
	pchr->carefultime = CAREFULTIME;

	// Enchant stuff
	pchr->firstenchant = MAX_ENC;
	pchr->undoenchant = MAX_ENC;
	pchr->missiletreatment = MISSILE_NORMAL;

	// Character stuff
	pchr->turnmode = TURNMODE_VELOCITY;
	pchr->alive = btrue;

	// Jumping
	pchr->jumptime = JUMPDELAY;

	// Grip info
	pchr->attachedto = ( CHR_REF )MAX_CHR;
	for ( cnt = 0; cnt < SLOT_COUNT; cnt++ )
	{
		pchr->holdingwhich[cnt] = ( CHR_REF )MAX_CHR;
	}

	// pack/inventory info
	pchr->pack.next = ( CHR_REF )MAX_CHR;
	for ( cnt = 0; cnt < INVEN_COUNT; cnt++ )
	{
		pchr->inventory[cnt] = ( CHR_REF )MAX_CHR;
	}

	// Set up position
	pchr->ori.map_facing_y = MAP_TURN_OFFSET;  // These two mean on level surface
	pchr->ori.map_facing_x = MAP_TURN_OFFSET;

	// start the character out in the "dance" animation
	chr_start_anim( pchr, ACTION_DA, btrue, btrue );

	// I think we have to set the dismount timer, otherwise objects that
	// are spawned by chests will behave strangely...
	// nope this did not fix it
	// ZF@> If this is != 0 then scorpion claws and riders are dropped at spawn (non-item objects)
	pchr->dismount_timer  = 0;
	pchr->dismount_object = ( CHR_REF )MAX_CHR;

	// set all of the integer references to invalid values
	pchr->firstenchant = MAX_ENC;
	pchr->undoenchant  = MAX_ENC;
	for ( cnt = 0; cnt < SLOT_COUNT; cnt++ )
	{
		pchr->holdingwhich[cnt] = ( CHR_REF )MAX_CHR;
	}

	pchr->pack.next = ( CHR_REF )MAX_CHR;
	for ( cnt = 0; cnt < INVEN_COUNT; cnt++ )
	{
		pchr->inventory[cnt] = ( CHR_REF )MAX_CHR;
	}

	pchr->onwhichplatform = ( CHR_REF )MAX_CHR;
	pchr->attachedto      = ( CHR_REF )MAX_CHR;

	// initialize the bsp node for this character
	pchr->bsp_leaf.data      = pchr;
	pchr->bsp_leaf.data_type = 1;
	pchr->bsp_leaf.index     = GET_INDEX_PCHR( pchr );

	//---- call the constructors of the "has a" classes

	// set the insance values to safe values
	chr_instance_ctor( &( pchr->inst ) );

	// intialize the ai_state
	ai_state_ctor( &( pchr->ai ) );

	// initialize the bsp node for this character
	BSP_leaf_ctor( &( pchr->bsp_leaf ), 3, pchr, 1 );
	pchr->bsp_leaf.index = GET_INDEX_PCHR( pchr );

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_dtor( chr_t * pchr )
{
	if( NULL == pchr ) return pchr;

	// destruct/free any allocated data
	chr_free( pchr );

	// Destroy the base object.
	// Sets the state to ego_object_terminated automatically.
	POBJ_TERMINATE( pchr );

	return pchr;
}

//--------------------------------------------------------------------------------------------
#if defined(__cplusplus)
s_chr::s_chr() { chr_ctor(this); };
s_chr::~s_chr() { chr_dtor( this ); };
#endif

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
int chr_count_free()
{
	return ChrList.free_count;
}

//--------------------------------------------------------------------------------------------
void flash_character_height( const CHR_REF by_reference character, Uint8 valuelow, Sint16 low,
							Uint8 valuehigh, Sint16 high )
{
	/// @details ZZ@> This function sets a character's lighting depending on vertex height...
	///    Can make feet dark and head light...

	Uint32 cnt;
	Sint16 z;

	mad_t * pmad;
	chr_instance_t * pinst;

	pinst = chr_get_pinstance( character );
	if ( NULL == pinst ) return;

	pmad = chr_get_pmad( character );
	if ( NULL == pmad ) return;

	for ( cnt = 0; cnt < pinst->vrt_count; cnt++ )
	{
		z = pinst->vrt_lst[cnt].pos[ZZ];

		if ( z < low )
		{
			pinst->color_amb = valuelow;
		}
		else
		{
			if ( z > high )
			{
				pinst->color_amb = valuehigh;
			}
			else
			{
				pinst->color_amb = ( valuehigh * ( z - low ) / ( high - low ) ) +
					( valuelow * ( high - z ) / ( high - low ) );
			}
		}
	}
}

//--------------------------------------------------------------------------------------------
void keep_weapons_with_holders()
{
	/// @details ZZ@> This function keeps weapons near their holders

	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		CHR_REF iattached = pchr->attachedto;

		if ( INGAME_CHR( iattached ) )
		{
			chr_t * pattached = ChrList.lst + iattached;

			// Keep in hand weapons with iattached
			if ( chr_matrix_valid( pchr ) )
			{
				chr_set_pos( pchr, mat_getTranslate_v( pchr->inst.matrix ) );
			}
			else
			{
				chr_set_pos( pchr, chr_get_pos_v(pattached) );
			}

			pchr->ori.facing_z = pattached->ori.facing_z;

			// Copy this stuff ONLY if it's a weapon, not for mounts
			if ( pattached->transferblend && pchr->isitem )
			{

				// Items become partially invisible in hands of players
				if ( pattached->isplayer && 255 != pattached->inst.alpha )
				{
					chr_set_alpha( pchr, SEEINVISIBLE );
				}
				else
				{
					// Only if not naturally transparent
					if ( 255 == pchr->alpha_base )
					{
						chr_set_alpha( pchr, pattached->inst.alpha );
					}
					else
					{
						chr_set_alpha( pchr, pchr->alpha_base );
					}
				}

				// Do light too
				if ( pattached->isplayer && 255 != pattached->inst.light )
				{
					chr_set_light( pchr, SEEINVISIBLE );
				}
				else
				{
					// Only if not naturally transparent
					if ( 255 == chr_get_pcap( cnt )->light )
					{
						chr_set_light( pchr, pattached->inst.light );
					}
					else
					{
						chr_set_light( pchr, pchr->light_base );
					}
				}
			}
		}
		else
		{
			pchr->attachedto = ( CHR_REF )MAX_CHR;

			// Keep inventory with iattached
			if ( !pchr->pack.is_packed )
			{
				PACK_BEGIN_LOOP( iattached, pchr->pack.next )
				{
					chr_set_pos( ChrList.lst + iattached, chr_get_pos_v( pchr ) );

					// Copy olds to make SendMessageNear work
					ChrList.lst[iattached].pos_old = pchr->pos_old;
				}
				PACK_END_LOOP( iattached );
			}
		}
	}
	CHR_END_LOOP();
}

//--------------------------------------------------------------------------------------------
void make_one_character_matrix( const CHR_REF by_reference ichr )
{
	/// @details ZZ@> This function sets one character's matrix

	chr_t * pchr;
	chr_instance_t * pinst;

	if ( !INGAME_CHR( ichr ) ) return;
	pchr = ChrList.lst + ichr;
	pinst = &( pchr->inst );

	// invalidate this matrix
	pinst->matrix_cache.matrix_valid = bfalse;

	if ( pchr->is_overlay )
	{
		// This character is an overlay and its ai.target points to the object it is overlaying
		// Overlays are kept with their target...
		if ( INGAME_CHR( pchr->ai.target ) )
		{
			chr_t * ptarget = ChrList.lst + pchr->ai.target;

			chr_set_pos( pchr, chr_get_pos_v(ptarget) );

			// copy the matrix
			CopyMatrix( &( pinst->matrix ), &( ptarget->inst.matrix ) );

			// copy the matrix data
			pinst->matrix_cache = ptarget->inst.matrix_cache;
		}
	}
	else
	{
		pinst->matrix = ScaleXYZRotateXYZTranslate( pchr->fat, pchr->fat, pchr->fat,
			TO_TURN( pchr->ori.facing_z ),
			TO_TURN( pchr->ori.map_facing_x - MAP_TURN_OFFSET ),
			TO_TURN( pchr->ori.map_facing_y - MAP_TURN_OFFSET ),
			pchr->pos.x, pchr->pos.y, pchr->pos.z );

		pinst->matrix_cache.valid        = btrue;
		pinst->matrix_cache.matrix_valid = btrue;
		pinst->matrix_cache.type_bits    = MAT_CHARACTER;

		pinst->matrix_cache.self_scale.x = pchr->fat;
		pinst->matrix_cache.self_scale.y = pchr->fat;
		pinst->matrix_cache.self_scale.z = pchr->fat;

		pinst->matrix_cache.rotate.x = CLIP_TO_16BITS( pchr->ori.map_facing_x - MAP_TURN_OFFSET );
		pinst->matrix_cache.rotate.y = CLIP_TO_16BITS( pchr->ori.map_facing_y - MAP_TURN_OFFSET );
		pinst->matrix_cache.rotate.z = pchr->ori.facing_z;

		pinst->matrix_cache.pos = chr_get_pos( pchr );
	}
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
void chr_log_script_time( const CHR_REF by_reference ichr )
{
	// log the amount of script time that this object used up

	chr_t * pchr;
	cap_t * pcap;
	FILE * ftmp;

	if ( !DEFINED_CHR( ichr ) ) return;
	pchr = ChrList.lst + ichr;

	if ( pchr->ai._clkcount <= 0 ) return;

	pcap = chr_get_pcap( ichr );
	if ( NULL == pcap ) return;

	ftmp = fopen( vfs_resolveWriteFilename( "/debug/script_timing.txt" ), "a+" );
	if ( NULL != ftmp )
	{
		fprintf( ftmp, "update == %d\tindex == %d\tname == \"%s\"\tclassname == \"%s\"\ttotal_time == %e\ttotal_calls == %f\n",
			update_wld, REF_TO_INT( ichr ), pchr->Name, pcap->classname,
			pchr->ai._clktime, pchr->ai._clkcount );
		fflush( ftmp );
		fclose( ftmp );
	}
}

//--------------------------------------------------------------------------------------------
void free_one_character_in_game( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function sticks a character back on the free character stack
	///
	/// @note This should only be called by cleanup_all_characters() or free_inventory_in_game()

	int     cnt;
	cap_t * pcap;
	chr_t * pchr;

	if ( !DEFINED_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	pcap = pro_get_pcap( pchr->iprofile );
	if ( NULL == pcap ) return;

	// Remove from stat list
	if ( pchr->StatusList_on )
	{
		bool_t stat_found;

		pchr->StatusList_on = bfalse;

		stat_found = bfalse;
		for ( cnt = 0; cnt < StatusList_count; cnt++ )
		{
			if ( StatusList[cnt] == character )
			{
				stat_found = btrue;
				break;
			}
		}

		if ( stat_found )
		{
			for ( cnt++; cnt < StatusList_count; cnt++ )
			{
				SWAP( CHR_REF, StatusList[cnt-1], StatusList[cnt] );
			}
			StatusList_count--;
		}
	}

	// Make sure everyone knows it died
	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		ai_state_t * pai;

		if ( !INGAME_CHR( cnt ) || cnt == character ) continue;
		pai = chr_get_pai( cnt );

		if ( pai->target == character )
		{
			pai->alert |= ALERTIF_TARGETKILLED;
			pai->target = cnt;
		}

		if ( chr_get_pteam( cnt )->leader == character )
		{
			pai->alert |= ALERTIF_LEADERKILLED;
		}
	}
	CHR_END_LOOP();

	// Handle the team
	if ( pchr->alive && !pcap->invictus && TeamStack.lst[pchr->baseteam].morale > 0 )
	{
		TeamStack.lst[pchr->baseteam].morale--;
	}

	if ( TeamStack.lst[pchr->team].leader == character )
	{
		TeamStack.lst[pchr->team].leader = NOLEADER;
	}

	// remove any attached particles
	disaffirm_attached_particles( character );

	// actually get rid of the character
	ChrList_free_one( character );
}

//--------------------------------------------------------------------------------------------
void free_inventory_in_game( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function frees every item in the character's inventory
	///
	/// @note this should only be called by cleanup_all_characters()

	CHR_REF cnt;

	if ( !DEFINED_CHR( character ) ) return;

	PACK_BEGIN_LOOP( cnt, ChrList.lst[character].pack.next )
	{
		free_one_character_in_game( cnt );
	}
	PACK_END_LOOP( cnt );

	// set the inventory to the "empty" state
	ChrList.lst[character].pack.count = 0;
	ChrList.lst[character].pack.next  = ( CHR_REF )MAX_CHR;
}

//--------------------------------------------------------------------------------------------
prt_t * place_particle_at_vertex( prt_t * pprt, const CHR_REF by_reference character, int vertex_offset )
{
	/// @details ZZ@> This function sets one particle's position to be attached to a character.
	///    It will kill the particle if the character is no longer around

	int     vertex;
	fvec4_t point[1], nupoint[1];

	chr_t * pchr;

	if ( !DEFINED_PPRT( pprt ) ) return pprt;

	if ( !INGAME_CHR( character ) )
	{
		goto place_particle_at_vertex_fail;
	}
	pchr = ChrList.lst + character;

	// Check validity of attachment
	if ( pchr->pack.is_packed )
	{
		goto place_particle_at_vertex_fail;
	}

	// Do we have a matrix???
	if ( !chr_matrix_valid( pchr ) )
	{
		chr_update_matrix( pchr, btrue );
	}

	// Do we have a matrix???
	if ( chr_matrix_valid( pchr ) )
	{
		// Transform the weapon vertex_offset from model to world space
		mad_t * pmad = chr_get_pmad( character );

		if ( vertex_offset == GRIP_ORIGIN )
		{
			fvec3_t tmp_pos = VECT3( pchr->inst.matrix.CNV( 3, 0 ), pchr->inst.matrix.CNV( 3, 1 ), pchr->inst.matrix.CNV( 3, 2 ) );
			prt_set_pos( pprt, tmp_pos.v );

			return pprt;
		}

		vertex = 0;
		if ( NULL != pmad )
		{
			vertex = (( int )pchr->inst.vrt_count ) - vertex_offset;

			// do the automatic update
			chr_instance_update_vertices( &( pchr->inst ), vertex, vertex, bfalse );

			// Calculate vertex_offset point locations with linear interpolation and other silly things
			point[0].x = pchr->inst.vrt_lst[vertex].pos[XX];
			point[0].y = pchr->inst.vrt_lst[vertex].pos[YY];
			point[0].z = pchr->inst.vrt_lst[vertex].pos[ZZ];
			point[0].w = 1.0f;
		}
		else
		{
			point[0].x =
				point[0].y =
				point[0].z = 0.0f;
			point[0].w = 1.0f;
		}

		// Do the transform
		TransformVertices( &( pchr->inst.matrix ), point, nupoint, 1 );

		prt_set_pos( pprt, nupoint[0].v );
	}
	else
	{
		// No matrix, so just wing it...
		prt_set_pos( pprt, chr_get_pos_v( pchr ) );
	}

	return pprt;

place_particle_at_vertex_fail:

	prt_request_terminate( GET_REF_PPRT( pprt ) );

	return NULL;
}

//--------------------------------------------------------------------------------------------
void make_all_character_matrices( bool_t do_physics )
{
	/// @details ZZ@> This function makes all of the character's matrices

	//int cnt;
	//bool_t done;

	//// blank the accumulators
	//for ( ichr = 0; ichr < MAX_CHR; ichr++ )
	//{
	//    ChrList.lst[ichr].phys.apos_0.x = 0.0f;
	//    ChrList.lst[ichr].phys.apos_0.y = 0.0f;
	//    ChrList.lst[ichr].phys.apos_0.z = 0.0f;

	//    ChrList.lst[ichr].phys.apos_1.x = 0.0f;
	//    ChrList.lst[ichr].phys.apos_1.y = 0.0f;
	//    ChrList.lst[ichr].phys.apos_1.z = 0.0f;

	//    ChrList.lst[ichr].phys.avel.x = 0.0f;
	//    ChrList.lst[ichr].phys.avel.y = 0.0f;
	//    ChrList.lst[ichr].phys.avel.z = 0.0f;
	//}

	// just call chr_update_matrix on every character
	CHR_BEGIN_LOOP_ACTIVE( ichr, pchr )
	{
		chr_update_matrix( pchr, btrue );
	}
	CHR_END_LOOP();

	////if ( do_physics )
	////{
	////    // accumulate the accumulators
	////    for ( ichr = 0; ichr < MAX_CHR; ichr++ )
	////    {
	////        float nrm[2];
	////        float tmpx, tmpy, tmpz;
	////        fvec3_t tmp_pos;
	////        chr_t * pchr;

	////        if ( !INGAME_CHR(ichr) ) continue;
	////        pchr = ChrList.lst + ichr;

	////        tmp_pos = chr_get_pos( pchr );

	////        // do the "integration" of the accumulated accelerations
	////        pchr->vel.x += pchr->phys.avel.x;
	////        pchr->vel.y += pchr->phys.avel.y;
	////        pchr->vel.z += pchr->phys.avel.z;

	////        // do the "integration" on the position
	////        if ( ABS(pchr->phys.apos_1.x) > 0 )
	////        {
	////            tmpx = tmp_pos.x;
	////            tmp_pos.x += pchr->phys.apos_1.x;
	////            if ( chr_hit_wall(ichr, nrm, NULL) )
	////            {
	////                // restore the old values
	////                tmp_pos.x = tmpx;
	////            }
	////            else
	////            {
	////                // pchr->vel.x += pchr->phys.apos_1.x;
	////                pchr->safe_pos.x = tmpx;
	////            }
	////        }

	////        if ( ABS(pchr->phys.apos_1.y) > 0 )
	////        {
	////            tmpy = tmp_pos.y;
	////            tmp_pos.y += pchr->phys.apos_1.y;
	////            if ( chr_hit_wall(ichr, nrm, NULL) )
	////            {
	////                // restore the old values
	////                tmp_pos.y = tmpy;
	////            }
	////            else
	////            {
	////                // pchr->vel.y += pchr->phys.apos_1.y;
	////                pchr->safe_pos.y = tmpy;
	////            }
	////        }

	////        if ( ABS(pchr->phys.apos_1.z) > 0 )
	////        {
	////            tmpz = tmp_pos.z;
	////            tmp_pos.z += pchr->phys.apos_1.z;
	////            if ( tmp_pos.z < pchr->enviro.level )
	////            {
	////                // restore the old values
	////                tmp_pos.z = tmpz;
	////            }
	////            else
	////            {
	////                // pchr->vel.z += pchr->phys.apos_1.z;
	////                pchr->safe_pos.z = tmpz;
	////            }
	////        }

	////        if ( 0 == chr_hit_wall(ichr, nrm, NULL) )
	////        {
	////            pchr->safe_valid = btrue;
	////        }
	////        chr_set_pos( pchr, tmp_pos.v );
	////    }

	////    // fix the matrix positions
	////    for ( ichr = 0; ichr < MAX_CHR; ichr++ )
	////    {
	////        chr_t * pchr;

	////        if ( !INGAME_CHR(ichr) ) continue;
	////        pchr = ChrList.lst + ichr;

	////        if( !pchr->inst.matrix_cache.valid ) continue;

	////        pchr->inst.matrix.CNV( 3, 0 ) = pchr->pos.x;
	////        pchr->inst.matrix.CNV( 3, 1 ) = pchr->pos.y;
	////        pchr->inst.matrix.CNV( 3, 2 ) = pchr->pos.z;
	////    }
	////}
}

//--------------------------------------------------------------------------------------------
void free_all_chraracters()
{
	/// @details ZZ@> This function resets the character allocation list

	// free all the characters
	ChrList_free_all();

	// free_all_players
	PlaStack.count = 0;
	local_numlpla = 0;
	local_noplayers = btrue;

	// free_all_stats
	StatusList_count = 0;
}

//--------------------------------------------------------------------------------------------
float chr_get_mesh_pressure( chr_t * pchr, float test_pos[] )
{
	float retval = 0.0f;
	float radius = 0.0f;

	if ( !DEFINED_PCHR( pchr ) ) return retval;

	if ( 0 == pchr->bump.size || INFINITE_WEIGHT == pchr->phys.weight ) return retval;

	// deal with the optional parameters
	if ( NULL == test_pos ) test_pos = pchr->pos.v;

	// calculate the radius based on whether the character is on camera
	radius = 0.0f;
	if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) )
	{
		if ( PMesh->tmem.tile_list[ pchr->onwhichgrid ].inrenderlist )
		{
			radius = pchr->bump.size;
		}
	}

	mesh_mpdfx_tests = 0;
	mesh_bound_tests = 0;
	mesh_pressure_tests = 0;
	{
		retval = mesh_get_pressure( PMesh, test_pos, radius, pchr->stoppedby );
	}
	chr_stoppedby_tests += mesh_mpdfx_tests;
	chr_pressure_tests += mesh_pressure_tests;

	return retval;
}

//--------------------------------------------------------------------------------------------
fvec2_t chr_get_diff( chr_t * pchr, float test_pos[], float center_pressure )
{
	fvec2_t retval = ZERO_VECT2;
	float   radius;

	if ( !DEFINED_PCHR( pchr ) ) return retval;

	if ( 0 == pchr->bump.size || INFINITE_WEIGHT == pchr->phys.weight ) return retval;

	// deal with the optional parameters
	if ( NULL == test_pos ) test_pos = pchr->pos.v;

	// calculate the radius based on whether the character is on camera
	radius = 0.0f;
	if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) )
	{
		if ( PMesh->tmem.tile_list[ pchr->onwhichgrid ].inrenderlist )
		{
			radius = pchr->bump.size;
		}
	}

	mesh_mpdfx_tests = 0;
	mesh_bound_tests = 0;
	mesh_pressure_tests = 0;
	{
		retval = mesh_get_diff( PMesh, test_pos, radius, center_pressure, pchr->stoppedby );
	}
	chr_stoppedby_tests += mesh_mpdfx_tests;
	chr_pressure_tests += mesh_pressure_tests;

	return retval;
}

//--------------------------------------------------------------------------------------------
Uint32 chr_hit_wall( chr_t * pchr, float test_pos[], float nrm[], float * pressure )
{
	/// @details ZZ@> This function returns nonzero if the character hit a wall that the
	///    character is not allowed to cross

	Uint32       retval;
	float        radius;

	if ( !DEFINED_PCHR( pchr ) ) return 0;

	if ( 0 == pchr->bump.size || INFINITE_WEIGHT == pchr->phys.weight ) return 0;

	// deal with the optional parameters
	if ( NULL == test_pos ) test_pos = pchr->pos.v;

	// calculate the radius based on whether the character is on camera
	radius = 0.0f;
	if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) )
	{
		if ( PMesh->tmem.tile_list[ pchr->onwhichgrid ].inrenderlist )
		{
			radius = pchr->bump.size;
		}
	}

	mesh_mpdfx_tests = 0;
	mesh_bound_tests = 0;
	mesh_pressure_tests = 0;
	{
		retval = mesh_hit_wall( PMesh, test_pos, radius, pchr->stoppedby, nrm, pressure );
	}
	chr_stoppedby_tests += mesh_mpdfx_tests;
	chr_pressure_tests  += mesh_pressure_tests;

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_test_wall( chr_t * pchr, float test_pos[] )
{
	/// @details ZZ@> This function returns nonzero if the character hit a wall that the
	///    character is not allowed to cross

	bool_t retval;
	float  radius;

	if ( !ACTIVE_PCHR( pchr ) ) return 0;

	if ( 0 == pchr->bump.size || INFINITE_WEIGHT == pchr->phys.weight ) return bfalse;

	radius = 0.0f;
	if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) )
	{
		if ( PMesh->tmem.tile_list[ pchr->onwhichgrid ].inrenderlist )
		{
			radius = pchr->bump_1.size;
		}
	}

	if( NULL == test_pos ) test_pos = pchr->pos.v;

	// do the wall test
	mesh_mpdfx_tests = 0;
	mesh_bound_tests = 0;
	mesh_pressure_tests = 0;
	{
		retval = mesh_test_wall( PMesh, test_pos, radius, pchr->stoppedby, NULL );
	}
	chr_stoppedby_tests += mesh_mpdfx_tests;
	chr_pressure_tests += mesh_pressure_tests;

	return retval;
}

//--------------------------------------------------------------------------------------------
void reset_character_accel( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function fixes a character's max acceleration

	ENC_REF enchant;
	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	// cleanup the enchant list
	cleanup_character_enchants( pchr );

	// Okay, remove all acceleration enchants
	enchant = pchr->firstenchant;
	while ( enchant != MAX_ENC )
	{
		enchant_remove_add( enchant, ADDACCEL );
		enchant = EncList.lst[enchant].nextenchant_ref;
	}

	// Set the starting value
	pchr->maxaccel = 0;
	pcap = chr_get_pcap( character );
	if ( NULL != pcap )
	{
		pchr->maxaccel = pcap->maxaccel[pchr->skin];
	}

	// cleanup the enchant list
	cleanup_character_enchants( pchr );

	// Put the acceleration enchants back on
	enchant = pchr->firstenchant;
	while ( enchant != MAX_ENC )
	{
		enchant_apply_add( enchant, ADDACCEL, enc_get_ieve( enchant ) );
		enchant = EncList.lst[enchant].nextenchant_ref;
	}
}

//--------------------------------------------------------------------------------------------
bool_t detach_character_from_mount( const CHR_REF by_reference character, Uint8 ignorekurse, Uint8 doshop )
{
	/// @details ZZ@> This function drops an item

	CHR_REF mount;
	Uint16  hand;
	ENC_REF enchant;
	bool_t  inshop;
	chr_t * pchr, * pmount;

	// Make sure the character is valid
	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	// Make sure the character is mounted
	mount = ChrList.lst[character].attachedto;
	if ( !INGAME_CHR( mount ) ) return bfalse;
	pmount = ChrList.lst + mount;

	// Don't allow living characters to drop kursed weapons
	if ( !ignorekurse && pchr->iskursed && pmount->alive && pchr->isitem )
	{
		pchr->ai.alert |= ALERTIF_NOTDROPPED;
		return bfalse;
	}

	// set the dismount timer
	pchr->dismount_timer  = PHYS_DISMOUNT_TIME;
	pchr->dismount_object = mount;

	// Figure out which hand it's in
	hand = pchr->inwhich_slot;

	// Rip 'em apart
	pchr->attachedto = ( CHR_REF )MAX_CHR;
	if ( pmount->holdingwhich[SLOT_LEFT] == character )
		pmount->holdingwhich[SLOT_LEFT] = ( CHR_REF )MAX_CHR;

	if ( pmount->holdingwhich[SLOT_RIGHT] == character )
		pmount->holdingwhich[SLOT_RIGHT] = ( CHR_REF )MAX_CHR;

	if ( pchr->alive )
	{
		// play the falling animation...
		chr_play_action( pchr, ACTION_JB + hand, bfalse );
	}
	else if ( pchr->inst.action_which < ACTION_KA || pchr->inst.action_which > ACTION_KD )
	{
		// play the "killed" animation...
		chr_play_action( pchr, ACTION_KA + generate_randmask( 0, 3 ), bfalse );
		pchr->inst.action_keep = btrue;
	}

	// Set the positions
	if ( chr_matrix_valid( pchr ) )
	{
		chr_set_pos( pchr, mat_getTranslate_v( pchr->inst.matrix ) );
	}
	else
	{
		chr_set_pos( pchr, chr_get_pos_v(pmount) );
	}

	// Make sure it's not dropped in a wall...
	if ( chr_test_wall( pchr, NULL ) )
	{
		fvec3_t pos_tmp;

		pos_tmp.x = pmount->pos.x;
		pos_tmp.y = pmount->pos.y;
		pos_tmp.z = pchr->pos.z;

		chr_set_pos( pchr, pos_tmp.v );

		chr_update_breadcrumb( pchr, btrue );
	}

	// Check for shop passages
	inshop = bfalse;
	if ( doshop )
	{
		inshop = do_shop_drop( mount, character );
	}

	// Make sure it works right
	pchr->hitready = btrue;
	if ( inshop )
	{
		// Drop straight down to avoid theft
		pchr->vel.x = 0;
		pchr->vel.y = 0;
	}
	else
	{
		pchr->vel.x = pmount->vel.x;
		pchr->vel.y = pmount->vel.y;
	}

	pchr->vel.z = DROPZVEL;

	// Turn looping off
	pchr->inst.action_loop = bfalse;

	// Reset the team if it is a mount
	if ( pmount->ismount )
	{
		pmount->team = pmount->baseteam;
		pmount->ai.alert |= ALERTIF_DROPPED;
	}

	pchr->team = pchr->baseteam;
	pchr->ai.alert |= ALERTIF_DROPPED;

	// Reset transparency
	if ( pchr->isitem && pmount->transferblend )
	{
		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		// Okay, reset transparency
		enchant = pchr->firstenchant;
		while ( enchant != MAX_ENC )
		{
			enchant_remove_set( enchant, SETALPHABLEND );
			enchant_remove_set( enchant, SETLIGHTBLEND );

			enchant = EncList.lst[enchant].nextenchant_ref;
		}

		chr_set_alpha( pchr, pchr->alpha_base );
		chr_set_light( pchr, pchr->light_base );

		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		// apply the blend enchants
		enchant = pchr->firstenchant;
		while ( enchant != MAX_ENC )
		{
			PRO_REF ipro = enc_get_ipro( enchant );

			if ( LOADED_PRO( ipro ) )
			{
				enchant_apply_set( enchant, SETALPHABLEND, ipro );
				enchant_apply_set( enchant, SETLIGHTBLEND, ipro );
			}

			enchant = EncList.lst[enchant].nextenchant_ref;
		}
	}

	// Set twist
	pchr->ori.map_facing_y = MAP_TURN_OFFSET;
	pchr->ori.map_facing_x = MAP_TURN_OFFSET;

	chr_update_matrix( pchr, btrue );

	return btrue;
}

//--------------------------------------------------------------------------------------------
void reset_character_alpha( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function fixes an item's transparency

	CHR_REF mount;
	ENC_REF enchant;
	chr_t * pchr, * pmount;

	// Make sure the character is valid
	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	// Make sure the character is mounted
	mount = ChrList.lst[character].attachedto;
	if ( !INGAME_CHR( mount ) ) return;
	pmount = ChrList.lst + mount;

	if ( pchr->isitem && pmount->transferblend )
	{
		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		// Okay, reset transparency
		enchant = pchr->firstenchant;
		while ( enchant != MAX_ENC )
		{
			enchant_remove_set( enchant, SETALPHABLEND );
			enchant_remove_set( enchant, SETLIGHTBLEND );

			enchant = EncList.lst[enchant].nextenchant_ref;
		}

		chr_set_alpha( pchr, pchr->alpha_base );
		chr_set_light( pchr, pchr->light_base );

		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		enchant = pchr->firstenchant;
		while ( enchant != MAX_ENC )
		{
			PRO_REF ipro = enc_get_ipro( enchant );

			if ( LOADED_PRO( ipro ) )
			{
				enchant_apply_set( enchant, SETALPHABLEND, ipro );
				enchant_apply_set( enchant, SETLIGHTBLEND, ipro );
			}

			enchant = EncList.lst[enchant].nextenchant_ref;
		}
	}
}

//--------------------------------------------------------------------------------------------
void attach_character_to_mount( const CHR_REF by_reference iitem, const CHR_REF by_reference iholder, grip_offset_t grip_off )
{
	/// @details ZZ@> This function attaches one character/item to another ( the holder/mount )
	///    at a certain vertex offset ( grip_off )

	slot_t slot;

	chr_t * pitem, * pholder;

	// Make sure the character/item is valid
	// this could be called before the item is fully instantiated
	if ( !DEFINED_CHR( iitem ) || ChrList.lst[iitem].pack.is_packed ) return;
	pitem = ChrList.lst + iitem;

	// make a reasonable time for the character to remount something
	// for characters jumping out of pots, etc
	if ( !pitem->isitem && pitem->dismount_timer > 0 )
		return;

	// Make sure the holder/mount is valid
	if ( !INGAME_CHR( iholder ) || ChrList.lst[iholder].pack.is_packed ) return;
	pholder = ChrList.lst + iholder;

#if !defined(ENABLE_BODY_GRAB)
	if ( !pitem->alive ) return;
#endif

	// Figure out which slot this grip_off relates to
	slot = grip_offset_to_slot( grip_off );

	// Make sure the the slot is valid
	if ( !chr_get_pcap( iholder )->slotvalid[slot] ) return;

	// This is a small fix that allows special grabbable mounts not to be mountable while
	// held by another character (such as the magic carpet for example)
	if ( !pitem->isitem && pholder->ismount && INGAME_CHR( pholder->attachedto ) ) return;

	// Put 'em together
	pitem->inwhich_slot   = slot;
	pitem->attachedto     = iholder;
	pholder->holdingwhich[slot] = iitem;

	// set the grip vertices for the iitem
	set_weapongrip( iitem, iholder, grip_off );

	chr_update_matrix( pitem, btrue );

	chr_set_pos( pitem, mat_getTranslate_v( pitem->inst.matrix ) );

	pitem->enviro.inwater  = bfalse;
	pitem->jumptime = JUMPDELAY * 4;

	// Run the held animation
	if ( pholder->ismount && grip_off == GRIP_ONLY )
	{
		// Riding iholder
		chr_play_action( pitem, ACTION_MI, btrue );
		pitem->inst.action_loop = btrue;
	}
	else if ( pitem->alive )
	{
		chr_play_action( pitem, ACTION_MM + slot, bfalse );
		if ( pitem->isitem )
		{
			// Item grab
			pitem->inst.action_keep = btrue;
		}
	}

	// Set the team
	if ( pitem->isitem )
	{
		pitem->team = pholder->team;

		// Set the alert
		if ( pitem->alive )
		{
			pitem->ai.alert |= ALERTIF_GRABBED;
		}
	}

	if ( pholder->ismount )
	{
		pholder->team = pitem->team;

		// Set the alert
		if ( !pholder->isitem && pholder->alive )
		{
			pholder->ai.alert |= ALERTIF_GRABBED;
		}
	}

	// It's not gonna hit the floor
	pitem->hitready = bfalse;
}

//--------------------------------------------------------------------------------------------
bool_t inventory_add_item( const CHR_REF by_reference item, const CHR_REF by_reference character )
{
	chr_t * pchr, * pitem;
	cap_t * pitem_cap;
	bool_t  slot_found, pack_added;
	int     slot_count;
	int     cnt;

	if ( !INGAME_CHR( item ) ) return bfalse;
	pitem = ChrList.lst + item;

	// don't allow sub-inventories
	if ( pitem->pack.is_packed || pitem->isequipped ) return bfalse;

	pitem_cap = pro_get_pcap( pitem->iprofile );
	if ( NULL == pitem_cap ) return bfalse;

	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	// don't allow sub-inventories
	if ( pchr->pack.is_packed || pchr->isequipped ) return bfalse;

	slot_found = bfalse;
	slot_count = 0;
	for ( cnt = 0; cnt < INVEN_COUNT; cnt++ )
	{
		if ( IDSZ_NONE == inventory_idsz[cnt] ) continue;

		if ( inventory_idsz[cnt] == pitem_cap->idsz[IDSZ_PARENT] )
		{
			slot_count = cnt;
			slot_found = btrue;
		}
	}

	if ( slot_found )
	{
		if ( INGAME_CHR( pchr->holdingwhich[slot_count] ) )
		{
			pchr->inventory[slot_count] = ( CHR_REF )MAX_CHR;
		}
	}

	pack_added = chr_add_pack_item( item, character );

	if ( slot_found && pack_added )
	{
		pchr->inventory[slot_count] = item;
	}

	return pack_added;
}

//--------------------------------------------------------------------------------------------
CHR_REF inventory_get_item( const CHR_REF by_reference ichr, grip_offset_t grip_off, bool_t ignorekurse )
{
	chr_t * pchr;
	CHR_REF iitem;
	int     cnt;

	if ( !INGAME_CHR( ichr ) ) return ( CHR_REF )MAX_CHR;
	pchr = ChrList.lst + ichr;

	if ( pchr->pack.is_packed || pchr->isitem || MAX_CHR == pchr->pack.next )
		return ( CHR_REF )MAX_CHR;

	if ( pchr->pack.count == 0 ) return ( CHR_REF )MAX_CHR;

	iitem = chr_get_pack_item( ichr, grip_off, ignorekurse );

	// remove it from the "equipped" slots
	if ( INGAME_CHR( iitem ) )
	{
		for ( cnt = 0; cnt < INVEN_COUNT; cnt++ )
		{
			if ( pchr->inventory[cnt] == iitem )
			{
				pchr->inventory[cnt] = iitem;
				ChrList.lst[iitem].isequipped = bfalse;
				break;
			}
		}
	}

	return iitem;
}

//--------------------------------------------------------------------------------------------
bool_t pack_add_item( pack_t * ppack, CHR_REF item )
{
	CHR_REF oldfirstitem;
	chr_t  * pitem;
	cap_t  * pitem_cap;
	pack_t * pitem_pack;

	if ( NULL == ppack || !INGAME_CHR( item ) ) return bfalse;

	if ( !INGAME_CHR( item ) ) return bfalse;
	pitem      = ChrList.lst + item;
	pitem_pack = &( pitem->pack );
	pitem_cap  = chr_get_pcap( item );

	oldfirstitem     = ppack->next;
	ppack->next      = item;
	pitem_pack->next = oldfirstitem;
	ppack->count++;

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t pack_remove_item( pack_t * ppack, CHR_REF iparent, CHR_REF iitem )
{
	CHR_REF old_next;
	chr_t * pitem, * pparent;

	// convert the iitem it to a pointer
	old_next = ( CHR_REF )MAX_CHR;
	pitem    = NULL;
	if ( DEFINED_CHR( iitem ) )
	{
		pitem    = ChrList.lst + iitem;
		old_next = pitem->pack.next;
	}

	// convert the pparent it to a pointer
	pparent = NULL;
	if ( DEFINED_CHR( iparent ) )
	{
		pparent = ChrList.lst + iparent;
	}

	// Remove the iitem from the pack
	if ( NULL != pitem )
	{
		pitem->pack.was_packed = pitem->pack.is_packed;
		pitem->pack.is_packed  = bfalse;
	}

	// adjust the iparent's next
	if ( NULL != pparent )
	{
		pparent->pack.next = old_next;
	}

	if ( NULL != ppack )
	{
		ppack->count--;
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
CHR_REF chr_pack_has_a_stack( const CHR_REF by_reference item, const CHR_REF by_reference character )
{
	/// @details ZZ@> This function looks in the character's pack for an item similar
	///    to the one given.  If it finds one, it returns the similar item's
	///    index number, otherwise it returns MAX_CHR.

	CHR_REF istack;
	Uint16  id;
	bool_t  found;

	chr_t * pitem;
	cap_t * pitem_cap;

	found  = bfalse;
	istack = ( CHR_REF )MAX_CHR;

	if ( !INGAME_CHR( item ) ) return istack;
	pitem = ChrList.lst + item;
	pitem_cap = chr_get_pcap( item );

	if ( pitem_cap->isstackable )
	{
		PACK_BEGIN_LOOP( istack, ChrList.lst[character].pack.next )
		{
			if ( INGAME_CHR( istack ) )
			{
				chr_t * pstack     = ChrList.lst + istack;
				cap_t * pstack_cap = chr_get_pcap( istack );

				found = pstack_cap->isstackable;

				if ( pstack->ammo >= pstack->ammomax )
				{
					found = bfalse;
				}

				// you can still stack something even if the profiles don't match exactly,
				// but they have to have all the same IDSZ properties
				if ( found && ( pstack->iprofile != pitem->iprofile ) )
				{
					for ( id = 0; id < IDSZ_COUNT && found; id++ )
					{
						if ( chr_get_idsz( istack, id ) != chr_get_idsz( item, id ) )
						{
							found = bfalse;
						}
					}
				}
			}

			if ( found ) break;
		}
		PACK_END_LOOP( istack );

		if ( !found )
		{
			istack = ( CHR_REF )MAX_CHR;
		}
	}

	return istack;
}

//--------------------------------------------------------------------------------------------
bool_t chr_add_pack_item( const CHR_REF by_reference item, const CHR_REF by_reference character )
{
	/// @details ZZ@> This function puts one character inside the other's pack

	CHR_REF stack;
	int     newammo;

	chr_t  * pchr, * pitem;
	cap_t  * pchr_cap,  * pitem_cap;
	pack_t * pchr_pack, * pitem_pack;

	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr      = ChrList.lst + character;
	pchr_pack = &( pchr->pack );
	pchr_cap  = chr_get_pcap( character );

	if ( !INGAME_CHR( item ) ) return bfalse;
	pitem      = ChrList.lst + item;
	pitem_pack = &( pitem->pack );
	pitem_cap  = chr_get_pcap( item );

	// Make sure everything is hunkydori
	if ( pitem_pack->is_packed || pchr_pack->is_packed || pchr->isitem )
		return bfalse;

	stack = chr_pack_has_a_stack( item, character );
	if ( INGAME_CHR( stack ) )
	{
		// We found a similar, stackable item in the pack

		chr_t  * pstack      = ChrList.lst + stack;
		cap_t  * pstack_cap  = chr_get_pcap( stack );

		// reveal the name of the item or the stack
		if ( pitem->nameknown || pstack->nameknown )
		{
			pitem->nameknown  = btrue;
			pstack->nameknown = btrue;
		}

		// reveal the usage of the item or the stack
		if ( pitem_cap->usageknown || pstack_cap->usageknown )
		{
			pitem_cap->usageknown  = btrue;
			pstack_cap->usageknown = btrue;
		}

		// add the item ammo to the stack
		newammo = pitem->ammo + pstack->ammo;
		if ( newammo <= pstack->ammomax )
		{
			// All transfered, so kill the in hand item
			pstack->ammo = newammo;
			if ( INGAME_CHR( pitem->attachedto ) )
			{
				detach_character_from_mount( item, btrue, bfalse );
			}

			chr_request_terminate( item );
		}
		else
		{
			// Only some were transfered,
			pitem->ammo     = pitem->ammo + pstack->ammo - pstack->ammomax;
			pstack->ammo    = pstack->ammomax;
			pchr->ai.alert |= ALERTIF_TOOMUCHBAGGAGE;
		}
	}
	else
	{
		// Make sure we have room for another item
		if ( pchr_pack->count >= MAXNUMINPACK )
		{
			pchr->ai.alert |= ALERTIF_TOOMUCHBAGGAGE;
			return bfalse;
		}

		// Take the item out of hand
		if ( INGAME_CHR( pitem->attachedto ) )
		{
			detach_character_from_mount( item, btrue, bfalse );

			// clear the dropped flag
			pitem->ai.alert &= ~ALERTIF_DROPPED;
		}

		// Remove the item from play
		pitem->hitready        = bfalse;
		pitem_pack->was_packed = pitem_pack->is_packed;
		pitem_pack->is_packed  = btrue;

		// Insert the item into the pack as the first one
		pack_add_item( pchr_pack, item );

		// fix the flags
		if ( pitem_cap->isequipment )
		{
			pitem->ai.alert |= ALERTIF_PUTAWAY;  // same as ALERTIF_ATLASTWAYPOINT;
		}
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_remove_pack_item( CHR_REF ichr, CHR_REF iparent, CHR_REF iitem )
{
	chr_t  * pchr;
	pack_t * pchr_pack;

	bool_t removed;

	if ( !DEFINED_CHR( ichr ) ) return bfalse;
	pchr = ChrList.lst + ichr;
	pchr_pack = &( pchr->pack );

	// remove it from the pack
	removed = pack_remove_item( pchr_pack, iparent, iitem );

	// unequip the item
	if ( removed && DEFINED_CHR( iitem ) )
	{
		ChrList.lst[iitem].isequipped = bfalse;
		ChrList.lst[iitem].team       = chr_get_iteam( ichr );
	}

	return removed;
}

//--------------------------------------------------------------------------------------------
CHR_REF chr_get_pack_item( const CHR_REF by_reference character, grip_offset_t grip_off, bool_t ignorekurse )
{
	/// @details ZZ@> This function takes the last item in the character's pack and puts
	///    it into the designated hand.  It returns the item number or MAX_CHR.

	CHR_REF item, found_item, found_item_parent;

	chr_t  * pchr, * pfound_item, *pfound_item_parent;
	pack_t * pchr_pack, * pfound_item_pack, *pfound_item_parent_pack;

	// does the character exist?
	if ( !DEFINED_CHR( character ) ) return bfalse;
	pchr      = ChrList.lst + character;
	pchr_pack = &( pchr->pack );

	// Can the character have a pack?
	if ( pchr_pack->is_packed || pchr->isitem ) return ( CHR_REF )MAX_CHR;

	// is the pack empty?
	if ( MAX_CHR == pchr_pack->next || 0 == pchr_pack->count ) return ( CHR_REF )MAX_CHR;

	// Find the last item in the pack
	found_item_parent = character;
	found_item        = character;
	PACK_BEGIN_LOOP( item, pchr_pack->next )
	{
		found_item_parent = found_item;
		found_item        = item;
	}
	PACK_END_LOOP( item );

	// did we find anything?
	if ( character == found_item || MAX_CHR == found_item ) return bfalse;

	// convert the found_item it to a pointer
	pfound_item      = NULL;
	pfound_item_pack = NULL;
	if ( DEFINED_CHR( found_item ) )
	{
		pfound_item = ChrList.lst + found_item;
		pfound_item_pack = &( pfound_item->pack );
	}

	// convert the pfound_item_parent it to a pointer
	pfound_item_parent      = NULL;
	pfound_item_parent_pack = NULL;
	if ( DEFINED_CHR( found_item_parent ) )
	{
		pfound_item_parent      = ChrList.lst + found_item_parent;
		pfound_item_parent_pack = &( pfound_item_parent->pack );
	}

	// did we find a valid object?
	if ( !INGAME_CHR( found_item ) )
	{
		chr_remove_pack_item( character, found_item_parent, found_item );

		return bfalse;
	}

	// Figure out what to do with it
	if ( pfound_item->iskursed && pfound_item->isequipped && !ignorekurse )
	{
		// Flag the last found_item as not removed
		pfound_item->ai.alert |= ALERTIF_NOTTAKENOUT;  // Same as ALERTIF_NOTPUTAWAY

		// Cycle it to the front
		pfound_item_pack->next        = pchr_pack->next;
		pfound_item_parent_pack->next = ( CHR_REF )MAX_CHR;
		pchr_pack->next               = found_item;

		if ( character == found_item_parent )
		{
			pfound_item_pack->next = ( CHR_REF )MAX_CHR;
		}

		found_item = ( CHR_REF )MAX_CHR;
	}
	else
	{
		// Remove the last found_item from the pack
		chr_remove_pack_item( character, found_item_parent, found_item );

		// Attach the found_item to the character's hand
		attach_character_to_mount( found_item, character, grip_off );

		// fix the flags
		pfound_item->ai.alert &= ~ALERTIF_GRABBED;
		pfound_item->ai.alert |= ALERTIF_TAKENOUT;
	}

	if ( MAX_CHR == pchr_pack->next )
	{
		pchr_pack->count = 0;
	}

	return found_item;
}

//--------------------------------------------------------------------------------------------
void drop_keys( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function drops all keys ( [KEYA] to [KEYZ] ) that are in a character's
	///    inventory ( Not hands ).

	chr_t  * pchr;
	CHR_REF  item, lastitem;
	FACING_T direction;
	IDSZ     testa, testz;

	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	if ( pchr->pos.z > ( PITDEPTH >> 1 ) ) // Don't lose keys in pits...
	{
		// The IDSZs to find
		testa = MAKE_IDSZ( 'K', 'E', 'Y', 'A' );  // [KEYA]
		testz = MAKE_IDSZ( 'K', 'E', 'Y', 'Z' );  // [KEYZ]

		lastitem = character;
		PACK_BEGIN_LOOP( item, pchr->pack.next )
		{
			if ( INGAME_CHR( item ) && item != character )  // Should never happen...
			{
				chr_t * pitem = ChrList.lst + item;

				if (( chr_get_idsz( item, IDSZ_PARENT ) >= testa && chr_get_idsz( item, IDSZ_PARENT ) <= testz ) ||
					( chr_get_idsz( item, IDSZ_TYPE ) >= testa && chr_get_idsz( item, IDSZ_TYPE ) <= testz ) )
				{
					// We found a key...
					TURN_T turn;

					direction = RANDIE;
					turn      = TO_TURN( direction );

					// unpack the item
					ChrList.lst[lastitem].pack.next = pitem->pack.next;
					pitem->pack.next = ( CHR_REF )MAX_CHR;
					pchr->pack.count--;
					pitem->attachedto = ( CHR_REF )MAX_CHR;
					pitem->ai.alert |= ALERTIF_DROPPED;
					pitem->hitready = btrue;
					pitem->pack.was_packed = pitem->pack.is_packed;
					pitem->pack.is_packed  = bfalse;
					pitem->isequipped    = bfalse;

					chr_set_pos( pitem, chr_get_pos_v( pchr ) );

					pitem->ori.facing_z           = direction + ATK_BEHIND;
					pitem->enviro.floor_level = pchr->enviro.floor_level;
					pitem->enviro.level       = pchr->enviro.level;
					pitem->enviro.fly_level   = pchr->enviro.fly_level;
					pitem->onwhichplatform    = pchr->onwhichplatform;
					pitem->vel.x              = turntocos[ turn ] * DROPXYVEL;
					pitem->vel.y              = turntosin[ turn ] * DROPXYVEL;
					pitem->vel.z              = DROPZVEL;
					pitem->team               = pitem->baseteam;

					chr_set_floor_level( pitem, pchr->enviro.floor_level );
				}
				else
				{
					lastitem = item;
				}
			}
		}
		PACK_END_LOOP( item );
	}
}

//--------------------------------------------------------------------------------------------
bool_t drop_all_items( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function drops all of a character's items

	CHR_REF  item;
	FACING_T direction;
	Sint16   diradd;
	chr_t  * pchr;

	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	detach_character_from_mount( pchr->holdingwhich[SLOT_LEFT], btrue, bfalse );
	detach_character_from_mount( pchr->holdingwhich[SLOT_RIGHT], btrue, bfalse );
	if ( pchr->pack.count > 0 )
	{
		direction = pchr->ori.facing_z + ATK_BEHIND;
		diradd    = 0x00010000 / pchr->pack.count;

		while ( pchr->pack.count > 0 )
		{
			item = inventory_get_item( character, GRIP_LEFT, bfalse );

			if ( INGAME_CHR( item ) )
			{
				chr_t * pitem = ChrList.lst + item;

				detach_character_from_mount( item, btrue, btrue );

				chr_set_pos( pitem, chr_get_pos_v(pchr) );
				pitem->hitready           = btrue;
				pitem->ai.alert          |= ALERTIF_DROPPED;
				pitem->enviro.floor_level = pchr->enviro.floor_level;
				pitem->enviro.level       = pchr->enviro.level;
				pitem->enviro.fly_level   = pchr->enviro.fly_level;
				pitem->onwhichplatform    = pchr->onwhichplatform;
				pitem->ori.facing_z       = direction + ATK_BEHIND;
				pitem->vel.x              = turntocos[( direction>>2 ) & TRIG_TABLE_MASK ] * DROPXYVEL;
				pitem->vel.y              = turntosin[( direction>>2 ) & TRIG_TABLE_MASK ] * DROPXYVEL;
				pitem->vel.z              = DROPZVEL;
				pitem->team               = pitem->baseteam;

				pitem->dismount_timer     = PHYS_DISMOUNT_TIME;
				pitem->dismount_object    = character;

				chr_set_floor_level( pitem, pchr->enviro.floor_level );
			}

			direction += diradd;
		}
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
struct s_grab_data
{
	CHR_REF ichr;
	float  dist;
};
typedef struct s_grab_data grab_data_t;

//--------------------------------------------------------------------------------------------
int grab_data_cmp( const void * pleft, const void * pright )
{
	int rv;
	float diff;

	grab_data_t * dleft  = ( grab_data_t * )pleft;
	grab_data_t * dright = ( grab_data_t * )pright;

	diff = dleft->dist - dright->dist;

	if ( diff < 0.0f )
	{
		rv = -1;
	}
	else if ( diff > 0.0f )
	{
		rv = 1;
	}
	else
	{
		rv = 0;
	}

	return rv;
}

//--------------------------------------------------------------------------------------------
bool_t character_grab_stuff( const CHR_REF by_reference ichr_a, grip_offset_t grip_off, bool_t grab_people )
{
	/// @details ZZ@> This function makes the character pick up an item if there's one around

	int       cnt;
	size_t    vertex;
	CHR_REF   ichr_b;
	int       frame_nxt;
	slot_t    slot;
	fvec4_t   point[1], nupoint[1];
	SDL_Color color_red = {0xFF, 0x7F, 0x7F, 0xFF};
	SDL_Color color_grn = {0x7F, 0xFF, 0x7F, 0xFF};
	//   SDL_Color color_blu = {0x7F, 0x7F, 0xFF, 0xFF};

	chr_t * pchr_a;

	int ticks;

	bool_t retval;

	// valid objects that can be grabbed
	int         grab_count = 0;
	grab_data_t grab_list[MAX_CHR];

	// valid objects that cannot be grabbed
	int         ungrab_count = 0;
	grab_data_t ungrab_list[MAX_CHR];

	if ( !INGAME_CHR( ichr_a ) ) return bfalse;
	pchr_a = ChrList.lst + ichr_a;

	ticks = SDL_GetTicks();

	// Make life easier
	slot = grip_offset_to_slot( grip_off );  // 0 is left, 1 is right

	// Make sure the character doesn't have something already, and that it has hands
	if ( INGAME_CHR( pchr_a->holdingwhich[slot] ) || !chr_get_pcap( ichr_a )->slotvalid[slot] )
		return bfalse;

	// Do we have a matrix???
	if ( chr_matrix_valid( pchr_a ) )
	{
		// Transform the weapon grip_off from pchr_a->iprofile to world space
		frame_nxt = pchr_a->inst.frame_nxt;
		vertex    = pchr_a->inst.vrt_count - grip_off;

		// do the automatic update
		chr_instance_update_vertices( &( pchr_a->inst ), vertex, vertex, bfalse );

		// Calculate grip_off point locations with linear interpolation and other silly things
		point[0].x = pchr_a->inst.vrt_lst[vertex].pos[XX];
		point[0].y = pchr_a->inst.vrt_lst[vertex].pos[YY];
		point[0].z = pchr_a->inst.vrt_lst[vertex].pos[ZZ];
		point[0].w = 1.0f;

		// Do the transform
		TransformVertices( &( pchr_a->inst.matrix ), point, nupoint, 1 );
	}
	else
	{
		// Just wing it
		nupoint[0].x = pchr_a->pos.x;
		nupoint[0].y = pchr_a->pos.y;
		nupoint[0].z = pchr_a->pos.z;
		nupoint[0].w = 1.0f;
	}

	// Go through all characters to find the best match
	CHR_BEGIN_LOOP_ACTIVE( ichr_b, pchr_b )
	{
		fvec3_t   pos_b;
		float     dx, dy, dz, dxy;
		bool_t    can_grab = btrue;

		// do nothing to yourself
		if ( ichr_a == ichr_b ) continue;

		if ( pchr_b->pack.is_packed ) continue;        // pickpocket not allowed yet
		if ( INGAME_CHR( pchr_b->attachedto ) ) continue; // disarm not allowed yet

		// do not pick up your mount
		if ( pchr_b->holdingwhich[SLOT_LEFT] == ichr_a ||
			pchr_b->holdingwhich[SLOT_RIGHT] == ichr_a ) continue;

		pos_b = pchr_b->pos;

		// First check absolute value diamond
		dx = ABS( nupoint[0].x - pos_b.x );
		dy = ABS( nupoint[0].y - pos_b.y );
		dz = ABS( nupoint[0].z - pos_b.z );
		dxy = dx + dy;

		if ( dxy > GRID_SIZE * 2 || dz > MAX( pchr_b->bump.height, GRABSIZE ) ) continue;

		// reasonable carrying capacity
		if ( pchr_b->phys.weight > pchr_a->phys.weight + pchr_a->strength * INV_FF )
		{
			can_grab = bfalse;
		}

		// grab_people == btrue allows you to pick up living non-items
		// grab_people == false allows you to pick up living (functioning) items
		if ( pchr_b->alive && ( grab_people == pchr_b->isitem ) )
		{
			can_grab = bfalse;
		}

		if ( can_grab )
		{
			grab_list[grab_count].ichr = ichr_b;
			grab_list[grab_count].dist = dxy;
			grab_count++;

			//iline = get_free_line();
			//if( iline >= 0)
			//{
			//    line_list[iline].src     = nupoint[0];
			//    line_list[iline].dst     = pchr_b->pos;
			//    line_list[iline].color.r = color_grn.r * INV_FF;
			//    line_list[iline].color.g = color_grn.g * INV_FF;
			//    line_list[iline].color.b = color_grn.b * INV_FF;
			//    line_list[iline].color.a = 1.0f;
			//    line_list[iline].time    = ticks + ONESECOND * 5;
			//}
		}
		else
		{
			ungrab_list[grab_count].ichr = ichr_b;
			ungrab_list[grab_count].dist = dxy;
			ungrab_count++;

			//iline = get_free_line();
			//if( iline >= 0)
			//{
			//    line_list[iline].src     = nupoint[0];
			//    line_list[iline].dst     = pchr_b->pos;
			//    line_list[iline].color.r = color_red.r * INV_FF;
			//    line_list[iline].color.g = color_red.g * INV_FF;
			//    line_list[iline].color.b = color_red.b * INV_FF;
			//    line_list[iline].color.a = 1.0f;
			//    line_list[iline].time    = ticks + ONESECOND * 5;
			//}
		}
	}
	CHR_END_LOOP();

	// sort the grab list
	if ( grab_count > 1 )
	{
		qsort( grab_list, grab_count, sizeof( grab_data_t ), grab_data_cmp );
	}

	// try to grab something
	retval = bfalse;
	for ( cnt = 0; cnt < grab_count; cnt++ )
	{
		bool_t can_grab;

		chr_t * pchr_b;

		ichr_b = grab_list[cnt].ichr;
		pchr_b = ChrList.lst + ichr_b;

		if ( grab_list[cnt].dist > GRABSIZE ) continue;

		can_grab = do_item_pickup( ichr_a, ichr_b );

		if ( can_grab )
		{
			// Stick 'em together and quit
			attach_character_to_mount( ichr_b, ichr_a, grip_off );
			if ( grab_people )
			{
				// Do a slam animation...  ( Be sure to drop!!! )
				chr_play_action( pchr_a, ACTION_MC + slot, bfalse );
			}
			retval = btrue;
			break;
		}
		else
		{
			// Lift the item a little and quit...
			pchr_b->vel.z = DROPZVEL;
			pchr_b->hitready = btrue;
			pchr_b->ai.alert |= ALERTIF_DROPPED;
			break;
		}

	}

	if ( !retval )
	{
		fvec3_t   vforward;

		//---- generate billboards for things that players can interact with
		if ( cfg.feedback != FEEDBACK_OFF && pchr_a->isplayer )
		{
			GLXvector4f default_tint = { 1.00f, 1.00f, 1.00f, 1.00f };

			// things that can be grabbed (5 secs and green)
			for ( cnt = 0; cnt < grab_count; cnt++ )
			{
				ichr_b = grab_list[cnt].ichr;
				chr_make_text_billboard( ichr_b, chr_get_name( ichr_b, CHRNAME_ARTICLE | CHRNAME_CAPITAL ), color_grn, default_tint, 5, bb_opt_none );
			}

			// things that can't be grabbed (5 secs and red)
			for ( cnt = 0; cnt < ungrab_count; cnt++ )
			{
				ichr_b = ungrab_list[cnt].ichr;
				chr_make_text_billboard( ichr_b, chr_get_name( ichr_b, CHRNAME_ARTICLE | CHRNAME_CAPITAL ), color_red, default_tint, 5, bb_opt_none );
			}
		}

		//---- if you can't grab anything, activate something using ALERTIF_BUMPED
		if ( pchr_a->isplayer && ungrab_count > 0 )
		{
			chr_getMatForward( pchr_a, &vforward );

			// sort the ungrab list
			if ( ungrab_count > 1 )
			{
				qsort( ungrab_list, ungrab_count, sizeof( grab_data_t ), grab_data_cmp );
			}

			for ( cnt = 0; cnt < ungrab_count; cnt++ )
			{
				float       ftmp;
				fvec3_t     diff;
				chr_t     * pchr_b;

				if ( ungrab_list[cnt].dist > GRABSIZE ) continue;

				ichr_b = ungrab_list[cnt].ichr;
				if ( !INGAME_CHR( ichr_b ) ) continue;

				pchr_b = ChrList.lst + ichr_b;

				diff = fvec3_sub( pchr_a->pos.v, pchr_b->pos.v );

				// ignore vertical displacement in the dot product
				ftmp = vforward.x * diff.x + vforward.y * diff.y;
				if ( ftmp > 0.0f )
				{
					ai_state_set_bumplast( &( pchr_b->ai ), ichr_a );
				}
			}
		}
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
void character_swipe( const CHR_REF by_reference ichr, slot_t slot )
{
	/// @details ZZ@> This function spawns an attack particle

	CHR_REF iweapon, ithrown, iholder;
	chr_t * pchr, * pweapon;
	cap_t * pweapon_cap;

	PRT_REF iparticle;

	int   spawn_vrt_offset;
	Uint8 action;
	Uint16 turn;
	float dampen;
	float velocity;

	bool_t unarmed_attack;

	if ( !INGAME_CHR( ichr ) ) return;
	pchr = ChrList.lst + ichr;

	iweapon = pchr->holdingwhich[slot];

	// See if it's an unarmed attack...
	if ( MAX_CHR == iweapon )
	{
		unarmed_attack   = btrue;
		iweapon          = ichr;
		spawn_vrt_offset = slot_to_grip_offset( slot );  // SLOT_LEFT -> GRIP_LEFT, SLOT_RIGHT -> GRIP_RIGHT
	}
	else
	{
		unarmed_attack   = bfalse;
		spawn_vrt_offset = GRIP_LAST;
		action = pchr->inst.action_which;
	}

	if ( !INGAME_CHR( iweapon ) ) return;
	pweapon = ChrList.lst + iweapon;

	pweapon_cap = chr_get_pcap( iweapon );
	if ( NULL == pweapon_cap ) return;

	// find the 1st non-item that is holding the weapon
	iholder = chr_get_lowest_attachment( iweapon, btrue );

	// What kind of attack are we going to do?
	if ( !unarmed_attack && (( pweapon_cap->isstackable && pweapon->ammo > 1 ) || ACTION_IS_TYPE( pweapon->inst.action_which, F ) ) )
	{
		// Throw the weapon if it's stacked or a hurl animation
		ithrown = spawn_one_character( pchr->pos, pweapon->iprofile, chr_get_iteam( iholder ), 0, pchr->ori.facing_z, pweapon->Name, ( CHR_REF )MAX_CHR );
		if ( INGAME_CHR( ithrown ) )
		{
			chr_t * pthrown = ChrList.lst + ithrown;

			pthrown->iskursed = bfalse;
			pthrown->ammo = 1;
			pthrown->ai.alert |= ALERTIF_THROWN;
			velocity = pchr->strength / ( pthrown->phys.weight * THROWFIX );
			velocity += MINTHROWVELOCITY;
			velocity = MIN( velocity, MAXTHROWVELOCITY );

			turn = TO_TURN( pchr->ori.facing_z + ATK_BEHIND );
			pthrown->vel.x += turntocos[ turn ] * velocity;
			pthrown->vel.y += turntosin[ turn ] * velocity;
			pthrown->vel.z = DROPZVEL;
			if ( pweapon->ammo <= 1 )
			{
				// Poof the item
				detach_character_from_mount( iweapon, btrue, bfalse );
				chr_request_terminate( GET_REF_PCHR( pweapon ) );
			}
			else
			{
				pweapon->ammo--;
			}
		}
	}
	else
	{
		// A generic attack. Spawn the damage particle.
		if ( pweapon->ammomax == 0 || pweapon->ammo != 0 )
		{
			if ( pweapon->ammo > 0 && !pweapon_cap->isstackable )
			{
				pweapon->ammo--;  // Ammo usage
			}

			// Spawn an attack particle
			if ( pweapon_cap->attack_pip != -1 )
			{
				// make the weapon's holder the owner of the attack particle?
				// will this mess up wands?
				iparticle = spawn_one_particle( pweapon->pos, pchr->ori.facing_z, pweapon->iprofile, pweapon_cap->attack_pip, iweapon, spawn_vrt_offset, chr_get_iteam( iholder ), iholder, ( PRT_REF )TOTAL_MAX_PRT, 0, ( CHR_REF )MAX_CHR );

				if ( ALLOCATED_PRT( iparticle ) )
				{
					fvec3_t tmp_pos;
					prt_t * pprt = PrtList.lst + iparticle;

					tmp_pos = prt_get_pos( pprt );

					if ( pweapon_cap->attack_attached )
					{
						// attached particles get a strength bonus for reeling...
						dampen = REELBASE + ( pchr->strength / REEL );

						pprt->vel_stt.x *= dampen;
						pprt->vel_stt.y *= dampen;
						pprt->vel_stt.z *= dampen;

						pprt = place_particle_at_vertex( pprt, iweapon, spawn_vrt_offset );
						if( NULL == pprt ) return;
					}
					else
					{
						// NOT ATTACHED
						pprt->attachedto_ref = ( CHR_REF )MAX_CHR;

						// Detach the particle
						if ( !prt_get_ppip( iparticle )->startontarget || !INGAME_CHR( pprt->target_ref ) )
						{
							pprt = place_particle_at_vertex( pprt, iweapon, spawn_vrt_offset );
							if( NULL == pprt ) return;

							// Correct Z spacing base, but nothing else...
							tmp_pos.z += prt_get_ppip( iparticle )->spacing_vrt_pair.base;
						}

						// Don't spawn in walls
						if ( prt_test_wall( pprt, tmp_pos.v ) )
						{
							tmp_pos.x = pweapon->pos.x;
							tmp_pos.y = pweapon->pos.y;
							if ( prt_test_wall( pprt, tmp_pos.v ) )
							{
								tmp_pos.x = pchr->pos.x;
								tmp_pos.y = pchr->pos.y;
							}
						}
					}

					// Initial particles get a bonus, which may be 0.00f
					pprt->damage.base += ( pchr->strength     * pweapon_cap->str_bonus );
					pprt->damage.base += ( pchr->wisdom       * pweapon_cap->wis_bonus );
					pprt->damage.base += ( pchr->intelligence * pweapon_cap->int_bonus );
					pprt->damage.base += ( pchr->dexterity    * pweapon_cap->dex_bonus );

					// Initial particles get an enchantment bonus
					pprt->damage.base += pweapon->damageboost;

					prt_set_pos( pprt, tmp_pos.v );
				}
			}
		}
		else
		{
			pweapon->ammoknown = btrue;
		}
	}
}

//--------------------------------------------------------------------------------------------
void drop_money( const CHR_REF by_reference character, int money )
{
	/// @details ZZ@> This function drops some of a character's money

	int huns, tfives, fives, ones, cnt;

	if ( !INGAME_CHR( character ) ) return;

	if ( money > ChrList.lst[character].money )  money = ChrList.lst[character].money;
	if ( money > 0 && ChrList.lst[character].pos.z > -2 )
	{
		ChrList.lst[character].money = ChrList.lst[character].money - money;
		huns = money / 100;  money -= ( huns << 7 ) - ( huns << 5 ) + ( huns << 2 );
		tfives = money / 25;  money -= ( tfives << 5 ) - ( tfives << 3 ) + tfives;
		fives = money / 5;  money -= ( fives << 2 ) + fives;
		ones = money;

		for ( cnt = 0; cnt < ones; cnt++ )
		{
			spawn_one_particle_global( ChrList.lst[character].pos, ATK_FRONT, PIP_COIN1, cnt );
		}

		for ( cnt = 0; cnt < fives; cnt++ )
		{
			spawn_one_particle_global( ChrList.lst[character].pos, ATK_FRONT, PIP_COIN5, cnt );
		}

		for ( cnt = 0; cnt < tfives; cnt++ )
		{
			spawn_one_particle_global( ChrList.lst[character].pos, ATK_FRONT, PIP_COIN25, cnt );
		}

		for ( cnt = 0; cnt < huns; cnt++ )
		{
			spawn_one_particle_global( ChrList.lst[character].pos, ATK_FRONT, PIP_COIN100, cnt );
		}

		ChrList.lst[character].damagetime = DAMAGETIME;  // So it doesn't grab it again
	}
}

//--------------------------------------------------------------------------------------------
void call_for_help( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function issues a call for help to all allies

	TEAM_REF team;

	if ( !INGAME_CHR( character ) ) return;

	team = chr_get_iteam( character );
	TeamStack.lst[team].sissy = character;

	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		if ( cnt != character && !team_hates_team( pchr->team, team ) )
		{
			pchr->ai.alert |= ALERTIF_CALLEDFORHELP;
		}
	}
	CHR_END_LOOP();
}

//--------------------------------------------------------------------------------------------
bool_t setup_xp_table( const CAP_REF by_reference icap )
{
	/// @details ZF@> This calculates the xp needed to reach next level and stores it in an array for later use

	Uint8 level;
	cap_t * pcap;

	if ( !LOADED_CAP( icap ) ) return bfalse;
	pcap = CapStack.lst + icap;

	// Calculate xp needed
	for ( level = MAXBASELEVEL; level < MAXLEVEL; level++ )
	{
		Uint32 xpneeded = pcap->experience_forlevel[MAXBASELEVEL - 1];
		xpneeded += ( level * level * level * 15 );
		xpneeded -= (( MAXBASELEVEL - 1 ) * ( MAXBASELEVEL - 1 ) * ( MAXBASELEVEL - 1 ) * 15 );
		pcap->experience_forlevel[level] = xpneeded;
	}
	return btrue;
}

//--------------------------------------------------------------------------------------------
void do_level_up( const CHR_REF by_reference character )
{
	/// @details BB@> level gains are done here, but only once a second

	Uint8 curlevel;
	int number;
	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	pcap = chr_get_pcap( character );
	if ( NULL == pcap ) return;

	// Do level ups and stat changes
	curlevel = pchr->experiencelevel + 1;
	if ( curlevel < MAXLEVEL )
	{
		Uint32 xpcurrent, xpneeded;

		xpcurrent = pchr->experience;
		xpneeded  = pcap->experience_forlevel[curlevel];
		if ( xpcurrent >= xpneeded )
		{
			// do the level up
			pchr->experiencelevel++;
			xpneeded = pcap->experience_forlevel[curlevel];

			// The character is ready to advance...
			if ( pchr->isplayer )
			{
				debug_printf( "%s gained a level!!!", chr_get_name( GET_REF_PCHR( pchr ), CHRNAME_ARTICLE | CHRNAME_DEFINITE | CHRNAME_CAPITAL ) );
				sound_play_chunk_full( g_wavelist[GSND_LEVELUP] );
			}

			// Size
			pchr->fat_goto += pcap->size_perlevel * 0.25f;  // Limit this?
			pchr->fat_goto_time += SIZETIME;

			// Strength
			number = generate_irand_range( pcap->strength_stat.perlevel );
			number += pchr->strength;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->strength = number;

			// Wisdom
			number = generate_irand_range( pcap->wisdom_stat.perlevel );
			number += pchr->wisdom;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->wisdom = number;

			// Intelligence
			number = generate_irand_range( pcap->intelligence_stat.perlevel );
			number += pchr->intelligence;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->intelligence = number;

			// Dexterity
			number = generate_irand_range( pcap->dexterity_stat.perlevel );
			number += pchr->dexterity;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->dexterity = number;

			// Life
			number = generate_irand_range( pcap->life_stat.perlevel );
			number += pchr->lifemax;
			if ( number > PERFECTBIG ) number = PERFECTBIG;
			pchr->life += ( number - pchr->lifemax );
			pchr->lifemax = number;

			// Mana
			number = generate_irand_range( pcap->mana_stat.perlevel );
			number += pchr->manamax;
			if ( number > PERFECTBIG ) number = PERFECTBIG;
			pchr->mana += ( number - pchr->manamax );
			pchr->manamax = number;

			// Mana Return
			number = generate_irand_range( pcap->manareturn_stat.perlevel );
			number += pchr->manareturn;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->manareturn = number;

			// Mana Flow
			number = generate_irand_range( pcap->manaflow_stat.perlevel );
			number += pchr->manaflow;
			if ( number > PERFECTSTAT ) number = PERFECTSTAT;
			pchr->manaflow = number;
		}
	}
}

//--------------------------------------------------------------------------------------------
void give_experience( const CHR_REF by_reference character, int amount, xp_type xptype, bool_t override_invictus )
{
	/// @details ZZ@> This function gives a character experience

	float newamount;

	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	pcap = chr_get_pcap( character );
	if ( NULL == pcap ) return;

	//No xp to give
	if ( 0 == amount ) return;

	if ( !pchr->invictus || override_invictus )
	{
		float intadd = ( FP8_TO_INT( pchr->intelligence ) - 10.0f ) / 200.0f;
		float wisadd = ( FP8_TO_INT( pchr->wisdom )       - 10.0f ) / 400.0f;

		// Figure out how much experience to give
		newamount = amount;
		if ( xptype < XP_COUNT )
		{
			newamount = amount * pcap->experience_rate[xptype];
		}

		// Intelligence and slightly wisdom increases xp gained (0,5% per int and 0,25% per wisdom above 10)
		newamount *= 1.00f + intadd + wisadd;

		// Apply XP bonus/penality depending on game difficulty
		if ( cfg.difficulty >= GAME_HARD ) newamount *= 1.20f;                // 20% extra on hard
		else if ( cfg.difficulty >= GAME_NORMAL ) newamount *= 1.10f;       // 10% extra on normal

		pchr->experience += newamount;
	}
}

//--------------------------------------------------------------------------------------------
void give_team_experience( const TEAM_REF by_reference team, int amount, Uint8 xptype )
{
	/// @details ZZ@> This function gives every character on a team experience

	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		if ( pchr->team == team )
		{
			give_experience( cnt, amount, xptype, bfalse );
		}
	}
	CHR_END_LOOP();
}

//--------------------------------------------------------------------------------------------
chr_t * resize_one_character( chr_t * pchr )
{
	/// @details ZZ@> This function makes the characters get bigger or smaller, depending
	///    on their fat_goto and fat_goto_time. Spellbooks do not resize
	///    BB@> assume that this will only be called from inside chr_config_do_active(),
	///         so pchr is just right to be used here

	CHR_REF ichr;
	cap_t * pcap;
	bool_t  willgetcaught;
	float   newsize;

	if( NULL == pchr ) return pchr;

	ichr = GET_REF_PCHR( pchr );
	pcap = chr_get_pcap( ichr );

	if ( pchr->fat_goto_time < 0 ) return pchr;

	if ( pchr->fat_goto != pchr->fat )
	{
		int bump_increase;

		bump_increase = ( pchr->fat_goto - pchr->fat ) * 0.10f * pchr->bump.size;

		// Make sure it won't get caught in a wall
		willgetcaught = bfalse;
		if ( pchr->fat_goto > pchr->fat )
		{
			pchr->bump.size += bump_increase;

			if ( chr_test_wall( pchr, NULL ) )
			{
				willgetcaught = btrue;
			}

			pchr->bump.size -= bump_increase;
		}

		// If it is getting caught, simply halt growth until later
		if ( !willgetcaught )
		{
			// Figure out how big it is
			pchr->fat_goto_time--;

			newsize = pchr->fat_goto;
			if ( pchr->fat_goto_time > 0 )
			{
				newsize = ( pchr->fat * 0.90f ) + ( newsize * 0.10f );
			}

			// Make it that big...
			chr_set_fat( pchr, newsize );

			if ( pcap->weight == 0xFF )
			{
				pchr->phys.weight = INFINITE_WEIGHT;
			}
			else
			{
				Uint32 itmp = pcap->weight * pchr->fat * pchr->fat * pchr->fat;
				pchr->phys.weight = MIN( itmp, MAX_WEIGHT );
			}
		}
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
//void resize_all_characters()
//{
//    /// @details ZZ@> This function makes the characters get bigger or smaller, depending
//    ///    on their fat_goto and fat_goto_time. Spellbooks do not resize
//
//    CHR_BEGIN_LOOP_ACTIVE( ichr, pchr )
//    {
//        resize_one_character( pchr );
//    }
//    CHR_END_LOOP();
//}

//--------------------------------------------------------------------------------------------
bool_t export_one_character_name_vfs( const char *szSaveName, const CHR_REF by_reference character )
{
	/// @details ZZ@> This function makes the naming.txt file for the character

	if ( !INGAME_CHR( character ) ) return bfalse;

	return chop_export_vfs( szSaveName, ChrList.lst[character].Name );
}

//--------------------------------------------------------------------------------------------
bool_t chr_upload_cap( chr_t * pchr, cap_t * pcap )
{
	/// @details BB@> prepare a character profile for exporting, by uploading some special values into the
	///     cap. Just so that there is no confusion when you export multiple items of the same type,
	///     DO NOT pass the pointer returned by chr_get_pcap(). Instead, use a custom cap_t declared on the stack,
	///     or something similar
	///
	/// @note This has been modified to basically reverse the actions of chr_download_cap().
	///       If all enchants have been removed, this should export all permanent changes to the
	///       base character profile.

	int tnc;

	if ( !DEFINED_PCHR( pchr ) ) return bfalse;

	if ( NULL == pcap || !pcap->loaded ) return bfalse;

	// export values that override spawn.txt values
	pcap->content_override   = pchr->ai.content;
	pcap->state_override     = pchr->ai.state;
	pcap->money              = pchr->money;
	pcap->skin_override      = pchr->skin;
	pcap->level_override     = pchr->experiencelevel;

	// export the current experience
	ints_to_range( pchr->experience, 0, &( pcap->experience ) );

	// export the current mana and life
	pcap->life_spawn         = CLIP( pchr->life, 0, pchr->lifemax );
	pcap->mana_spawn         = CLIP( pchr->mana, 0, pchr->manamax );

	// Movement
	pcap->sneakspd = pchr->sneakspd;
	pcap->walkspd = pchr->walkspd;
	pcap->runspd = pchr->runspd;

	// weight and size
	pcap->size       = pchr->fat_goto;
	pcap->bumpdampen = pchr->phys.bumpdampen;
	if ( pchr->phys.weight == INFINITE_WEIGHT )
	{
		pcap->weight = 0xFF;
	}
	else
	{
		Uint32 itmp = pchr->phys.weight / pchr->fat / pchr->fat / pchr->fat;
		pcap->weight = MIN( itmp, 0xFE );
	}

	// Other junk
	pcap->flyheight   = pchr->flyheight;
	pcap->alpha       = pchr->alpha_base;
	pcap->light       = pchr->light_base;
	pcap->flashand    = pchr->flashand;
	pcap->dampen      = pchr->phys.dampen;

	// Jumping
	pcap->jump       = pchr->jump_power;
	pcap->jumpnumber = pchr->jumpnumberreset;

	// Flags
	pcap->stickybutt      = pchr->stickybutt;
	pcap->canopenstuff    = pchr->openstuff;
	pcap->transferblend   = pchr->transferblend;
	pcap->waterwalk       = pchr->waterwalk;
	pcap->platform        = pchr->platform;
	pcap->canuseplatforms = pchr->canuseplatforms;
	pcap->isitem          = pchr->isitem;
	pcap->invictus        = pchr->invictus;
	pcap->ismount         = pchr->ismount;
	pcap->cangrabmoney    = pchr->cangrabmoney;

	// Damage
	pcap->attachedprt_reaffirmdamagetype = pchr->reaffirmdamagetype;
	pcap->damagetargettype               = pchr->damagetargettype;

	// SWID
	ints_to_range( pchr->strength    , 0, &( pcap->strength_stat.val ) );
	ints_to_range( pchr->wisdom      , 0, &( pcap->wisdom_stat.val ) );
	ints_to_range( pchr->intelligence, 0, &( pcap->intelligence_stat.val ) );
	ints_to_range( pchr->dexterity   , 0, &( pcap->dexterity_stat.val ) );

	// Life and Mana
	pcap->lifecolor = pchr->lifecolor;
	pcap->manacolor = pchr->manacolor;
	ints_to_range( pchr->lifemax     , 0, &( pcap->life_stat.val ) );
	ints_to_range( pchr->manamax     , 0, &( pcap->mana_stat.val ) );
	ints_to_range( pchr->manareturn  , 0, &( pcap->manareturn_stat.val ) );
	ints_to_range( pchr->manaflow    , 0, &( pcap->manaflow_stat.val ) );

	// Gender
	pcap->gender  = pchr->gender;

	// Ammo
	pcap->ammomax = pchr->ammomax;
	pcap->ammo    = pchr->ammo;

	// update any skills that have been learned
	pcap->canjoust              = pchr->canjoust;
	pcap->canuseadvancedweapons = pchr->canuseadvancedweapons;
	pcap->shieldproficiency     = pchr->shieldproficiency;
	pcap->canusedivine          = pchr->canusedivine;
	pcap->canusearcane          = pchr->canusearcane;
	pcap->canusetech            = pchr->canusetech;
	pcap->candisarm             = pchr->candisarm;
	pcap->canbackstab           = pchr->canbackstab;
	pcap->canusepoison          = pchr->canusepoison;
	pcap->canread               = pchr->canread;
	pcap->canseekurse           = pchr->canseekurse;
	pcap->hascodeofconduct      = pchr->hascodeofconduct;
	pcap->darkvision_level      = pchr->darkvision_level;

	// Enchant stuff
	pcap->see_invisible_level = pchr->see_invisible_level;

	// base kurse state
	pcap->kursechance = pchr->iskursed ? 100 : 0;

	// Model stuff
	pcap->stoppedby = pchr->stoppedby;
	pcap->life_heal = pchr->life_heal;
	pcap->manacost  = pchr->manacost;
	pcap->nameknown = pchr->nameknown || pchr->ammoknown;          // make sure that identified items are saved as identified
	pcap->draw_icon = pchr->draw_icon;

	// sound stuff...
	for ( tnc = 0; tnc < SOUND_COUNT; tnc++ )
	{
		pcap->sound_index[tnc] = pchr->sound_index[tnc];
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_download_cap( chr_t * pchr, cap_t * pcap )
{
	/// @details BB@> grab all of the data from the data.txt file

	int iTmp, tnc;

	if ( !DEFINED_PCHR( pchr ) ) return bfalse;

	if ( NULL == pcap || !pcap->loaded ) return bfalse;

	// sound stuff...  copy from the cap
	for ( tnc = 0; tnc < SOUND_COUNT; tnc++ )
	{
		pchr->sound_index[tnc] = pcap->sound_index[tnc];
	}

	// Set up model stuff
	pchr->stoppedby = pcap->stoppedby;
	pchr->life_heal = pcap->life_heal;
	pchr->manacost  = pcap->manacost;
	pchr->nameknown = pcap->nameknown;
	pchr->ammoknown = pcap->nameknown;
	pchr->draw_icon = pcap->draw_icon;

	// calculate a base kurse state. this may be overridden later
	if ( pcap->isitem )
	{
		IPair loc_rand = {1, 100};
		pchr->iskursed = ( generate_irand_pair( loc_rand ) <= pcap->kursechance );
	}

	// Enchant stuff
	pchr->see_invisible_level = pcap->see_invisible_level;

	// Skillz
	pchr->canjoust = pcap->canjoust;
	pchr->canuseadvancedweapons = pcap->canuseadvancedweapons;
	pchr->shieldproficiency = pcap->shieldproficiency;
	pchr->canusedivine = pcap->canusedivine;
	pchr->canusearcane = pcap->canusearcane;
	pchr->canusetech = pcap->canusetech;
	pchr->candisarm = pcap->candisarm;
	pchr->canbackstab = pcap->canbackstab;
	pchr->canusepoison = pcap->canusepoison;
	pchr->canread = pcap->canread;
	pchr->canseekurse = pcap->canseekurse;
	pchr->hascodeofconduct = pcap->hascodeofconduct;
	pchr->darkvision_level = pcap->darkvision_level;
	pchr->darkvision_level_base = pcap->darkvision_level;

	// Ammo
	pchr->ammomax = pcap->ammomax;
	pchr->ammo = pcap->ammo;

	// Gender
	pchr->gender = pcap->gender;
	if ( pchr->gender == GENDER_RANDOM )  pchr->gender = generate_randmask( GENDER_FEMALE, GENDER_MALE );

	// Life and Mana
	pchr->lifecolor = pcap->lifecolor;
	pchr->manacolor = pcap->manacolor;
	pchr->lifemax = generate_irand_range( pcap->life_stat.val );
	pchr->life_return = pcap->life_return;
	pchr->manamax = generate_irand_range( pcap->mana_stat.val );
	pchr->manaflow = generate_irand_range( pcap->manaflow_stat.val );
	pchr->manareturn = generate_irand_range( pcap->manareturn_stat.val );

	// SWID
	pchr->strength = generate_irand_range( pcap->strength_stat.val );
	pchr->wisdom = generate_irand_range( pcap->wisdom_stat.val );
	pchr->intelligence = generate_irand_range( pcap->intelligence_stat.val );
	pchr->dexterity = generate_irand_range( pcap->dexterity_stat.val );

	// Skin
	pchr->skin = 0;
	if ( pcap->skin_override != NO_SKIN_OVERRIDE )
	{
		pchr->skin = pcap->skin_override % MAX_SKIN;
	}

	// Damage
	pchr->defense = pcap->defense[pchr->skin];
	pchr->reaffirmdamagetype = pcap->attachedprt_reaffirmdamagetype;
	pchr->damagetargettype = pcap->damagetargettype;
	for ( tnc = 0; tnc < DAMAGE_COUNT; tnc++ )
	{
		pchr->damagemodifier[tnc] = pcap->damagemodifier[tnc][pchr->skin];
	}

	// Flags
	pchr->stickybutt      = pcap->stickybutt;
	pchr->openstuff       = pcap->canopenstuff;
	pchr->transferblend   = pcap->transferblend;
	pchr->waterwalk       = pcap->waterwalk;
	pchr->platform        = pcap->platform;
	pchr->canuseplatforms = pcap->canuseplatforms;
	pchr->isitem          = pcap->isitem;
	pchr->invictus        = pcap->invictus;
	pchr->ismount         = pcap->ismount;
	pchr->cangrabmoney    = pcap->cangrabmoney;

	// Jumping
	pchr->jump_power = pcap->jump;
	pchr->jumpnumberreset = pcap->jumpnumber;

	// Other junk
	pchr->flyheight   = pcap->flyheight;
	pchr->maxaccel    = pcap->maxaccel[pchr->skin];
	pchr->alpha_base   = pcap->alpha;
	pchr->light_base   = pcap->light;
	pchr->flashand    = pcap->flashand;
	pchr->phys.dampen = pcap->dampen;

	// Load current life and mana. this may be overridden later
	pchr->life = CLIP( pcap->life_spawn, LOWSTAT, pchr->lifemax );
	pchr->mana = CLIP( pcap->mana_spawn,       0, pchr->manamax );

	pchr->phys.bumpdampen = pcap->bumpdampen;
	if ( pcap->weight == 0xFF )
	{
		pchr->phys.weight = INFINITE_WEIGHT;
	}
	else
	{
		Uint32 itmp = pcap->weight * pcap->size * pcap->size * pcap->size;
		pchr->phys.weight = MIN( itmp, MAX_WEIGHT );
	}

	// Image rendering
	pchr->uoffvel = pcap->uoffvel;
	pchr->voffvel = pcap->voffvel;

	// Movement
	pchr->sneakspd = pcap->sneakspd;
	pchr->walkspd = pcap->walkspd;
	pchr->runspd = pcap->runspd;

	// Money is added later
	pchr->money = pcap->money;

	// Experience
	iTmp = generate_irand_range( pcap->experience );
	pchr->experience      = MIN( iTmp, MAXXP );
	pchr->experiencelevel = pcap->level_override;

	// Particle attachments
	pchr->reaffirmdamagetype = pcap->attachedprt_reaffirmdamagetype;

	// Character size and bumping
	chr_init_size( pchr, pcap );

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t export_one_character_profile_vfs( const char *szSaveName, const CHR_REF by_reference character )
{
	/// @details ZZ@> This function creates a data.txt file for the given character.
	///    it is assumed that all enchantments have been done away with

	chr_t * pchr;
	cap_t * pcap;

	// a local version of the cap, so that the CapStack data won't be corrupted
	cap_t cap_tmp;

	if ( INVALID_CSTR( szSaveName ) && !DEFINED_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	pcap = pro_get_pcap( pchr->iprofile );
	if ( NULL == pcap ) return bfalse;

	// load up the temporary cap
	memcpy( &cap_tmp, pcap, sizeof( cap_t ) );

	// fill in the cap values with the ones we want to export from the character profile
	chr_upload_cap( pchr, &cap_tmp );

	return save_one_cap_file_vfs( szSaveName, NULL, &cap_tmp );
}

//--------------------------------------------------------------------------------------------
bool_t export_one_character_skin_vfs( const char *szSaveName, const CHR_REF by_reference character )
{
	/// @details ZZ@> This function creates a skin.txt file for the given character.

	vfs_FILE* filewrite;

	if ( !INGAME_CHR( character ) ) return bfalse;

	// Open the file
	filewrite = vfs_openWrite( szSaveName );
	if ( NULL == filewrite ) return bfalse;

	vfs_printf( filewrite, "// This file is used only by the import menu\n" );
	vfs_printf( filewrite, ": %d\n", ChrList.lst[character].skin );
	vfs_close( filewrite );
	return btrue;
}

//--------------------------------------------------------------------------------------------
CAP_REF load_one_character_profile_vfs( const char * tmploadname, int slot_override, bool_t required )
{
	/// @details ZZ@> This function fills a character profile with data from data.txt, returning
	/// the icap slot that the profile was stuck into.  It may cause the program
	/// to abort if bad things happen.

	CAP_REF  icap = ( CAP_REF )MAX_CAP;
	cap_t * pcap;
	STRING  szLoadName;

	if ( VALID_PRO_RANGE( slot_override ) )
	{
		icap = ( CAP_REF )slot_override;
	}
	else
	{
		icap = pro_get_slot_vfs( tmploadname, MAX_PROFILE );
	}

	if ( !VALID_CAP_RANGE( icap ) )
	{
		// The data file wasn't found
		if ( required )
		{
			log_debug( "load_one_character_profile_vfs() - \"%s\" was not found. Overriding a global object?\n", szLoadName );
		}
		else if ( VALID_CAP_RANGE( slot_override ) && slot_override > PMod->importamount * MAXIMPORTPERPLAYER )
		{
			log_warning( "load_one_character_profile_vfs() - Not able to open file \"%s\"\n", szLoadName );
		}

		return ( CAP_REF )MAX_CAP;
	}

	pcap = CapStack.lst + icap;

	// if there is data in this profile, release it
	if ( pcap->loaded )
	{
		// Make sure global objects don't load over existing models
		if ( required && SPELLBOOK == icap )
		{
			log_error( "Object slot %i is a special reserved slot number (cannot be used by %s).\n", SPELLBOOK, szLoadName );
		}
		else if ( required && overrideslots )
		{
			log_error( "Object slot %i used twice (%s, %s)\n", REF_TO_INT( icap ), pcap->name, szLoadName );
		}
		else
		{
			// Stop, we don't want to override it
			return ( CAP_REF )MAX_CAP;
		}

		// If loading over an existing model is allowed (?how?) then make sure to release the old one
		release_one_cap( icap );
	}

	if ( NULL == load_one_cap_file_vfs( tmploadname, pcap ) )
	{
		return ( CAP_REF )MAX_CAP;
	}

	// do the rest of the levels not listed in data.txt
	setup_xp_table( icap );

	if ( cfg.gourard_req )
	{
		pcap->uniformlit = bfalse;
	}

	// limit the wave indices to rational values
	pcap->sound_index[SOUND_FOOTFALL] = CLIP( pcap->sound_index[SOUND_FOOTFALL], INVALID_SOUND, MAX_WAVE );
	pcap->sound_index[SOUND_JUMP]     = CLIP( pcap->sound_index[SOUND_JUMP], INVALID_SOUND, MAX_WAVE );

	// bumpdampen == 0 means infinite mass, and causes some problems
	pcap->bumpdampen = MAX( INV_FF, pcap->bumpdampen );

	return icap;
}

//--------------------------------------------------------------------------------------------
bool_t heal_character( const CHR_REF by_reference character, const CHR_REF by_reference healer, int amount, bool_t ignore_invictus )
{
	/// @details ZF@> This function gives some pure life points to the target, ignoring any resistances and so forth
	chr_t * pchr, *pchr_h;

	//Setup the healed character
	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	//Setup the healer
	if ( !INGAME_CHR( healer ) ) return bfalse;
	pchr_h = ChrList.lst + healer;
	
	//Don't heal dead and invincible stuff
	if ( !pchr->alive || ( pchr->invictus && !ignore_invictus ) ) return bfalse;

	//This actually heals the character
	pchr->life = CLIP( pchr->life, pchr->life + ABS( amount ), pchr->lifemax );

	// Set alerts, but don't alert that we healed ourselves
	if ( healer != character && pchr_h->attachedto != character && ABS( amount ) > HURTDAMAGE )
	{
		pchr->ai.alert |= ALERTIF_HEALED;
		pchr->ai.attacklast = healer;
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
void cleanup_one_character( chr_t * pchr )
{
	/// @details BB@> Everything necessary to disconnect one character from the game

	CHR_REF  ichr, itmp;
	SHOP_REF ishop;

	if ( !ALLOCATED_PCHR( pchr ) ) return;
	ichr = GET_REF_PCHR( pchr );

	pchr->sparkle = NOSPARKLE;

	// Remove it from the team
	pchr->team = pchr->baseteam;
	if ( TeamStack.lst[pchr->team].morale > 0 ) TeamStack.lst[pchr->team].morale--;

	if ( TeamStack.lst[pchr->team].leader == ichr )
	{
		// The team now has no leader if the character is the leader
		TeamStack.lst[pchr->team].leader = NOLEADER;
	}

	// Clear all shop passages that it owned...
	for ( ishop = 0; ishop < ShopStack.count; ishop++ )
	{
		if ( ShopStack.lst[ishop].owner != ichr ) continue;
		ShopStack.lst[ishop].owner = SHOP_NOOWNER;
	}

	// detach from any mount
	if ( INGAME_CHR( pchr->attachedto ) )
	{
		detach_character_from_mount( ichr, btrue, bfalse );
	}

	// drop your left item
	itmp = pchr->holdingwhich[SLOT_LEFT];
	if ( INGAME_CHR( itmp ) && ChrList.lst[itmp].isitem )
	{
		detach_character_from_mount( itmp, btrue, bfalse );
	}

	// drop your right item
	itmp = pchr->holdingwhich[SLOT_RIGHT];
	if ( INGAME_CHR( itmp ) && ChrList.lst[itmp].isitem )
	{
		detach_character_from_mount( itmp, btrue, bfalse );
	}

	// start with a clean list
	cleanup_character_enchants( pchr );

	// remove enchants from the character
	if ( pchr->life >= 0 )
	{
		disenchant_character( ichr );
	}
	else
	{
		eve_t * peve;
		ENC_REF enc_now, enc_next;

		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		// remove all invalid enchants
		enc_now = pchr->firstenchant;
		while ( enc_now != MAX_ENC )
		{
			enc_next = EncList.lst[enc_now].nextenchant_ref;

			peve = enc_get_peve( enc_now );
			if ( NULL != peve && !peve->stayiftargetdead )
			{
				remove_enchant( enc_now, NULL );
			}

			enc_now = enc_next;
		}
	}

	// Stop all sound loops for this object
	looped_stop_object_sounds( ichr );
}

//--------------------------------------------------------------------------------------------
void kill_character( const CHR_REF by_reference ichr, const CHR_REF by_reference killer, bool_t ignore_invictus )
{
	/// @details BB@> Handle a character death. Set various states, disconnect it from the world, etc.

	chr_t * pchr;
	cap_t * pcap;
	int action;
	Uint16 experience;
	TEAM_REF killer_team;

	if ( !DEFINED_CHR( ichr ) ) return;
	pchr = ChrList.lst + ichr;

	//No need to continue is there?
	if ( !pchr->alive || ( pchr->invictus && !ignore_invictus ) ) return;

	pcap = pro_get_pcap( pchr->iprofile );
	if ( NULL == pcap ) return;

	killer_team = chr_get_iteam( killer );

	pchr->alive = bfalse;
	pchr->waskilled = btrue;

	pchr->life            = -1;
	pchr->platform        = btrue;
	pchr->canuseplatforms = btrue;
	pchr->phys.bumpdampen = pchr->phys.bumpdampen / 2.0f;

	// Play the death animation
	action = ACTION_KA + generate_randmask( 0, 3 );
	chr_play_action( pchr, action, bfalse );
	pchr->inst.action_keep = btrue;

	// Give kill experience
	experience = pcap->experience_worth + ( pchr->experience * pcap->experience_exchange );

	// distribute experience to the attacker
	if ( INGAME_CHR( killer ) )
	{
		// Set target
		pchr->ai.target = killer;
		if ( killer_team == TEAM_DAMAGE || killer_team == TEAM_NULL )  pchr->ai.target = ichr;

		// Award experience for kill?
		if ( team_hates_team( killer_team, pchr->team ) )
		{
			//Check for special hatred
			if ( chr_get_idsz( killer, IDSZ_HATE ) == chr_get_idsz( ichr, IDSZ_PARENT ) ||
				chr_get_idsz( killer, IDSZ_HATE ) == chr_get_idsz( ichr, IDSZ_TYPE ) )
			{
				give_experience( killer, experience, XP_KILLHATED, bfalse );
			}

			// Nope, award direct kill experience instead
			else give_experience( killer, experience, XP_KILLENEMY, bfalse );
		}
	}

	//Set various alerts to let others know it has died
	//and distribute experience to whoever needs it
	pchr->ai.alert |= ALERTIF_KILLED;

	CHR_BEGIN_LOOP_ACTIVE( tnc, plistener )
	{
		if ( !plistener->alive ) continue;

		// All allies get team experience, but only if they also hate the dead guy's team
		if ( tnc != killer && !team_hates_team( plistener->team, killer_team ) && team_hates_team( plistener->team, pchr->team ) )
		{
			give_experience( tnc, experience, XP_TEAMKILL, bfalse );
		}

		// Check if it was a leader
		if ( TeamStack.lst[pchr->team].leader == ichr && chr_get_iteam( tnc ) == pchr->team )
		{
			// All folks on the leaders team get the alert
			plistener->ai.alert |= ALERTIF_LEADERKILLED;
		}

		// Let the other characters know it died
		if ( plistener->ai.target == ichr )
		{
			plistener->ai.alert |= ALERTIF_TARGETKILLED;
		}
	}
	CHR_END_LOOP();

	// Detach the character from the game
	cleanup_one_character( pchr );

	// If it's a player, let it die properly before enabling respawn
	if ( pchr->isplayer ) revivetimer = ONESECOND; // 1 second

	// Let it's AI script run one last time
	pchr->ai.timer = update_wld + 1;            // Prevent IfTimeOut in scr_run_chr_script()
	scr_run_chr_script( ichr );

	// Stop any looped sounds
	if ( pchr->loopedsound_channel != INVALID_SOUND ) sound_stop_channel( pchr->loopedsound_channel );
	looped_stop_object_sounds( ichr );
	pchr->loopedsound_channel = INVALID_SOUND;
}

//--------------------------------------------------------------------------------------------
int damage_character( const CHR_REF by_reference character, FACING_T direction,
					 IPair  damage, Uint8 damagetype, TEAM_REF team,
					 CHR_REF attacker, Uint16 effects, bool_t ignore_invictus )
{
	/// @details ZZ@> This function calculates and applies damage to a character.  It also
	///    sets alerts and begins actions.  Blocking and frame invincibility
	///    are done here too.  Direction is ATK_FRONT if the attack is coming head on,
	///    ATK_RIGHT if from the right, ATK_BEHIND if from the back, ATK_LEFT if from the
	///    left.

	int     action;
	int     actual_damage, base_damage, max_damage;
	chr_t * pchr;
	cap_t * pcap;
	bool_t do_feedback = (FEEDBACK_OFF != cfg.feedback);
	bool_t friendly_fire = bfalse;

	if ( !INGAME_CHR( character ) ) return 0;
	pchr = ChrList.lst + character;

	pcap = pro_get_pcap( pchr->iprofile );
	if ( NULL == pcap ) return 0;

	// determine some optional behavior
	if( !INGAME_CHR(attacker) )
	{
		do_feedback = bfalse;
	}
	else
	{
		// do not show feedback for damaging yourself
		if( attacker == character )
		{
			do_feedback = bfalse;
		}

		// identify friendly fire for color selection :)
		if( chr_get_iteam(character) == chr_get_iteam(attacker) )
		{
			friendly_fire = btrue;
		}

		// don't show feedback from random objects hitting each other
		if( !ChrList.lst[attacker].StatusList_on )
		{
			do_feedback = bfalse;
		}

		// don't show damage to players since they get feedback from the status bars
		if( pchr->StatusList_on || pchr->isplayer )
		{
			do_feedback = bfalse;
		}
	}

	actual_damage = 0;
	max_damage = ABS(damage.base) + ABS(damage.rand);
	if ( pchr->alive && damage.base + damage.rand > 0 )
	{
		// Lessen actual_damage for resistance, 0 = Weakness, 1 = Normal, 2 = Resist, 3 = Big Resist
		// This can also be used to lessen effectiveness of healing
		actual_damage = generate_irand_pair( damage );
		base_damage   = actual_damage;
		actual_damage = actual_damage >> ( pchr->damagemodifier[damagetype] & DAMAGESHIFT );

		// Allow actual_damage to be dealt to mana (mana shield spell)
		if ( pchr->damagemodifier[damagetype]&DAMAGEMANA )
		{
			int manadamage;
			manadamage = MAX( pchr->mana - actual_damage, 0 );
			pchr->mana = manadamage;
			actual_damage -= manadamage;
			if ( pchr->ai.index != attacker )
			{
				pchr->ai.alert |= ALERTIF_ATTACKED;
				pchr->ai.attacklast = attacker;
			}
		}

		// Allow charging (Invert actual_damage to mana)
		if ( pchr->damagemodifier[damagetype]&DAMAGECHARGE )
		{
			pchr->mana += actual_damage;
			if ( pchr->mana > pchr->manamax )
			{
				pchr->mana = pchr->manamax;
			}
			return 0;
		}

		// Invert actual_damage to heal
		if ( pchr->damagemodifier[damagetype]&DAMAGEINVERT )
			actual_damage = -actual_damage;

		// Remember the actual_damage type
		pchr->ai.damagetypelast = damagetype;
		pchr->ai.directionlast  = direction;

		// Check for blocking and invictus, no need to continue if they have
		if ( is_invictus_direction( direction, character, effects ) )
		{
			actual_damage = 0;
			spawn_defense_ping( pchr, attacker );
		}

		// Do it already
		if ( actual_damage > pchr->damagethreshold )
		{
			// Only actual_damage if not invincible
			if ( 0 == pchr->damagetime || ignore_invictus )
			{
				// Hard mode deals 25% extra actual damage to players!
				if ( cfg.difficulty >= GAME_HARD && pchr->isplayer && !ChrList.lst[attacker].isplayer ) actual_damage *= 1.25f;

				// Easy mode deals 25% extra actual damage by players and 25% less to players
				if ( cfg.difficulty <= GAME_EASY )
				{
					if ( ChrList.lst[attacker].isplayer && !pchr->isplayer ) actual_damage *= 1.25f;
					if ( !ChrList.lst[attacker].isplayer && pchr->isplayer ) actual_damage *= 0.75f;
				}

				if ( actual_damage != 0 )
				{
					if ( HAS_NO_BITS( DAMFX_ARMO, effects ) )
					{
						actual_damage = ( actual_damage * pchr->defense  * INV_FF );
					}

					pchr->life -= actual_damage;

					// Spawn blud particles
					if ( pcap->blud_valid )
					{
						if ( pcap->blud_valid == ULTRABLUDY || ( base_damage > HURTDAMAGE && damagetype < DAMAGE_HOLY ) )
						{
							spawn_one_particle( pchr->pos, pchr->ori.facing_z + direction, pchr->iprofile, pcap->blud_pip,
								( CHR_REF )MAX_CHR, GRIP_LAST, pchr->team, character, ( PRT_REF )TOTAL_MAX_PRT, 0, ( CHR_REF )MAX_CHR );
						}
					}

					// Set attack alert if it wasn't an accident
					if ( base_damage > HURTDAMAGE )
					{
						if ( team == TEAM_DAMAGE )
						{
							pchr->ai.attacklast = ( CHR_REF )MAX_CHR;
						}
						else
						{
							// Don't alert the character too much if under constant fire
							if ( pchr->carefultime == 0 )
							{
								// Don't let characters chase themselves...  That would be silly
								if ( attacker != character )
								{
									pchr->ai.alert |= ALERTIF_ATTACKED;
									pchr->ai.attacklast = attacker;
									pchr->carefultime = CAREFULTIME;
								}
							}
						}
					}

					// Taking actual_damage action
					if ( pchr->life <= 0 )
					{
						kill_character( character, attacker, ignore_invictus );
					}
					else
					{
						action = ACTION_HA;
						if ( base_damage > HURTDAMAGE )
						{
							action += generate_randmask( 0, 3 );
							chr_play_action( pchr, action, bfalse );

							// Make the character invincible for a limited time only
							if ( HAS_NO_BITS( effects, DAMFX_TIME ) )
							{
								pchr->damagetime = DAMAGETIME;
							}
						}
					}
				}

				/// @test spawn a fly-away damage indicator?
				if ( do_feedback )
				{
					const char * tmpstr;
					int rank;
					
					tmpstr = describe_wounds( pchr->lifemax, pchr->life );

					tmpstr = describe_value( actual_damage, INT_TO_FP8(10), &rank );
					if ( rank < 4 )
					{
						tmpstr = describe_value( actual_damage, max_damage, &rank );
						if( rank < 0 )
						{
							tmpstr = "Fumble!";
						}
						else
						{
							tmpstr = describe_damage( actual_damage, pchr->lifemax, &rank );
							if ( rank >= -1 && rank <= 1 )
							{
								tmpstr = describe_wounds( pchr->lifemax, pchr->life );
							}
						}
					}

					if( NULL != tmpstr )
					{
						const float lifetime = 3;
						STRING text_buffer = EMPTY_CSTR;

						// "white" text
						SDL_Color text_color = {0xFF, 0xFF, 0xFF, 0xFF};

						// friendly fire damage = "purple"
						GLXvector4f tint_friend = { 0.88f, 0.75f, 1.00f, 1.00f };

						// enemy damage = "red"
						GLXvector4f tint_enemy  = { 1.00f, 0.75f, 0.75f, 1.00f };
	
						// write the string into the buffer
						snprintf( text_buffer, SDL_arraysize( text_buffer ), "%s", tmpstr );

						chr_make_text_billboard( character, text_buffer, text_color, friendly_fire ? tint_friend : tint_enemy, lifetime, bb_opt_all );
					}
				}
			}
		}
		else if ( actual_damage < 0 )
		{
			// Heal 'em
			heal_character( character, attacker, actual_damage, ignore_invictus );

			// Isssue an alert
			if ( team == TEAM_DAMAGE )
			{
				pchr->ai.attacklast = ( CHR_REF )MAX_CHR;
			}

			/// @test spawn a fly-away heal indicator?
			if ( do_feedback )
			{
				const float lifetime = 3;
				STRING text_buffer = EMPTY_CSTR;

				// "white" text
				SDL_Color text_color = {0xFF, 0xFF, 0xFF, 0xFF};

				// heal == yellow, right ;)
				GLXvector4f tint = { 1.00f, 1.00f, 0.75f, 1.00f };

				// write the string into the buffer
				snprintf( text_buffer, SDL_arraysize( text_buffer ), "%s", describe_value( -actual_damage, damage.base + damage.rand, NULL ) );

				chr_make_text_billboard( character, text_buffer, text_color, tint, lifetime, bb_opt_all );
			}
		}
	}

	return actual_damage;
}

//--------------------------------------------------------------------------------------------
void spawn_defense_ping( chr_t *pchr, const CHR_REF by_reference attacker )
{
	/// @details ZF@> Spawn a defend particle

	spawn_one_particle_global( pchr->pos, pchr->ori.facing_z, PIP_DEFEND, 0 );

	pchr->damagetime    = DEFENDTIME;
	pchr->ai.alert     |= ALERTIF_BLOCKED;
	pchr->ai.attacklast = attacker;                 // For the ones attacking a shield
}

//--------------------------------------------------------------------------------------------
void spawn_poof( const CHR_REF by_reference character, const PRO_REF by_reference profile )
{
	/// @details ZZ@> This function spawns a character poof

	FACING_T facing_z;
	CHR_REF  origin;
	int      cnt;

	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( character ) ) return;
	pchr = ChrList.lst + character;

	pcap = pro_get_pcap( profile );
	if ( NULL == pcap ) return;

	origin = pchr->ai.owner;
	facing_z   = pchr->ori.facing_z;
	for ( cnt = 0; cnt < pcap->gopoofprt_amount; cnt++ )
	{
		spawn_one_particle( pchr->pos_old, facing_z, profile, pcap->gopoofprt_pip,
			( CHR_REF )MAX_CHR, GRIP_LAST, pchr->team, origin, ( PRT_REF )TOTAL_MAX_PRT, cnt, ( CHR_REF )MAX_CHR );

		facing_z += pcap->gopoofprt_facingadd;
	}
}

//--------------------------------------------------------------------------------------------
void ai_state_spawn( ai_state_t * pself, const CHR_REF by_reference index, const PRO_REF by_reference iobj, Uint16 rank )
{
	chr_t * pchr;
	pro_t * ppro;
	cap_t * pcap;

	pself = ai_state_ctor( pself );

	if ( NULL == pself || !DEFINED_CHR( index ) ) return;
	pchr = ChrList.lst + index;

	// a character cannot be spawned without a valid profile
	if ( !LOADED_PRO( iobj ) ) return;
	ppro = ProList.lst + iobj;

	// a character cannot be spawned without a valid cap
	pcap = pro_get_pcap( iobj );
	if ( NULL == pcap ) return;

	pself->index      = index;
	pself->type       = ppro->iai;
	pself->alert      = ALERTIF_SPAWNED;
	pself->state      = pcap->state_override;
	pself->content    = pcap->content_override;
	pself->passage    = 0;
	pself->target     = index;
	pself->owner      = index;
	pself->child      = index;
	pself->target_old = index;

	pself->bumplast   = index;
	pself->hitlast    = index;

	pself->order_counter = rank;
	pself->order_value   = 0;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
chr_t * chr_config_do_init( chr_t * pchr )
{
	CHR_REF  ichr;
	CAP_REF  icap;
	TEAM_REF loc_team;
	int      tnc, iteam, kursechance;

	cap_t * pcap;
	fvec3_t pos_tmp;

	if ( NULL == pchr ) return NULL;
	ichr = GET_INDEX_PCHR( pchr );

	// can't use chr_get_pcap() because pchr is not a valid character yet
	icap = pro_get_icap( pchr->spawn_data.profile );
	pcap = pro_get_pcap( pchr->spawn_data.profile );

	// make a copy of the data in pchr->spawn_data.pos
	pos_tmp = pchr->spawn_data.pos;

	// download all the values from the character pchr->spawn_data.profile
	chr_download_cap( pchr, pcap );

	// Make sure the pchr->spawn_data.team is valid
	loc_team = pchr->spawn_data.team;
	iteam = REF_TO_INT( loc_team );
	iteam = CLIP( iteam, 0, TEAM_MAX );
	loc_team = ( TEAM_REF )iteam;

	// IMPORTANT!!!
	pchr->missilehandler = ichr;

	// Set up model stuff
	pchr->iprofile  = pchr->spawn_data.profile;
	pchr->basemodel = pchr->spawn_data.profile;

	// Kurse state
	if ( pcap->isitem )
	{
		IPair loc_rand = {1, 100};

		kursechance = pcap->kursechance;
		if ( cfg.difficulty >= GAME_HARD )                        kursechance *= 2.0f;  // Hard mode doubles chance for Kurses
		if ( cfg.difficulty < GAME_NORMAL && kursechance != 100 ) kursechance *= 0.5f;  // Easy mode halves chance for Kurses
		pchr->iskursed = ( generate_irand_pair( loc_rand ) <= kursechance );
	}

	// AI stuff
	ai_state_spawn( &( pchr->ai ), ichr, pchr->iprofile, TeamStack.lst[loc_team].morale );

	// Team stuff
	pchr->team     = loc_team;
	pchr->baseteam = loc_team;
	if ( !pchr->invictus )  TeamStack.lst[loc_team].morale++;

	// Firstborn becomes the leader
	if ( TeamStack.lst[loc_team].leader == NOLEADER )
	{
		TeamStack.lst[loc_team].leader = ichr;
	}

	// Skin
	if ( pcap->skin_override != NO_SKIN_OVERRIDE )
	{
		// override the value passed into the function from spawn.txt
		// with the calue from the expansion in data.txt
		pchr->spawn_data.skin = pchr->skin;
	}
	if ( pchr->spawn_data.skin >= ProList.lst[pchr->spawn_data.profile].skins )
	{
		// place this here so that the random number generator advances
		// no matter the state of ProList.lst[pchr->spawn_data.profile].skins... Eases
		// possible synch problems with other systems?
		int irand = RANDIE;

		pchr->spawn_data.skin = 0;
		if ( 0 != ProList.lst[pchr->spawn_data.profile].skins )
		{
			pchr->spawn_data.skin = irand % ProList.lst[pchr->spawn_data.profile].skins;
		}
	}
	pchr->skin = pchr->spawn_data.skin;

	// fix the pchr->spawn_data.skin-related parameters, in case there was some funy business with overriding
	// the pchr->spawn_data.skin from the data.txt file
	if ( pchr->spawn_data.skin != pchr->skin )
	{
		pchr->skin = pchr->spawn_data.skin;

		pchr->defense = pcap->defense[pchr->skin];
		for ( tnc = 0; tnc < DAMAGE_COUNT; tnc++ )
		{
			pchr->damagemodifier[tnc] = pcap->damagemodifier[tnc][pchr->skin];
		}

		pchr->maxaccel  = pcap->maxaccel[pchr->skin];
	}

	// override the default behavior for an "easy" game
	if ( cfg.difficulty < GAME_NORMAL )
	{
		pchr->life = pchr->lifemax;
		pchr->mana = pchr->manamax;
	}

	// Character size and bumping
	pchr->fat_goto      = pchr->fat;
	pchr->fat_goto_time = 0;

	// Set up position
	pchr->enviro.floor_level = get_mesh_level( PMesh, pos_tmp.x, pos_tmp.y, pchr->waterwalk ) + RAISE;
	pchr->enviro.level       = pchr->enviro.floor_level;
	pchr->enviro.fly_level   = get_mesh_level( PMesh, pos_tmp.x, pos_tmp.y, btrue ) + RAISE;

	if ( 0 != pchr->flyheight && pchr->enviro.fly_level < 0 ) 
	{
		// fly above pits...
		pchr->enviro.fly_level = 0;
	}

	if ( pos_tmp.z < pchr->enviro.floor_level ) pos_tmp.z = pchr->enviro.floor_level;

	chr_set_pos( pchr, pos_tmp.v );

	pchr->pos_stt  = pos_tmp;
	pchr->pos_old  = pos_tmp;

	pchr->ori.facing_z     = pchr->spawn_data.facing;
	pchr->ori_old.facing_z = pchr->ori.facing_z;

	// Name the character
	if ( CSTR_END == pchr->spawn_data.name[0] )
	{
		// Generate a random pchr->spawn_data.name
		snprintf( pchr->Name, SDL_arraysize( pchr->Name ), "%s", pro_create_chop( pchr->spawn_data.profile ) );
	}
	else
	{
		// A pchr->spawn_data.name has been given
		tnc = 0;

		while ( tnc < MAXCAPNAMESIZE - 1 )
		{
			pchr->Name[tnc] = pchr->spawn_data.name[tnc];
			tnc++;
		}

		pchr->Name[tnc] = CSTR_END;
	}

	// Particle attachments
	for ( tnc = 0; tnc < pcap->attachedprt_amount; tnc++ )
	{
		spawn_one_particle( pchr->pos, 0, pchr->iprofile, pcap->attachedprt_pip,
			ichr, GRIP_LAST + tnc, pchr->team, ichr, ( PRT_REF )TOTAL_MAX_PRT, tnc, ( CHR_REF )MAX_CHR );
	}

	// is the object part of a shop's inventory?
	if ( !DEFINED_CHR( pchr->attachedto ) && pchr->isitem && !pchr->pack.is_packed )
	{
		SHOP_REF ishop;

		// Items that are spawned inside shop passages are more expensive than normal
		pchr->isshopitem = bfalse;
		for ( ishop = 0; ishop < ShopStack.count; ishop++ )
		{
			// Make sure the owner is not dead
			if ( SHOP_NOOWNER == ShopStack.lst[ishop].owner ) continue;

			if ( object_is_in_passage( ShopStack.lst[ishop].passage, pchr->pos.x, pchr->pos.y, pchr->bump.size ) )
			{
				pchr->isshopitem = btrue;               // Full value
				pchr->iskursed   = bfalse;              // Shop items are never kursed

				// Identify cheap items in a shop
				if ( chr_get_price( ichr ) <= SHOP_IDENTIFY )
				{
					pchr->nameknown  = btrue;
				}
				break;
			}
		}
	}

	// override the shopitem flag if the item is known to be valuable
	if ( pcap->isvaluable )
	{
		pchr->isshopitem = btrue;
	}

	// initalize the character instance
	chr_spawn_instance( &( pchr->inst ), pchr->spawn_data.profile, pchr->spawn_data.skin );
	chr_update_matrix( pchr, btrue );

	// determine whether the object is hidden
	chr_update_hide( pchr );

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, btrue );

#if defined (USE_DEBUG) && defined(DEBUG_WAYPOINTS)
	if ( DEFINED_CHR( pchr->attachedto ) && INFINITE_WEIGHT != pchr->phys.weight && !pchr->safe_valid )
	{
		log_warning( "spawn_one_character() - \n\tinitial spawn position <%f,%f> is \"inside\" a wall. Wall normal is <%f,%f>\n",
			pchr->pos.x, pchr->pos.y, nrm.x, nrm.y );
	}
#endif

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_do_active( chr_t * pchr )
{
	cap_t * pcap;
	int     ripand;
	CHR_REF ichr;

	if( NULL == pchr ) return pchr;
	ichr = GET_REF_PCHR( pchr );

	//then do status updates
	chr_update_hide( pchr );

	if ( pchr->pack.is_packed || pchr->is_hidden ) return pchr;

	// do the character interaction with water
	pcap = pro_get_pcap( pchr->iprofile );
	if ( NULL == pcap ) return pchr;

	if ( pchr->pos.z < water.surface_level && ( 0 != mesh_test_fx( PMesh, pchr->onwhichgrid, MPDFX_WATER ) ) )
	{
		// do splash and ripple
		if ( !pchr->enviro.inwater )
		{
			// Splash
			fvec3_t vtmp = VECT3( pchr->pos.x, pchr->pos.y, water.surface_level + RAISE );

			spawn_one_particle_global( vtmp, ATK_FRONT, PIP_SPLASH, 0 );

			if ( water.is_water )
			{
				pchr->ai.alert |= ALERTIF_INWATER;
			}
		}
		else
		{
			// Ripples
			if ( pcap->ripple && pchr->pos.z + pchr->chr_chr_cv.maxs[OCT_Z] + RIPPLETOLERANCE > water.surface_level && pchr->pos.z + pchr->chr_chr_cv.mins[OCT_Z] < water.surface_level )
			{
				int ripple_suppression;

				// suppress ripples if we are far below the surface
				ripple_suppression = water.surface_level - ( pchr->pos.z + pchr->chr_chr_cv.maxs[OCT_Z] );
				ripple_suppression = ( 4 * ripple_suppression ) / RIPPLETOLERANCE;
				ripple_suppression = CLIP( ripple_suppression, 0, 4 );

				// make more ripples if we are moving
				ripple_suppression -= (( int )pchr->vel.x != 0 ) | (( int )pchr->vel.y != 0 );

				if ( ripple_suppression > 0 )
				{
					ripand = ~(( ~RIPPLEAND ) << ripple_suppression );
				}
				else
				{
					ripand = RIPPLEAND >> ( -ripple_suppression );
				}

				if ( 0 == (( update_wld + pchr->obj_base.guid ) & ripand ) && pchr->pos.z < water.surface_level && pchr->alive )
				{
					fvec3_t   vtmp = VECT3( pchr->pos.x, pchr->pos.y, water.surface_level );

					spawn_one_particle_global( vtmp, ATK_FRONT, PIP_RIPPLE, 0 );
				}
			}

			if ( water.is_water && HAS_NO_BITS( update_wld, 7 ) )
			{
				pchr->jumpready = btrue;
				pchr->jumpnumber = 1;
			}
		}

		pchr->enviro.inwater  = btrue;
	}
	else
	{
		pchr->enviro.inwater = bfalse;
	}

	// the following functions should not be done the first time through the update loop
	if ( 0 == update_wld ) return pchr;

	// Do timers and such

	// reduce attack cooldowns
	if ( pchr->reloadtime > 0 )
	{
		pchr->reloadtime--;
	}

	// Texture movement
	pchr->inst.uoffset += pchr->uoffvel;
	pchr->inst.voffset += pchr->voffvel;

	if ( !pchr->pack.is_packed )
	{
		// Down that ol' damage timer
		if ( pchr->damagetime > 0 )
		{
			pchr->damagetime--;
		}

		// Do "Be careful!" delay
		if ( pchr->carefultime > 0 )
		{
			pchr->carefultime--;
		}
	}

	// Do stats once every second
	if ( clock_chr_stat >= ONESECOND )
	{
		// check for a level up
		do_level_up( ichr );

		// do the mana and life regen for "living" characters
		if ( pchr->alive )
		{
			int manaregen = 0;
			int liferegen = 0;
			get_chr_regeneration( pchr, &liferegen, &manaregen );

			pchr->mana += manaregen;
			pchr->mana = MAX( 0, MIN( pchr->mana, pchr->manamax ) );

			pchr->life += liferegen;
			pchr->life = MAX( 1, MIN( pchr->life, pchr->lifemax ) );
		}

		// countdown confuse effects
		if ( pchr->grogtime > 0 )
		{
			pchr->grogtime--;
		}

		if ( pchr->dazetime > 0 )
		{
			pchr->dazetime--;
		}

		// possibly gain/lose darkvision
		update_chr_darkvision( ichr );
	}

	pchr = resize_one_character( pchr );

	return pchr;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
chr_t * chr_config_construct( chr_t * pchr, int max_iterations )
{
	int                 iterations;
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// if the character is already beyond this stage, deconstruct it and start over
	if ( pbase->state > ( int )( ego_object_constructing + 1 ) )
	{
		chr_t * tmp_chr = chr_config_deconstruct( pchr, max_iterations );
		if ( tmp_chr == pchr ) return NULL;
	}

	iterations = 0;
	while ( NULL != pchr && pbase->state <= ego_object_constructing && iterations < max_iterations )
	{
		chr_t * ptmp = chr_run_config( pchr );
		if ( ptmp != pchr ) return NULL;
		iterations++;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_initialize( chr_t * pchr, int max_iterations )
{
	int                 iterations;
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// if the character is already beyond this stage, deconstruct it and start over
	if ( pbase->state > ( int )( ego_object_initializing + 1 ) )
	{
		chr_t * tmp_chr = chr_config_deconstruct( pchr, max_iterations );
		if ( tmp_chr == pchr ) return NULL;
	}

	iterations = 0;
	while ( NULL != pchr && pbase->state <= ego_object_initializing && iterations < max_iterations )
	{
		chr_t * ptmp = chr_run_config( pchr );
		if ( ptmp != pchr ) return NULL;
		iterations++;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_activate( chr_t * pchr, int max_iterations )
{
	int                 iterations;
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// if the character is already beyond this stage, deconstruct it and start over
	if ( pbase->state > ( int )( ego_object_active + 1 ) )
	{
		chr_t * tmp_chr = chr_config_deconstruct( pchr, max_iterations );
		if ( tmp_chr == pchr ) return NULL;
	}

	iterations = 0;
	while ( NULL != pchr && pbase->state < ego_object_active && iterations < max_iterations )
	{
		chr_t * ptmp = chr_run_config( pchr );
		if ( ptmp != pchr ) return NULL;
		iterations++;
	}

	assert( pbase->state == ego_object_active );
	if( pbase->state == ego_object_active )
	{
		ChrList_add_used( GET_INDEX_PCHR( pchr ) );
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_deinitialize( chr_t * pchr, int max_iterations )
{
	int                 iterations;
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// if the character is already beyond this stage, deinitialize it
	if ( pbase->state > ( int )( ego_object_deinitializing + 1 ) )
	{
		return pchr;
	}
	else if ( pbase->state < ego_object_deinitializing )
	{
		pbase->state = ego_object_deinitializing;
	}

	iterations = 0;
	while ( NULL != pchr && pbase->state <= ego_object_deinitializing && iterations < max_iterations )
	{
		chr_t * ptmp = chr_run_config( pchr );
		if ( ptmp != pchr ) return NULL;
		iterations++;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_deconstruct( chr_t * pchr, int max_iterations )
{
	int                 iterations;
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// if the character is already beyond this stage, do nothing
	if ( pbase->state > ( int )( ego_object_destructing + 1 ) )
	{
		return pchr;
	}
	else if ( pbase->state < ego_object_deinitializing )
	{
		// make sure that you deinitialize before destructing
		pbase->state = ego_object_deinitializing;
	}

	iterations = 0;
	while ( NULL != pchr && pbase->state <= ego_object_destructing && iterations < max_iterations )
	{
		chr_t * ptmp = chr_run_config( pchr );
		if ( ptmp != pchr ) return NULL;
		iterations++;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
chr_t * chr_run_config( chr_t * pchr )
{
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	// set the object to deinitialize if it is not "dangerous" and if was requested
	if ( pbase->kill_me )
	{
		if ( pbase->state > ego_object_constructing && pbase->state < ego_object_deinitializing )
		{
			pbase->state = ego_object_deinitializing;
		}

		pbase->kill_me = bfalse;
	}

	switch ( pbase->state )
	{
	default:
	case ego_object_invalid:
		pchr = NULL;
		break;

	case ego_object_constructing:
		pchr = chr_config_ctor( pchr );
		break;

	case ego_object_initializing:
		pchr = chr_config_init( pchr );
		break;

	case ego_object_active:
		pchr = chr_config_active( pchr );
		break;

	case ego_object_deinitializing:
		pchr = chr_config_deinit( pchr );
		break;

	case ego_object_destructing:
		pchr = chr_config_dtor( pchr );
		break;

	case ego_object_waiting:
	case ego_object_terminated:
		/* do nothing */
		break;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_ctor( chr_t * pchr )
{
	/// @details BB@> initialize the character data to safe values
	///     since we use memset(..., 0, ...), all = 0, = false, and = 0.0f
	///     statements are redundant

	ego_object_base_t * pbase;

	// grab the base object
	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase ) return NULL;

	// if we aren't in the correct state, abort.
	if ( !STATE_CONSTRUCTING_PBASE( pbase ) ) return pchr;

	pchr = chr_ctor( pchr );
	if( NULL == pchr ) return pchr;

	// we are done constructing. move on to initializing.
	pchr->obj_base.state = ego_object_initializing;

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_init( chr_t * pchr )
{
	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase ) return NULL;

	if ( !STATE_INITIALIZING_PBASE( pbase ) ) return pchr;

	POBJ_BEGIN_SPAWN(pchr);

	pchr = chr_config_do_init( pchr );
	if ( NULL == pchr ) return NULL;

	if ( 0 == chr_loop_depth )
	{
		pchr->obj_base.on = btrue;
	}
	else
	{
		ChrList_add_activation( GET_INDEX_PPRT( pchr ) );
	}

	pbase->state = ego_object_active;

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_active( chr_t * pchr )
{
	// there's nothing to configure if the object is active...

	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase || !pbase->allocated ) return NULL;

	if ( !STATE_ACTIVE_PBASE( pbase ) ) return pchr;

	POBJ_END_SPAWN( pchr );

	pchr = chr_config_do_active( pchr );

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_deinit( chr_t * pchr )
{
	/// @details BB@> deinitialize the character data

	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase ) return NULL;

	if ( !STATE_DEINITIALIZING_PBASE( pbase ) ) return pchr;

	POBJ_END_SPAWN( pchr );

	pbase->state = ego_object_destructing;
	pbase->on    = bfalse;

	return pchr;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_config_dtor( chr_t * pchr )
{
	/// @details BB@> deinitialize the character data

	ego_object_base_t * pbase;

	pbase = POBJ_GET_PBASE( pchr );
	if ( NULL == pbase ) return NULL;

	if ( !STATE_DESTRUCTING_PBASE( pbase ) ) return pchr;

	POBJ_END_SPAWN( pchr );

	return chr_dtor( pchr );
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
CHR_REF spawn_one_character( fvec3_t pos, const PRO_REF by_reference profile, const TEAM_REF by_reference team,
							Uint8 skin, FACING_T facing, const char *name, const CHR_REF by_reference override )
{
	/// @details ZZ@> This function spawns a character and returns the character's index number
	///               if it worked, MAX_CHR otherwise

	CHR_REF   ichr;
	chr_t   * pchr;

	// fix a "bad" name
	if ( NULL == name ) name = "";

	if ( profile >= MAX_PROFILE )
	{
		log_warning( "spawn_one_character() - profile value too large %d out of %d\n", REF_TO_INT( profile ), MAX_PROFILE );
		return ( CHR_REF )MAX_CHR;
	}

	if ( !LOADED_PRO( profile ) )
	{
		if ( profile > PMod->importamount * MAXIMPORTPERPLAYER )
		{
			log_warning( "spawn_one_character() - trying to spawn using invalid profile %d\n", REF_TO_INT( profile ) );
		}
		return ( CHR_REF )MAX_CHR;
	}

	// allocate a new character
	ichr = ChrList_allocate( override );
	if ( !DEFINED_CHR( ichr ) )
	{
		log_warning( "spawn_one_character() - failed to spawn character (invalid index number %d?)\n", REF_TO_INT( ichr ) );
		return ( CHR_REF )MAX_CHR;
	}

	pchr = ChrList.lst + ichr;

	// just set the spawn info
	pchr->spawn_data.pos      = pos;
	pchr->spawn_data.profile  = profile;
	pchr->spawn_data.team     = team;
	pchr->spawn_data.skin     = skin;
	pchr->spawn_data.facing   = facing;
	strncpy( pchr->spawn_data.name, name, SDL_arraysize( pchr->spawn_data.name ) );
	pchr->spawn_data.override = override;

	// actually force the character to spawn
	chr_config_activate( pchr, 100 );

#ifdef DEBUG_OBJECT_SPAWN
	{
		CAP_REF icap = pro_get_icap( profile );
		log_debug("spawn_one_character() - slot: %i, index: %i, name: %s, class: %s\n", REF_TO_INT( profile ), REF_TO_INT( ichr ), name, CapStack.lst[icap].classname);
	}
#endif

	return ichr;
}

//--------------------------------------------------------------------------------------------
void respawn_character( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function respawns a character

	CHR_REF item;
	int old_attached_prt_count, new_attached_prt_count;

	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( character ) || ChrList.lst[character].alive ) return;
	pchr = ChrList.lst + character;

	pcap = chr_get_pcap( character );
	if ( NULL == pcap ) return;

	old_attached_prt_count = number_of_attached_particles( character );

	spawn_poof( character, pchr->iprofile );
	disaffirm_attached_particles( character );

	pchr->alive = btrue;
	pchr->boretime = BORETIME;
	pchr->carefultime = CAREFULTIME;
	pchr->life = pchr->lifemax;
	pchr->mana = pchr->manamax;
	chr_set_pos( pchr, pchr->pos_stt.v );
	pchr->vel.x = 0;
	pchr->vel.y = 0;
	pchr->vel.z = 0;
	pchr->team = pchr->baseteam;
	pchr->canbecrushed = bfalse;
	pchr->ori.map_facing_y = MAP_TURN_OFFSET;  // These two mean on level surface
	pchr->ori.map_facing_x = MAP_TURN_OFFSET;
	if ( NOLEADER == TeamStack.lst[pchr->team].leader )  TeamStack.lst[pchr->team].leader = character;
	if ( !pchr->invictus )  TeamStack.lst[pchr->baseteam].morale++;

	// start the character out in the "dance" animation
	chr_start_anim( pchr, ACTION_DA, btrue, btrue );

	// reset all of the bump size information
	{
		float old_fat = pchr->fat;
		chr_init_size( pchr, pcap );
		chr_set_fat( pchr, old_fat );
	}

	pchr->platform        = pcap->platform;
	pchr->canuseplatforms = pcap->canuseplatforms;
	pchr->flyheight       = pcap->flyheight;
	pchr->phys.bumpdampen = pcap->bumpdampen;

	pchr->ai.alert = ALERTIF_CLEANEDUP;
	pchr->ai.target = character;
	pchr->ai.timer  = 0;

	pchr->grogtime = 0;
	pchr->dazetime = 0;

	// Let worn items come back
	PACK_BEGIN_LOOP( item, pchr->pack.next )
	{
		if ( INGAME_CHR( item ) && ChrList.lst[item].isequipped )
		{
			ChrList.lst[item].isequipped = bfalse;
			chr_get_pai( item )->alert |= ALERTIF_PUTAWAY; // same as ALERTIF_ATLASTWAYPOINT
		}
	}
	PACK_END_LOOP( item );

	// re-initialize the instance
	chr_spawn_instance( &( pchr->inst ), pchr->iprofile, pchr->skin );
	chr_update_matrix( pchr, btrue );

	// determine whether the object is hidden
	chr_update_hide( pchr );

	if ( !pchr->is_hidden )
	{
		reaffirm_attached_particles( character );
		new_attached_prt_count = number_of_attached_particles( character );
	}

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, btrue );
}

//--------------------------------------------------------------------------------------------
int chr_change_skin( const CHR_REF by_reference character, int skin )
{
	chr_t * pchr;
	pro_t * ppro;
	mad_t * pmad;
	chr_instance_t * pinst;

	if ( !INGAME_CHR( character ) ) return 0;
	pchr  = ChrList.lst + character;
	pinst = &( pchr->inst );

	ppro = chr_get_ppro( character );

	pmad = pro_get_pmad( pchr->iprofile );
	if ( NULL == pmad )
	{
		// make sure that the instance has a valid imad
		if ( NULL != ppro && !LOADED_MAD( pinst->imad ) )
		{
			if ( chr_instance_set_mad( pinst, ppro->imad ) )
			{
				chr_update_collision_size( pchr, btrue );
			}
			pmad = chr_get_pmad( character );
		}
	}

	if ( NULL == pmad )
	{
		pchr->skin     = 0;
		pinst->texture = TX_WATER_TOP;
	}
	else
	{
		TX_REF txref = ( TX_REF )TX_WATER_TOP;

		// do the best we can to change the skin
		if ( NULL == ppro || 0 == ppro->skins )
		{
			ppro->skins = 1;
			ppro->tex_ref[0] = TX_WATER_TOP;

			skin  = 0;
			txref = TX_WATER_TOP;
		}
		else
		{
			if ( skin > ppro->skins )
			{
				skin = 0;
			}

			txref = ppro->tex_ref[skin];
		}

		pchr->skin     = skin;
		pinst->texture = txref;
	}

	// If the we are respawning a player, then the camera needs to be reset
	if ( pchr->isplayer )
	{
		camera_reset_target( PCamera, PMesh );
	}

	return pchr->skin;
}

//--------------------------------------------------------------------------------------------
int change_armor( const CHR_REF by_reference character, int skin )
{
	/// @details ZZ@> This function changes the armor of the character

	ENC_REF enchant;
	int     iTmp;
	cap_t * pcap;
	chr_t * pchr;

	if ( !INGAME_CHR( character ) ) return 0;
	pchr = ChrList.lst + character;

	// cleanup the enchant list
	cleanup_character_enchants( pchr );

	// Remove armor enchantments
	enchant = pchr->firstenchant;
	while ( enchant < MAX_ENC )
	{
		enchant_remove_set( enchant, SETSLASHMODIFIER );
		enchant_remove_set( enchant, SETCRUSHMODIFIER );
		enchant_remove_set( enchant, SETPOKEMODIFIER );
		enchant_remove_set( enchant, SETHOLYMODIFIER );
		enchant_remove_set( enchant, SETEVILMODIFIER );
		enchant_remove_set( enchant, SETFIREMODIFIER );
		enchant_remove_set( enchant, SETICEMODIFIER );
		enchant_remove_set( enchant, SETZAPMODIFIER );

		enchant = EncList.lst[enchant].nextenchant_ref;
	}

	// Change the skin
	pcap = chr_get_pcap( character );
	skin = chr_change_skin( character, skin );

	// Change stats associated with skin
	pchr->defense = pcap->defense[skin];

	for ( iTmp = 0; iTmp < DAMAGE_COUNT; iTmp++ )
	{
		pchr->damagemodifier[iTmp] = pcap->damagemodifier[iTmp][skin];
	}

	pchr->maxaccel = pcap->maxaccel[skin];

	// cleanup the enchant list
	cleanup_character_enchants( pchr );

	// Reset armor enchantments
	/// @todo These should really be done in reverse order ( Start with last enchant ), but
	/// I don't care at this point !!!BAD!!!
	enchant = pchr->firstenchant;
	while ( enchant < MAX_ENC )
	{
		PRO_REF ipro = enc_get_ipro( enchant );

		if ( LOADED_PRO( ipro ) )
		{
			EVE_REF ieve = pro_get_ieve( ipro );

			enchant_apply_set( enchant, SETSLASHMODIFIER, ipro );
			enchant_apply_set( enchant, SETCRUSHMODIFIER, ipro );
			enchant_apply_set( enchant, SETPOKEMODIFIER,  ipro );
			enchant_apply_set( enchant, SETHOLYMODIFIER,  ipro );
			enchant_apply_set( enchant, SETEVILMODIFIER,  ipro );
			enchant_apply_set( enchant, SETFIREMODIFIER,  ipro );
			enchant_apply_set( enchant, SETICEMODIFIER,   ipro );
			enchant_apply_set( enchant, SETZAPMODIFIER,   ipro );

			enchant_apply_add( enchant, ADDACCEL,         ieve );
			enchant_apply_add( enchant, ADDDEFENSE,       ieve );
		}

		enchant = EncList.lst[enchant].nextenchant_ref;
	}

	return skin;
}

//--------------------------------------------------------------------------------------------
void change_character_full( const CHR_REF by_reference ichr, const PRO_REF by_reference profile, Uint8 skin, Uint8 leavewhich )
{
	/// @details ZF@> This function polymorphs a character permanently so that it can be exported properly
	/// A character turned into a frog with this function will also export as a frog!

	MAD_REF imad_old, imad_new;

	if ( !LOADED_PRO( profile ) ) return;

	imad_new = pro_get_imad( profile );
	if ( !LOADED_MAD( imad_new ) ) return;

	imad_old = chr_get_imad( ichr );
	if ( !LOADED_MAD( imad_old ) ) return;

	// copy the new name
	strncpy( MadStack.lst[imad_old].name, MadStack.lst[imad_new].name, SDL_arraysize( MadStack.lst[imad_old].name ) );

	// change their model
	change_character( ichr, profile, skin, leavewhich );

	// set the base model to the new model, too
	ChrList.lst[ichr].basemodel = profile;
}

//--------------------------------------------------------------------------------------------
bool_t set_weapongrip( const CHR_REF by_reference iitem, const CHR_REF by_reference iholder, Uint16 vrt_off )
{
	int i;

	bool_t needs_update;
	Uint16 grip_verts[GRIP_VERTS];

	matrix_cache_t * mcache;
	chr_t * pitem;

	needs_update = bfalse;

	if ( !INGAME_CHR( iitem ) ) return bfalse;
	pitem = ChrList.lst + iitem;
	mcache = &( pitem->inst.matrix_cache );

	// is the item attached to this valid holder?
	if ( pitem->attachedto != iholder ) return bfalse;

	needs_update  = btrue;

	if ( GRIP_VERTS == get_grip_verts( grip_verts, iholder, vrt_off ) )
	{
		//---- detect any changes in the matrix_cache data

		needs_update  = bfalse;

		if ( iholder != mcache->grip_chr || pitem->attachedto != iholder )
		{
			needs_update  = btrue;
		}

		if ( pitem->inwhich_slot != mcache->grip_slot )
		{
			needs_update  = btrue;
		}

		// check to see if any of the
		for ( i = 0; i < GRIP_VERTS; i++ )
		{
			if ( grip_verts[i] != mcache->grip_verts[i] )
			{
				needs_update = btrue;
				break;
			}
		}
	}

	if ( needs_update )
	{
		// cannot create the matrix, therefore the current matrix must be invalid
		mcache->matrix_valid = bfalse;

		mcache->grip_chr  = iholder;
		mcache->grip_slot = pitem->inwhich_slot;

		for ( i = 0; i < GRIP_VERTS; i++ )
		{
			mcache->grip_verts[i] = grip_verts[i];
		}
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
void change_character( const CHR_REF by_reference ichr, const PRO_REF by_reference profile_new, Uint8 skin, Uint8 leavewhich )
{
	/// @details ZZ@> This function polymorphs a character, changing stats, dropping weapons

	int tnc;
	ENC_REF enchant;
	CHR_REF item_ref, item;
	chr_t * pchr;

	pro_t * pobj_new;
	cap_t * pcap_new;
	mad_t * pmad_new;

	int old_attached_prt_count, new_attached_prt_count;

	if ( !LOADED_PRO( profile_new ) || !INGAME_CHR( ichr ) ) return;
	pchr = ChrList.lst + ichr;

	old_attached_prt_count = number_of_attached_particles( ichr );

	if ( !LOADED_PRO( profile_new ) ) return;
	pobj_new = ProList.lst + profile_new;

	pcap_new = pro_get_pcap( profile_new );
	pmad_new = pro_get_pmad( profile_new );

	// Drop left weapon
	item_ref = pchr->holdingwhich[SLOT_LEFT];
	if ( INGAME_CHR( item_ref ) && ( !pcap_new->slotvalid[SLOT_LEFT] || pcap_new->ismount ) )
	{
		detach_character_from_mount( item_ref, btrue, btrue );
		if ( pchr->ismount )
		{
			fvec3_t tmp_pos;

			ChrList.lst[item_ref].vel.z    = DISMOUNTZVEL;
			ChrList.lst[item_ref].jumptime = JUMPDELAY;

			tmp_pos = chr_get_pos( ChrList.lst + item_ref );
			tmp_pos.z += DISMOUNTZVEL;
			chr_set_pos( ChrList.lst + item_ref, tmp_pos.v );
		}
	}

	// Drop right weapon
	item_ref = pchr->holdingwhich[SLOT_RIGHT];
	if ( INGAME_CHR( item_ref ) && !pcap_new->slotvalid[SLOT_RIGHT] )
	{
		detach_character_from_mount( item_ref, btrue, btrue );
		if ( pchr->ismount )
		{
			fvec3_t tmp_pos;

			ChrList.lst[item_ref].vel.z    = DISMOUNTZVEL;
			ChrList.lst[item_ref].jumptime = JUMPDELAY;

			tmp_pos = chr_get_pos( ChrList.lst + item_ref );
			tmp_pos.z += DISMOUNTZVEL;
			chr_set_pos( ChrList.lst + item_ref, tmp_pos.v );
		}
	}

	// Remove particles
	disaffirm_attached_particles( ichr );

	// clean up the enchant list before doing anything
	cleanup_character_enchants( pchr );

	// Remove enchantments
	if ( leavewhich == ENC_LEAVE_FIRST )
	{
		// cleanup the enchant list
		cleanup_character_enchants( pchr );

		// Remove all enchantments except top one
		enchant = pchr->firstenchant;
		if ( enchant != MAX_ENC )
		{
			enchant = EncList.lst[enchant].nextenchant_ref;
			while ( MAX_ENC != enchant )
			{
				remove_enchant( enchant, NULL );

				enchant = EncList.lst[enchant].nextenchant_ref;
			}
		}
	}
	else if ( ENC_LEAVE_NONE == leavewhich )
	{
		// Remove all enchantments
		disenchant_character( ichr );
	}

	// Stuff that must be set
	pchr->iprofile  = profile_new;
	pchr->stoppedby = pcap_new->stoppedby;
	pchr->life_heal  = pcap_new->life_heal;
	pchr->manacost  = pcap_new->manacost;

	// Ammo
	pchr->ammomax = pcap_new->ammomax;
	pchr->ammo    = pcap_new->ammo;

	// Gender
	if ( pcap_new->gender != GENDER_RANDOM )  // GENDER_RANDOM means keep old gender
	{
		pchr->gender = pcap_new->gender;
	}

	// Sound effects
	for ( tnc = 0; tnc < SOUND_COUNT; tnc++ )
	{
		pchr->sound_index[tnc] = pcap_new->sound_index[tnc];
	}

	// AI stuff
	pchr->ai.type           = pobj_new->iai;
	pchr->ai.state          = 0;
	pchr->ai.timer          = 0;
	pchr->turnmode          = TURNMODE_VELOCITY;

	latch_init( &( pchr->latch ) );

	// Flags
	pchr->stickybutt      = pcap_new->stickybutt;
	pchr->openstuff       = pcap_new->canopenstuff;
	pchr->transferblend   = pcap_new->transferblend;
	pchr->platform        = pcap_new->platform;
	pchr->canuseplatforms = pcap_new->canuseplatforms;
	pchr->isitem          = pcap_new->isitem;
	pchr->invictus        = pcap_new->invictus;
	pchr->ismount         = pcap_new->ismount;
	pchr->cangrabmoney    = pcap_new->cangrabmoney;
	pchr->jumptime        = JUMPDELAY;
	pchr->alpha_base       = pcap_new->alpha;
	pchr->light_base       = pcap_new->light;

	// change the skillz, too, jack!
	pchr->canjoust              = pcap_new->canjoust;
	pchr->canuseadvancedweapons = pcap_new->canuseadvancedweapons;
	pchr->shieldproficiency     = pcap_new->shieldproficiency;
	pchr->canusedivine          = pcap_new->canusedivine;
	pchr->canusearcane          = pcap_new->canusearcane;
	pchr->canusetech            = pcap_new->canusetech;
	pchr->candisarm             = pcap_new->candisarm;
	pchr->canbackstab           = pcap_new->canbackstab;
	pchr->canusepoison          = pcap_new->canusepoison;
	pchr->canread               = pcap_new->canread;
	pchr->canseekurse           = pcap_new->canseekurse;
	pchr->hascodeofconduct      = pcap_new->hascodeofconduct;
	pchr->darkvision_level      = pcap_new->darkvision_level;

	/// @note BB@> changing this could be disasterous, in case you can't un-morph youself???
	/// pchr->canusearcane          = pcap_new->canusearcane;
	/// @note ZF@> No, we want this, I have specifically scripted morph books to handle unmorphing
	/// even if you cannot cast arcane spells. Some morph spells specifically morph the player
	/// into a fighter or a tech user, but as a balancing factor prevents other spellcasting.

	// Character size and bumping
	// set the character size so that the new model is the same size as the old model
	// the model will then morph its size to the correct size over time
	{
		float old_fat = pchr->fat;
		float new_fat;

		if ( 0.0f == pchr->bump.size )
		{
			new_fat = pcap_new->size;
		}
		else
		{
			new_fat = ( pcap_new->bump_size * pcap_new->size ) / pchr->bump.size;
		}

		// Spellbooks should stay the same size, even if their spell effect cause changes in size
		if ( pchr->iprofile == SPELLBOOK ) new_fat = old_fat = 1.00f;

		// copy all the cap size info over, as normal
		chr_init_size( pchr, pcap_new );

		// make the model's size congruent
		if ( 0.0f != new_fat && new_fat != old_fat )
		{
			chr_set_fat( pchr, new_fat );
			pchr->fat_goto      = old_fat;
			pchr->fat_goto_time = SIZETIME;
		}
		else
		{
			chr_set_fat( pchr, old_fat );
			pchr->fat_goto      = old_fat;
			pchr->fat_goto_time = 0;
		}
	}

	//Physics
	pchr->phys.bumpdampen   = pcap_new->bumpdampen;
	pchr->holdingweight     = 0;
	pchr->onwhichplatform   = ( CHR_REF )MAX_CHR;

	if ( pcap_new->weight == 0xFF )
	{
		pchr->phys.weight = INFINITE_WEIGHT;
	}
	else
	{
		Uint32 itmp = pcap_new->weight * pchr->fat * pchr->fat * pchr->fat;
		pchr->phys.weight = MIN( itmp, MAX_WEIGHT );
	}

	// Image rendering
	pchr->uoffvel = pcap_new->uoffvel;
	pchr->voffvel = pcap_new->voffvel;

	// Movement
	pchr->sneakspd = pcap_new->sneakspd;
	pchr->walkspd  = pcap_new->walkspd;
	pchr->runspd   = pcap_new->runspd;

	// initialize the instance
	chr_spawn_instance( &( pchr->inst ), profile_new, skin );
	chr_update_matrix( pchr, btrue );

	// Action stuff that must be down after chr_spawn_instance()
	pchr->inst.action_ready = bfalse;
	pchr->inst.action_keep  = bfalse;
	pchr->inst.action_loop  = bfalse;
	if ( pchr->alive )
	{
		chr_play_action( pchr, ACTION_DA, bfalse );
	}
	else
	{
		chr_play_action( pchr, ACTION_KA + generate_randmask( 0, 3 ), bfalse );
		pchr->inst.action_keep = btrue;
	}

	// Set the skin after changing the model in chr_spawn_instance()
	change_armor( ichr, skin );

	// Must set the wepon grip AFTER the model is changed in chr_spawn_instance()
	if ( INGAME_CHR( pchr->attachedto ) )
	{
		set_weapongrip( ichr, pchr->attachedto, slot_to_grip_offset( pchr->inwhich_slot ) );
	}

	item = pchr->holdingwhich[SLOT_LEFT];
	if ( INGAME_CHR( item ) )
	{
		EGOBOO_ASSERT( ChrList.lst[item].attachedto == ichr );
		set_weapongrip( item, ichr, GRIP_LEFT );
	}

	item = pchr->holdingwhich[SLOT_RIGHT];
	if ( INGAME_CHR( item ) )
	{
		EGOBOO_ASSERT( ChrList.lst[item].attachedto == ichr );
		set_weapongrip( item, ichr, GRIP_RIGHT );
	}

	// determine whether the object is hidden
	chr_update_hide( pchr );

	// Reaffirm them particles...
	pchr->reaffirmdamagetype = pcap_new->attachedprt_reaffirmdamagetype;
	//reaffirm_attached_particles( ichr );              /// @note ZF@> so that books dont burn when dropped
	new_attached_prt_count = number_of_attached_particles( ichr );

	ai_state_set_changed( &( pchr->ai ) );

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, btrue );
}

//--------------------------------------------------------------------------------------------
bool_t cost_mana( const CHR_REF by_reference character, int amount, const CHR_REF by_reference killer )
{
	/// @details ZZ@> This function takes mana from a character ( or gives mana ),
	///    and returns btrue if the character had enough to pay, or bfalse
	///    otherwise. This can kill a character in hard mode.

	int mana_final;
	bool_t mana_paid;

	chr_t * pchr;

	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	mana_paid  = bfalse;
	mana_final = pchr->mana - amount;

	if ( mana_final < 0 )
	{
		int mana_debt = -mana_final;

		pchr->mana = 0;

		if ( pchr->canchannel )
		{
			pchr->life -= mana_debt;

			if ( pchr->life <= 0 && cfg.difficulty >= GAME_HARD )
			{
				kill_character( character, !INGAME_CHR( killer ) ? character : killer, bfalse );
			}

			mana_paid = btrue;
		}
	}
	else
	{
		int mana_surplus = 0;

		pchr->mana = mana_final;

		if ( mana_final > pchr->manamax )
		{
			mana_surplus = mana_final - pchr->manamax;
			pchr->mana   = pchr->manamax;
		}

		// allow surplus mana to go to health if you can channel?
		if ( pchr->canchannel && mana_surplus > 0 )
		{
			// use some factor, divide by 2
			heal_character( GET_REF_PCHR( pchr ), killer, mana_surplus << 1, btrue );
		}

		mana_paid = btrue;

	}

	return mana_paid;
}

//--------------------------------------------------------------------------------------------
void switch_team( const CHR_REF by_reference character, const TEAM_REF by_reference team )
{
	/// @details ZZ@> This function makes a character join another team...

	if ( !INGAME_CHR( character ) || team >= TEAM_MAX ) return;

	if ( !ChrList.lst[character].invictus )
	{
		if ( chr_get_pteam_base( character )->morale > 0 ) chr_get_pteam_base( character )->morale--;
		TeamStack.lst[team].morale++;
	}
	if (( !ChrList.lst[character].ismount || !INGAME_CHR( ChrList.lst[character].holdingwhich[SLOT_LEFT] ) ) &&
		( !ChrList.lst[character].isitem  || !INGAME_CHR( ChrList.lst[character].attachedto ) ) )
	{
		ChrList.lst[character].team = team;
	}

	ChrList.lst[character].baseteam = team;
	if ( TeamStack.lst[team].leader == NOLEADER )
	{
		TeamStack.lst[team].leader = character;
	}
}

//--------------------------------------------------------------------------------------------
void issue_clean( const CHR_REF by_reference character )
{
	/// @details ZZ@> This function issues a clean up order to all teammates

	TEAM_REF team;

	if ( !INGAME_CHR( character ) ) return;

	team = chr_get_iteam( character );

	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		if ( team != chr_get_iteam( cnt ) ) continue;

		if ( !pchr->alive )
		{
			pchr->ai.timer  = update_wld + 2;  // Don't let it think too much...
		}

		pchr->ai.alert |= ALERTIF_CLEANEDUP;
	}
	CHR_END_LOOP();
}

//--------------------------------------------------------------------------------------------
int restock_ammo( const CHR_REF by_reference character, IDSZ idsz )
{
	/// @details ZZ@> This function restocks the characters ammo, if it needs ammo and if
	///    either its parent or type idsz match the given idsz.  This
	///    function returns the amount of ammo given.

	int amount;

	chr_t * pchr;

	if ( !INGAME_CHR( character ) ) return 0;
	pchr = ChrList.lst + character;

	amount = 0;
	if ( chr_is_type_idsz( character, idsz ) )
	{
		if ( ChrList.lst[character].ammo < ChrList.lst[character].ammomax )
		{
			amount = ChrList.lst[character].ammomax - ChrList.lst[character].ammo;
			ChrList.lst[character].ammo = ChrList.lst[character].ammomax;
		}
	}

	return amount;
}

//--------------------------------------------------------------------------------------------
int check_skills( const CHR_REF by_reference who, IDSZ whichskill )
{
	// @details ZF@> This checks if the specified character has the required skill.
	//               Also checks Skill expansions. Returns the level of the skill.

	int result = 0;

	//Any [NONE] IDSZ returns always "true"
	if ( MAKE_IDSZ( 'N', 'O', 'N', 'E' ) == whichskill ) return 1;

	// First check the character Skill ID matches
	// Then check for expansion skills too.
	if ( chr_get_idsz( who, IDSZ_SKILL )  == whichskill ) result = 1;
	else if ( MAKE_IDSZ( 'A', 'W', 'E', 'P' ) == whichskill ) result = ChrList.lst[who].canuseadvancedweapons;
	else if ( MAKE_IDSZ( 'C', 'K', 'U', 'R' ) == whichskill ) result = ChrList.lst[who].canseekurse;
	else if ( MAKE_IDSZ( 'J', 'O', 'U', 'S' ) == whichskill ) result = ChrList.lst[who].canjoust;
	else if ( MAKE_IDSZ( 'S', 'H', 'P', 'R' ) == whichskill ) result = ChrList.lst[who].shieldproficiency;
	else if ( MAKE_IDSZ( 'T', 'E', 'C', 'H' ) == whichskill ) result = ChrList.lst[who].canusetech;
	else if ( MAKE_IDSZ( 'W', 'M', 'A', 'G' ) == whichskill ) result = ChrList.lst[who].canusearcane;
	else if ( MAKE_IDSZ( 'H', 'M', 'A', 'G' ) == whichskill ) result = ChrList.lst[who].canusedivine;
	else if ( MAKE_IDSZ( 'D', 'I', 'S', 'A' ) == whichskill ) result = ChrList.lst[who].candisarm;
	else if ( MAKE_IDSZ( 'S', 'T', 'A', 'B' ) == whichskill ) result = ChrList.lst[who].canbackstab;
	else if ( MAKE_IDSZ( 'R', 'E', 'A', 'D' ) == whichskill ) result = ChrList.lst[who].canread + ( ChrList.lst[who].see_invisible_level && ChrList.lst[who].canseekurse ? 1 : 0 ); // Truesight allows reading
	else if ( MAKE_IDSZ( 'P', 'O', 'I', 'S' ) == whichskill && !ChrList.lst[who].hascodeofconduct ) result = ChrList.lst[who].canusepoison;                                //Only if not restriced by code of conduct
	else if ( MAKE_IDSZ( 'C', 'O', 'D', 'E' ) == whichskill ) result = ChrList.lst[who].hascodeofconduct;
	else if ( MAKE_IDSZ( 'D', 'A', 'R', 'K' ) == whichskill ) result = ChrList.lst[who].darkvision_level;

	return result;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
bool_t update_chr_darkvision( const CHR_REF by_reference character )
{
	/// @detalis BB@> as an offset to negative status effects like things like poisoning, a
	///               character gains darkvision ability the more they are "poisoned".
	///               True poisoning can be removed by [HEAL] and tints the character green
	eve_t * peve;
	ENC_REF enc_now, enc_next;
	int life_regen = 0;

	chr_t * pchr;

	if ( !INGAME_CHR( character ) ) return bfalse;
	pchr = ChrList.lst + character;

	// cleanup the enchant list
	cleanup_character_enchants( pchr );

	// grab the life loss due poison to determine how much darkvision a character has earned, he he he!
	// clean up the enchant list before doing anything
	enc_now = pchr->firstenchant;
	while ( enc_now != MAX_ENC )
	{
		enc_next = EncList.lst[enc_now].nextenchant_ref;
		peve = enc_get_peve( enc_now );

		//Is it true poison?
		if ( NULL != peve && MAKE_IDSZ( 'H', 'E', 'A', 'L' ) == peve->removedbyidsz )
		{
			life_regen += EncList.lst[enc_now].target_life;
			if ( EncList.lst[enc_now].owner_ref == pchr->ai.index ) life_regen += EncList.lst[enc_now].owner_life;
		}

		enc_now = enc_next;
	}

	if ( life_regen < 0 )
	{
		int tmp_level = ( 10 * -life_regen ) / pchr->lifemax;
		pchr->darkvision_level = MAX( pchr->darkvision_level_base, tmp_level );
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
void update_all_characters()
{
	/// @details ZZ@> This function updates stats and such for every character

	CHR_REF ichr;

	for ( ichr = 0; ichr < MAX_CHR; ichr++ )
	{
		chr_run_config( ChrList.lst + ichr );
	}

	// fix the stat timer
	if ( clock_chr_stat >= ONESECOND )
	{
		// Reset the clock
		clock_chr_stat -= ONESECOND;
	}

}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
void move_one_character_get_environment( chr_t * pchr )
{
	Uint32 itile;
	float floor_level;

	if ( !ACTIVE_PCHR( pchr ) ) return;

	//---- character "floor" level
	//floor_level = get_chr_level( PMesh, pchr );
	floor_level = get_mesh_level( PMesh, pchr->pos.x, pchr->pos.y, pchr->waterwalk ) + RAISE;

	// use this function so that the reflections and some other stuff comes out correctly
	// @note - using get_mesh_level() will actually force a water tile to have a reflection?
	chr_set_floor_level( pchr, floor_level );

	//---- The actual level of the characer.
	//     Estimate platform attachment from whatever is in the onwhichplatform variable from the
	//     last loop
	pchr->enviro.level = pchr->enviro.floor_level;
	if ( INGAME_CHR( pchr->onwhichplatform ) )
	{
		pchr->enviro.level     = ChrList.lst[pchr->onwhichplatform].pos.z + ChrList.lst[pchr->onwhichplatform].chr_chr_cv.maxs[OCT_Z];
		pchr->enviro.fly_level = MAX( pchr->enviro.fly_level, pchr->enviro.level );

	}

	if ( 0 != pchr->flyheight && pchr->enviro.fly_level < 0 )
	{
		pchr->enviro.fly_level = 0;  // fly above pits...
	}

	// set the zlerp after we have done everything to the particle's level we care to
	pchr->enviro.zlerp = ( pchr->pos.z - pchr->enviro.level ) / PLATTOLERANCE;
	pchr->enviro.zlerp = CLIP( pchr->enviro.zlerp, 0, 1 );

	pchr->enviro.grounded = ( 0 == pchr->flyheight ) && ( pchr->enviro.zlerp < 0.25f );

	//---- the "twist" of the floor
	pchr->enviro.twist = TWIST_FLAT;
	itile          = INVALID_TILE;
	if ( INGAME_CHR( pchr->onwhichplatform ) )
	{
		// this only works for 1 level of attachment
		itile = ChrList.lst[pchr->onwhichplatform].onwhichgrid;
	}
	else
	{
		itile = pchr->onwhichgrid;
	}

	if ( mesh_grid_is_valid( PMesh, itile ) )
	{
		pchr->enviro.twist = PMesh->gmem.grid_list[itile].twist;
	}

	// the "watery-ness" of whatever water might be here
	pchr->enviro.is_watery = water.is_water && pchr->enviro.inwater;
	pchr->enviro.is_slippy = !pchr->enviro.is_watery && ( 0 != mesh_test_fx( PMesh, pchr->onwhichgrid, MPDFX_SLIPPY ) );

	//---- traction
	pchr->enviro.traction = 1.0f;
	if ( 0 != pchr->flyheight )
	{
		// any traction factor here
		/* traction = ??; */
	}
	else if ( INGAME_CHR( pchr->onwhichplatform ) )
	{
		// in case the platform is tilted
		// unfortunately platforms are attached in the collision section
		// which occurs after the movement section.

		fvec3_t   platform_up;

		chr_getMatUp( ChrList.lst + pchr->onwhichplatform, &platform_up );
		platform_up = fvec3_normalize( platform_up.v );

		pchr->enviro.traction = ABS( platform_up.z ) * ( 1.0f - pchr->enviro.zlerp ) + 0.25 * pchr->enviro.zlerp;

		if ( pchr->enviro.is_slippy )
		{
			pchr->enviro.traction /= hillslide * ( 1.0f - pchr->enviro.zlerp ) + 1.0f * pchr->enviro.zlerp;
		}
	}
	else if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) )
	{
		pchr->enviro.traction = ABS( map_twist_nrm[pchr->enviro.twist].z ) * ( 1.0f - pchr->enviro.zlerp ) + 0.25 * pchr->enviro.zlerp;

		if ( pchr->enviro.is_slippy )
		{
			pchr->enviro.traction /= hillslide * ( 1.0f - pchr->enviro.zlerp ) + 1.0f * pchr->enviro.zlerp;
		}
	}

	//---- the friction of the fluid we are in
	if ( pchr->enviro.is_watery )
	{
		pchr->enviro.fluid_friction_vrt  = waterfriction;
		pchr->enviro.fluid_friction_hrz = waterfriction;
	}
	else
	{
		pchr->enviro.fluid_friction_hrz = pchr->enviro.air_friction;       // like real-life air friction
		pchr->enviro.fluid_friction_vrt  = pchr->enviro.air_friction;
	}

	//---- friction
	pchr->enviro.friction_hrz       = 1.0f;
	if ( 0 != pchr->flyheight )
	{
		if ( pchr->platform )
		{
			// override the z friction for platforms.
			// friction in the z direction will make the bouncing stop
			pchr->enviro.fluid_friction_vrt = 1.0f;
		}
	}
	else
	{
		// Make the characters slide
		float temp_friction_xy = noslipfriction;
		if ( mesh_grid_is_valid( PMesh, pchr->onwhichgrid ) && pchr->enviro.is_slippy )
		{
			// It's slippy all right...
			temp_friction_xy = slippyfriction;
		}

		pchr->enviro.friction_hrz = pchr->enviro.zlerp * 1.0f + ( 1.0f - pchr->enviro.zlerp ) * temp_friction_xy;
	}

	//---- jump stuff
	if ( 0 != pchr->flyheight )
	{
		// Flying
		pchr->jumpready = bfalse;
	}
	else
	{
		// Character is in the air
		pchr->jumpready = pchr->enviro.grounded;

		// Down jump timer
		if ( (pchr->jumpready || pchr->jumpnumber > 0) && pchr->jumptime > 0 ) pchr->jumptime--;

		// Do ground hits
		if ( pchr->enviro.grounded && pchr->vel.z < -STOPBOUNCING && pchr->hitready )
		{
			pchr->ai.alert |= ALERTIF_HITGROUND;
			pchr->hitready = bfalse;
		}

		// Special considerations for slippy surfaces
		if ( pchr->enviro.is_slippy )
		{
			if ( map_twist_flat[pchr->enviro.twist] )
			{
				// Reset jumping on flat areas of slippiness
				if ( pchr->enviro.grounded && pchr->jumptime == 0 )
				{
					pchr->jumpnumber = pchr->jumpnumberreset;
				}
			}
		}
		else if ( pchr->enviro.grounded && pchr->jumptime == 0 )
		{
			// Reset jumping
			pchr->jumpnumber = pchr->jumpnumberreset;
		}
	}
}

//--------------------------------------------------------------------------------------------
void move_one_character_do_floor_friction( chr_t * pchr )
{
	/// @details BB@> Friction is complicated when you want to have sliding characters :P

	float temp_friction_xy;
	fvec3_t   vup, floor_acc, fric, fric_floor;

	if ( !ACTIVE_PCHR( pchr ) ) return;

	if ( 0 != pchr->flyheight ) return;

	// figure out the acceleration due to the current "floor"
	floor_acc.x = floor_acc.y = floor_acc.z = 0.0f;
	temp_friction_xy = 1.0f;
	if ( INGAME_CHR( pchr->onwhichplatform ) )
	{
		chr_t * pplat = ChrList.lst + pchr->onwhichplatform;

		temp_friction_xy = platstick;

		floor_acc.x = pplat->vel.x - pplat->vel_old.x;
		floor_acc.y = pplat->vel.y - pplat->vel_old.y;
		floor_acc.z = pplat->vel.z - pplat->vel_old.z;

		chr_getMatUp( pplat, &vup );
	}
	else if ( !pchr->alive || pchr->isitem )
	{
		temp_friction_xy = 0.5f;
		floor_acc.x = -pchr->vel.x;
		floor_acc.y = -pchr->vel.y;
		floor_acc.z = -pchr->vel.z;

		if ( TWIST_FLAT == pchr->enviro.twist )
		{
			vup.x = vup.y = 0.0f;
			vup.z = 1.0f;
		}
		else
		{
			vup = map_twist_nrm[pchr->enviro.twist];
		}

	}
	else
	{
		temp_friction_xy = pchr->enviro.friction_hrz;

		if ( TWIST_FLAT == pchr->enviro.twist )
		{
			vup.x = vup.y = 0.0f;
			vup.z = 1.0f;
		}
		else
		{
			vup = map_twist_nrm[pchr->enviro.twist];
		}

		if ( ABS( pchr->vel.x ) + ABS( pchr->vel.y ) + ABS( pchr->vel.z ) > 0.0f )
		{
			float ftmp;
			fvec3_t   vfront = mat_getChrForward( pchr->inst.matrix );

			floor_acc.x = -pchr->vel.x;
			floor_acc.y = -pchr->vel.y;
			floor_acc.z = -pchr->vel.z;

			//---- get the "bad" velocity (perpendicular to the direction of motion)
			vfront = fvec3_normalize( vfront.v );
			ftmp = fvec3_dot_product( floor_acc.v, vfront.v );

			floor_acc.x -= ftmp * vfront.x;
			floor_acc.y -= ftmp * vfront.y;
			floor_acc.z -= ftmp * vfront.z;
		}
	}

	// the first guess about the floor friction
	fric_floor.x = floor_acc.x * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;
	fric_floor.y = floor_acc.y * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;
	fric_floor.z = floor_acc.z * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;

	// the total "friction" due to the floor
	fric.x = fric_floor.x + pchr->enviro.acc.x;
	fric.y = fric_floor.y + pchr->enviro.acc.y;
	fric.z = fric_floor.z + pchr->enviro.acc.z;

	//---- limit the friction to whatever is horizontal to the mesh
	if ( TWIST_FLAT == pchr->enviro.twist )
	{
		floor_acc.z = 0.0f;
		fric.z      = 0.0f;
	}
	else
	{
		float ftmp;
		fvec3_t   vup = map_twist_nrm[pchr->enviro.twist];

		ftmp = fvec3_dot_product( floor_acc.v, vup.v );

		floor_acc.x -= ftmp * vup.x;
		floor_acc.y -= ftmp * vup.y;
		floor_acc.z -= ftmp * vup.z;

		ftmp = fvec3_dot_product( fric.v, vup.v );

		fric.x -= ftmp * vup.x;
		fric.y -= ftmp * vup.y;
		fric.z -= ftmp * vup.z;
	}

	// test to see if the player has any more friction left?
	pchr->enviro.is_slipping = ( ABS( fric.x ) + ABS( fric.y ) + ABS( fric.z ) > pchr->enviro.friction_hrz );

	if ( pchr->enviro.is_slipping )
	{
		pchr->enviro.traction *= 0.5f;
		temp_friction_xy  = SQRT( temp_friction_xy );

		fric_floor.x = floor_acc.x * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;
		fric_floor.y = floor_acc.y * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;
		fric_floor.z = floor_acc.z * ( 1.0f - pchr->enviro.zlerp ) * ( 1.0f - temp_friction_xy ) * pchr->enviro.traction;
	}

	//apply the floor friction
	pchr->vel.x += fric_floor.x;
	pchr->vel.y += fric_floor.y;
	pchr->vel.z += fric_floor.z;

	// Apply fluid friction from last time
	pchr->vel.x += -pchr->vel.x * ( 1.0f - pchr->enviro.fluid_friction_hrz );
	pchr->vel.y += -pchr->vel.y * ( 1.0f - pchr->enviro.fluid_friction_hrz );
	pchr->vel.z += -pchr->vel.z * ( 1.0f - pchr->enviro.fluid_friction_vrt );
}

//--------------------------------------------------------------------------------------------
void move_one_character_do_voluntary( chr_t * pchr )
{
	float dvx, dvy, dvmax;
	float maxspeed;
	float dv2;
	float new_ax, new_ay;
	CHR_REF ichr;

	mad_t       * pmad;
	int           frame_count;
	MD2_Frame_t * frame_list;
	MD2_Frame_t * pframe_nxt;

	if ( !ACTIVE_PCHR( pchr ) ) return;

	ichr = GET_REF_PCHR( pchr );

	if ( !pchr->alive ) return;

	// do voluntary motion

	pchr->enviro.new_vx = pchr->vel.x;
	pchr->enviro.new_vy = pchr->vel.y;

	if ( INGAME_CHR( pchr->attachedto ) ) return;

	pmad        = chr_get_pmad( GET_REF_PCHR( pchr ) );
	frame_count = md2_get_numFrames( pmad->md2_ptr );
	frame_list  = ( MD2_Frame_t * )md2_get_Frames( pmad->md2_ptr );
	pframe_nxt  = frame_list + pchr->inst.frame_nxt;
	EGOBOO_ASSERT( pchr->inst.frame_nxt < frame_count );

	dvx = dvy = 0.0f;
	new_ax = new_ay = 0.0f;
	if ( ACTION_IS_TYPE( pchr->inst.action_which, P ) )
	{
		// handle the parry case of a parry/block animation
		new_ax = -pchr->vel.x;
		new_ay = -pchr->vel.y;

		pchr->enviro.new_vx = 0.0f;
		pchr->enviro.new_vy = 0.0f;
	}
	else
	{
		// Character latches for generalized movement
		dvx = pchr->latch.x;
		dvy = pchr->latch.y;

		// Reverse movements for daze
		if ( pchr->dazetime > 0 )
		{
			dvx = -dvx;
			dvy = -dvy;
		}

		// Switch x and y for grog
		if ( pchr->grogtime > 0 )
		{
			SWAP( float, dvx, dvy );
		}

		// this is the maximum speed that a character could go under the v2.22 system
		maxspeed = pchr->maxaccel * airfriction / ( 1.0f - airfriction );

		pchr->enviro.new_vx = pchr->enviro.new_vy = 0.0f;
		if ( ABS( dvx ) + ABS( dvy ) > 0 )
		{
			dv2 = dvx * dvx + dvy * dvy;

			if ( pchr->isplayer )
			{
				float speed;
				float dv = POW( dv2, 0.25f );

				if ( maxspeed < pchr->runspd )
				{
					maxspeed = pchr->runspd;
					dv *= 0.75f;
				}

				if ( dv >= 1.0f )
				{
					speed = maxspeed;
				}
				else if ( dv >= 0.75f )
				{
					speed = ( dv - 0.75f ) / 0.25f * maxspeed + ( 1.0f - dv ) / 0.25f * pchr->runspd;
				}
				else if ( dv >= 0.50f )
				{
					speed = ( dv - 0.50f ) / 0.25f * pchr->runspd + ( 0.75f - dv ) / 0.25f * pchr->walkspd;
				}
				else if ( dv >= 0.25f )
				{
					speed = ( dv - 0.25f ) / 0.25f * pchr->walkspd + ( 0.25f - dv ) / 0.25f * pchr->sneakspd;
				}
				else
				{
					speed = dv / 0.25f * pchr->sneakspd;
				}

				pchr->enviro.new_vx = speed * dvx / dv;
				pchr->enviro.new_vy = speed * dvy / dv;
			}
			else
			{
				float scale = 1.0f;

				if ( dv2 > 1.0f )
				{
					scale = 1.0f / POW( dv2, 0.5f );
				}
				else
				{
					scale = POW( dv2, 0.25f ) / POW( dv2, 0.5f );
				}

				pchr->enviro.new_vx = dvx * maxspeed * scale;
				pchr->enviro.new_vy = dvy * maxspeed * scale;
			}
		}

	}

	dvmax = pchr->maxaccel;
	if ( new_ax < -dvmax ) new_ax = -dvmax;
	if ( new_ax >  dvmax ) new_ax =  dvmax;
	if ( new_ay < -dvmax ) new_ay = -dvmax;
	if ( new_ay >  dvmax ) new_ay =  dvmax;

	// do platform friction
	if ( INGAME_CHR( pchr->onwhichplatform ) )
	{
		chr_t * pplat = ChrList.lst + pchr->onwhichplatform;

		new_ax += ( pplat->vel.x + pchr->enviro.new_vx - pchr->vel.x );
		new_ay += ( pplat->vel.y + pchr->enviro.new_vy - pchr->vel.y );
	}
	else
	{
		new_ax += ( pchr->enviro.new_vx - pchr->vel.x );
		new_ay += ( pchr->enviro.new_vy - pchr->vel.y );
	}

	new_ax *= pchr->enviro.traction;
	new_ay *= pchr->enviro.traction;

	//Figure out how to turn around
	switch ( pchr->turnmode )
	{
		// Get direction from ACTUAL change in velocity
	default:
	case TURNMODE_VELOCITY:
		{
			if ( ABS( dvx ) > TURNSPD || ABS( dvy ) > TURNSPD )
			{
				if ( pchr->isplayer )
				{
					// Players turn quickly
					pchr->ori.facing_z = terp_dir_fast( pchr->ori.facing_z, vec_to_facing( dvx , dvy ) );
				}
				else
				{
					// AI turn slowly
					pchr->ori.facing_z = terp_dir( pchr->ori.facing_z, vec_to_facing( dvx , dvy ) );
				}
			}
		}
		break;

		// Get direction from the DESIRED change in velocity
	case TURNMODE_WATCH:
		{
			if (( ABS( dvx ) > WATCHMIN || ABS( dvy ) > WATCHMIN ) )
			{
				pchr->ori.facing_z = terp_dir( pchr->ori.facing_z, vec_to_facing( dvx , dvy ) );
			}
		}
		break;

		// Face the target
	case TURNMODE_WATCHTARGET:
		{
			if ( ichr != pchr->ai.target )
			{
				pchr->ori.facing_z = terp_dir( pchr->ori.facing_z, vec_to_facing( ChrList.lst[pchr->ai.target].pos.x - pchr->pos.x , ChrList.lst[pchr->ai.target].pos.y - pchr->pos.y ) );
			}
		}
		break;

		// Otherwise make it spin
	case TURNMODE_SPIN:
		{
			pchr->ori.facing_z += SPINRATE;
		}
		break;

	}

	if ( chr_get_framefx( pchr ) & MADFX_STOP )
	{
		new_ax = 0;
		new_ay = 0;
	}
	else
	{
		pchr->vel.x += new_ax;
		pchr->vel.y += new_ay;
	}
}

//--------------------------------------------------------------------------------------------
bool_t chr_do_latch_attack( chr_t * pchr, int which_slot )
{
	chr_t * pweapon;
	cap_t * pweapon_cap;
	CHR_REF ichr, iweapon;
	MAD_REF imad;

	int    base_action, hand_action, action;
	bool_t action_valid, allowedtoattack;

	bool_t retval = bfalse;

	if ( !ACTIVE_PCHR( pchr ) ) return bfalse;
	ichr = GET_REF_PCHR( pchr );

	imad = chr_get_imad( ichr );

	if ( which_slot < 0 || which_slot >= SLOT_COUNT ) return bfalse;

	// Which iweapon?
	iweapon = pchr->holdingwhich[which_slot];
	if ( !INGAME_CHR( iweapon ) )
	{
		// Unarmed means character itself is the iweapon
		iweapon = ichr;
	}
	pweapon     = ChrList.lst + iweapon;
	pweapon_cap = chr_get_pcap( iweapon );

	// grab the iweapon's action
	base_action = pweapon_cap->weaponaction;
	hand_action = randomize_action( base_action, which_slot );

	// see if the character can play this action
	action       = mad_get_action( imad, hand_action );
	action_valid = ( ACTION_COUNT != action );

	// Can it do it?
	allowedtoattack = btrue;

	// First check if reload time and action is okay
	if ( !action_valid || pweapon->reloadtime > 0 )
	{
		allowedtoattack = bfalse;
	}
	else
	{
		// Then check if a skill is needed
		if ( pweapon_cap->needskillidtouse )
		{
			if ( 0 == check_skills( ichr, chr_get_idsz( iweapon, IDSZ_SKILL ) ) )
			{
				allowedtoattack = bfalse;
			}
		}
	}

	// Don't allow users with kursed weapon in the other hand to use longbows
	if ( allowedtoattack && action <= ACTION_LA && action >= ACTION_LD )
	{
		CHR_REF test_weapon;
		test_weapon = pchr->holdingwhich[which_slot == SLOT_LEFT ? SLOT_RIGHT : SLOT_LEFT];
		if ( INGAME_CHR( test_weapon ) )
		{
			chr_t * weapon;
			weapon     = ChrList.lst + test_weapon;
			if ( weapon->iskursed ) allowedtoattack = bfalse;
		}
	}

	if ( !allowedtoattack )
	{
		if ( 0 == pweapon->reloadtime )
		{
			// This character can't use this iweapon
			pweapon->reloadtime = 50;
			if ( pchr->StatusList_on )
			{
				// Tell the player that they can't use this iweapon
				debug_printf( "%s can't use this item...", chr_get_name( GET_REF_PCHR( pchr ), CHRNAME_ARTICLE | CHRNAME_CAPITAL ) );
			}
		}
	}

	if ( action == ACTION_DA )
	{
		allowedtoattack = bfalse;
		if ( 0 == pweapon->reloadtime )
		{
			pweapon->ai.alert |= ALERTIF_USED;
		}
	}

	// deal with your mount (which could steal your attack)
	if ( allowedtoattack )
	{
		// Rearing mount
		CHR_REF mount = pchr->attachedto;
		if ( INGAME_CHR( mount ) )
		{
			chr_t * pmount = ChrList.lst + mount;
			cap_t * pmount_cap = chr_get_pcap( mount );

			// let the mount steal the rider's attack
			if ( !pmount_cap->ridercanattack ) allowedtoattack = bfalse;

			// can the mount do anything?
			if ( pmount->alive )
			{
				// can the mount be told what to do?
				if ( pmount->ismount && !pmount->isplayer && pmount->inst.action_ready )
				{
					if ( !ACTION_IS_TYPE( action, P ) || !pmount_cap->ridercanattack )
					{
						chr_play_action( pmount, generate_randmask( ACTION_UA, 1 ), bfalse );
						pmount->ai.alert     |= ALERTIF_USED;
						pchr->ai.lastitemused = mount;

						retval = btrue;
					}
				}
			}
		}
	}

	// Attack button
	if ( allowedtoattack )
	{
		if ( pchr->inst.action_ready && action_valid )
		{
			// Check mana cost
			bool_t mana_paid = cost_mana( ichr, pweapon->manacost, iweapon );

			if ( mana_paid )
			{
				// Check life healing
				pchr->life += pweapon->life_heal;
				if ( pchr->life > pchr->lifemax )  pchr->life = pchr->lifemax;

				// randomize the action
				action = randomize_action( action, which_slot );

				// make sure it is valid
				action = mad_get_action( imad, action );

				if ( ACTION_IS_TYPE( action, P ) )
				{
					// we must set parry actions to be interrupted by anything
					chr_play_action( pchr, action, btrue );
				}
				else
				{
					chr_play_action( pchr, action, bfalse );
				}

				if ( iweapon != ichr )
				{
					// Make the iweapon attack too
					chr_play_action( pweapon, ACTION_MJ, bfalse );
				}

				// let everyone know what we did
				pweapon->ai.alert |= ALERTIF_USED;
				pchr->ai.lastitemused = iweapon;

				retval = btrue;
			}
		}
	}

	//Reset boredom timer if the attack succeeded
	if ( retval )
	{
		pchr->boretime = BORETIME;
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_do_latch_button( chr_t * pchr )
{
	/// @details BB@> Character latches for generalized buttons

	CHR_REF ichr;
	ai_state_t * pai;

	CHR_REF item;
	bool_t attack_handled;

	if ( !ACTIVE_PCHR( pchr ) ) return bfalse;
	ichr = GET_REF_PCHR( pchr );

	pai = &( pchr->ai );

	if ( !pchr->alive || 0 == pchr->latch.b ) return btrue;

	if ( HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_JUMP ) && 0 == pchr->jumptime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_JUMP;

		if ( INGAME_CHR( pchr->attachedto ) )
		{
			int ijump;
			fvec3_t tmp_pos;

			detach_character_from_mount( ichr, btrue, btrue );
			pchr->jumptime = JUMPDELAY;
			if ( pchr->flyheight != 0 )
			{
				pchr->vel.z += DISMOUNTZVELFLY;
			}
			else
			{
				pchr->vel.z += DISMOUNTZVEL;
			}

			tmp_pos = chr_get_pos( pchr );
			tmp_pos.z += pchr->vel.z;
			chr_set_pos( pchr, tmp_pos.v );

			if ( pchr->jumpnumberreset != JUMPINFINITE && pchr->jumpnumber != 0 )
				pchr->jumpnumber--;

			// Play the jump sound
			ijump = pro_get_pcap( pchr->iprofile )->sound_index[SOUND_JUMP];
			if ( VALID_SND( ijump ) )
			{
				sound_play_chunk( pchr->pos, chr_get_chunk_ptr( pchr, ijump ) );
			}

		}

		else if ( 0 != pchr->jumpnumber && 0 == pchr->flyheight )
		{
			if ( pchr->jumpnumberreset != 1 || pchr->jumpready )
			{
				int ijump;
				cap_t * pcap;

				// Make the character jump
				pchr->hitready = btrue;
				if ( pchr->enviro.inwater )
				{
					//pchr->vel.z += WATERJUMP;
					pchr->jumptime = JUMPDELAY * 4;         //To prevent 'bunny jumping' in water
					pchr->vel.z += WATERJUMP;
				}
				else
				{
					pchr->jumptime = JUMPDELAY;
					pchr->vel.z += pchr->jump_power * 2;
				}

				pchr->jumpready = bfalse;
				if ( pchr->jumpnumberreset != JUMPINFINITE ) pchr->jumpnumber--;

				// Set to jump animation if not doing anything better
				if ( pchr->inst.action_ready )
				{
					chr_play_action( pchr, ACTION_JA, btrue );
				}

				// Play the jump sound (Boing!)
				pcap = pro_get_pcap( pchr->iprofile );
				if ( NULL != pcap )
				{
					ijump = pcap->sound_index[SOUND_JUMP];
					if ( VALID_SND( ijump ) )
					{
						sound_play_chunk( pchr->pos, chr_get_chunk_ptr( pchr, ijump ) );
					}
				}
			}
		}

	}
	if ( HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_ALTLEFT ) && pchr->inst.action_ready && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_ALTLEFT;

		pchr->reloadtime = GRABDELAY;
		if ( !INGAME_CHR( pchr->holdingwhich[SLOT_LEFT] ) )
		{
			// Grab left
			chr_play_action( pchr, ACTION_ME, bfalse );
		}
		else
		{
			// Drop left
			chr_play_action( pchr, ACTION_MA, bfalse );
		}
	}
	if ( HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_ALTRIGHT ) && pchr->inst.action_ready && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_ALTRIGHT;

		pchr->reloadtime = GRABDELAY;
		if ( !INGAME_CHR( pchr->holdingwhich[SLOT_RIGHT] ) )
		{
			// Grab right
			chr_play_action( pchr, ACTION_MF, bfalse );
		}
		else
		{
			// Drop right
			chr_play_action( pchr, ACTION_MB, bfalse );
		}
	}
	if ( HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_PACKLEFT ) && pchr->inst.action_ready && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_PACKLEFT;

		pchr->reloadtime = PACKDELAY;
		item = pchr->holdingwhich[SLOT_LEFT];

		if ( INGAME_CHR( item ) )
		{
			chr_t * pitem = ChrList.lst + item;

			if (( pitem->iskursed || pro_get_pcap( pitem->iprofile )->istoobig ) && !pro_get_pcap( pitem->iprofile )->isequipment )
			{
				// The item couldn't be put away
				pitem->ai.alert |= ALERTIF_NOTPUTAWAY;
				if ( pchr->isplayer )
				{
					if ( pro_get_pcap( pitem->iprofile )->istoobig )
					{
						debug_printf( "%s is too big to be put away...", chr_get_name( item, CHRNAME_ARTICLE | CHRNAME_DEFINITE | CHRNAME_CAPITAL ) );
					}
					else if ( pitem->iskursed )
					{
						debug_printf( "%s is sticky...", chr_get_name( item, CHRNAME_ARTICLE | CHRNAME_DEFINITE | CHRNAME_CAPITAL ) );
					}
				}
			}
			else
			{
				// Put the item into the pack
				inventory_add_item( item, ichr );
			}
		}
		else
		{
			// Get a new one out and put it in hand
			inventory_get_item( ichr, GRIP_LEFT, bfalse );
		}

		// Make it take a little time
		chr_play_action( pchr, ACTION_MG, bfalse );
	}
	if ( HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_PACKRIGHT ) && pchr->inst.action_ready && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_PACKRIGHT;

		pchr->reloadtime = PACKDELAY;
		item = pchr->holdingwhich[SLOT_RIGHT];
		if ( INGAME_CHR( item ) )
		{
			chr_t * pitem     = ChrList.lst + item;
			cap_t * pitem_cap = chr_get_pcap( item );

			if (( pitem->iskursed || pitem_cap->istoobig ) && !pitem_cap->isequipment )
			{
				// The item couldn't be put away
				pitem->ai.alert |= ALERTIF_NOTPUTAWAY;
				if ( pchr->isplayer )
				{
					if ( pitem_cap->istoobig )
					{
						debug_printf( "%s is too big to be put away...", chr_get_name( item, CHRNAME_ARTICLE | CHRNAME_DEFINITE | CHRNAME_CAPITAL ) );
					}
					else if ( pitem->iskursed )
					{
						debug_printf( "%s is sticky...", chr_get_name( item, CHRNAME_ARTICLE | CHRNAME_DEFINITE | CHRNAME_CAPITAL ) );
					}
				}
			}
			else
			{
				// Put the item into the pack
				inventory_add_item( item, ichr );
			}
		}
		else
		{
			// Get a new one out and put it in hand
			inventory_get_item( ichr, GRIP_RIGHT, bfalse );
		}

		// Make it take a little time
		chr_play_action( pchr, ACTION_MG, bfalse );
	}

	// LATCHBUTTON_LEFT and LATCHBUTTON_RIGHT are mutually exclusive
	attack_handled = bfalse;
	if ( !attack_handled && HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_LEFT ) && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_LEFT;
		attack_handled = chr_do_latch_attack( pchr, SLOT_LEFT );
	}
	if ( !attack_handled && HAS_SOME_BITS( pchr->latch.b, LATCHBUTTON_RIGHT ) && 0 == pchr->reloadtime )
	{
		//pchr->latch.b &= ~LATCHBUTTON_RIGHT;

		attack_handled = chr_do_latch_attack( pchr, SLOT_RIGHT );
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
void move_one_character_do_z_motion( chr_t * pchr )
{
	if ( !ACTIVE_PCHR( pchr ) ) return;

	//---- do z acceleration
	if ( 0 != pchr->flyheight )
	{
		pchr->vel.z += ( pchr->enviro.fly_level + pchr->flyheight - pchr->pos.z ) * FLYDAMPEN;
	}
	else
	{
		if ( pchr->enviro.is_slippy && pchr->phys.weight != INFINITE_WEIGHT &&
			pchr->enviro.twist != TWIST_FLAT && pchr->enviro.zlerp < 1.0f )
		{
			// Slippy hills make characters slide

			fvec3_t   gpara, gperp;

			gperp.x = map_twistvel_x[pchr->enviro.twist];
			gperp.y = map_twistvel_y[pchr->enviro.twist];
			gperp.z = map_twistvel_z[pchr->enviro.twist];

			gpara.x = 0       - gperp.x;
			gpara.y = 0       - gperp.y;
			gpara.z = gravity - gperp.z;

			pchr->vel.x += gpara.x + gperp.x * pchr->enviro.zlerp;
			pchr->vel.y += gpara.y + gperp.y * pchr->enviro.zlerp;
			pchr->vel.z += gpara.z + gperp.z * pchr->enviro.zlerp;
		}
		else
		{
			pchr->vel.z += pchr->enviro.zlerp * gravity;
		}
	}
}

//--------------------------------------------------------------------------------------------
bool_t chr_update_safe_raw( chr_t * pchr )
{
	bool_t retval = bfalse;

	bool_t hit_a_wall;

	if( !ALLOCATED_PCHR( pchr ) ) return bfalse;

	hit_a_wall = chr_hit_wall( pchr, NULL, NULL, NULL );
	if( !hit_a_wall )
	{
		pchr->safe_valid = btrue;
		pchr->safe_pos   = chr_get_pos( pchr );
		pchr->safe_time  = update_wld;
		pchr->safe_grid  = mesh_get_tile(PMesh, pchr->pos.x, pchr->pos.y);

		retval = btrue;
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_update_safe( chr_t * pchr, bool_t force )
{
	Uint32 new_grid;
	bool_t retval = bfalse;
	bool_t needs_update = bfalse;

	if( !ALLOCATED_PCHR(pchr) ) return bfalse;

	if( force || !pchr->safe_valid )
	{
		needs_update = btrue;
	}
	else
	{
		new_grid = mesh_get_tile( PMesh, pchr->pos.x, pchr->pos.y );

		if( INVALID_TILE == new_grid )
		{
			if( ABS(pchr->pos.x - pchr->safe_pos.x) > GRID_SIZE ||
				ABS(pchr->pos.y - pchr->safe_pos.y) > GRID_SIZE )
			{
				needs_update = btrue;
			}
		}
		else if ( new_grid != pchr->safe_grid )
		{
			needs_update = btrue;
		}
	}

	if( needs_update )
	{
		retval = chr_update_safe_raw( pchr );
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_get_safe( chr_t * pchr, fvec3_base_t pos_v )
{
	bool_t found = bfalse;
	fvec3_t loc_pos;

	if( !ALLOCATED_PCHR(pchr) ) return bfalse;

	// handle optional parameters
	if( NULL == pos_v ) pos_v = loc_pos.v;

	if( !found && pchr->safe_valid )
	{
		if( !chr_hit_wall( pchr, NULL, NULL, NULL ) )
		{
			found = btrue;
			memmove( pos_v, pchr->safe_pos.v, sizeof(fvec3_base_t) );
		}
	}

	if( !found )
	{
		breadcrumb_t * bc;

		bc = breadcrumb_list_last_valid( &(pchr->crumbs) );

		if ( NULL != bc )
		{
			found = btrue;
			memmove( pos_v, bc->pos.v, sizeof(fvec3_base_t) );
		}
	}

	// maybe there is one last fallback after this? we could check the character's current position?

	return found;
}

//--------------------------------------------------------------------------------------------
bool_t chr_update_breadcrumb_raw( chr_t * pchr )
{
	breadcrumb_t bc;
	bool_t retval = bfalse;

	if( !ALLOCATED_PCHR( pchr ) ) return bfalse;

	breadcrumb_init_chr( &bc, pchr );

	if( bc.valid )
	{
		retval = breadcrumb_list_add( &(pchr->crumbs), &bc );
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_update_breadcrumb( chr_t * pchr, bool_t force )
{
	Uint32 new_grid;
	bool_t retval = bfalse;
	bool_t needs_update = bfalse;
	breadcrumb_t * bc_ptr, bc;

	if( !ALLOCATED_PCHR(pchr) ) return bfalse;

	bc_ptr = breadcrumb_list_last_valid( &(pchr->crumbs) );
	if( NULL == bc_ptr )
	{
		force  = btrue;
		bc_ptr = &bc;
		breadcrumb_init_chr( bc_ptr, pchr );
	}

	if( force )
	{
		needs_update = btrue;
	}
	else
	{
		new_grid = mesh_get_tile( PMesh, pchr->pos.x, pchr->pos.y );

		if( INVALID_TILE == new_grid )
		{
			if( ABS(pchr->pos.x - bc_ptr->pos.x) > GRID_SIZE ||
				ABS(pchr->pos.y - bc_ptr->pos.y) > GRID_SIZE )
			{
				needs_update = btrue;
			}
		}
		else if ( new_grid != bc_ptr->grid )
		{
			needs_update = btrue;
		}
	}

	if( needs_update )
	{
		retval = chr_update_breadcrumb_raw( pchr );
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
breadcrumb_t * chr_get_last_breadcrumb( chr_t * pchr )
{
	if( !ALLOCATED_PCHR(pchr) ) return NULL;

	if( 0 == pchr->crumbs.count ) return NULL;

	return breadcrumb_list_last_valid( &(pchr->crumbs) );
}

//--------------------------------------------------------------------------------------------
bool_t move_one_character_integrate_motion_attached( chr_t * pchr )
{
	Uint32 chr_update;

	if ( !ACTIVE_PCHR( pchr ) ) return bfalse;

	// make a timer that is individual for each object
	chr_update = pchr->obj_base.guid + update_wld;

	if (  0 == ( chr_update & 7 ) )
	{
		chr_update_safe( pchr, btrue );
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t move_one_character_integrate_motion( chr_t * pchr )
{
	/// @details BB@> Figure out the next position of the character.
	///    Include collisions with the mesh in this step.

	CHR_REF  ichr;
	ai_state_t * pai;

	float   bumpdampen;
	bool_t  needs_test, updated_2d;

	fvec3_t tmp_pos;

	if ( !ACTIVE_PCHR( pchr ) ) return bfalse;

	if( ACTIVE_CHR(pchr->attachedto) )
	{
		return move_one_character_integrate_motion_attached( pchr );
	}

	tmp_pos = chr_get_pos( pchr );

	pai = &( pchr->ai );
	ichr = pai->index;

	bumpdampen = CLIP( pchr->phys.bumpdampen, 0.0f, 1.0f );
	bumpdampen = ( bumpdampen + 1.0f ) / 2.0f;

	// interaction with the mesh
	//if ( ABS( pchr->vel.z ) > 0.0f )
	{
		tmp_pos.z += pchr->vel.z;
		LOG_NAN( tmp_pos.z );
		if ( tmp_pos.z < pchr->enviro.floor_level )
		{
			pchr->vel.z *= -pchr->phys.bumpdampen;

			if ( ABS( pchr->vel.z ) < STOPBOUNCING )
			{
				pchr->vel.z = 0.0f;
				tmp_pos.z = pchr->enviro.level;
			}
			else
			{
				float diff = pchr->enviro.level - tmp_pos.z;
				tmp_pos.z = pchr->enviro.level + diff;
			}
		}
	}

	// fixes to the z-position
	if ( 0.0f != pchr->flyheight )
	{
		if ( tmp_pos.z < 0.0f ) tmp_pos.z = 0.0f;  // Don't fall in pits...
	}

	updated_2d = bfalse;
	needs_test = bfalse;

	// interaction with the grid flags
	updated_2d = bfalse;
	needs_test = bfalse;
	//if ( ABS( pchr->vel.x ) + ABS( pchr->vel.y ) > 0.0f )
	{
		float old_x, old_y, new_x, new_y;

		old_x = tmp_pos.x; LOG_NAN( old_x );
		old_y = tmp_pos.y; LOG_NAN( old_y );

		new_x = old_x + pchr->vel.x; LOG_NAN( new_x );
		new_y = old_y + pchr->vel.y; LOG_NAN( new_y );

		tmp_pos.x = new_x;
		tmp_pos.y = new_y;

		if ( !chr_test_wall( pchr, tmp_pos.v ) )
		{
			updated_2d = btrue;
		}
		else
		{
			fvec2_t nrm;
			float   pressure;
			bool_t diff_function_called = bfalse;

			chr_hit_wall( pchr, tmp_pos.v, nrm.v, &pressure );

			// how is the character hitting the wall?
			if ( 0.0f != pressure )
			{
				bool_t         found_nrm  = bfalse;
				bool_t         found_safe = bfalse;
				fvec3_t        safe_pos   = ZERO_VECT3;

				bool_t         found_diff = bfalse;
				fvec2_t        diff       = ZERO_VECT2;

				breadcrumb_t * bc         = NULL;

				// try to get the correct "outward" pressure from nrm
				if( !found_nrm && ABS(nrm.x) + ABS(nrm.y) > 0.0f )
				{
					found_nrm = btrue;
				}

				if( !found_diff && pchr->safe_valid )
				{
					if( !found_safe )
					{
						found_safe = btrue;
						safe_pos   = pchr->safe_pos;
					}

					diff.x = pchr->safe_pos.x - pchr->pos.x;
					diff.y = pchr->safe_pos.y - pchr->pos.y;

					if( ABS(diff.x) + ABS(diff.y) > 0.0f )
					{
						found_diff = btrue;
					}
				}

				// try to get a diff from a breadcrumb
				if( !found_diff )
				{
					bc = chr_get_last_breadcrumb( pchr );

					if( NULL != bc && bc->valid )
					{
						if( !found_safe )
						{
							found_safe = btrue;
							safe_pos   = pchr->safe_pos;
						}

						diff.x = bc->pos.x - pchr->pos.x;
						diff.y = bc->pos.y - pchr->pos.y;

						if( ABS(diff.x) + ABS(diff.y) > 0.0f )
						{
							found_diff = btrue;
						}
					}
				}

				// try to get a normal from the mesh_get_diff() function
				if( !found_nrm )
				{
					fvec2_t diff;

					diff = chr_get_diff( pchr, tmp_pos.v, pressure );
					diff_function_called = btrue;

					nrm.x = diff.x;
					nrm.y = diff.y;

					if( ABS(nrm.x) + ABS(nrm.y) > 0.0f )
					{
						found_nrm = btrue;
					}
				}

				if( !found_diff )
				{
					// try to get the diff from the character velocity
					diff.x = pchr->vel.x;
					diff.y = pchr->vel.y;

					// make sure that the diff is in the same direction as the velocity
					if( fvec2_dot_product(diff.v, nrm.v) < 0.0f  )
					{
						diff.x *= -1.0f;
						diff.y *= -1.0f;
					}

					if( ABS(diff.x) + ABS(diff.y) > 0.0f )
					{
						found_diff = btrue;
					}
				}

				if( !found_nrm )
				{
					// After all of our best efforts, we can't generate a normal to the wall.
					// This can happen if the object is completely inside a wall,
					// (like if it got pushed in there) or if a passage closed around it.
					// Just teleport the character to a "safe" position.

					if( !found_safe && NULL == bc )
					{
						bc = chr_get_last_breadcrumb( pchr );

						if( NULL != bc && bc->valid )
						{
							found_safe = btrue;
							safe_pos   = pchr->safe_pos;
						}
					}

					if( !found_safe )
					{
						// the only safe position is the spawn point???
						found_safe = btrue;
						safe_pos = pchr->pos_stt;
					}

					tmp_pos = safe_pos;
				}
				else if( found_diff && found_nrm )
				{
					const float tile_fraction = 0.1f;
					float ftmp, dot, pressure_old, pressure_new;
					fvec3_t save_pos;
					float nrm2;

					fvec2_t v_perp = ZERO_VECT2;
					fvec2_t diff_perp = ZERO_VECT2;

					nrm2 = fvec2_dot_product( nrm.v, nrm.v );

					save_pos = tmp_pos;

					// make the diff point "out"
					dot = fvec2_dot_product( diff.v, nrm.v );
					if( dot < 0.0f )
					{
						diff.x *= -1.0f;
						diff.y *= -1.0f;
						dot    *= -1.0f;
					}

					// find the part of the diff that is parallel to the normal
					diff_perp.x = nrm.x * dot / nrm2;
					diff_perp.y = nrm.y * dot / nrm2;

					// normalize the diff_perp so that it is at most tile_fraction of a grid in any direction
					ftmp = MAX(ABS(diff_perp.x),ABS(diff_perp.y));
					assert(ftmp > 0.0f);
					diff_perp.x *= tile_fraction * GRID_SIZE / ftmp;
					diff_perp.y *= tile_fraction * GRID_SIZE / ftmp;

					// try moving the character
					tmp_pos.x += diff_perp.x * pressure;
					tmp_pos.y += diff_perp.y * pressure;

					// determine whether the pressure is less at this location
					pressure_old = chr_get_mesh_pressure( pchr, save_pos.v );
					pressure_new = chr_get_mesh_pressure( pchr, tmp_pos.v );

					if( pressure_new < pressure_old )
					{
						// !!success!!
						needs_test = ( tmp_pos.x != save_pos.x ) || ( tmp_pos.y != save_pos.y );
					}
					else
					{
						// !!failure!! restore the saved position
						tmp_pos = save_pos;
					}

					dot = fvec2_dot_product( pchr->vel.v, nrm.v );
					if( dot < 0.0f )
					{
						float bumpdampen;
						cap_t * pcap = chr_get_pcap( GET_REF_PCHR(pchr) );

						bumpdampen = 0.0f;
						if( NULL == pcap )
						{
							bumpdampen = pcap->bumpdampen;
						}
						v_perp.x = nrm.x * dot / nrm2;
						v_perp.y = nrm.y * dot / nrm2;

						pchr->vel.x += - (1.0f + bumpdampen) * v_perp.x * pressure;
						pchr->vel.y += - (1.0f + bumpdampen) * v_perp.y * pressure;
					}
				}
			}
		}
	}

	chr_set_pos( pchr, tmp_pos.v );

	// we need to test the validity of the current position every 8 frames or so,
	// no matter what
	if ( !needs_test )
	{
		// make a timer that is individual for each object
		Uint32 chr_update = pchr->obj_base.guid + update_wld;

		needs_test = ( 0 == ( chr_update & 7 ) );
	}

	if ( needs_test || updated_2d )
	{
		chr_update_safe( pchr, needs_test );
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_handle_madfx( chr_t * pchr )
{
	CHR_REF ichr;
	Uint32 framefx;

	if ( NULL == pchr ) return bfalse;

	ichr    = GET_REF_PCHR( pchr );
	framefx = chr_get_framefx( pchr );

	// Check frame effects
	if ( framefx&MADFX_ACTLEFT )
	{
		character_swipe( ichr, SLOT_LEFT );
	}

	if ( framefx&MADFX_ACTRIGHT )
	{
		character_swipe( ichr, SLOT_RIGHT );
	}

	if ( framefx&MADFX_GRABLEFT )
	{
		character_grab_stuff( ichr, GRIP_LEFT, bfalse );
	}

	if ( framefx&MADFX_GRABRIGHT )
	{
		character_grab_stuff( ichr, GRIP_RIGHT, bfalse );
	}

	if ( framefx&MADFX_CHARLEFT )
	{
		character_grab_stuff( ichr, GRIP_LEFT, btrue );
	}

	if ( framefx&MADFX_CHARRIGHT )
	{
		character_grab_stuff( ichr, GRIP_RIGHT, btrue );
	}

	if ( framefx&MADFX_DROPLEFT )
	{
		detach_character_from_mount( pchr->holdingwhich[SLOT_LEFT], bfalse, btrue );
	}

	if ( framefx&MADFX_DROPRIGHT )
	{
		detach_character_from_mount( pchr->holdingwhich[SLOT_RIGHT], bfalse, btrue );
	}

	if ( framefx&MADFX_POOF && !pchr->isplayer )
	{
		pchr->ai.poof_time = update_wld;
	}

	if ( framefx&MADFX_FOOTFALL )
	{
		cap_t * pcap = pro_get_pcap( pchr->iprofile );
		if ( NULL != pcap )
		{
			int ifoot = pcap->sound_index[SOUND_FOOTFALL];
			if ( VALID_SND( ifoot ) )
			{
				sound_play_chunk( pchr->pos, chr_get_chunk_ptr( pchr, ifoot ) );
			}
		}
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
void move_one_character_do_animation( chr_t * pchr )
{
	Uint8 speed, framelip;
	CHR_REF ichr;

	chr_instance_t * pinst;
	mad_t          * pmad;

	if ( NULL == pchr || !pchr->onwhichblock ) return;
	ichr  = GET_REF_PCHR( pchr );
	pinst = &( pchr->inst );

	pmad = chr_get_pmad( ichr );
	if ( NULL == pmad ) return;

	// Animate the character.
	// Right now there are 50/4 = 12.5 animation frames per second
	pinst->flip += 0.25f * pinst->rate;

	while ( pinst->flip > 0.25f )
	{
		pinst->flip -= 0.25f;
		pinst->ilip  = ( pinst->ilip + 1 ) % 4;

		// handle frame FX for the new frame
		if ( 3 == pinst->ilip )
		{
			chr_handle_madfx( pchr );
		}

		if ( 0 == pinst->ilip )
		{
			chr_increment_frame( pchr );
		}
	}

	// go back to a base animation rate, in case the next frame is not a
	// "variable speed frame"
	pinst->rate = 1.0f;

	// Get running, walking, sneaking, or dancing, from speed
	if ( !pinst->action_keep && !pinst->action_loop && !( pchr->inst.action_which >= ACTION_PA && pchr->inst.action_which <= ACTION_PD ) )
	{
		int           frame_count = md2_get_numFrames( pmad->md2_ptr );
		MD2_Frame_t * frame_list  = ( MD2_Frame_t * )md2_get_Frames( pmad->md2_ptr );
		MD2_Frame_t * pframe_nxt  = frame_list + pinst->frame_nxt;
		EGOBOO_ASSERT( pinst->frame_nxt < frame_count );

		framelip = pframe_nxt->framelip;  // 0 - 15...  Way through animation
		if ( pinst->action_ready && pinst->ilip == 0 && pchr->enviro.grounded && pchr->flyheight == 0 && ( framelip&7 ) < 2 )
		{
			// Do the motion stuff

			// new_vx, new_vy is the speed before any latches are applied
			speed = ABS( pchr->enviro.new_vx ) + ABS( pchr->enviro.new_vy );

			if ( speed < 0.5f * pchr->sneakspd )
			{
				// Do boredom
				pchr->boretime--;
				if ( pchr->boretime < 0 )
				{
					pchr->ai.alert |= ALERTIF_BORED;
					pchr->boretime = BORETIME;
				}
				else if ( pchr->boretime == 0 || speed == 0 )
				{
					// Do standstill
					if ( !ACTION_IS_TYPE( pinst->action_which, D ) )
					{
						int tmp_action = mad_get_action( pinst->imad, ACTION_DA );
						chr_start_anim( pchr, tmp_action, btrue, btrue );
					}
				}
				else
				{
					int tmp_action = mad_get_action( pinst->imad, ACTION_WA );
					if ( ACTION_COUNT != tmp_action )
					{
						if ( pinst->action_which != tmp_action )
						{
							chr_set_anim( pchr, tmp_action, pmad->frameliptowalkframe[LIPWA][framelip], btrue, btrue );
						}

						pinst->action_next = tmp_action;

						if ( pchr->fat != 0.0f )
						{
							pinst->rate = ( float )speed / ( float )pchr->sneakspd / pchr->fat;
						}
					}
				}
			}
			else
			{
				pchr->boretime = BORETIME;

				if ( speed < 0.5f *( pchr->sneakspd + pchr->walkspd ) )
				{
					//Sneak
					int tmp_action = mad_get_action( pinst->imad, ACTION_WA );
					if ( ACTION_COUNT != tmp_action )
					{
						if ( pinst->action_which != tmp_action )
						{
							chr_set_anim( pchr, tmp_action, pmad->frameliptowalkframe[LIPWA][framelip], btrue, btrue );
						}

						pinst->action_next = tmp_action;

						if ( pchr->fat != 0.0f )
						{
							pinst->rate = ( float )speed / ( float )pchr->sneakspd / pchr->fat;
						}
					}
				}
				else if ( speed < 0.5f *( pchr->walkspd + pchr->runspd ) )
				{
					//Walk
					int tmp_action = mad_get_action( pinst->imad, ACTION_WB );
					if ( ACTION_COUNT != tmp_action )
					{
						if ( pinst->action_which != tmp_action )
						{
							chr_set_anim( pchr, tmp_action, pmad->frameliptowalkframe[LIPWB][framelip], btrue, btrue );
						}

						pinst->action_next = tmp_action;

						if ( pchr->fat != 0.0f )
						{
							pinst->rate = ( float )speed / ( float )pchr->walkspd / pchr->fat;
						}
					}
				}
				else if ( pchr->runspd != 0 )
				{
					//Run
					int tmp_action = mad_get_action( pinst->imad, ACTION_WC );
					if ( ACTION_COUNT != tmp_action )
					{
						if ( pinst->action_which != tmp_action )
						{
							chr_set_anim( pchr, tmp_action, pmad->frameliptowalkframe[LIPWC][framelip], btrue, btrue );
						}

						pinst->action_next = tmp_action;

						if ( pchr->fat != 0.0f )
						{
							pinst->rate = speed / pchr->runspd;
						}
					}
				}
			}
		}
	}

	pinst->rate = CLIP( pinst->rate, 0.1f, 10.0f );
}

//--------------------------------------------------------------------------------------------
void move_one_character( chr_t * pchr )
{
	if ( !ACTIVE_PCHR( pchr ) ) return;

	if ( pchr->pack.is_packed ) return;

	// save the acceleration from the last time-step
	pchr->enviro.acc = fvec3_sub( pchr->vel.v, pchr->vel_old.v );

	// Character's old location
	pchr->pos_old        = chr_get_pos( pchr );
	pchr->vel_old        = pchr->vel;
	pchr->ori_old.facing_z     = pchr->ori.facing_z;

	pchr->enviro.new_vx = pchr->vel.x;
	pchr->enviro.new_vy = pchr->vel.y;

	move_one_character_get_environment( pchr );

	// do friction with the floor before voluntary motion
	move_one_character_do_floor_friction( pchr );

	move_one_character_do_voluntary( pchr );

	chr_do_latch_button( pchr );

	move_one_character_do_z_motion( pchr );

	move_one_character_integrate_motion( pchr );

	move_one_character_do_animation( pchr );

	// Characters with sticky butts lie on the surface of the mesh
	if ( pchr->stickybutt || !pchr->alive )
	{
		float fkeep = ( 7 + pchr->enviro.zlerp ) / 8.0f;
		float fnew  = ( 1 - pchr->enviro.zlerp ) / 8.0f;

		if ( fnew > 0 )
		{
			pchr->ori.map_facing_x = pchr->ori.map_facing_x * fkeep + map_twist_x[pchr->enviro.twist] * fnew;
			pchr->ori.map_facing_y = pchr->ori.map_facing_y * fkeep + map_twist_y[pchr->enviro.twist] * fnew;
		}
	}
}

//--------------------------------------------------------------------------------------------
void move_all_characters( void )
{
	/// @details ZZ@> This function handles character physics

	const float air_friction = 0.9868f;  // gives the same terminal velocity in terms of the size of the game characters
	const float ice_friction = 0.9738f;  // the square of air_friction

	chr_stoppedby_tests = 0;

	// Move every character
	CHR_BEGIN_LOOP_ACTIVE( cnt, pchr )
	{
		// prime the environment
		pchr->enviro.air_friction = air_friction;
		pchr->enviro.ice_friction = ice_friction;

		move_one_character( pchr );
	}
	CHR_END_LOOP();

	// The following functions need to be called any time you actually change a charcter's position
	keep_weapons_with_holders();
	attach_all_particles();
	make_all_character_matrices( update_wld != 0 );
}

//--------------------------------------------------------------------------------------------
void cleanup_all_characters()
{
	CHR_REF cnt;

	// Do poofing
	for ( cnt = 0; cnt < MAX_CHR; cnt++ )
	{
		chr_t * pchr;
		bool_t time_out;

		if ( !ALLOCATED_CHR( cnt ) ) continue;
		pchr = ChrList.lst + cnt;

		time_out = ( pchr->ai.poof_time >= 0 ) && ( pchr->ai.poof_time <= ( Sint32 )update_wld );
		if ( !WAITING_PBASE( POBJ_GET_PBASE( pchr ) ) && !time_out ) continue;

		// detach the character from the game
		cleanup_one_character( pchr );

		// free the character's inventory
		free_inventory_in_game( cnt );

		// free the character
		free_one_character_in_game( cnt );
	}
}

//--------------------------------------------------------------------------------------------
void bump_all_characters_update_counters()
{
	CHR_REF cnt;

	for ( cnt = 0; cnt < MAX_CHR; cnt++ )
	{
		ego_object_base_t * pbase;

		pbase = POBJ_GET_PBASE( ChrList.lst + cnt );
		if ( !ACTIVE_PBASE( pbase ) ) continue;

		pbase->update_count++;
	}
}

//--------------------------------------------------------------------------------------------
bool_t is_invictus_direction( FACING_T direction, const CHR_REF by_reference character, Uint16 effects )
{
	FACING_T left, right;

	chr_t * pchr;
	cap_t * pcap;
	mad_t * pmad;

	bool_t  is_invictus;

	if ( !INGAME_CHR( character ) ) return btrue;
	pchr = ChrList.lst + character;

	pmad = chr_get_pmad( character );
	if ( NULL == pmad ) return btrue;

	pcap = chr_get_pcap( character );
	if ( NULL == pcap ) return btrue;

	// if the invictus flag is set, we are invictus
	if ( pchr->invictus ) return btrue;

	// if the effect is armor piercing, ignore shielding
	if ( HAS_SOME_BITS( effects, DAMFX_NBLOC ) ) return bfalse;

	// if the character's frame is invictus, then check the angles
	if ( 0 != ( chr_get_framefx( pchr ) & MADFX_INVICTUS ) ) //HAS_SOME_BITS( chr_get_framefx( pchr ), MADFX_INVICTUS ) )
	{
		//I Frame
		direction -= pcap->iframefacing;
		left       = ( FACING_T )(( int )0x00010000 - ( int )pcap->iframeangle );
		right      = pcap->iframeangle;

		// If using shield, use the shield invictus instead
		if ( ACTION_IS_TYPE( pchr->inst.action_which, P ) )
		{
			bool_t parry_left = ( pchr->inst.action_which < ACTION_PC );

			// Using a shield?
			if ( parry_left )
			{
				// Check left hand
				cap_t * pcap_tmp = chr_get_pcap( pchr->holdingwhich[SLOT_LEFT] );
				if ( NULL != pcap )
				{
					left  = ( FACING_T )(( int )0x00010000 - ( int )pcap_tmp->iframeangle );
					right = pcap_tmp->iframeangle;
				}
			}
			else
			{
				// Check right hand
				cap_t * pcap_tmp = chr_get_pcap( pchr->holdingwhich[SLOT_RIGHT] );
				if ( NULL != pcap )
				{
					left  = ( FACING_T )(( int )0x00010000 - ( int )pcap_tmp->iframeangle );
					right = pcap_tmp->iframeangle;
				}
			}
		}
	}
	else
	{
		// N Frame
		direction -= pcap->nframefacing;
		left       = ( FACING_T )(( int )0x00010000 - ( int )pcap->nframeangle );
		right      = pcap->nframeangle;
	}

	// Check that direction
	is_invictus = bfalse;
	if ( direction >= left && direction <= right )
	{
		is_invictus = btrue;
	}

	return is_invictus;
}

//--------------------------------------------------------------------------------------------
chr_reflection_cache_t * chr_reflection_cache_init( chr_reflection_cache_t * pcache )
{
	if ( NULL == pcache ) return pcache;

	memset( pcache, 0, sizeof( *pcache ) );

	pcache->alpha = 127;
	pcache->light = 255;

	return pcache;
}

//--------------------------------------------------------------------------------------------
void chr_instance_clear_cache( chr_instance_t * pinst )
{
	/// @details BB@> force chr_instance_update_vertices() recalculate the vertices the next time
	///     the function is called

	vlst_cache_init( &( pinst->save ) );

	matrix_cache_init( &( pinst->matrix_cache ) );

	chr_reflection_cache_init( &( pinst->ref ) );

	pinst->lighting_update_wld = 0;
	pinst->lighting_frame_all  = 0;
}

//--------------------------------------------------------------------------------------------
chr_instance_t * chr_instance_dtor( chr_instance_t * pinst )
{
	if ( NULL == pinst ) return pinst;

	chr_instance_free( pinst );

	EGOBOO_ASSERT( NULL == pinst->vrt_lst );

	memset( pinst, 0, sizeof( *pinst ) );

	return pinst;
}

//--------------------------------------------------------------------------------------------
chr_instance_t * chr_instance_ctor( chr_instance_t * pinst )
{
	Uint32 cnt;

	if ( NULL == pinst ) return pinst;

	memset( pinst, 0, sizeof( *pinst ) );

	// model parameters
	pinst->imad = MAX_MAD;
	pinst->vrt_count = 0;

	// set the initial cache parameters
	chr_instance_clear_cache( pinst );

	// Set up initial fade in lighting
	pinst->color_amb = 0;
	for ( cnt = 0; cnt < pinst->vrt_count; cnt++ )
	{
		pinst->vrt_lst[cnt].color_dir = 0;
	}

	// clear out the matrix cache
	matrix_cache_init( &( pinst->matrix_cache ) );

	// the matrix should never be referenced if the cache is not valid,
	// but it never pays to have a 0 matrix...
	pinst->matrix = IdentityMatrix();

	// set the animation state
	pinst->rate         = 1.0f;
	pinst->action_next  = ACTION_DA;
	pinst->action_ready = btrue;                     // argh! this must be set at the beginning, script's spawn animations do not work!
	pinst->frame_nxt    = pinst->frame_lst = 0;

	// the vlst_cache parameters are not valid
	pinst->save.valid = bfalse;

	return pinst;
}

//--------------------------------------------------------------------------------------------
bool_t chr_instance_free( chr_instance_t * pinst )
{
	if ( NULL == pinst ) return bfalse;

	EGOBOO_DELETE_ARY( pinst->vrt_lst );
	pinst->vrt_count = 0;

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_instance_alloc( chr_instance_t * pinst, size_t vlst_size )
{
	if ( NULL == pinst ) return bfalse;

	chr_instance_free( pinst );

	if ( 0 == vlst_size ) return btrue;

	pinst->vrt_lst = EGOBOO_NEW_ARY( GLvertex, vlst_size );
	if ( NULL != pinst->vrt_lst )
	{
		pinst->vrt_count = vlst_size;
	}

	return ( NULL != pinst->vrt_lst );
}

//--------------------------------------------------------------------------------------------
bool_t chr_instance_set_mad( chr_instance_t * pinst, const MAD_REF by_reference imad )
{
	/// @details BB@> try to set the model used by the character instance.
	///     If this fails, it leaves the old data. Just to be safe it
	///     would be best to check whether the old modes is valid, and
	///     if not, the data chould be set to safe values...

	mad_t * pmad;
	bool_t updated = bfalse;
	size_t vlst_size;

	if ( !LOADED_MAD( imad ) ) return bfalse;
	pmad = MadStack.lst + imad;

	if ( NULL == pmad || pmad->md2_ptr == NULL ) 
	{
		log_error("Invalid pmad instance spawn. (Slot number %i)\n", imad );
		return bfalse;
	}

	if ( pinst->imad != imad )
	{
		updated = btrue;
		pinst->imad = imad;
	}

	// set the vertex size
	vlst_size = md2_get_numVertices( pmad->md2_ptr );
	if ( pinst->vrt_count != vlst_size )
	{
		updated = btrue;
		chr_instance_alloc( pinst, vlst_size );
	}

	// set the frames to frame 0 of this object's data
	if ( pinst->frame_nxt != 0 || pinst->frame_lst != 0 )
	{
		updated = btrue;
		pinst->frame_nxt = pinst->frame_lst = 0;

		// the vlst_cache parameters are not valid
		pinst->save.valid = bfalse;
	}

	if ( updated )
	{
		// update the vertex and lighting cache
		chr_instance_clear_cache( pinst );
		chr_instance_update_vertices( pinst, -1, -1, btrue );
	}

	return updated;
}

//--------------------------------------------------------------------------------------------
bool_t chr_instance_update_ref( chr_instance_t * pinst, float floor_level, bool_t need_matrix )
{
	int trans_temp;

	if ( NULL == pinst ) return bfalse;

	if ( need_matrix )
	{
		// reflect the ordinary matrix
		apply_reflection_matrix( pinst, floor_level );
	}

	trans_temp = 255;
	if ( pinst->ref.matrix_valid )
	{
		float pos_z;

		// determine the reflection alpha
		pos_z = floor_level - pinst->ref.matrix.CNV( 3, 2 );
		if ( pos_z < 0 ) pos_z = 0;

		trans_temp -= (( int )pos_z ) >> 1;
		if ( trans_temp < 0 ) trans_temp = 0;

		trans_temp |= gfx.reffadeor;  // Fix for Riva owners
		trans_temp = CLIP( trans_temp, 0, 255 );
	}

	pinst->ref.alpha = ( pinst->alpha * trans_temp * INV_FF ) * 0.5f;
	pinst->ref.light = ( 255 == pinst->light ) ? 255 : ( pinst->light * trans_temp * INV_FF ) * 0.5f;

	pinst->ref.redshift = pinst->redshift + 1;
	pinst->ref.grnshift = pinst->grnshift + 1;
	pinst->ref.blushift = pinst->blushift + 1;

	pinst->ref.sheen    = pinst->sheen >> 1;

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_spawn_instance( chr_instance_t * pinst, const PRO_REF by_reference profile, Uint8 skin )
{
	Sint8 greensave = 0, redsave = 0, bluesave = 0;

	pro_t * pobj;
	cap_t * pcap;

	if ( NULL == pinst ) return bfalse;

	// Remember any previous color shifts in case of lasting enchantments
	greensave = pinst->grnshift;
	redsave   = pinst->redshift;
	bluesave  = pinst->blushift;

	// clear the instance
	chr_instance_ctor( pinst );

	if ( !LOADED_PRO( profile ) ) return bfalse;
	pobj = ProList.lst + profile;

	pcap = pro_get_pcap( profile );

	// lighting parameters
	pinst->texture   = pobj->tex_ref[skin];
	pinst->enviro    = pcap->enviro;
	pinst->alpha     = pcap->alpha;
	pinst->light     = pcap->light;
	pinst->sheen     = pcap->sheen;
	pinst->grnshift  = greensave;
	pinst->redshift  = redsave;
	pinst->blushift  = bluesave;

	// model parameters
	chr_instance_set_mad( pinst, pro_get_imad( profile ) );

	// set the initial action, all actions override it
	chr_instance_play_action( pinst, ACTION_DA, btrue );

	// upload these parameters to the reflection cache, but don't compute the matrix
	chr_instance_update_ref( pinst, 0, bfalse );

	return btrue;
}

//--------------------------------------------------------------------------------------------
grip_offset_t slot_to_grip_offset( slot_t slot )
{
	int retval = GRIP_ORIGIN;

	retval = ( slot + 1 ) * GRIP_VERTS;

	return ( grip_offset_t )retval;
}

//--------------------------------------------------------------------------------------------
slot_t grip_offset_to_slot( grip_offset_t grip_off )
{
	slot_t retval = SLOT_LEFT;

	if ( 0 != grip_off % GRIP_VERTS )
	{
		// this does not correspond to a valid slot
		// coerce it to the "default" slot
		retval = SLOT_LEFT;
	}
	else
	{
		int islot = (( int )grip_off / GRIP_VERTS ) - 1;

		// coerce the slot number to fit within the valid range
		islot = CLIP( islot, 0, SLOT_COUNT );

		retval = ( slot_t ) islot;
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
void init_slot_idsz()
{
	inventory_idsz[INVEN_PACK]  = IDSZ_NONE;
	inventory_idsz[INVEN_NECK]  = MAKE_IDSZ( 'N', 'E', 'C', 'K' );
	inventory_idsz[INVEN_WRIS]  = MAKE_IDSZ( 'W', 'R', 'I', 'S' );
	inventory_idsz[INVEN_FOOT]  = MAKE_IDSZ( 'F', 'O', 'O', 'T' );
}

//--------------------------------------------------------------------------------------------
bool_t ai_add_order( ai_state_t * pai, Uint32 value, Uint16 counter )
{
	bool_t retval;

	if ( NULL == pai ) return bfalse;

	// this function is only truely valid if there is no other order
	retval = HAS_NO_BITS( pai->alert, ALERTIF_ORDERED );

	pai->alert        |= ALERTIF_ORDERED;
	pai->order_value   = value;
	pai->order_counter = counter;

	return retval;
}

//--------------------------------------------------------------------------------------------
BBOARD_REF chr_add_billboard( const CHR_REF by_reference ichr, Uint32 lifetime_secs )
{
	/// @details BB@> Attach a basic billboard to a character. You set the billboard texture
	///     at any time after this. Returns the index of the billboard or INVALID_BILLBOARD
	///     if the allocation fails.
	///
	///    must be called with a valid character, so be careful if you call this function from within
	///    spawn_one_character()

	chr_t * pchr;

	if ( !INGAME_CHR( ichr ) ) return ( BBOARD_REF )INVALID_BILLBOARD;
	pchr = ChrList.lst + ichr;

	if ( INVALID_BILLBOARD != pchr->ibillboard )
	{
		BillboardList_free_one( REF_TO_INT( pchr->ibillboard ) );
		pchr->ibillboard = INVALID_BILLBOARD;
	}

	pchr->ibillboard = BillboardList_get_free( lifetime_secs );

	// attachr the billboard to the character
	if ( INVALID_BILLBOARD != pchr->ibillboard )
	{
		billboard_data_t * pbb = BillboardList.lst + pchr->ibillboard;

		pbb->ichr = ichr;
	}

	return pchr->ibillboard;
}

//--------------------------------------------------------------------------------------------
billboard_data_t * chr_make_text_billboard( const CHR_REF by_reference ichr, const char * txt, SDL_Color text_color, GLXvector4f tint, int lifetime_secs, Uint32 opt_bits )
{
	chr_t            * pchr;
	billboard_data_t * pbb;
	int                rv;

	BBOARD_REF ibb = ( BBOARD_REF )INVALID_BILLBOARD;

	if ( !INGAME_CHR( ichr ) ) return NULL;
	pchr = ChrList.lst + ichr;

	// create a new billboard or override the old billboard
	ibb = chr_add_billboard( ichr, lifetime_secs );
	if ( INVALID_BILLBOARD == ibb ) return NULL;

	pbb = BillboardList_get_ptr( pchr->ibillboard );
	if ( NULL == pbb ) return pbb;

	rv = billboard_data_printf_ttf( pbb, ui_getFont(), text_color, "%s", txt );

	if ( rv < 0 )
	{
		pchr->ibillboard = INVALID_BILLBOARD;
		BillboardList_free_one( REF_TO_INT( ibb ) );
		pbb = NULL;
	}
	else
	{
		// copy the tint over
		memmove( pbb->tint, tint, sizeof(GLXvector4f) );

		if( HAS_SOME_BITS(opt_bits, bb_opt_randomize_pos) )
		{
			// make a random offset from the character
			pbb->offset[XX] = ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * GRID_SIZE / 5.0f;
			pbb->offset[YY] = ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * GRID_SIZE / 5.0f;
			pbb->offset[ZZ] = ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * GRID_SIZE / 5.0f;
		}

		if( HAS_SOME_BITS(opt_bits, bb_opt_randomize_vel) )
		{
			// make the text fly away in a random direction
			pbb->offset_add[XX] += ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * 2.0f * GRID_SIZE / lifetime_secs / TARGET_UPS;
			pbb->offset_add[YY] += ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * 2.0f * GRID_SIZE / lifetime_secs / TARGET_UPS;
			pbb->offset_add[ZZ] += ((( rand() << 1 ) - RAND_MAX ) / ( float )RAND_MAX ) * 2.0f * GRID_SIZE / lifetime_secs / TARGET_UPS;
		}

		if( HAS_SOME_BITS(opt_bits, bb_opt_fade) )
		{
			// make the billboard fade to transparency
			pbb->tint_add[AA] = -1.0f / lifetime_secs / TARGET_UPS;
		}

		if( HAS_SOME_BITS(opt_bits, bb_opt_burn) )
		{
			float minval, maxval;

			minval = MIN(MIN(pbb->tint[RR], pbb->tint[GG]),pbb->tint[BB]);
			maxval = MAX(MAX(pbb->tint[RR], pbb->tint[GG]),pbb->tint[BB]);

			if( pbb->tint[RR] != maxval )
			{
				pbb->tint_add[RR] = -pbb->tint[RR] / lifetime_secs / TARGET_UPS;
			}

			if( pbb->tint[GG] != maxval )
			{
				pbb->tint_add[GG] = -pbb->tint[GG] / lifetime_secs / TARGET_UPS;
			}

			if( pbb->tint[BB] != maxval )
			{
				pbb->tint_add[BB] = -pbb->tint[BB] / lifetime_secs / TARGET_UPS;
			}
		}
	}

	return pbb;
}

//--------------------------------------------------------------------------------------------
const char * chr_get_name( const CHR_REF by_reference ichr, Uint32 bits )
{
	static STRING szName;

	if ( !DEFINED_CHR( ichr ) )
	{
		// the default name
		strncpy( szName, "Unknown", SDL_arraysize( szName ) );
	}
	else
	{
		chr_t * pchr = ChrList.lst + ichr;
		cap_t * pcap = pro_get_pcap( pchr->iprofile );

		if ( pchr->nameknown )
		{
			snprintf( szName, SDL_arraysize( szName ), "%s", pchr->Name );
		}
		else if ( NULL != pcap )
		{
			char lTmp;

			if ( 0 != ( bits & CHRNAME_ARTICLE ) )
			{
				const char * article;

				if ( 0 != ( bits & CHRNAME_DEFINITE ) )
				{
					article = "the";
				}
				else
				{
					lTmp = toupper( pcap->classname[0] );

					if ( 'A' == lTmp || 'E' == lTmp || 'I' == lTmp || 'O' == lTmp || 'U' == lTmp )
					{
						article = "an";
					}
					else
					{
						article = "a";
					}
				}

				snprintf( szName, SDL_arraysize( szName ), "%s %s", article, pcap->classname );
			}
			else
			{
				snprintf( szName, SDL_arraysize( szName ), "%s", pcap->classname );
			}
		}
		else
		{
			strncpy( szName, "Invalid", SDL_arraysize( szName ) );
		}
	}

	if ( 0 != ( bits & CHRNAME_CAPITAL ) )
	{
		// capitalize the name ?
		szName[0] = toupper( szName[0] );
	}

	return szName;
}

//--------------------------------------------------------------------------------------------
const char * chr_get_dir_name( const CHR_REF by_reference ichr )
{
	static STRING buffer = EMPTY_CSTR;
	chr_t * pchr;

	strncpy( buffer, "/debug", SDL_arraysize( buffer ) );

	if ( !DEFINED_CHR( ichr ) ) return buffer;
	pchr = ChrList.lst + ichr;

	if ( !LOADED_PRO( pchr->iprofile ) )
	{
		char * sztmp;

		// copy the character's data.txt path
		strncpy( buffer, pchr->obj_base._name, SDL_arraysize( buffer ) );

		// the name should be "...some path.../data.txt"
		// grab the path

		sztmp = strstr( buffer, "/\\" );
		if ( NULL != sztmp ) *sztmp = CSTR_END;
	}
	else
	{
		pro_t * ppro = ProList.lst + pchr->iprofile;

		// copy the character's data.txt path
		strncpy( buffer, ppro->name, SDL_arraysize( buffer ) );
	}

	return buffer;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_update_collision_size( chr_t * pchr, bool_t update_matrix )
{
	///< @detalis BB@> use this function to update the pchr->chr_prt_cv and  pchr->chr_chr_cv with
	///<       values that reflect the best possible collision volume
	///<
	///< @note This function takes quite a bit of time, so it must only be called when the
	///< vertices change because of an animation or because the matrix changes.
	///<
	///< @todo it might be possible to cache the src[] array used in this function.
	///< if the matrix changes, then you would not need to recalculate this data...

	int       vcount;   // the actual number of vertices, in case the object is square
	fvec4_t   src[16];  // for the upper and lower octagon points
	fvec4_t   dst[16];  // for the upper and lower octagon points

	oct_bb_t bsrc;

	mad_t * pmad;
	oct_bb_t * pbmp;

	if ( !DEFINED_PCHR( pchr ) ) return rv_error;
	pbmp = &( pchr->chr_chr_cv );

	pmad = chr_get_pmad( GET_REF_PCHR( pchr ) );
	if ( NULL == pmad ) return rv_error;

	// make sure the matrix is updated properly
	if ( update_matrix )
	{
		// call chr_update_matrix() but pass in a false value to prevent a recursize call
		if ( rv_error == chr_update_matrix( pchr, bfalse ) )
		{
			return rv_error;
		}
	}

	// make sure the bounding box is calculated properly
	if ( rv_error == chr_instance_update_bbox( &( pchr->inst ) ) )
	{
		return rv_error;
	}

	// convert the point cloud in the GLvertex array (pchr->inst.vrt_lst) to
	// a level 1 bounding box. Subtract off the position of the character
	memcpy( &bsrc, &( pchr->inst.bbox ), sizeof( bsrc ) );

	// convert the corners of the level 1 bounding box to a point cloud
	vcount = oct_bb_to_points( &bsrc, src, 16 );

	// transform the new point cloud
	TransformVertices( &( pchr->inst.matrix ), src, dst, vcount );

	// convert the new point cloud into a level 1 bounding box
	points_to_oct_bb( pbmp, dst, vcount );

	// convert the level 1 bounding box to a level 0 bounding box
	oct_bb_downgrade( pbmp, pchr->bump, &( pchr->bump_1 ), &( pchr->chr_prt_cv ) );

	return rv_success;
}

//--------------------------------------------------------------------------------------------
const char* describe_value( float value, float maxval, int * rank_ptr )
{
	/// @details ZF@> This converts a stat number into a more descriptive word

	static STRING retval;

	float fval;
	int local_rank;

	if ( NULL == rank_ptr ) rank_ptr = &local_rank;

	if ( cfg.feedback == FEEDBACK_NUMBER )
	{
		snprintf( retval, SDL_arraysize( retval ), "%2.1f", FP8_TO_FLOAT( value ) );
		return retval;
	}

	fval = ( 0 == maxval ) ? 1.0f : value / maxval;

	*rank_ptr = -5;
	strcpy( retval, "Unknown" );

	if ( fval >= .83 )		{ strcpy( retval, "Godlike!" );   *rank_ptr =  8; }
	else if ( fval >= .66 ) { strcpy( retval, "Ultimate" );   *rank_ptr =  7; }
	else if ( fval >= .56 ) { strcpy( retval, "Epic" );       *rank_ptr =  6; }
	else if ( fval >= .50 ) { strcpy( retval, "Powerful" );   *rank_ptr =  5; }
	else if ( fval >= .43 ) { strcpy( retval, "Heroic" );     *rank_ptr =  4; }
	else if ( fval >= .36 ) { strcpy( retval, "Very High" );  *rank_ptr =  3; }
	else if ( fval >= .30 ) { strcpy( retval, "High" );       *rank_ptr =  2; }
	else if ( fval >= .23 ) { strcpy( retval, "Good" );       *rank_ptr =  1; }
	else if ( fval >= .17 ) { strcpy( retval, "Average" );    *rank_ptr =  0; }
	else if ( fval >= .11 ) { strcpy( retval, "Pretty Low" ); *rank_ptr = -1; }
	else if ( fval >= .07 ) { strcpy( retval, "Bad" );        *rank_ptr = -2; }
	else if ( fval >  .00 ) { strcpy( retval, "Terrible" );   *rank_ptr = -3; }
	else                    { strcpy( retval, "None" );       *rank_ptr = -4; }

	return retval;
}

//--------------------------------------------------------------------------------------------
const char* describe_damage( float value, float maxval, int * rank_ptr )
{
	/// @details ZF@> This converts a damage value into a more descriptive word

	static STRING retval;

	float fval;
	int local_rank;

	if ( NULL == rank_ptr ) rank_ptr = &local_rank;

	if ( cfg.feedback == FEEDBACK_NUMBER )
	{
		snprintf( retval, SDL_arraysize( retval ), "%2.1f", FP8_TO_FLOAT( value ) );
		return retval;
	}

	fval = ( 0 == maxval ) ? 1.0f : value / maxval;

	*rank_ptr = -5;
	strcpy( retval, "Unknown" );

	if ( fval >= 1.50 )		 { strcpy( retval, "Annihilation!" ); *rank_ptr =  5; }
	else if ( fval >= 1.00 ) { strcpy( retval, "Overkill!" );     *rank_ptr =  4; }
	else if ( fval >= 0.80 ) { strcpy( retval, "Deadly" );        *rank_ptr =  3; }
	else if ( fval >= 0.70 ) { strcpy( retval, "Crippling" );     *rank_ptr =  2; }
	else if ( fval >= 0.50 ) { strcpy( retval, "Devastating" );   *rank_ptr =  1; }
	else if ( fval >= 0.25 ) { strcpy( retval, "Hurtful" );       *rank_ptr =  0; }
	else if ( fval >= 0.10 ) { strcpy( retval, "A Scratch" );     *rank_ptr = -1; }
	else if ( fval >= 0.05 ) { strcpy( retval, "Ticklish" );      *rank_ptr = -2; }
	else if ( fval >= 0.00 ) { strcpy( retval, "Meh..." );        *rank_ptr = -3; }

	return retval;
}

//--------------------------------------------------------------------------------------------
const char* describe_wounds( float max, float current )
{
	/// @details ZF@> This tells us how badly someone is injured

	static STRING retval;
	float fval;

	//Is it already dead?
	if( current <= 0 )
	{
		strcpy( retval, "Dead!" );
		return retval;
	}

	//Calculate the percentage
	if( max == 0 ) return NULL;
	fval = (current/ max) * 100;

	if ( cfg.feedback == FEEDBACK_NUMBER )
	{
		snprintf( retval, SDL_arraysize( retval ), "%2.1f", FP8_TO_FLOAT( current ) );
		return retval;
	}

	strcpy( retval, "Uninjured" );
	if	( fval <= 5  )			strcpy( retval, "Almost Dead!" );
	else if ( fval <= 10 )	    strcpy( retval, "Near Death" );
	else if ( fval <= 25 )	    strcpy( retval, "Mortally Wounded" );
	else if ( fval <= 40 )		strcpy( retval, "Badly Wounded" );
	else if ( fval <= 60 )		strcpy( retval, "Injured" );
	else if ( fval <= 75 )		strcpy( retval, "Hurt" );
	else if ( fval <= 90 )		strcpy( retval, "Bruised" );
	else if ( fval < 100 )		strcpy( retval, "Scratched" );

	return retval;
}

//--------------------------------------------------------------------------------------------
TX_REF chr_get_icon_ref( const CHR_REF by_reference item )
{
	/// @details BB@> Get the index to the icon texture (in TxTexture) that is supposed to be used with this object.
	///               If none can be found, return the index to the texture of the null icon.

	size_t iskin;
	TX_REF icon_ref = ( TX_REF )ICON_NULL;
	bool_t is_spell_fx, is_book, draw_book;

	cap_t * pitem_cap;
	chr_t * pitem;
	pro_t * pitem_pro;

	if ( !DEFINED_CHR( item ) ) return icon_ref;
	pitem = ChrList.lst + item;

	if ( !LOADED_PRO( pitem->iprofile ) ) return icon_ref;
	pitem_pro = ProList.lst + pitem->iprofile;

	pitem_cap = pro_get_pcap( pitem->iprofile );
	if ( NULL == pitem_cap ) return icon_ref;

	// what do we need to draw?
	is_spell_fx = (NO_SKIN_OVERRIDE != pitem_cap->spelleffect_type);       // the value of spelleffect_type == the skin of the book or -1 for not a spell effect
	is_book     = (SPELLBOOK == pitem->iprofile);
	draw_book = ( is_book || ( is_spell_fx && !pitem->draw_icon ) || ( is_spell_fx && MAX_CHR != pitem->attachedto ) ) && ( bookicon_count > 0 );

	if ( !draw_book )
	{
		iskin = pitem->skin;

		icon_ref = pitem_pro->ico_ref[iskin];
	}
	else if ( draw_book )
	{
		iskin = 0;

		if ( pitem_cap->spelleffect_type > 0 )
		{
			iskin = pitem_cap->spelleffect_type;
		}
		else if ( pitem_cap->skin_override > 0 )
		{
			iskin = pitem_cap->skin_override;
		}

		iskin = CLIP( iskin, 0, bookicon_count );

		icon_ref = bookicon_ref[ iskin ];
	}

	return icon_ref;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
void init_all_cap()
{
	/// @details BB@> initialize every character profile in the game

	CAP_REF cnt;

	for ( cnt = 0; cnt < MAX_PROFILE; cnt++ )
	{
		cap_init( CapStack.lst + cnt );
	}
}

//--------------------------------------------------------------------------------------------
void release_all_cap()
{
	/// @details BB@> release every character profile in the game

	CAP_REF cnt;

	for ( cnt = 0; cnt < MAX_PROFILE; cnt++ )
	{
		release_one_cap( cnt );
	};
}

//--------------------------------------------------------------------------------------------
bool_t release_one_cap( const CAP_REF by_reference icap )
{
	/// @details BB@> release any allocated data and return all values to safe values

	cap_t * pcap;

	if ( !VALID_CAP_RANGE( icap ) ) return bfalse;
	pcap = CapStack.lst + icap;

	if ( !pcap->loaded ) return btrue;

	cap_init( pcap );

	pcap->loaded  = bfalse;
	pcap->name[0] = CSTR_END;

	return btrue;
}

//--------------------------------------------------------------------------------------------
void reset_teams()
{
	/// @details ZZ@> This function makes everyone hate everyone else

	TEAM_REF teama, teamb;

	for ( teama = 0; teama < TEAM_MAX; teama++ )
	{
		// Make the team hate everyone
		for ( teamb = 0; teamb < TEAM_MAX; teamb++ )
		{
			TeamStack.lst[teama].hatesteam[REF_TO_INT( teamb )] = btrue;
		}

		// Make the team like itself
		TeamStack.lst[teama].hatesteam[REF_TO_INT( teama )] = bfalse;

		// Set defaults
		TeamStack.lst[teama].leader = NOLEADER;
		TeamStack.lst[teama].sissy = 0;
		TeamStack.lst[teama].morale = 0;
	}

	// Keep the null team neutral
	for ( teama = 0; teama < TEAM_MAX; teama++ )
	{
		TeamStack.lst[teama].hatesteam[TEAM_NULL] = bfalse;
		TeamStack.lst[( TEAM_REF )TEAM_NULL].hatesteam[REF_TO_INT( teama )] = bfalse;
	}
}

//--------------------------------------------------------------------------------------------
bool_t chr_teleport( const CHR_REF by_reference ichr, float x, float y, float z, FACING_T facing_z )
{
	/// @details BB@> Determine whether the character can be teleported to the specified location
	///               and do it, if possible. Success returns btrue, failure returns bfalse;

	chr_t  * pchr;
	FACING_T facing_old, facing_new;
	fvec3_t  pos_old, pos_new;
	bool_t   retval;

	if ( !INGAME_CHR( ichr ) ) return bfalse;
	pchr = ChrList.lst + ichr;

	if ( x < 0.0f || x > PMesh->gmem.edge_x ) return bfalse;
	if ( y < 0.0f || y > PMesh->gmem.edge_y ) return bfalse;

	pos_old  = chr_get_pos( pchr );
	facing_old = pchr->ori.facing_z;

	pos_new.x  = x;
	pos_new.y  = y;
	pos_new.z  = z;
	facing_new = facing_z;

	if ( chr_hit_wall( pchr, pos_new.v, NULL, NULL ) )
	{
		// No it didn't...
		chr_set_pos( pchr, pos_old.v );
		pchr->ori.facing_z = facing_old;

		retval = bfalse;
	}
	else
	{
		// Yeah!  It worked!

		// update the old position
		pchr->pos_old          = pos_new;
		pchr->ori_old.facing_z = facing_new;

		// update the new position
		chr_set_pos( pchr, pos_new.v );
		pchr->ori.facing_z = facing_new;

		if ( !detach_character_from_mount( ichr, btrue, bfalse ) )
		{
			// detach_character_from_mount() updates the character matrix unless it is not mounted
			chr_update_matrix( pchr, btrue );
		}

		retval = btrue;
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
bool_t chr_request_terminate( const CHR_REF by_reference ichr )
{
	/// @details BB@> Mark this character for deletion

	if ( !DEFINED_CHR( ichr ) ) return bfalse;

	POBJ_REQUEST_TERMINATE( ChrList.lst + ichr );

	return btrue;
}

//--------------------------------------------------------------------------------------------
chr_t * chr_update_hide( chr_t * pchr )
{
	/// @details BB@> Update the hide state of the character. Should be called every time that
	///               the state variable in an ai_state_t structure is updated

	Sint8 hide;
	cap_t * pcap;

	if ( !DEFINED_PCHR( pchr ) ) return pchr;

	hide = NOHIDE;
	pcap = chr_get_pcap( GET_REF_PCHR( pchr ) );
	if ( NULL != pcap )
	{
		hide = pcap->hidestate;
	}

	pchr->is_hidden = bfalse;
	if ( hide != NOHIDE && hide == pchr->ai.state )
	{
		pchr->is_hidden = btrue;
	}

	return pchr;
}

//--------------------------------------------------------------------------------------------
bool_t ai_state_set_changed( ai_state_t * pai )
{
	/// @details BB@> do something tricky here

	bool_t retval = bfalse;

	if ( NULL == pai ) return bfalse;

	if ( HAS_NO_BITS( pai->alert, ALERTIF_CHANGED ) )
	{
		pai->alert |= ALERTIF_CHANGED;
		retval = btrue;
	}

	if ( !pai->changed )
	{
		pai->changed = btrue;
		retval = btrue;
	}

	return retval;
}

//--------------------------------------------------------------------------------------------
matrix_cache_t * matrix_cache_init( matrix_cache_t * mcache )
{
	/// @details BB@> clear out the matrix cache data

	int cnt;

	if ( NULL == mcache ) return mcache;

	memset( mcache, 0, sizeof( *mcache ) );

	mcache->type_bits = MAT_UNKNOWN;
	mcache->grip_chr  = ( CHR_REF )MAX_CHR;
	for ( cnt = 0; cnt < GRIP_VERTS; cnt++ )
	{
		mcache->grip_verts[cnt] = 0xFFFF;
	}

	mcache->rotate.x = 0;
	mcache->rotate.y = 0;
	mcache->rotate.z = 0;

	return mcache;
}

//--------------------------------------------------------------------------------------------
bool_t chr_matrix_valid( chr_t * pchr )
{
	/// @details BB@> Determine whether the character has a valid matrix

	if ( !DEFINED_PCHR( pchr ) ) return bfalse;

	// both the cache and the matrix need to be valid
	return pchr->inst.matrix_cache.valid && pchr->inst.matrix_cache.matrix_valid;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
int get_grip_verts( Uint16 grip_verts[], const CHR_REF by_reference imount, int vrt_offset )
{
	/// @details BB@> Fill the grip_verts[] array from the mount's data.
	///     Return the number of vertices found.

	Uint32  i;
	int vrt_count, tnc;

	chr_t * pmount;
	mad_t * pmount_mad;

	if ( NULL == grip_verts ) return 0;

	// set all the vertices to a "safe" value
	for ( i = 0; i < GRIP_VERTS; i++ )
	{
		grip_verts[i] = 0xFFFF;
	}

	if ( !INGAME_CHR( imount ) ) return 0;
	pmount = ChrList.lst + imount;

	pmount_mad = chr_get_pmad( imount );
	if ( NULL == pmount_mad ) return 0;

	if ( 0 == pmount->inst.vrt_count ) return 0;

	//---- set the proper weapongrip vertices
	tnc = ( int )pmount->inst.vrt_count - ( int )vrt_offset;

	// if the starting vertex is less than 0, just take the first vertex
	if ( tnc < 0 )
	{
		grip_verts[0] = 0;
		return 1;
	}

	vrt_count = 0;
	for ( i = 0; i < GRIP_VERTS; i++ )
	{
		if ( tnc + i < pmount->inst.vrt_count )
		{
			grip_verts[i] = tnc + i;
			vrt_count++;
		}
		else
		{
			grip_verts[i] = 0xFFFF;
		}
	}

	return vrt_count;
}

//--------------------------------------------------------------------------------------------
bool_t chr_get_matrix_cache( chr_t * pchr, matrix_cache_t * mc_tmp )
{
	/// @details BB@> grab the matrix cache data for a given character and put it into mc_tmp.

	bool_t handled;
	CHR_REF itarget, ichr;

	if ( NULL == mc_tmp ) return bfalse;
	if ( !DEFINED_PCHR( pchr ) ) return bfalse;
	ichr = GET_REF_PCHR( pchr );

	handled = bfalse;
	itarget = ( CHR_REF )MAX_CHR;

	// initialize xome parameters in case we fail
	mc_tmp->valid     = bfalse;
	mc_tmp->type_bits = MAT_UNKNOWN;

	mc_tmp->self_scale.x = mc_tmp->self_scale.y = mc_tmp->self_scale.z = pchr->fat;

	// handle the overlay first of all
	if ( !handled && pchr->is_overlay && ichr != pchr->ai.target && INGAME_CHR( pchr->ai.target ) )
	{
		// this will pretty much fail the cmp_matrix_cache() every time...

		chr_t * ptarget = ChrList.lst + pchr->ai.target;

		// make sure we have the latst info from the target
		chr_update_matrix( ptarget, btrue );

		// grab the matrix cache into from the character we are overlaying
		memcpy( mc_tmp, &( ptarget->inst.matrix_cache ), sizeof( matrix_cache_t ) );

		// just in case the overlay's matrix cannot be corrected
		// then treat it as if it is not an overlay
		handled = mc_tmp->valid;
	}

	// this will happen if the overlay "failed" or for any non-overlay character
	if ( !handled )
	{
		// assume that the "target" of the MAT_CHARACTER data will be the character itself
		itarget = GET_REF_PCHR( pchr );

		//---- update the MAT_WEAPON data
		if ( DEFINED_CHR( pchr->attachedto ) )
		{
			chr_t * pmount = ChrList.lst + pchr->attachedto;

			// make sure we have the latst info from the target
			chr_update_matrix( pmount, btrue );

			// just in case the mounts's matrix cannot be corrected
			// then treat it as if it is not mounted... yuck
			if ( pmount->inst.matrix_cache.matrix_valid )
			{
				mc_tmp->valid     = btrue;
				mc_tmp->type_bits     |= MAT_WEAPON;        // add in the weapon data

				mc_tmp->grip_chr  = pchr->attachedto;
				mc_tmp->grip_slot = pchr->inwhich_slot;
				get_grip_verts( mc_tmp->grip_verts, pchr->attachedto, slot_to_grip_offset( pchr->inwhich_slot ) );

				itarget = pchr->attachedto;
			}
		}

		//---- update the MAT_CHARACTER data
		if ( DEFINED_CHR( itarget ) )
		{
			chr_t * ptarget = ChrList.lst + itarget;

			mc_tmp->valid   = btrue;
			mc_tmp->type_bits   |= MAT_CHARACTER;  // add in the MAT_CHARACTER-type data for the object we are "connected to"

			mc_tmp->rotate.x = CLIP_TO_16BITS( ptarget->ori.map_facing_x - MAP_TURN_OFFSET );
			mc_tmp->rotate.y = CLIP_TO_16BITS( ptarget->ori.map_facing_y - MAP_TURN_OFFSET );
			mc_tmp->rotate.z = ptarget->ori.facing_z;

			mc_tmp->pos = chr_get_pos( ptarget );

			mc_tmp->grip_scale.x = mc_tmp->grip_scale.y = mc_tmp->grip_scale.z = ptarget->fat;
		}
	}

	return mc_tmp->valid;
}

//--------------------------------------------------------------------------------------------
int convert_grip_to_local_points( chr_t * pholder, Uint16 grip_verts[], fvec4_t dst_point[] )
{
	/// @details ZZ@> a helper function for apply_one_weapon_matrix()

	int cnt, point_count;

	if ( NULL == grip_verts || NULL == dst_point ) return 0;

	if ( !ACTIVE_PCHR( pholder ) ) return 0;

	// count the valid weapon connection dst_points
	point_count = 0;
	for ( cnt = 0; cnt < GRIP_VERTS; cnt++ )
	{
		if ( 0xFFFF != grip_verts[cnt] )
		{
			point_count++;
		}
	}

	// do the best we can
	if ( 0 == point_count )
	{
		// punt! attach to origin
		dst_point[0].x = pholder->pos.x;
		dst_point[0].y = pholder->pos.y;
		dst_point[0].z = pholder->pos.z;
		dst_point[0].w = 1;

		point_count = 1;
	}
	else
	{
		// update the grip vertices
		chr_instance_update_grip_verts( &( pholder->inst ), grip_verts, GRIP_VERTS );

		// copy the vertices into dst_point[]
		for ( point_count = 0, cnt = 0; cnt < GRIP_VERTS; cnt++, point_count++ )
		{
			Uint16 vertex = grip_verts[cnt];

			if ( 0xFFFF == vertex ) continue;

			dst_point[point_count].x = pholder->inst.vrt_lst[vertex].pos[XX];
			dst_point[point_count].y = pholder->inst.vrt_lst[vertex].pos[YY];
			dst_point[point_count].z = pholder->inst.vrt_lst[vertex].pos[ZZ];
			dst_point[point_count].w = 1.0f;
		}
	}

	return point_count;
}

//--------------------------------------------------------------------------------------------
int convert_grip_to_global_points( const CHR_REF by_reference iholder, Uint16 grip_verts[], fvec4_t   dst_point[] )
{
	/// @details ZZ@> a helper function for apply_one_weapon_matrix()

	chr_t *   pholder;
	int       point_count;
	fvec4_t   src_point[GRIP_VERTS];

	if ( !INGAME_CHR( iholder ) ) return 0;
	pholder = ChrList.lst + iholder;

	// find the grip points in the character's "local" or "body-fixed" coordinates
	point_count = convert_grip_to_local_points( pholder, grip_verts, src_point );

	if ( 0 == point_count ) return 0;

	// use the math function instead of rolling out own
	TransformVertices( &( pholder->inst.matrix ), src_point, dst_point, point_count );

	return point_count;
}

//--------------------------------------------------------------------------------------------
bool_t apply_one_weapon_matrix( chr_t * pweap, matrix_cache_t * mc_tmp )
{
	/// @details ZZ@> Request that the data in the matrix cache be used to create a "character matrix".
	///               i.e. a matrix that is not being held by anything.

	fvec4_t   nupoint[GRIP_VERTS];
	int       iweap_points;

	chr_t * pholder;
	matrix_cache_t * pweap_mcache;

	if ( NULL == mc_tmp || !mc_tmp->valid || 0 == ( MAT_WEAPON & mc_tmp->type_bits ) ) return bfalse;

	if ( !DEFINED_PCHR( pweap ) ) return bfalse;
	pweap_mcache = &( pweap->inst.matrix_cache );

	if ( !INGAME_CHR( mc_tmp->grip_chr ) ) return bfalse;
	pholder = ChrList.lst + mc_tmp->grip_chr;

	// make sure that the matrix is invalid incase of an error
	pweap_mcache->matrix_valid = bfalse;

	// grab the grip points in world coordinates
	iweap_points = convert_grip_to_global_points( mc_tmp->grip_chr, mc_tmp->grip_verts, nupoint );

	if ( 4 == iweap_points )
	{
		// Calculate weapon's matrix based on positions of grip points
		// chrscale is recomputed at time of attachment
		pweap->inst.matrix = FourPoints( nupoint[0].v, nupoint[1].v, nupoint[2].v, nupoint[3].v, mc_tmp->self_scale.z );

		// update the weapon position
		chr_set_pos( pweap, nupoint[3].v );

		memcpy( &( pweap->inst.matrix_cache ), mc_tmp, sizeof( matrix_cache_t ) );

		pweap_mcache->matrix_valid = btrue;
	}
	else if ( iweap_points > 0 )
	{
		// cannot find enough vertices. punt.
		// ignore the shape of the grip and just stick the character to the single mount point

		// update the character position
		chr_set_pos( pweap, nupoint[0].v );

		// make sure we have the right data
		chr_get_matrix_cache( pweap, mc_tmp );

		// add in the appropriate mods
		// this is a hybrid character and weapon matrix
		mc_tmp->type_bits  |= MAT_CHARACTER;

		// treat it like a normal character matrix
		apply_one_character_matrix( pweap, mc_tmp );
	}

	return pweap_mcache->matrix_valid;
}

//--------------------------------------------------------------------------------------------
bool_t apply_one_character_matrix( chr_t * pchr, matrix_cache_t * mc_tmp )
{
	/// @details ZZ@> Request that the matrix cache data be used to create a "weapon matrix".
	///               i.e. a matrix that is attached to a specific grip.

	if ( NULL == mc_tmp ) return bfalse;

	// only apply character matrices using this function
	if ( 0 == ( MAT_CHARACTER & mc_tmp->type_bits ) ) return bfalse;

	pchr->inst.matrix_cache.matrix_valid = bfalse;

	if ( !DEFINED_PCHR( pchr ) ) return bfalse;

	pchr->inst.matrix = ScaleXYZRotateXYZTranslate( mc_tmp->self_scale.x, mc_tmp->self_scale.y, mc_tmp->self_scale.z,
		TO_TURN( mc_tmp->rotate.z ), TO_TURN( mc_tmp->rotate.x ), TO_TURN( mc_tmp->rotate.y ),
		mc_tmp->pos.x, mc_tmp->pos.y, mc_tmp->pos.z );

	memcpy( &( pchr->inst.matrix_cache ), mc_tmp, sizeof( matrix_cache_t ) );

	pchr->inst.matrix_cache.matrix_valid = btrue;

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t apply_reflection_matrix( chr_instance_t * pinst, float floor_level )
{
	/// @detalis BB@> Generate the extra data needed to display a reflection for this character

	if ( NULL == pinst ) return bfalse;

	pinst->ref.matrix_valid = bfalse;

	// actually flip the matrix
	pinst->ref.matrix_valid = pinst->matrix_cache.valid;

	if ( pinst->ref.matrix_valid )
	{
		pinst->ref.matrix = pinst->matrix;

		pinst->ref.matrix.CNV( 0, 2 ) = -pinst->ref.matrix.CNV( 0, 2 );
		pinst->ref.matrix.CNV( 1, 2 ) = -pinst->ref.matrix.CNV( 1, 2 );
		pinst->ref.matrix.CNV( 2, 2 ) = -pinst->ref.matrix.CNV( 2, 2 );
		pinst->ref.matrix.CNV( 3, 2 ) = 2 * floor_level - pinst->ref.matrix.CNV( 3, 2 );
	}

	return pinst->ref.matrix_valid;
}

//--------------------------------------------------------------------------------------------
bool_t apply_matrix_cache( chr_t * pchr, matrix_cache_t * mc_tmp )
{
	/// @detalis BB@> request that the info in the matrix cache mc_tmp, be used to
	///               make a matrix for the character pchr.

	bool_t applied = bfalse;

	if ( !DEFINED_PCHR( pchr ) ) return bfalse;
	if ( NULL == mc_tmp || !mc_tmp->valid ) return bfalse;

	if ( 0 != ( MAT_WEAPON & mc_tmp->type_bits ) )
	{
		if ( DEFINED_CHR( mc_tmp->grip_chr ) )
		{
			applied = apply_one_weapon_matrix( pchr, mc_tmp );
		}
		else
		{
			matrix_cache_t * mcache = &( pchr->inst.matrix_cache );

			// !!!the mc_tmp was mis-labeled as a MAT_WEAPON!!!
			make_one_character_matrix( GET_REF_PCHR( pchr ) );

			// recover the matrix_cache values from the character
			mcache->type_bits |= MAT_CHARACTER;
			if ( mcache->matrix_valid )
			{
				mcache->valid     = btrue;
				mcache->type_bits = MAT_CHARACTER;

				mcache->self_scale.x =
					mcache->self_scale.y =
					mcache->self_scale.z = pchr->fat;

				mcache->grip_scale = mcache->self_scale;

				mcache->rotate.x = CLIP_TO_16BITS( pchr->ori.map_facing_x - MAP_TURN_OFFSET );
				mcache->rotate.y = CLIP_TO_16BITS( pchr->ori.map_facing_y - MAP_TURN_OFFSET );
				mcache->rotate.z = pchr->ori.facing_z;

				mcache->pos = chr_get_pos( pchr );

				applied = btrue;
			}
		}
	}
	else if ( 0 != ( MAT_CHARACTER & mc_tmp->type_bits ) )
	{
		applied = apply_one_character_matrix( pchr, mc_tmp );
	}

	if ( applied )
	{
		apply_reflection_matrix( &( pchr->inst ), pchr->enviro.floor_level );
	}

	return applied;
}

//--------------------------------------------------------------------------------------------
int cmp_matrix_cache( const void * vlhs, const void * vrhs )
{
	/// @details BB@> check for differences between the data pointed to
	///     by vlhs and vrhs, assuming that they point to matrix_cache_t data.
	///
	///    The function is implemented this way so that in pronciple
	///    if could be used in a function like qsort().
	///
	///    We could almost certainly make something easier and quicker by
	///    using the function memcmp()

	int   itmp, cnt;
	float ftmp;

	matrix_cache_t * plhs = ( matrix_cache_t * )vlhs;
	matrix_cache_t * prhs = ( matrix_cache_t * )vrhs;

	// handle problems with pointers
	if ( plhs == prhs )
	{
		return 0;
	}
	else if ( NULL == plhs )
	{
		return 1;
	}
	else if ( NULL == prhs )
	{
		return -1;
	}

	// handle one of both if the matrix caches being invalid
	if ( !plhs->valid && !prhs->valid )
	{
		return 0;
	}
	else if ( !plhs->valid )
	{
		return 1;
	}
	else if ( !prhs->valid )
	{
		return -1;
	}

	// handle differences in the type
	itmp = plhs->type_bits - prhs->type_bits;
	if ( 0 != itmp ) goto cmp_matrix_cache_end;

	//---- check for differences in the MAT_WEAPON data
	if ( 0 != ( plhs->type_bits & MAT_WEAPON ) )
	{
		itmp = ( signed )REF_TO_INT( plhs->grip_chr ) - ( signed )REF_TO_INT( prhs->grip_chr );
		if ( 0 != itmp ) goto cmp_matrix_cache_end;

		itmp = ( signed )plhs->grip_slot - ( signed )prhs->grip_slot;
		if ( 0 != itmp ) goto cmp_matrix_cache_end;

		for ( cnt = 0; cnt < GRIP_VERTS; cnt++ )
		{
			itmp = ( signed )plhs->grip_verts[cnt] - ( signed )prhs->grip_verts[cnt];
			if ( 0 != itmp ) goto cmp_matrix_cache_end;
		}

		// handle differences in the scale of our mount
		for ( cnt = 0; cnt < 3; cnt ++ )
		{
			ftmp = plhs->grip_scale.v[cnt] - prhs->grip_scale.v[cnt];
			if ( 0.0f != ftmp ) { itmp = SGN( ftmp ); goto cmp_matrix_cache_end; }
		}
	}

	//---- check for differences in the MAT_CHARACTER data
	if ( 0 != ( plhs->type_bits & MAT_CHARACTER ) )
	{
		// handle differences in the "Euler" rotation angles in 16-bit form
		for ( cnt = 0; cnt < 3; cnt++ )
		{
			ftmp = plhs->rotate.v[cnt] - prhs->rotate.v[cnt];
			if ( 0.0f != ftmp ) { itmp = SGN( ftmp ); goto cmp_matrix_cache_end; }
		}

		// handle differences in the translate vector
		for ( cnt = 0; cnt < 3; cnt++ )
		{
			ftmp = plhs->pos.v[cnt] - prhs->pos.v[cnt];
			if ( 0.0f != ftmp ) { itmp = SGN( ftmp ); goto cmp_matrix_cache_end; }
		}
	}

	//---- check for differences in the shared data
	if ( 0 != ( plhs->type_bits & MAT_WEAPON ) || 0 != ( plhs->type_bits & MAT_CHARACTER ) )
	{
		// handle differences in our own scale
		for ( cnt = 0; cnt < 3; cnt ++ )
		{
			ftmp = plhs->self_scale.v[cnt] - prhs->self_scale.v[cnt];
			if ( 0.0f != ftmp ) { itmp = SGN( ftmp ); goto cmp_matrix_cache_end; }
		}
	}

	// if it got here, the data is all the same
	itmp = 0;

cmp_matrix_cache_end:

	return SGN( itmp );
}

//--------------------------------------------------------------------------------------------
egoboo_rv matrix_cache_needs_update( chr_t * pchr, matrix_cache_t * pmc )
{
	/// @details BB@> determine whether a matrix cache has become invalid and needs to be updated

	matrix_cache_t local_mc;
	bool_t needs_cache_update;

	if ( !DEFINED_PCHR( pchr ) ) return rv_error;

	if ( NULL == pmc ) pmc = &local_mc;

	// get the matrix data that is supposed to be used to make the matrix
	chr_get_matrix_cache( pchr, pmc );

	// compare that data to the actual data used to make the matrix
	needs_cache_update = ( 0 != cmp_matrix_cache( pmc, &( pchr->inst.matrix_cache ) ) );

	return needs_cache_update ? rv_success : rv_fail;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_update_matrix( chr_t * pchr, bool_t update_size )
{
	/// @details BB@> Do everything necessary to set the current matrix for this character.
	///     This might include recursively going down the list of this character's mounts, etc.
	///
	///     Return btrue if a new matrix is applied to the character, bfalse otherwise.

	bool_t         needs_update = bfalse;
	bool_t         applied      = bfalse;
	matrix_cache_t mc_tmp;
	egoboo_rv      retval;

	if ( !DEFINED_PCHR( pchr ) ) return rv_error;

	// recursively make sure that any mount matrices are updated
	if ( DEFINED_CHR( pchr->attachedto ) )
	{
		// if this fails, we should probably do something...
		if ( rv_error == chr_update_matrix( ChrList.lst + pchr->attachedto, btrue ) )
		{
			pchr->inst.matrix_cache.matrix_valid = bfalse;
			return rv_error;
		}
	}

	// does the matrix cache need an update at all?
	retval = matrix_cache_needs_update( pchr, &mc_tmp );
	if ( rv_error == retval ) return rv_error;
	needs_update = ( rv_success == retval );

	// Update the grip vertices no matter what (if they are used)
	if ( 0 != ( MAT_WEAPON & mc_tmp.type_bits ) && INGAME_CHR( mc_tmp.grip_chr ) )
	{
		egoboo_rv retval;
		chr_t   * ptarget = ChrList.lst + mc_tmp.grip_chr;

		// has that character changes its animation?
		retval = chr_instance_update_grip_verts( &( ptarget->inst ), mc_tmp.grip_verts, GRIP_VERTS );

		if ( rv_error   == retval ) return rv_error;
		if ( rv_success == retval ) needs_update = btrue;
	}

	// if it is not the same, make a new matrix with the new data
	applied = bfalse;
	if ( needs_update )
	{
		// we know the matrix is not valid
		pchr->inst.matrix_cache.matrix_valid = bfalse;

		applied = apply_matrix_cache( pchr, &mc_tmp );
	}

	if ( applied && update_size )
	{
		// call chr_update_collision_size() but pass in a false value to prevent a recursize call
		chr_update_collision_size( pchr, bfalse );
	}

	return applied ? rv_success : rv_fail;
}

//--------------------------------------------------------------------------------------------
bool_t ai_state_set_bumplast( ai_state_t * pself, const CHR_REF by_reference ichr )
{
	/// @details BB@> bumping into a chest can initiate whole loads of update messages.
	///     Try to throttle the rate that new "bump" messages can be passed to the ai

	if ( NULL == pself ) return bfalse;

	if ( !INGAME_CHR( ichr ) ) return bfalse;

	// 5 bumps per second?
	if ( pself->bumplast != ichr ||  update_wld > pself->bumplast_time + TARGET_UPS / 5 )
	{
		pself->bumplast_time = update_wld;
		pself->alert |= ALERTIF_BUMPED;
	}
	pself->bumplast = ichr;

	return btrue;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
CHR_REF chr_has_inventory_idsz( const CHR_REF by_reference ichr, IDSZ idsz, bool_t equipped, CHR_REF * pack_last )
{
	/// @details BB@> check the pack a matching item

	bool_t matches_equipped;
	CHR_REF item, tmp_item, tmp_var;
	chr_t * pchr;

	if ( !INGAME_CHR( ichr ) ) return ( CHR_REF )MAX_CHR;
	pchr = ChrList.lst + ichr;

	// make sure that pack_last points to something
	if ( NULL == pack_last ) pack_last = &tmp_var;

	item = ( CHR_REF )MAX_CHR;

	*pack_last = GET_REF_PCHR( pchr );

	PACK_BEGIN_LOOP( tmp_item, pchr->pack.next )
	{
		matches_equipped = ( !equipped || ( INGAME_CHR( tmp_item ) && ChrList.lst[tmp_item].isequipped ) );

		if ( chr_is_type_idsz( tmp_item, idsz ) && matches_equipped )
		{
			item = tmp_item;
			break;
		}

		*pack_last = tmp_item;
	}
	PACK_END_LOOP( tmp_item );

	return item;
}

//--------------------------------------------------------------------------------------------
CHR_REF chr_holding_idsz( const CHR_REF by_reference ichr, IDSZ idsz )
{
	/// @details BB@> check the character's hands for a matching item

	bool_t found;
	CHR_REF item, tmp_item;
	chr_t * pchr;

	if ( !INGAME_CHR( ichr ) ) return ( CHR_REF )MAX_CHR;
	pchr = ChrList.lst + ichr;

	item = ( CHR_REF )MAX_CHR;
	found = bfalse;

	if ( !found )
	{
		// Check right hand. technically a held item cannot be equipped...
		tmp_item = pchr->holdingwhich[SLOT_RIGHT];

		if ( chr_is_type_idsz( tmp_item, idsz ) )
		{
			found = btrue;
			item = tmp_item;
		}
	}

	if ( !found )
	{
		// Check left hand. technically a held item cannot be equipped...
		tmp_item = pchr->holdingwhich[SLOT_LEFT];

		if ( chr_is_type_idsz( tmp_item, idsz ) )
		{
			found = btrue;
			item = tmp_item;
		}
	}

	return item;
}

//--------------------------------------------------------------------------------------------
CHR_REF chr_has_item_idsz( const CHR_REF by_reference ichr, IDSZ idsz, bool_t equipped, CHR_REF * pack_last )
{
	/// @detalis BB@> is ichr holding an item matching idsz, or is such an item in his pack?
	///               return the index of the found item, or MAX_CHR if not found. Also return
	///               the previous pack item in *pack_last, or MAX_CHR if it was not in a pack.

	bool_t found;
	CHR_REF item, tmp_var;
	chr_t * pchr;

	if ( !INGAME_CHR( ichr ) ) return ( CHR_REF )MAX_CHR;
	pchr = ChrList.lst + ichr;

	// make sure that pack_last points to something
	if ( NULL == pack_last ) pack_last = &tmp_var;

	// Check the pack
	*pack_last = ( CHR_REF )MAX_CHR;
	item       = ( CHR_REF )MAX_CHR;
	found      = bfalse;

	if ( !found )
	{
		item = chr_holding_idsz( ichr, idsz );
		found = INGAME_CHR( item );
	}

	if ( !found )
	{
		item = chr_has_inventory_idsz( ichr, idsz, equipped, pack_last );
		found = INGAME_CHR( item );
	}

	return item;
}

//--------------------------------------------------------------------------------------------
bool_t chr_can_see_object( const CHR_REF by_reference ichr, const CHR_REF by_reference iobj )
{
	/// @detalis BB@> can ichr see iobj?

	chr_t * pchr, * pobj;
	int     light, self_light, enviro_light;
	int     alpha;

	if ( !INGAME_CHR( ichr ) ) return bfalse;
	pchr = ChrList.lst + ichr;

	if ( !INGAME_CHR( iobj ) ) return bfalse;
	pobj = ChrList.lst + iobj;

	/// @note ZF@> Invictus characters can always see (spells, items, quest handlers, etc.)
	if ( pchr->invictus ) return btrue;

	alpha = pobj->inst.alpha;
	if ( pchr->see_invisible_level > 0 )
	{
		alpha *= pchr->see_invisible_level + 1;
	}
	alpha = CLIP( alpha, 0, 255 );

	enviro_light = ( alpha * pobj->inst.max_light ) * INV_FF;
	self_light   = ( pobj->inst.light == 255 ) ? 0 : pobj->inst.light;
	light        = MAX( enviro_light, self_light );

	if ( pchr->darkvision_level > 0 )
	{
		light *= pchr->darkvision_level + 1;
	}

	return light >= INVISIBLE;
}

//--------------------------------------------------------------------------------------------
int chr_get_price( const CHR_REF by_reference ichr )
{
	/// @detalis BB@> determine the correct price for an item

	CAP_REF icap;
	Uint16  iskin;
	float   price;

	chr_t * pchr;
	cap_t * pcap;

	if ( !INGAME_CHR( ichr ) ) return 0;
	pchr = ChrList.lst + ichr;

	// Make sure spell books are priced according to their spell and not the book itself
	if ( pchr->iprofile == SPELLBOOK )
	{
		icap = pro_get_icap( pchr->basemodel );
		iskin = 0;
	}
	else
	{
		icap  = pro_get_icap( pchr->iprofile );
		iskin = pchr->skin;
	}

	if ( !LOADED_CAP( icap ) ) return 0;
	pcap = CapStack.lst + icap;

	price = ( float ) pcap->skincost[iskin];

	// Items spawned in shops are more valuable
	if ( !pchr->isshopitem ) price *= 0.5f;

	// base the cost on the number of items/charges
	if ( pcap->isstackable )
	{
		price *= pchr->ammo;
	}
	else if ( pcap->isranged && pchr->ammo < pchr->ammomax )
	{
		if ( 0 != pchr->ammo )
		{
			price *= ( float ) pchr->ammo / ( float ) pchr->ammomax;
		}
	}

	return ( int )price;
}

//--------------------------------------------------------------------------------------------
void chr_set_floor_level( chr_t * pchr, float level )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	if ( level != pchr->enviro.floor_level )
	{
		pchr->enviro.floor_level = level;
		apply_reflection_matrix( &( pchr->inst ), level );
	}
}

//--------------------------------------------------------------------------------------------
void chr_set_redshift( chr_t * pchr, int rs )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.redshift = rs;

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_set_grnshift( chr_t * pchr, int gs )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.grnshift = gs;

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_set_blushift( chr_t * pchr, int bs )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.blushift = bs;

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_set_sheen( chr_t * pchr, int sheen )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.sheen = sheen;

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_set_alpha( chr_t * pchr, int alpha )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.alpha = CLIP( alpha, 0, 255 );

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_set_light( chr_t * pchr, int light )
{
	if ( !DEFINED_PCHR( pchr ) ) return;

	pchr->inst.light = CLIP( light, 0, 255 );

	chr_instance_update_ref( &( pchr->inst ), pchr->enviro.floor_level, bfalse );
}

//--------------------------------------------------------------------------------------------
void chr_instance_get_tint( chr_instance_t * pinst, GLfloat * tint, Uint32 bits )
{
	int i;
	float weight_sum;
	GLXvector4f local_tint;

	int local_alpha;
	int local_light;
	int local_sheen;
	int local_redshift;
	int local_grnshift;
	int local_blushift;

	if ( NULL == tint ) tint = local_tint;

	if ( 0 != ( bits & CHR_REFLECT ) )
	{
		// this is a reflection, use the reflected parameters
		local_alpha    = pinst->ref.alpha;
		local_light    = pinst->ref.light;
		local_sheen    = pinst->ref.sheen;
		local_redshift = pinst->ref.redshift;
		local_grnshift = pinst->ref.grnshift;
		local_blushift = pinst->ref.blushift;
	}
	else
	{
		// this is NOT a reflection, use the normal parameters
		local_alpha    = pinst->alpha;
		local_light    = pinst->light;
		local_sheen    = pinst->sheen;
		local_redshift = pinst->redshift;
		local_grnshift = pinst->grnshift;
		local_blushift = pinst->blushift;
	}

	// modify these values based on local characte abilities
	local_alpha = get_local_alpha( local_alpha );
	local_light = get_local_light( local_light );

	// clear out the tint
	weight_sum = 0;
	for ( i = 0; i < 4; i++ ) tint[i] = 0;

	if ( 0 != ( bits & CHR_SOLID ) )
	{
		// solid characters are not blended onto the canvas
		// the alpha channel is not important
		weight_sum += 1.0;

		tint[0] += 1.0f / ( 1 << local_redshift );
		tint[1] += 1.0f / ( 1 << local_grnshift );
		tint[2] += 1.0f / ( 1 << local_blushift );
		tint[3] += 1.0f;
	}

	if ( 0 != ( bits & CHR_ALPHA ) )
	{
		// alpha characters are blended onto the canvas using the alpha channel
		// the alpha channel is not important
		weight_sum += 1.0;

		tint[0] += 1.0f / ( 1 << local_redshift );
		tint[1] += 1.0f / ( 1 << local_grnshift );
		tint[2] += 1.0f / ( 1 << local_blushift );
		tint[3] += local_alpha * INV_FF;
	}

	if ( 0 != ( bits & CHR_LIGHT ) )
	{
		// alpha characters are blended onto the canvas by adding their color
		// the more black the colors, the less visible the character
		// the alpha channel is not important

		weight_sum += 1.0;

		if ( local_light < 255 )
		{
			tint[0] += local_light * INV_FF / ( 1 << local_redshift );
			tint[1] += local_light * INV_FF / ( 1 << local_grnshift );
			tint[2] += local_light * INV_FF / ( 1 << local_blushift );
		}

		tint[3] += 1.0f;
	}

	if ( 0 != ( bits & CHR_PHONG ) )
	{
		// phong is essentially the same as light, but it is the
		// sheen that sets the effect

		float amount;

		weight_sum += 1.0;

		amount = ( CLIP( local_sheen, 0, 15 ) << 4 ) / 240.0f;

		tint[0] += amount;
		tint[1] += amount;
		tint[2] += amount;
		tint[3] += 1.0f;
	}

	// average the tint
	if ( weight_sum != 0.0f && weight_sum != 1.0f )
	{
		for ( i = 0; i < 4; i++ )
		{
			tint[i] /= weight_sum;
		}
	}
}

//--------------------------------------------------------------------------------------------
CHR_REF chr_get_lowest_attachment( const CHR_REF by_reference ichr, bool_t non_item )
{
	/// @details BB@> Find the lowest attachment for a given object.
	///               This was basically taken from the script function scr_set_TargetToLowestTarget()
	///
	///               You should be able to find the holder of a weapon by specifying non_item == btrue
	///
	///               To prevent possible loops in the data structures, use a counter to limit
	///               the depth of the search, and make sure that ichr != ChrList.lst[object].attachedto

	int cnt;
	CHR_REF original_object, object, object_next;

	if ( !INGAME_CHR( ichr ) ) return ( CHR_REF )MAX_CHR;

	original_object = object = ichr;
	for ( cnt = 0, object = ichr; cnt < MAX_CHR && INGAME_CHR( object ); cnt++ )
	{
		object_next = ChrList.lst[object].attachedto;

		if ( non_item && !ChrList.lst[object].isitem )
		{
			break;
		}

		// check for a list with a loop. shouldn't happen, but...
		if ( !INGAME_CHR( object_next ) || object_next == original_object )
		{
			break;
		}

		object = object_next;
	}

	return object;
}

//--------------------------------------------------------------------------------------------
bool_t chr_get_mass_pair( chr_t * pchr_a, chr_t * pchr_b, float * wta, float * wtb )
{
	/// @details BB@> calculate a "mass" for each object, taking into account possible infinite masses.

	float loc_wta, loc_wtb;

	if ( !ACTIVE_PCHR( pchr_a ) || !ACTIVE_PCHR( pchr_b ) ) return bfalse;

	if ( NULL == wta ) wta = &loc_wta;
	if ( NULL == wtb ) wtb = &loc_wtb;

	*wta = ( INFINITE_WEIGHT == pchr_a->phys.weight ) ? -( float )INFINITE_WEIGHT : pchr_a->phys.weight;
	*wtb = ( INFINITE_WEIGHT == pchr_b->phys.weight ) ? -( float )INFINITE_WEIGHT : pchr_b->phys.weight;

	if ( *wta == 0 && *wtb == 0 )
	{
		*wta = *wtb = 1;
	}
	else if ( *wta == 0 )
	{
		*wta = 1;
		*wtb = -( float )INFINITE_WEIGHT;
	}
	else if ( *wtb == 0 )
	{
		*wtb = 1;
		*wta = -( float )INFINITE_WEIGHT;
	}

	if ( 0.0f == pchr_a->phys.bumpdampen && 0.0f == pchr_b->phys.bumpdampen )
	{
		/* do nothing */
	}
	else if ( 0.0f == pchr_a->phys.bumpdampen )
	{
		// make the weight infinite
		*wta = -( float )INFINITE_WEIGHT;
	}
	else if ( 0.0f == pchr_b->phys.bumpdampen )
	{
		// make the weight infinite
		*wtb = -( float )INFINITE_WEIGHT;
	}
	else
	{
		// adjust the weights to respect bumpdampen
		( *wta ) /= pchr_a->phys.bumpdampen;
		( *wtb ) /= pchr_b->phys.bumpdampen;
	}

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_can_mount( const CHR_REF by_reference ichr_a, const CHR_REF by_reference ichr_b )
{
	bool_t is_valid_rider_a, is_valid_mount_b, has_ride_anim;
	int action_mi;

	chr_t * pchr_a, * pchr_b;
	cap_t * pcap_a, * pcap_b;

	// make sure that A is valid
	if ( !INGAME_CHR( ichr_a ) ) return bfalse;
	pchr_a = ChrList.lst + ichr_a;

	pcap_a = chr_get_pcap( ichr_a );
	if ( NULL == pcap_a ) return bfalse;

	// make sure that B is valid
	if ( !INGAME_CHR( ichr_b ) ) return bfalse;
	pchr_b = ChrList.lst + ichr_b;

	pcap_b = chr_get_pcap( ichr_b );
	if ( NULL == pcap_b ) return bfalse;

	action_mi = mad_get_action( chr_get_imad( ichr_a ), ACTION_MI );
	has_ride_anim = ( ACTION_COUNT != action_mi && !ACTION_IS_TYPE( action_mi, D ) );

	is_valid_rider_a = !pchr_a->isitem && pchr_a->alive && 0 == pchr_a->flyheight &&
		!INGAME_CHR( pchr_a->attachedto ) && has_ride_anim;

	is_valid_mount_b = pchr_b->ismount && pchr_b->alive &&
		pcap_b->slotvalid[SLOT_LEFT] && !INGAME_CHR( pchr_b->holdingwhich[SLOT_LEFT] );

	return is_valid_rider_a && is_valid_mount_b;
}

//--------------------------------------------------------------------------------------------
Uint32 chr_get_framefx( chr_t * pchr )
{
	int           frame_count;
	MD2_Frame_t * frame_list, * pframe_nxt;
	mad_t       * pmad;
	MD2_Model_t * pmd2;

	if ( !DEFINED_PCHR( pchr ) ) return 0;

	pmad = chr_get_pmad( GET_REF_PCHR( pchr ) );
	if ( NULL == pmad ) return 0;

	pmd2 = pmad->md2_ptr;
	if ( NULL == pmd2 ) return 0;

	frame_count = md2_get_numFrames( pmd2 );
	EGOBOO_ASSERT( pchr->inst.frame_nxt < frame_count );

	frame_list  = ( MD2_Frame_t * )md2_get_Frames( pmd2 );
	pframe_nxt  = frame_list + pchr->inst.frame_nxt;

	return pframe_nxt->framefx;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_invalidate_child_instances( chr_t * pchr )
{
	int cnt;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	// invalidate vlst_cache of everything in this character's holdingwhich array
	for ( cnt = 0; cnt < SLOT_COUNT; cnt++ )
	{
		CHR_REF iitem = pchr->holdingwhich[SLOT_LEFT];
		if ( !INGAME_CHR( iitem ) ) continue;

		// invalidate the matrix_cache
		ChrList.lst[iitem].inst.matrix_cache.valid = bfalse;
	}

	return rv_success;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_set_action( chr_t * pchr, int action, bool_t action_ready, bool_t override_action )
{
	egoboo_rv retval;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	retval = chr_instance_set_action( &( pchr->inst ), action, action_ready, override_action );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_start_anim( chr_t * pchr, int action, bool_t action_ready, bool_t override_action )
{
	egoboo_rv retval;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	retval = chr_instance_start_anim( &( pchr->inst ), action, action_ready, override_action );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_set_anim( chr_t * pchr, int action, int frame, bool_t action_ready, bool_t override_action )
{
	egoboo_rv retval;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	retval = chr_instance_set_anim( &( pchr->inst ), action, frame, action_ready, override_action );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_increment_action( chr_t * pchr )
{
	egoboo_rv retval;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	retval = chr_instance_increment_action( &( pchr->inst ) );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_increment_frame( chr_t * pchr )
{
	egoboo_rv retval;
	mad_t * pmad;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	pmad = chr_get_pmad( GET_REF_PCHR( pchr ) );
	if ( NULL == pmad ) return rv_error;

	retval = chr_instance_increment_frame( &( pchr->inst ), pmad, pchr->attachedto );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
egoboo_rv chr_play_action( chr_t * pchr, int action, bool_t action_ready )
{
	egoboo_rv retval;

	if ( !ACTIVE_PCHR( pchr ) ) return rv_error;

	retval = chr_instance_play_action( &( pchr->inst ), action, action_ready );
	if ( rv_success != retval ) return retval;

	chr_invalidate_child_instances( pchr );

	return retval;
}

//--------------------------------------------------------------------------------------------
MAD_REF chr_get_imad( const CHR_REF by_reference ichr )
{
	chr_t * pchr;

	if ( !DEFINED_CHR( ichr ) ) return ( MAD_REF )MAX_MAD;
	pchr = ChrList.lst + ichr;

	// try to repair a bad model if it exists
	if ( !LOADED_MAD( pchr->inst.imad ) )
	{
		MAD_REF imad_tmp = pro_get_imad( pchr->iprofile );
		if ( LOADED_MAD( imad_tmp ) )
		{
			if ( chr_instance_set_mad( &( pchr->inst ), imad_tmp ) )
			{
				chr_update_collision_size( pchr, btrue );
			}
		}
		if ( !LOADED_MAD( pchr->inst.imad ) ) return ( MAD_REF )MAX_MAD;
	}

	return pchr->inst.imad;
}

//--------------------------------------------------------------------------------------------
mad_t * chr_get_pmad( const CHR_REF by_reference ichr )
{
	chr_t * pchr;

	if ( !DEFINED_CHR( ichr ) ) return NULL;
	pchr = ChrList.lst + ichr;

	// try to repair a bad model if it exists
	if ( !LOADED_MAD( pchr->inst.imad ) )
	{
		MAD_REF imad_tmp = pro_get_imad( pchr->iprofile );
		if ( LOADED_MAD( imad_tmp ) )
		{
			chr_instance_set_mad( &( pchr->inst ), imad_tmp );
		}
	}

	if ( !LOADED_MAD( pchr->inst.imad ) ) return NULL;

	return MadStack.lst + pchr->inst.imad;
}

//--------------------------------------------------------------------------------------------
/*void kill_character( const CHR_REF by_reference character, const CHR_REF by_reference killer )
{
/// @details ZZ@> This function kills a character...  MAX_CHR killer for accidental death

Uint8 modifier;
Uint16 threshold;
chr_t * pchr;

if ( !INGAME_CHR( character ) ) return;
pchr = ChrList.lst + character;

if ( pchr->alive )
{
IPair tmp_damage = {512,1};

pchr->damagetime = 0;
pchr->life = 1;

//Remember some values
modifier = pchr->damagemodifier[DAMAGE_CRUSH];
threshold = pchr->damagethreshold;

//Set those values so we are sure it will die
pchr->damagemodifier[DAMAGE_CRUSH] = 1;
pchr->damagethreshold = 0;

if ( INGAME_CHR( killer ) )
{
damage_character( character, ATK_FRONT, tmp_damage, DAMAGE_CRUSH, chr_get_iteam(killer), killer, DAMFX_ARMO | DAMFX_NBLOC, btrue );
}
else
{
damage_character( character, ATK_FRONT, tmp_damage, DAMAGE_CRUSH, TEAM_DAMAGE, pchr->ai.bumplast, DAMFX_ARMO | DAMFX_NBLOC, btrue );
}

//Revert back to original again
pchr->damagemodifier[DAMAGE_CRUSH] = modifier;
pchr->damagethreshold = threshold;
}
}*/

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
fvec3_t chr_get_pos( chr_t * pchr )
{
	fvec3_t vtmp = ZERO_VECT3;

	if( !ALLOCATED_PCHR(pchr) ) return vtmp;

	return pchr->pos;
}

//--------------------------------------------------------------------------------------------
float * chr_get_pos_v( chr_t * pchr )
{
	static fvec3_t vtmp = ZERO_VECT3;

	if( !ALLOCATED_PCHR(pchr) ) return vtmp.v;

	return pchr->pos.v;
}

//--------------------------------------------------------------------------------------------
bool_t chr_update_pos( chr_t * pchr )
{
	if( !ALLOCATED_PCHR(pchr) ) return bfalse;

	pchr->onwhichgrid   = mesh_get_tile( PMesh, pchr->pos.x, pchr->pos.y );
	pchr->onwhichblock  = mesh_get_block( PMesh, pchr->pos.x, pchr->pos.y );

	// update whether the current character position is safe
	chr_update_safe( pchr, bfalse );

	// update the breadcrumb list
	chr_update_breadcrumb( pchr, bfalse );

	return btrue;
}

//--------------------------------------------------------------------------------------------
bool_t chr_set_pos( chr_t * pchr, fvec3_base_t pos )
{
	bool_t retval = bfalse;

	if( !ALLOCATED_PCHR(pchr) ) return retval;

	retval = btrue;

	if( (pos[kX] != pchr->pos.v[kX]) || (pos[kY] != pchr->pos.v[kY]) || (pos[kZ] != pchr->pos.v[kZ]) )
	{
		memmove( pchr->pos.v, pos, sizeof(fvec3_base_t) );

		retval = chr_update_pos( pchr );
	}

	return retval;
}
