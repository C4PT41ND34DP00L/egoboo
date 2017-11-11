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

/// @file  egolib/egoboo_setup.c
/// @brief Functions for handling the <tt>setup.txt</tt> file.
/// @details

#include "egolib/egoboo_setup.h"

#include "egolib/_math.h"
#include "game/Graphics/Camera.hpp"

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------

egoboo_config_t egoboo_config_t::_singleton;

egoboo_config_t& egoboo_config_t::get()
{
    return _singleton;
}

egoboo_config_t::egoboo_config_t() :
    // Graphic configuration section.
    graphic_fullscreen(false,"graphic.fullscreen","enable/disable fullscreen mode."
                       "graphic.fullscreen and graphic.window.fullscreenDesktop are mutually exclusive"),
    graphic_colorBuffer_bitDepth(32,"graphic.colourBuffer.bitDepth","bit depth of the colour buffer"),
    graphic_depthBuffer_bitDepth(24,"graphic.depthBuffer.bitDepth","bit depth of the depth buffer"),
    graphic_stencilBuffer_bitDepth(8, "graphic.stencilBuffer.bitDepth","bit depth of the stencil buffer"),
    graphic_accumulationBuffer_bitDepth(32, "graphic.accumulationBuffer.bitDepth", "bit depth of the accumulation buffer"),
    graphic_resolution_horizontal(800,"graphic.resolution.horizontal", "horizontal resolution"),
    graphic_resolution_vertical(600,"graphic.resolution.vertical", "vertical resolution"),
    graphic_perspectiveCorrection_enable(false,"graphic.perspectiveCorrection.enable","enable/displable perspective correction"),
    graphic_dithering_enable(false, "graphic.dithering.enable","enable/disable dithering"),
    graphic_reflections_enable(true, "graphic.reflections.enable","enable/disable reflections"),
    graphic_reflections_particleReflections_enable(true, "graphic.reflections.particleReflections.enable", "enable/disable particle reflections"),
    graphic_shadows_enable(true, "graphic.shadows.enable", "enable/disable shadows"),
    graphic_shadows_highQuality_enable(true, "graphic.shadows.highQuality.enable", "enable/disable high quality shadows"),                              // Shadow sprites?
    graphic_overlay_enable(true, "graphic.overlay.enable", "enable/disable overlay"),
    graphic_specularHighlights_enable(true,"graphic.specularHighlights.enable","enable/disable specular highlights"),
    graphic_twoLayerWater_enable(true,"graphic.twoLayerWater.enable","enable/disable two layer water"),
    graphic_background_enable(true, "graphic.background.enable", "enable/disable background"),
    graphic_fog_enable(false, "graphic.fog.enable", "enable/disable fog"),
    graphic_gouraudShading_enable(true, "graphic.gouraudShading.enable", "enable/disable Gouraud shading"),
    graphic_antialiasing(2, "graphic.antialiasing", "set antialiasing level 0 (off), 1 (2x), 2 (4x), 3 (8x), 4 (16x)"),
    graphic_anisotropy_enable(false, "graphic.anisotropy.enable", "enable anisotropic texture filtering"),
    graphic_anisotropy_levels(1.0f, "graphic.anisotropy.levels", "anisotropy levels", 1.0f, 16.0f),
    graphic_doubleBuffering_enable(true, "graphic.doubleBuffering.enable", "enable/disable double buffering"),
    graphic_textureFilter_minFilter(Ego::TextureFilter::Linear, "graphic.textureFilter.minFilter", "texture filter used for minification",
    {
        { "none", Ego::TextureFilter::None },
        { "nearest", Ego::TextureFilter::Nearest },
        { "linear", Ego::TextureFilter::Linear },
    }),
    graphic_textureFilter_magFilter(Ego::TextureFilter::Linear, "graphic.textureFilter.magFilter", "texture filter used for magnification",
    {
        { "none", Ego::TextureFilter::None },
        { "nearest", Ego::TextureFilter::Nearest },
        { "linear", Ego::TextureFilter::Linear },
    }),
    graphic_textureFilter_mipMapFilter(Ego::TextureFilter::Linear, "graphic.textureFilter.mipMapFilter", "filter used for mip map selection",
    {
        { "none", Ego::TextureFilter::None },
        { "nearest", Ego::TextureFilter::Nearest },
        { "linear", Ego::TextureFilter::Linear }
    }),
    graphic_simultaneousDynamicLights_max(32, "graphic.simultaneousDynamicLights.max", "inclusive upper bound of simultaneous dynamic lights"),
    graphic_framesPerSecond_max(30, "graphic.framesPerSecond.max", "inclusive upper bound of frames per second"),
    graphic_simultaneousParticles_max(768, "graphic.simultaneousParticles.max", "inclusive upper bound of simultaneous particles"),
    graphic_hd_textures_enable(true, "graphic.graphic_hd_textures_enable", "enable/disable HD textures"),
    //
    graphic_window_borderless(false, "graphic.window.bordless",
                              "if the window is borderless. A bordless window neither has a caption nor an edge frame"),
    graphic_window_resizable(false, "graphic.window.resizable",
                             "if the window is resizable"),
    graphic_window_allowHighDpi(false, "graphic.window.allowHighDpi",
                                "if the window supports high-DPI modes (in Apple terminology 'Retina')"),
    graphic_window_fullscreenDesktop(false, "graphic.window.fullscreenDesktop",
                                     "if the window is a fullscreen desktop window."
                                     "A fullscreen desktop window always covers the entire display or is minimized"
                                     "graphic.fullscreen and graphic.window.fullscreenDesktop are mutually exclusive"),
    // Sound configuration section.
    sound_effects_enable(true, "sound.effects.enable", "enable/disable effects"),
    sound_effects_volume(90, "sound.effects.volume", "effects volume"),
    sound_music_enable(true, "sound.music.enable", "enable/disable music"),
    sound_music_volume(70,"sound.music.volume", "music volume"),
    sound_channel_count(32,"sound.channel.count", "number of audio channels.\n"
    "The number of audio channels available limits the number of sounds playing at the same time"),
    sound_outputBuffer_size(4096, "sound.outputBuffer.size", "size of the output buffers in samples.\n"
    "Should be a power of 2, good values seem to range between 512 (inclusive) and 8192 (inclusive).\n"
    "Smaller values yield faster response time, but can lead to underflow if the audio buffer is not filled in time"),
    sound_highQuality_enable(false,"sound.highQuality.enable","enable/disable high quality sound"),
    sound_footfallEffects_enable(true,"sound.footfallEffects.enable","enable/disable footfall effects"),
    // Network configuration section.
    network_enable(false,"network.enable","enable/disable networking"),
    network_lagTolerance(10,"network.lagTolerance","tolerance of lag in seconds"),
    network_hostName("Egoboo host","network.hostName", "name of host to join"),
    network_playerName("Egoboo player", "network.playerName", "player name in network games"),
    // Game configuration section.
    game_difficulty(Ego::GameDifficulty::Normal, "game.difficulty", "game difficulty",
    {
        { "Easy", Ego::GameDifficulty::Easy },
        { "Normal", Ego::GameDifficulty::Normal },
        { "Hard", Ego::GameDifficulty::Hard },
    }),
    // Camera configuration section.
    camera_control(CameraTurnMode::Auto, "camera.control", "type of camera control",
    {
        { "Good", CameraTurnMode::Good },
        { "Auto", CameraTurnMode::Auto },
        { "None", CameraTurnMode::None },
    }),
    // HUD configuration section.
    hud_feedback(Ego::FeedbackType::Text, "hud.feedback", "feed back given to the player",
    {
        { "None",    Ego::FeedbackType::None },
        { "Numeric", Ego::FeedbackType::Number },
        { "Textual", Ego::FeedbackType::Text },
    }),
    hud_simultaneousMessages_max(6, "hud.simultaneousMessages.max", "inclusive upper bound of simultaneous messages"),
    hud_messageDuration(200, "hud.messageDuration", "time in seconds to keep a message alive"),
    hud_messages_enable(true, "hud.messages.enable", "enable/disable messages"),
    hud_displayStatusBars(true, "hud.displayStatusBars", "show/hide status bar"),
    hud_displayGameTime(false, "hud.displayGameTime","show/hide game timer"),
    hud_displayFramesPerSecond(false, "hud.displayFramesPerSecond", "show/hide frames per second"),
    // Debug configuration section.
	debug_mesh_renderHeightMap(false, "debug.mesh.renderHeightMap", "render mesh's height map"),
	debug_mesh_renderNormals(false, "debug.mesh.renderNormals", "render mesh's normals"),
    debug_object_renderBoundingBoxes(false, "debug.object.renderBoundingBoxes", "render object's bounding boxes"),
    debug_object_renderGrips(false, "debug.object.renderGrips", "render object's grips"),
    debug_hideMouse(true,"debug.hideMouse","show/hide mouse"),
    debug_grabMouse(true,"debug.grabMouse","grab/don't grab mouse"),
    debug_developerMode_enable(false,"debug.developerMode.enable","enable/disable developer mode"),
    debug_sdlImage_enable(true,"debug.SDL_Image.enable","enable/disable advanced SDL_image function")
{}

