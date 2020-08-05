/*****************************************************************************
 * freetype.c : Put text on the video, using freetype2
 *****************************************************************************
 * Copyright (C) 2002 - 2015 VLC authors and VideoLAN
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
 *          Gildas Bazin <gbazin@videolan.org>
 *          Bernie Purcell <bitmap@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Felix Paul Kühne <fkuehne@videolan.org>
 *          Salah-Eddin Shaban <salshaaban@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdint.h>

#include <vlc_common.h>
#include <vlc_filter.h>                     /* filter_sys_t */
#include <vlc_dialog.h>                     /* FcCache dialog */

#include <fontconfig/fontconfig.h>

#include "../platform_fonts.h"
#include "backends.h"

static FcConfig *config;
static uintptr_t refs;
static vlc_mutex_t lock = VLC_STATIC_MUTEX;

int FontConfig_Prepare( vlc_font_select_t *fs )
{
    vlc_tick_t ts;

    vlc_mutex_lock( &lock );
    if( refs++ > 0 )
    {
        vlc_mutex_unlock( &lock );
        return VLC_SUCCESS;
    }

    msg_Dbg( fs->p_obj, "Building font databases.");
    ts = vlc_tick_now();

#ifndef _WIN32
    config = FcInitLoadConfigAndFonts();
    if( unlikely(config == NULL) )
        refs = 0;

#else
    unsigned int i_dialog_id = 0;
    dialog_progress_bar_t *p_dialog = NULL;
    config = FcInitLoadConfig();

    int i_ret =
        vlc_dialog_display_progress( fs->p_obj, true, 0.0, NULL,
                                     _("Building font cache"),
                                     _("Please wait while your font cache is rebuilt.\n"
                                     "This should take less than a few minutes.") );

    i_dialog_id = i_ret > 0 ? i_ret : 0;

    if( FcConfigBuildFonts( config ) == FcFalse )
        return VLC_ENOMEM;

    if( i_dialog_id != 0 )
        vlc_dialog_cancel( fs->p_obj, i_dialog_id );

#endif

    vlc_mutex_unlock( &lock );
    msg_Dbg( fs->p_obj, "Took %" PRId64 " microseconds", vlc_tick_now() - ts );

    return (config != NULL) ? VLC_SUCCESS : VLC_EGENERIC;
}

void FontConfig_Unprepare( vlc_font_select_t *fs )
{
    VLC_UNUSED(fs);
    vlc_mutex_lock( &lock );
    assert( refs > 0 );
    if( --refs == 0 )
        FcConfigDestroy( config );

    vlc_mutex_unlock( &lock );
}

int FontConfig_SelectAmongFamilies( vlc_font_select_t *fs, const fontfamilies_t *families,
                                    const vlc_family_t **pp_result )
{
    vlc_family_t *p_family = NULL;

    for( int i = 0; i < 4; ++i ) /* Iterate through FC_{SLANT,WEIGHT} combos */
    {
        bool const b_bold = i & 1;
        bool const b_italic = i & 2;

        int i_index = 0;
        FcResult result = FcResultMatch;
        FcPattern *pat, *p_pat;
        FcChar8* val_s;
        FcBool val_b;
        char *psz_fontfile = NULL;

        /* Create a pattern and fill it */
        pat = FcPatternCreate();
        if (!pat) continue;

        /* */
        const char *psz_lcname;
        vlc_vector_foreach( psz_lcname, &families->vec )
            FcPatternAddString( pat, FC_FAMILY, (const FcChar8*) psz_lcname );
        FcPatternAddBool( pat, FC_OUTLINE, FcTrue );
        FcPatternAddInteger( pat, FC_SLANT, b_italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN );
        FcPatternAddInteger( pat, FC_WEIGHT, b_bold ? FC_WEIGHT_EXTRABOLD : FC_WEIGHT_NORMAL );

        /* */
        FcDefaultSubstitute( pat );
        if( !FcConfigSubstitute( config, pat, FcMatchPattern ) )
        {
            FcPatternDestroy( pat );
            continue;
        }

        /* Find the best font for the pattern, destroy the pattern */
        p_pat = FcFontMatch( config, pat, &result );
        FcPatternDestroy( pat );
        if( !p_pat || result == FcResultNoMatch ) continue;

        if( FcResultMatch != FcPatternGetString( p_pat, FC_FAMILY, 0, &val_s ) )
        {
            FcPatternDestroy( p_pat );
            continue;
        }

        if( !p_family )
        {
            char *psz_fnlc = LowercaseDup((const char *)val_s);
            p_family = vlc_dictionary_value_for_key( &fs->family_map, psz_fnlc );
            if( p_family == kVLCDictionaryNotFound )
            {
                p_family = NewFamily( fs, psz_fnlc, &fs->p_families,
                                      &fs->family_map, psz_fnlc );
                if( !p_family )
                {
                    free(psz_fnlc);
                    FcPatternDestroy( pat );
                    return VLC_ENOMEM;
                }
            }
            free(psz_fnlc);
        }

        /* Check the new pattern */
        if( ( FcResultMatch != FcPatternGetBool( p_pat, FC_OUTLINE, 0, &val_b ) )
            || ( val_b != FcTrue ) )
        {
            FcPatternDestroy( p_pat );
            continue;
        }

        if( FcResultMatch != FcPatternGetInteger( p_pat, FC_INDEX, 0, &i_index ) )
        {
            i_index = 0;
        }

        if( FcResultMatch == FcPatternGetString( p_pat, FC_FILE, 0, &val_s ) )
            psz_fontfile = strdup( (const char*)val_s );

        FcPatternDestroy( p_pat );

        if( !psz_fontfile )
            continue;

        NewFont( psz_fontfile, i_index, b_bold, b_italic, p_family );
    }

    *pp_result = p_family;
    return VLC_SUCCESS;
}

