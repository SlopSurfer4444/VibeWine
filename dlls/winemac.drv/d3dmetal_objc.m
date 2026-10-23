/*
 * Mac graphics driver hooks used by D3DMetal (part of the Apple Game Porting Toolkit)
 *
 * Copyright 2025 Brendan Shanks for CodeWeavers, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if defined(__x86_64__)

#include "config.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include "macdrv_cocoa.h"
#import "cocoa_app.h"
#import "cocoa_event.h"
#import "cocoa_window.h"
#import "d3dmetal_objc.h"

#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"


#include "drawable_phase_diag.h"
#include "visibility_snapshot.inc"
#include "visibility_startup.inc"

@implementation WineMetalLayer

    /* Extend [CAMetalLayer nextDrawable] so client_surface_present() can be
     * called for the corresponding client_surface.
     */
    - (id<CAMetalDrawable>) nextDrawable
    {
        struct w7pd_sample diag = w7pd_begin();
        uintptr_t diag_queue = 0, diag_surface = 0;
        id<CAMetalDrawable> drawable;
        bool diag_posted = false;
        w7vis_startup_from_layer(self);
        /* CAMetalLayer's delegate is the WineMetalView that contains it (seems like that's always true
         * for an NSView backing layer).
         * That WineMetalView's parent is the client_surface->cocoa_view WineContentView, it has the
         * client_surface set on it as an associated object.
         */
        if ([self.delegate isKindOfClass:NSClassFromString(@"WineMetalView")])
        {
            NSView* view = (NSView *)self.delegate;
            if ([view.superview isKindOfClass:NSClassFromString(@"WineContentView")] &&
                [view.window    isKindOfClass:NSClassFromString(@"WineWindow")])
            {
                void *client_surface = macdrv_get_view_d3dmetal_client_surface((macdrv_view)view.superview);
                if (client_surface)
                {
                    macdrv_event* event;
                    event = macdrv_create_event(CLIENT_SURFACE_PRESENTED, (WineWindow*)view.window);
                    event->client_surface_presented.client_surface = client_surface;

                    WineEventQueue *queue = [(WineWindow*)view.window queue];
                    diag_queue = (uintptr_t)queue;
                    diag_surface = (uintptr_t)client_surface;
                    w7pd_mark(&diag, 1);
                    [queue postEvent:event];
                    w7pd_mark(&diag, 2);
                    diag_posted = true;
                    macdrv_release_event(event);
                }
            }
        }

        w7pd_mark(&diag, 3);
        if (diag.active && !diag_posted) {
            diag.wall[1] = diag.wall[2] = diag.wall[3];
            diag.cpu[1] = diag.cpu[2] = diag.cpu[3];
        }
        drawable = [super nextDrawable];
        w7pd_mark(&diag, 4);
        w7pd_report(&diag, "drawable", (uintptr_t)self, diag_queue,
                    diag_surface, (uintptr_t)drawable, 0, 0, -1);
        return drawable;
    }

@end

#endif
