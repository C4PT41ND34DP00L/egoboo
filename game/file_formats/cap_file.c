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

/// @file cap_file.c
/// @brief routines for reading and writing the character profile file data.txt

#include "cap_file.h"

#include "char.h"

#include "template.h"

#include "egoboo_fileutil.h"
#include "egoboo_strutil.h"
#include "egoboo_vfs.h"
#include "egoboo.h"

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
cap_t * cap_init( cap_t * pcap )
{
    /// @details BB@> initialize the character profile data to safe values
    ///     since we use memset(..., 0, ...), all = 0, = false, and = 0.0f
    ///     statements are redundant

    int cnt;

    if( NULL == pcap ) return pcap;

    // clear out all the data
    memset( pcap, 0, sizeof(cap_t) );

    for( cnt=0; cnt<IDSZ_COUNT; cnt++ )
    {
        pcap->idsz[cnt] = IDSZ_NONE;
    }

    // clear out the sounds
    for ( cnt = 0; cnt < SOUND_COUNT; cnt++ )
    {
        pcap->soundindex[cnt] = -1;
    }

    // Clear expansions...
    pcap->reflect = btrue;
    pcap->hidestate = NOHIDE;
    pcap->skinoverride = NOSKINOVERRIDE;


    // either these will be overridden by data in the data.txt, or
    // they will be limited by the spawning character's max stats
    pcap->spawnlife = PERFECTBIG;
    pcap->spawnmana = PERFECTBIG;

    // More stuff I forgot
    pcap->stoppedby  = MPDFX_IMPASS;

    // Skills

    pcap->spelleffect_type = -1;

    return pcap;
}