egoboo_config_t::~egoboo_config_t()
{}

egoboo_config_t& egoboo_config_t::operator=(const egoboo_config_t& other)
{
    make_variable_tuple(*this) = make_variable_tuple(other);
    return *this;
}

//--------------------------------------------------------------------------------------------

namespace Ego {

std::shared_ptr<ConfigFile> Setup::file = nullptr;
const std::string Setup::fileName = "setup.txt";
bool Setup::started = false;

bool Setup::begin() {
    if (started) {
        return true;
    }
    // Parse the configuration file.
    ConfigFileParser parser(fileName);
    try {
        file = parser.parse();
    } catch (...) {
    }
    // If parsing the configuration file failed:
    if (!file) {
        // Revert to default configuration.
        Log::Entry e(Log::Level::Warning, __FILE__, __LINE__);
        e << "unable to load setup file `" << file->getFileName() << "` - reverting to default configuration" << Log::EndOfEntry;
        Log::get() << e;
        started = true;
        try {
            file = std::make_shared<ConfigFile>(fileName);
        } catch (...) {
        }
        // If reverting to default configuration failed:
        if (!file) {
            // Fail.
            Log::get() << Log::Entry::create(Log::Level::Error, __FILE__, __LINE__, "unable to revert to default "
                                             "configuration", Log::EndOfEntry);
            return false;
        }
        started = true;
    } else {
        Log::get() << Log::Entry::create(Log::Level::Info, __FILE__, __LINE__, "setup file ", "`", file->getFileName(), "`",
                                         " loaded", Log::EndOfEntry);
        started = true;
    }
    return started;
}

bool Setup::end() {
    ConfigFileUnParser unparser;
    if (unparser.unparse(file)) {
        file = nullptr;
        return true;
    } else {
        Log::get() << Log::Entry::create(Log::Level::Warning, __FILE__, __LINE__, "unable to save setup file ", "`",
                                         file->getFileName(), "`", Log::EndOfEntry);
        file = nullptr;
        return false;
    }
}

bool Setup::download(egoboo_config_t& cfg) {
    if (!file) {
        Log::Entry e(Log::Level::Error, __FILE__, __LINE__);
        e << "setup file `" << fileName << "` not loaded" << Log::EndOfEntry;
        Log::get() << e;
        throw std::logic_error(e.getText());
    }
    cfg.load(file);
    return true;
}

bool Setup::upload(egoboo_config_t& cfg) {
    if (!file) {
        Log::Entry e(Log::Level::Error, __FILE__, __LINE__);
        e << "setup file `" << fileName << " not loaded" << Log::EndOfEntry;
        Log::get() << e;
        throw std::logic_error(e.getText());
    }
    cfg.store(file);
    return true;
}

} // namespace Ego

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
void setup_init_base_vfs_paths()
{
    //---- tell the vfs to add the basic search paths
    vfs_set_base_search_paths();

    //---- mount all of the default global directories

    // mount the global basicdat directory t the beginning of the list
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat"), Ego::VfsPath("mp_data"), 1 );

    // Create a mount point for the /user/modules directory
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath("modules"), Ego::VfsPath("mp_modules"), 1 );

    // Create a mount point for the /data/modules directory
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("modules"), Ego::VfsPath("mp_modules"), 1 );

    // Create a mount point for the /user/players directory
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath("players"), Ego::VfsPath("mp_players"), 1 );

    // Create a mount point for the /data/players directory
    //vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("players"), Ego::VfsPath("mp_players"), 1 );     //ZF> Let's remove the local players folder since it caused so many problems for people

    // Create a mount point for the /user/remote directory
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath("import"), Ego::VfsPath("mp_import"), 1 );

    // Create a mount point for the /user/remote directory
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath("remote"), Ego::VfsPath("mp_remote"), 1 );
}

