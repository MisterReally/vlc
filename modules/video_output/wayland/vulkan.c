/**
 * @file vulkan.c
 * @brief Vulkan platform-specific code for Wayland
 */
/*****************************************************************************
 * Copyright © 2020 VideoLabs
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>

#include "../vulkan/instance.h"

static void ClosePlatform(vlc_vk_t *vk)
    { (void)vk; }

static int CreateSurface(vlc_vk_t *vk, VkInstance vkinst, VkSurfaceKHR *surface_out)
{
    VkWaylandSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = vk->window->display.wl,
        .surface = vk->window->handle.wl,
    };

    VkResult res = vkCreateWaylandSurfaceKHR(vkinst, &surface_info, NULL, surface_out);
    if (res != VK_SUCCESS) {
        msg_Err(vk, "Failed creating Wayland surface");
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static const struct vlc_vk_operations platform_ops =
{
    .close = ClosePlatform,
    .create_surface = CreateSurface,
};

static int InitPlatform(vlc_vk_t *vk)
{
    if (vk->window->type != VOUT_WINDOW_TYPE_WAYLAND)
        return VLC_EGENERIC;

    vk->platform_ext = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    vk->ops = &platform_ops;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("Vulkan Wayland")
    set_description(N_("Wayland platform support for Vulkan"))
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vulkan platform", 50)
    set_callback(InitPlatform)
    add_shortcut("vk_wl")
vlc_module_end()