int FontConfig_GetFamily( vlc_font_select_t *fs, const char *psz_lcname,
                          const vlc_family_t **pp_result )
{
    fontfamilies_t families;
    families.psz_key = psz_lcname;
    vlc_vector_init( &families.vec );
    vlc_vector_push( &families.vec, (char *) psz_lcname );
    int ret = FontConfig_SelectAmongFamilies( fs, &families, pp_result );
    vlc_vector_clear( &families.vec );
    return ret;
}

int FontConfig_GetFallbacksAmongFamilies( vlc_font_select_t *fs, const fontfamilies_t *families,
                                          uni_char_t codepoint, vlc_family_t **pp_result )
{

    VLC_UNUSED( codepoint );
    vlc_family_t *p_family = NULL;

    p_family = vlc_dictionary_value_for_key( &fs->fallback_map, families->psz_key );
    if( p_family != kVLCDictionaryNotFound )
    {
        *pp_result = p_family;
        return VLC_SUCCESS;
    }
    else
        p_family = NULL;
    const char *psz_last_name = "";

    FcPattern  *p_pattern = FcPatternCreate();
    if (!p_pattern)
        return VLC_EGENERIC;

    const char *psz_lcname;
    vlc_vector_foreach( psz_lcname, &families->vec )
        FcPatternAddString( p_pattern, FC_FAMILY, (const FcChar8*) psz_lcname );

    if( FcConfigSubstitute( config, p_pattern, FcMatchPattern ) == FcTrue )
    {
        FcDefaultSubstitute( p_pattern );
        FcResult result;
        FcFontSet* p_font_set = FcFontSort( config, p_pattern, FcTrue, NULL, &result );
        if( p_font_set )
        {
            for( int i = 0; i < p_font_set->nfont; ++i )
            {
                char* psz_name = NULL;
                FcPatternGetString( p_font_set->fonts[i],
                                    FC_FAMILY, 0, ( FcChar8** )( &psz_name ) );

                /* Avoid duplicate family names */
                if( strcasecmp( psz_last_name, psz_name ) )
                {
                    vlc_family_t *p_temp = NewFamilyFromMixedCase( fs, psz_name,
                                                      &p_family, NULL, NULL );

                    if( unlikely( !p_temp ) )
                    {
                        FcFontSetDestroy( p_font_set );
                        FcPatternDestroy( p_pattern );
                        if( p_family )
                            FreeFamilies( p_family, NULL );
                        return VLC_EGENERIC;
                    }

                    psz_last_name = p_temp->psz_name;
                }
            }
            FcFontSetDestroy( p_font_set );
        }
    }
    FcPatternDestroy( p_pattern );

    if( p_family )
        vlc_dictionary_insert( &fs->fallback_map, families->psz_key, p_family );

    *pp_result = p_family;
    return VLC_SUCCESS;
}