//--------------------------------------------------------------------------------------------
cap_t * load_one_cap_file( const char * tmploadname, cap_t * pcap )
{
    /// @details ZZ@> This function fills a character profile with data from data.txt, returning
    ///     the icap slot that the profile was stuck into.  It may cause the program
    ///     to abort if bad things happen.

    vfs_FILE* fileread;

    int   iTmp;
    char  cTmp;
    float fTmp;

    Uint8 damagetype, level, xptype;
    int idsz_cnt;
    IDSZ idsz;
    int cnt;
    STRING szLoadName;

    if( NULL == pcap ) return NULL;

    make_newloadname( tmploadname, SLASH_STR "data.txt", szLoadName );

    // Open the file
    fileread = vfs_openRead( szLoadName );
    if ( NULL == fileread )
    {
        return NULL;
    }

    // ignore this, since it was already read
    iTmp = fget_next_int( fileread );

    cap_init( pcap );

    // mark the source filename
    strncpy( pcap->name, szLoadName, SDL_arraysize(pcap->name) );

    // mark it as loaded
    pcap->loaded = btrue;

    // Read in the real general data
    fget_next_name( fileread, pcap->classname, SDL_arraysize(pcap->classname) );

    // Light cheat
    pcap->uniformlit = fget_next_bool( fileread );

    // Ammo
    pcap->ammomax = fget_next_int( fileread );
    pcap->ammo = fget_next_int( fileread );

    // Gender
    cTmp = fget_next_char( fileread );
         if ( 'F' == toupper(cTmp) )  pcap->gender = GENDER_FEMALE;
    else if ( 'M' == toupper(cTmp) )  pcap->gender = GENDER_MALE;
    else if ( 'R' == toupper(cTmp) )  pcap->gender = GENDER_RANDOM;
    else                              pcap->gender = GENDER_OTHER;

    // Read in the icap stats
    pcap->lifecolor = fget_next_int( fileread );
    pcap->manacolor = fget_next_int( fileread );

    fget_next_range( fileread, &(pcap->life_stat.val) );
    fget_next_range( fileread, &(pcap->life_stat.perlevel) );

    fget_next_range( fileread, &(pcap->mana_stat.val) );
    fget_next_range( fileread, &(pcap->mana_stat.perlevel) );

    fget_next_range( fileread, &(pcap->manareturn_stat.val) );
    fget_next_range( fileread, &(pcap->manareturn_stat.perlevel) );

    fget_next_range( fileread, &(pcap->manaflow_stat.val) );
    fget_next_range( fileread, &(pcap->manaflow_stat.perlevel) );

    fget_next_range( fileread, &(pcap->strength_stat.val) );
    fget_next_range( fileread, &(pcap->strength_stat.perlevel) );

    fget_next_range( fileread, &(pcap->wisdom_stat.val) );
    fget_next_range( fileread, &(pcap->wisdom_stat.perlevel) );

    fget_next_range( fileread, &(pcap->intelligence_stat.val) );
    fget_next_range( fileread, &(pcap->intelligence_stat.perlevel) );

    fget_next_range( fileread, &(pcap->dexterity_stat.val) );
    fget_next_range( fileread, &(pcap->dexterity_stat.perlevel) );

    // More physical attributes
    pcap->size = fget_next_float( fileread );
    pcap->sizeperlevel = fget_next_float( fileread );
    pcap->shadowsize = fget_next_int( fileread );
    pcap->bumpsize = fget_next_int( fileread );
    pcap->bumpheight = fget_next_int( fileread );
    pcap->bumpdampen = fget_next_float( fileread );
    pcap->weight = fget_next_int( fileread );
    pcap->jump = fget_next_float( fileread );
    pcap->jumpnumber = fget_next_int( fileread );
    pcap->sneakspd = fget_next_int( fileread );
    pcap->walkspd = fget_next_int( fileread );
    pcap->runspd = fget_next_int( fileread );
    pcap->flyheight = fget_next_int( fileread );
    pcap->flashand = fget_next_int( fileread );
    pcap->alpha = fget_next_int( fileread );
    pcap->light = fget_next_int( fileread );
    pcap->transferblend = fget_next_bool( fileread );

    pcap->sheen = fget_next_int( fileread );
    pcap->enviro = fget_next_bool( fileread );

    fTmp = fget_next_float( fileread );
    pcap->uoffvel    = FLOAT_TO_FFFF(fTmp);
    fTmp = fget_next_float( fileread );
    pcap->voffvel    = FLOAT_TO_FFFF(fTmp);
    pcap->stickybutt = fget_next_bool( fileread );

    // Invulnerability data
    pcap->invictus     = fget_next_bool( fileread );
    pcap->nframefacing = fget_next_int( fileread );
    pcap->nframeangle  = fget_next_int( fileread );
    pcap->iframefacing = fget_next_int( fileread );
    pcap->iframeangle  = fget_next_int( fileread );

    // Resist burning and stuck arrows with nframe angle of 1 or more
    if ( pcap->nframeangle > 0 )
    {
        if ( pcap->nframeangle == 1 )
        {
            pcap->nframeangle = 0;
        }
    }

    // Skin defenses ( 4 skins )
    goto_colon( NULL, fileread, bfalse );
    for( cnt=0; cnt<MAX_SKIN; cnt++ )
    {
        iTmp = 0xFF - fget_int( fileread );
        pcap->defense[cnt] = CLIP( iTmp, 0, 0xFF );
    }

    for ( damagetype = 0; damagetype < DAMAGE_COUNT; damagetype++ )
    {
        goto_colon( NULL, fileread, bfalse );
        for( cnt=0; cnt<MAX_SKIN; cnt++ )
        {
            pcap->damagemodifier[damagetype][cnt] = fget_int( fileread );
        }
    }

    for ( damagetype = 0; damagetype < DAMAGE_COUNT; damagetype++ )
    {
        goto_colon( NULL, fileread, bfalse );

        for( cnt=0; cnt<MAX_SKIN; cnt++ )
        {
            cTmp = fget_first_letter( fileread );
                 if ( 'T' == toupper(cTmp) )  pcap->damagemodifier[damagetype][cnt] |= DAMAGEINVERT;
            else if ( 'C' == toupper(cTmp) )  pcap->damagemodifier[damagetype][cnt] |= DAMAGECHARGE;
            else if ( 'M' == toupper(cTmp) )  pcap->damagemodifier[damagetype][cnt] |= DAMAGEMANA;
        }
    }

    goto_colon( NULL, fileread, bfalse );
    for( cnt=0; cnt<MAX_SKIN; cnt++ )
    {
        pcap->maxaccel[cnt] = fget_float( fileread ) / 80.0f;
    }

    // Experience and level data
    pcap->experienceforlevel[0] = 0;
    for ( level = 1; level < MAXBASELEVEL; level++ )
    {
        pcap->experienceforlevel[level] = fget_next_int( fileread );
    }

    fget_next_range( fileread, &(pcap->experience) );
    pcap->experience.from /= 256.0f;
    pcap->experience.to   /= 256.0f;

    pcap->experienceworth    = fget_next_int( fileread );
    pcap->experienceexchange = fget_next_float( fileread );

    for ( xptype = 0; xptype < XP_COUNT; xptype++ )
    {
        pcap->experiencerate[xptype] = fget_next_float( fileread ) + 0.001f;
    }

    // IDSZ tags
    for ( idsz_cnt = 0; idsz_cnt < IDSZ_COUNT; idsz_cnt++ )
    {
        pcap->idsz[idsz_cnt] = fget_next_idsz( fileread );
    }

    // Item and damage flags
    pcap->isitem               = fget_next_bool( fileread );
    pcap->ismount              = fget_next_bool( fileread );
    pcap->isstackable          = fget_next_bool( fileread );
    pcap->nameknown            = fget_next_bool( fileread );
    pcap->usageknown           = fget_next_bool( fileread );
    pcap->cancarrytonextmodule = fget_next_bool( fileread );
    pcap->needskillidtouse     = fget_next_bool( fileread );
    pcap->platform             = fget_next_bool( fileread );
    pcap->cangrabmoney         = fget_next_bool( fileread );
    pcap->canopenstuff         = fget_next_bool( fileread );

    // More item and damage stuff
    pcap->damagetargettype = fget_next_damage_type( fileread );
    pcap->weaponaction     = action_which( fget_next_char( fileread ) );

    // Particle attachments
    pcap->attachedprt_amount             = fget_next_int( fileread );
    pcap->attachedprt_reaffirmdamagetype = fget_next_damage_type( fileread );
    pcap->attachedprt_pip                = fget_next_int( fileread );

    // Character hands
    pcap->slotvalid[SLOT_LEFT]  = fget_next_bool( fileread );
    pcap->slotvalid[SLOT_RIGHT] = fget_next_bool( fileread );

    // Attack order ( weapon )
    pcap->attack_attached = fget_next_bool( fileread );
    pcap->attack_pip  = fget_next_int( fileread );

    // GoPoof
    pcap->gopoofprt_amount    = fget_next_int( fileread );
    pcap->gopoofprt_facingadd = fget_next_int( fileread );
    pcap->gopoofprt_pip       = fget_next_int( fileread );

    // Blud
    cTmp = fget_next_char( fileread );
         if ( 'T' == toupper(cTmp) )  pcap->blud_valid = btrue;
    else if ( 'U' == toupper(cTmp) )  pcap->blud_valid = ULTRABLUDY;
    else                              pcap->blud_valid = bfalse;

    pcap->blud_pip = fget_next_int( fileread );

    // Stuff I forgot
    pcap->waterwalk = fget_next_bool( fileread );
    pcap->dampen    = fget_next_float( fileread );

    // More stuff I forgot
    pcap->lifeheal    = fget_next_float( fileread ) * 256;
    pcap->manacost    = fget_next_float( fileread ) * 256;
    pcap->lifereturn  = fget_next_int( fileread );
    pcap->stoppedby  |= fget_next_int( fileread );

    for(cnt=0; cnt<MAX_SKIN; cnt++)
    {
        fget_next_name( fileread, pcap->skinname[cnt], sizeof(pcap->skinname[cnt]) );
    }

    for(cnt=0; cnt<MAX_SKIN; cnt++)
    {
        pcap->skincost[cnt] = fget_next_int( fileread );
    }

    pcap->strengthdampen = fget_next_float( fileread );

    // Another memory lapse
    pcap->ridercanattack = !fget_next_bool( fileread );
    pcap->canbedazed     =  fget_next_bool( fileread );
    pcap->canbegrogged   =  fget_next_bool( fileread );

    goto_colon( NULL, fileread, bfalse );  // !!!BAD!!! Life add
    goto_colon( NULL, fileread, bfalse );  // !!!BAD!!! Mana add
    pcap->canseeinvisible = fget_next_bool( fileread );

    pcap->kursechance                = fget_next_int( fileread );
    pcap->soundindex[SOUND_FOOTFALL] = fget_next_int( fileread );  // Footfall sound
    pcap->soundindex[SOUND_JUMP]     = fget_next_int( fileread );  // Jump sound

    // assume the normal dependence of ripple on isitem
    pcap->ripple = !pcap->isitem;

    // assume a round object
    pcap->bumpsizebig = pcap->bumpsize * SQRT_TWO;

    // assume the normal icon usage
    pcap->icon = pcap->usageknown;

    // assume normal platform usage
    pcap->canuseplatforms = !pcap->platform;

    // Read expansions
    while ( goto_colon( NULL, fileread, btrue ) )
    {
        idsz = fget_idsz( fileread );

             if ( idsz == MAKE_IDSZ( 'D', 'R', 'E', 'S' ) ) pcap->skindressy |= 1 << fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'G', 'O', 'L', 'D' ) ) pcap->money = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'T', 'U', 'K' ) ) pcap->resistbumpspawn = 1 - fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'P', 'A', 'C', 'K' ) ) pcap->istoobig = 1 - fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'V', 'A', 'M', 'P' ) ) pcap->reflect = 1 - fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'D', 'R', 'A', 'W' ) ) pcap->alwaysdraw = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'R', 'A', 'N', 'G' ) ) pcap->isranged = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'H', 'I', 'D', 'E' ) ) pcap->hidestate = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'E', 'Q', 'U', 'I' ) ) pcap->isequipment = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'Q', 'U', 'A' ) ) pcap->bumpsizebig = pcap->bumpsize * 2;
        else if ( idsz == MAKE_IDSZ( 'I', 'C', 'O', 'N' ) ) pcap->icon = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'H', 'A', 'D' ) ) pcap->forceshadow = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'C', 'K', 'U', 'R' ) ) pcap->canseekurse = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'K', 'I', 'N' ) ) pcap->skinoverride = fget_int( fileread ) & 3;
        else if ( idsz == MAKE_IDSZ( 'C', 'O', 'N', 'T' ) ) pcap->contentoverride = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'T', 'A', 'T' ) ) pcap->stateoverride = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'L', 'E', 'V', 'L' ) ) pcap->leveloverride = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'P', 'L', 'A', 'T' ) ) pcap->canuseplatforms = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'R', 'I', 'P', 'P' ) ) pcap->ripple = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'V', 'A', 'L', 'U' ) ) pcap->isvaluable = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'L', 'I', 'F', 'E' ) ) pcap->spawnlife = 256 * fget_float( fileread );
        else if ( idsz == MAKE_IDSZ( 'M', 'A', 'N', 'A' ) ) pcap->spawnmana = 256 * fget_float( fileread );
        else if ( idsz == MAKE_IDSZ( 'B', 'O', 'O', 'K' ) ) pcap->spelleffect_type = fget_int( fileread );

        // Read Skills
        else if ( idsz == MAKE_IDSZ( 'A', 'W', 'E', 'P' ) ) pcap->canuseadvancedweapons = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'H', 'P', 'R' ) ) pcap->shieldproficiency = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'J', 'O', 'U', 'S' ) ) pcap->canjoust = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'W', 'M', 'A', 'G' ) ) pcap->canusearcane = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'H', 'M', 'A', 'G' ) ) pcap->canusedivine = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'T', 'E', 'C', 'H' ) ) pcap->canusetech = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'D', 'I', 'S', 'A' ) ) pcap->candisarm = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'S', 'T', 'A', 'B' ) ) pcap->canbackstab = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'P', 'O', 'I', 'S' ) ) pcap->canusepoison = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'R', 'E', 'A', 'D' ) ) pcap->canread = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'C', 'O', 'D', 'E' ) ) pcap->hascodeofconduct = fget_int( fileread );
        else if ( idsz == MAKE_IDSZ( 'D', 'A', 'R', 'K' ) ) pcap->hasdarkvision = fget_int( fileread );
    }
    vfs_close( fileread );

    //log_info( "load_one_character_profile() - loaded icap %s (%d)\n", pcap->classname, icap );

    return pcap;
}