void setup_clear_base_vfs_paths()
{
    vfs_remove_mount_point( Ego::VfsPath("mp_data") );
    vfs_remove_mount_point( Ego::VfsPath("mp_modules") );
    vfs_remove_mount_point( Ego::VfsPath("mp_players") );
    vfs_remove_mount_point( Ego::VfsPath("mp_remote") );
}

//--------------------------------------------------------------------------------------------
bool setup_init_module_vfs_paths(const std::string& mod_path)
{
    const char * path_seperator_1, * path_seperator_2;
    const char * mod_dir_ptr;


    if (mod_path  == "") return false;

    // revert to the program's basic mount points
    setup_clear_module_vfs_paths();

    path_seperator_1 = strrchr( mod_path.c_str(), SLASH_CHR );
    path_seperator_2 = strrchr( mod_path.c_str(), NET_SLASH_CHR );
    path_seperator_1 = std::max( path_seperator_1, path_seperator_2 );

    if ( NULL == path_seperator_1 )
    {
        mod_dir_ptr = &(mod_path[0]);
    }
    else
    {
        mod_dir_ptr = path_seperator_1 + 1;
    }

	std::string mod_dir_string = mod_dir_ptr;

	std::string tmpDir;
    //==== set the module-dependent mount points

    //---- add the "/modules/*.mod/objects" directories to mp_objects
	tmpDir = std::string("modules") + SLASH_STR + mod_dir_string + SLASH_STR + "objects";

    // mount the user's module objects directory at the beginning of the mount point list
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath(tmpDir), Ego::VfsPath("mp_objects"), 1 );

    // mount the global module objects directory next in the mount point list
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath(tmpDir), Ego::VfsPath("mp_objects"), 1 );

    //---- add the "/basicdat/globalobjects/*" directories to mp_objects
    //ZF> TODO: Maybe we should dynamically search for all folders in this directory and add them as valid mount points?
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "items"),            Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "magic"),            Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "magic_item"),       Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "misc"),             Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "monsters"),         Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "players"),          Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "potions"),          Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "unique"),           Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "weapons"),          Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "work_in_progress"), Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "traps"),            Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "pets"),             Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "scrolls"),          Ego::VfsPath("mp_objects"), 1 );
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalobjects" SLASH_STR "armor"),            Ego::VfsPath("mp_objects"), 1 );

    //---- add the "/modules/*.mod/gamedat" directory to mp_data
    tmpDir = std::string("modules") + SLASH_STR + mod_dir_string + SLASH_STR  + "gamedat";

    // mount the user's module gamedat directory at the beginning of the mount point list
    vfs_add_mount_point( fs_getUserDirectory(), Ego::FsPath(tmpDir), Ego::VfsPath("mp_data"), 1 );

    // append the global module gamedat directory
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath(tmpDir), Ego::VfsPath("mp_data"), 1 );

    // put the global globalparticles data after the module gamedat data
    vfs_add_mount_point( fs_getDataDirectory(), Ego::FsPath("basicdat" SLASH_STR "globalparticles"), Ego::VfsPath("mp_data"), 1 );

    return true;
}

//--------------------------------------------------------------------------------------------
void setup_clear_module_vfs_paths()
{
    /// @author BB
    /// @details clear out the all mount points

    // clear out the basic mount points
    setup_clear_base_vfs_paths();

    // clear out the module's mount points
    vfs_remove_mount_point( Ego::VfsPath("mp_objects") );

    // set up the basic mount points again
    setup_init_base_vfs_paths();
}