//--------------------------------------------------------------------------------------------
bool_t save_one_cap_file( const char * szSaveName, cap_t * pcap )
{
    /// @details BB@> export one cap_t struct to a "data.txt" file
    ///     converted to using the template file

    vfs_FILE* filewrite, * filetemp;

    int damagetype, skin;

    if( NULL == pcap ) return bfalse;

    // Open the file
    filewrite = vfs_openWrite( szSaveName );
    if ( NULL == filewrite ) return bfalse;

    // open the template file
    filetemp = template_open( "/basicdat/templates/data.txt" );
    if ( NULL == filetemp ) 
    {
        vfs_close( filewrite );
        return bfalse;
    }

    // Real general data
    template_put_int   ( filetemp, filewrite, -1 );  // -1 signals a flexible load thing
    template_put_string_under( filetemp, filewrite, pcap->classname );
    template_put_bool  ( filetemp, filewrite, pcap->uniformlit );
    template_put_int   ( filetemp, filewrite, pcap->ammomax );
    template_put_int   ( filetemp, filewrite, pcap->ammo );
    template_put_gender( filetemp, filewrite, pcap->gender );

    // Object stats
    template_put_int  ( filetemp, filewrite, pcap->lifecolor );
    template_put_int  ( filetemp, filewrite, pcap->manacolor );
    template_put_range( filetemp, filewrite, pcap->life_stat.val );
    template_put_range( filetemp, filewrite, pcap->life_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->mana_stat.val );
    template_put_range( filetemp, filewrite, pcap->mana_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->manareturn_stat.val );
    template_put_range( filetemp, filewrite, pcap->manareturn_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->manaflow_stat.val );
    template_put_range( filetemp, filewrite, pcap->manaflow_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->strength_stat.val );
    template_put_range( filetemp, filewrite, pcap->strength_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->wisdom_stat.val );
    template_put_range( filetemp, filewrite, pcap->wisdom_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->intelligence_stat.val );
    template_put_range( filetemp, filewrite, pcap->intelligence_stat.perlevel );
    template_put_range( filetemp, filewrite, pcap->dexterity_stat.val );
    template_put_range( filetemp, filewrite, pcap->dexterity_stat.perlevel );

    // More physical attributes
    template_put_float( filetemp, filewrite, pcap->size );
    template_put_float( filetemp, filewrite, pcap->sizeperlevel );
    template_put_int  ( filetemp, filewrite, pcap->shadowsize );
    template_put_int  ( filetemp, filewrite, pcap->bumpsize );
    template_put_int  ( filetemp, filewrite, pcap->bumpheight );
    template_put_float( filetemp, filewrite, pcap->bumpdampen );
    template_put_int  ( filetemp, filewrite, pcap->weight );
    template_put_float( filetemp, filewrite, pcap->jump );
    template_put_int  ( filetemp, filewrite, pcap->jumpnumber );
    template_put_int  ( filetemp, filewrite, pcap->sneakspd );
    template_put_int  ( filetemp, filewrite, pcap->walkspd );
    template_put_int  ( filetemp, filewrite, pcap->runspd );
    template_put_int  ( filetemp, filewrite, pcap->flyheight );
    template_put_int  ( filetemp, filewrite, pcap->flashand );
    template_put_int  ( filetemp, filewrite, pcap->alpha );
    template_put_int  ( filetemp, filewrite, pcap->light );
    template_put_bool ( filetemp, filewrite, pcap->transferblend );
    template_put_int  ( filetemp, filewrite, pcap->sheen );
    template_put_bool ( filetemp, filewrite, pcap->enviro );
    template_put_float( filetemp, filewrite, FFFF_TO_FLOAT(pcap->uoffvel) );
    template_put_float( filetemp, filewrite, FFFF_TO_FLOAT(pcap->voffvel) );
    template_put_bool ( filetemp, filewrite, pcap->stickybutt );

    // Invulnerability data
    template_put_bool( filetemp, filewrite, pcap->invictus );
    template_put_int ( filetemp, filewrite, pcap->nframefacing );
    template_put_int ( filetemp, filewrite, pcap->nframeangle );
    template_put_int ( filetemp, filewrite, pcap->iframefacing );
    template_put_int ( filetemp, filewrite, pcap->iframeangle );

    // Skin defenses
    template_put_int ( filetemp, filewrite, 255 - pcap->defense[0] );
    template_put_int ( filetemp, filewrite, 255 - pcap->defense[1] );
    template_put_int ( filetemp, filewrite, 255 - pcap->defense[2] );
    template_put_int ( filetemp, filewrite, 255 - pcap->defense[3] );

    for ( damagetype = 0; damagetype < DAMAGE_COUNT; damagetype++ )
    {
        template_put_int ( filetemp, filewrite, pcap->damagemodifier[damagetype][0] & DAMAGESHIFT );
        template_put_int ( filetemp, filewrite, pcap->damagemodifier[damagetype][1] & DAMAGESHIFT );
        template_put_int ( filetemp, filewrite, pcap->damagemodifier[damagetype][2] & DAMAGESHIFT );
        template_put_int ( filetemp, filewrite, pcap->damagemodifier[damagetype][3] & DAMAGESHIFT );
    }

    for ( damagetype = 0; damagetype < DAMAGE_COUNT; damagetype++ )
    {
        char code;

        for ( skin = 0; skin < MAX_SKIN; skin++ )
        {
            if ( pcap->damagemodifier[damagetype][skin]&DAMAGEMANA )
            {
                code = 'M';
            }
            else if ( pcap->damagemodifier[damagetype][skin]&DAMAGECHARGE )
            {
                code = 'C';
            }
            else if ( pcap->damagemodifier[damagetype][skin]&DAMAGEINVERT )
            {
                code = 'T';
            }
            else
            {
                code = 'F';
            }

            template_put_char( filetemp, filewrite, code );
        }
    }

    template_put_float( filetemp, filewrite, pcap->maxaccel[0]*80 );
    template_put_float( filetemp, filewrite, pcap->maxaccel[1]*80 );
    template_put_float( filetemp, filewrite, pcap->maxaccel[2]*80 );
    template_put_float( filetemp, filewrite, pcap->maxaccel[3]*80 );

    // Experience and level data
    template_put_int  ( filetemp, filewrite, pcap->experienceforlevel[1] );
    template_put_int  ( filetemp, filewrite, pcap->experienceforlevel[2] );
    template_put_int  ( filetemp, filewrite, pcap->experienceforlevel[3] );
    template_put_int  ( filetemp, filewrite, pcap->experienceforlevel[4] );
    template_put_int  ( filetemp, filewrite, pcap->experienceforlevel[5] );
    template_put_float( filetemp, filewrite, pcap->experience.from * 256.0f );
    template_put_int  ( filetemp, filewrite, pcap->experienceworth );
    template_put_float( filetemp, filewrite, pcap->experienceexchange );
    template_put_float( filetemp, filewrite, pcap->experiencerate[0] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[1] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[2] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[3] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[4] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[5] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[6] );
    template_put_float( filetemp, filewrite, pcap->experiencerate[7] );

    // IDSZ identification tags
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_PARENT] );
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_TYPE] );
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_SKILL] );
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_SPECIAL] );
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_HATE] );
    template_put_idsz( filetemp, filewrite, pcap->idsz[IDSZ_VULNERABILITY] );

    // Item and damage flags
    template_put_bool( filetemp, filewrite, pcap->isitem );
    template_put_bool( filetemp, filewrite, pcap->ismount );
    template_put_bool( filetemp, filewrite, pcap->isstackable );
    template_put_bool( filetemp, filewrite, pcap->nameknown );
    template_put_bool( filetemp, filewrite, pcap->usageknown );
    template_put_bool( filetemp, filewrite, pcap->cancarrytonextmodule );
    template_put_bool( filetemp, filewrite, pcap->needskillidtouse );
    template_put_bool( filetemp, filewrite, pcap->platform );
    template_put_bool( filetemp, filewrite, pcap->cangrabmoney );
    template_put_bool( filetemp, filewrite, pcap->canopenstuff );

    // Other item and damage stuff
    template_put_damage_type( filetemp, filewrite, pcap->damagetargettype );
    template_put_action     ( filetemp, filewrite, pcap->weaponaction );

    // Particle attachments
    template_put_int        ( filetemp, filewrite, pcap->attachedprt_amount );
    template_put_damage_type( filetemp, filewrite, pcap->attachedprt_reaffirmdamagetype );
    template_put_int        ( filetemp, filewrite, pcap->attachedprt_pip );

    // Character hands
    template_put_bool( filetemp, filewrite, pcap->slotvalid[SLOT_LEFT] );
    template_put_bool( filetemp, filewrite, pcap->slotvalid[SLOT_RIGHT] );

    // Particle spawning on attack
    template_put_bool( filetemp, filewrite, pcap->attack_attached );
    template_put_int ( filetemp, filewrite, pcap->attack_pip );

    // Particle spawning for GoPoof
    template_put_int( filetemp, filewrite, pcap->gopoofprt_amount );
    template_put_int( filetemp, filewrite, pcap->gopoofprt_facingadd );
    template_put_int( filetemp, filewrite, pcap->gopoofprt_pip );

    // Particle spawning for blud
    template_put_bool( filetemp, filewrite, pcap->blud_valid );
    template_put_int ( filetemp, filewrite, pcap->blud_pip );

    // Extra stuff
    template_put_bool ( filetemp, filewrite, pcap->waterwalk );
    template_put_float( filetemp, filewrite, pcap->dampen );

    // More stuff
    template_put_float( filetemp, filewrite, FP8_TO_FLOAT(pcap->lifeheal) );       // These two are seriously outdated
    template_put_float( filetemp, filewrite, FP8_TO_FLOAT(pcap->manacost) );       // and shouldnt be used. Use scripts instead.
    template_put_int  ( filetemp, filewrite, pcap->lifereturn );
    template_put_int  ( filetemp, filewrite, pcap->stoppedby );
    template_put_string_under( filetemp, filewrite, pcap->skinname[0] );
    template_put_string_under( filetemp, filewrite, pcap->skinname[1] );
    template_put_string_under( filetemp, filewrite, pcap->skinname[2] );
    template_put_string_under( filetemp, filewrite, pcap->skinname[3] );
    template_put_int  ( filetemp, filewrite, pcap->skincost[0] );
    template_put_int  ( filetemp, filewrite, pcap->skincost[1] );
    template_put_int  ( filetemp, filewrite, pcap->skincost[2] );
    template_put_int  ( filetemp, filewrite, pcap->skincost[3] );
    template_put_float( filetemp, filewrite, pcap->strengthdampen );

    // Another memory lapse
    template_put_bool( filetemp, filewrite, btrue - pcap->ridercanattack );
    template_put_bool( filetemp, filewrite, pcap->canbedazed );
    template_put_bool( filetemp, filewrite, pcap->canbegrogged );
    template_put_int ( filetemp, filewrite, 0 );
    template_put_int ( filetemp, filewrite, 0 );
    template_put_bool( filetemp, filewrite, pcap->canseeinvisible );
    template_put_int ( filetemp, filewrite, pcap->kursechance );
    template_put_int ( filetemp, filewrite, pcap->soundindex[SOUND_FOOTFALL] );
    template_put_int ( filetemp, filewrite, pcap->soundindex[SOUND_JUMP] );

    vfs_flush( filewrite );

    // copy the template file to the next free output section
    template_seek_free( filetemp, filewrite );

    // Expansions
    if ( pcap->skindressy&1 )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','R','E','S'), 0 );

    if ( pcap->skindressy&2 )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','R','E','S'), 1 );

    if ( pcap->skindressy&4 )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','R','E','S'), 2 );

    if ( pcap->skindressy&8 )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','R','E','S'), 3 );

    if ( pcap->resistbumpspawn )
        fput_expansion(filewrite, "", MAKE_IDSZ('S','T','U','K'), 0 );

    if ( pcap->istoobig )
        fput_expansion(filewrite, "", MAKE_IDSZ('P','A','C','K'), 0 );

    if ( !pcap->reflect )
        fput_expansion(filewrite, "", MAKE_IDSZ('V','A','M','P'), 1 );

    if ( pcap->alwaysdraw )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','R','A','W'), 1 );

    if ( pcap->isranged )
        fput_expansion(filewrite, "", MAKE_IDSZ('R','A','N','G'), 1 );

    if ( pcap->hidestate != NOHIDE )
        fput_expansion(filewrite, "", MAKE_IDSZ('H','I','D','E'), pcap->hidestate );

    if ( pcap->isequipment )
        fput_expansion(filewrite, "", MAKE_IDSZ('E','Q','U','I'), 1 );

    if ( pcap->bumpsizebig >= pcap->bumpsize * 2 )
        fput_expansion(filewrite, "", MAKE_IDSZ('S','Q','U','A'), 1 );

    if ( pcap->icon != pcap->usageknown )
        fput_expansion(filewrite, "", MAKE_IDSZ('I','C','O','N'), pcap->icon );

    if ( pcap->forceshadow )
        fput_expansion(filewrite, "", MAKE_IDSZ('S','H','A','D'), 1 );

    if ( pcap->ripple == pcap->isitem )
        fput_expansion(filewrite, "", MAKE_IDSZ('R','I','P','P'), pcap->ripple );

    if ( pcap->isvaluable != -1 )
        fput_expansion(filewrite, "", MAKE_IDSZ('V','A','L','U'), pcap->isvaluable );

    // Basic stuff that is always written
    fput_expansion(filewrite, "", MAKE_IDSZ('G','O','L','D'), pcap->money );
    fput_expansion(filewrite, "", MAKE_IDSZ('P','L','A','T'), pcap->canuseplatforms );
    fput_expansion(filewrite, "", MAKE_IDSZ('S','K','I','N'), pcap->skinoverride );
    fput_expansion(filewrite, "", MAKE_IDSZ('C','O','N','T'), pcap->contentoverride );
    fput_expansion(filewrite, "", MAKE_IDSZ('S','T','A','T'), pcap->stateoverride );
    fput_expansion(filewrite, "", MAKE_IDSZ('L','E','V','L'), pcap->leveloverride );

    vfs_printf( filewrite, ": [LIFE] %4.2f\n", FP8_TO_FLOAT(pcap->spawnlife) );
    vfs_printf( filewrite, ": [MANA] %4.2f\n", FP8_TO_FLOAT(pcap->spawnmana) );

    // Copy all skill expansions
    if ( pcap->shieldproficiency > 0 )
        fput_expansion(filewrite, "", MAKE_IDSZ('S','H','P','R'), pcap->shieldproficiency );

    if ( pcap->canuseadvancedweapons > 0 )
        fput_expansion(filewrite, "", MAKE_IDSZ('A','W','E','P'), pcap->canuseadvancedweapons );

    if ( pcap->canjoust > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('J','O','U','S'), pcap->canjoust );

    if ( pcap->candisarm > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','I','S','A'), pcap->candisarm );

    if ( pcap->canseekurse > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('C','K','U','R'), pcap->canseekurse );

    if ( pcap->canusepoison > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('P','O','I','S'), pcap->canusepoison );

    if ( pcap->canread > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('R','E','A','D'), pcap->canread );

    if ( pcap->canbackstab > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('S','T','A','B'), pcap->canbackstab );

    if ( pcap->canusedivine > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('H','M','A','G'), pcap->canusedivine );

    if ( pcap->canusearcane > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('W','M','A','G'), pcap->canusearcane );

    if ( pcap->canusetech > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('T','E','C','H'), pcap->canusetech );

    if ( pcap->hascodeofconduct > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('C','O','D','E'), pcap->hascodeofconduct );

    if ( pcap->hasdarkvision > 0  )
        fput_expansion(filewrite, "", MAKE_IDSZ('D','A','R','K'), pcap->hasdarkvision );

    // dump the rest of the template file
    template_flush( filetemp, filewrite );

    // The end
    vfs_close( filewrite );
    template_close( filetemp );

    return btrue;
}
