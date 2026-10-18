/*
 * MACDRV Cocoa initialization code
 *
 * Copyright 2011, 2012, 2013 Ken Thomases for CodeWeavers Inc.
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

#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

#include "macdrv_cocoa.h"
#import "cocoa_app.h"

#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"


/* Condition values for an NSConditionLock. Used to signal between run_cocoa_app
   and macdrv_start_cocoa_app so the latter knows when the former is running
   the application event loop. */
enum {
    COCOA_APP_NOT_RUNNING,
    COCOA_APP_RUNNING,
};


struct cocoa_app_startup_info {
    unsigned int        refs;
    NSConditionLock*    lock;
    unsigned long long  tickcount;
    uint64_t            uptime_ns;
    BOOL                success;
    BOOL                cancelled;
};


/* The waiter, run loop source, and active callback each own a reference.
   In particular, timeout must not leave the source pointing into the waiter's
   stack, and completion must not release state still used by the callback. */
static const void* retain_startup_info(const void* info)
{
    struct cocoa_app_startup_info* startup_info = (void*)info;
    __sync_add_and_fetch(&startup_info->refs, 1);
    return info;
}

static void release_startup_info(const void* info)
{
    struct cocoa_app_startup_info* startup_info = (void*)info;
    if (!__sync_sub_and_fetch(&startup_info->refs, 1))
    {
        [startup_info->lock release];
        free(startup_info);
    }
}


/***********************************************************************
 *              run_cocoa_app
 *
 * Transforms the main thread from merely idling in its run loop to
 * being a Cocoa application running its event loop.
 *
 * This will be the perform callback of a custom run loop source that
 * will be scheduled in the main thread's run loop from a secondary
 * thread by macdrv_start_cocoa_app.  This function communicates that
 * it has successfully started the application by changing the condition
 * of a shared NSConditionLock, passed in via the info parameter.
 *
 * If it starts a new application successfully, this function becomes
 * the permanent home of the main thread. Otherwise it returns.
 */
static void run_cocoa_app(void* info)
{
    struct cocoa_app_startup_info* startup_info = (void*)retain_startup_info(info);
    NSConditionLock* lock = startup_info->lock;
    BOOL created_app = FALSE, success = FALSE;

    [lock lock];
    if (startup_info->cancelled)
    {
        [lock unlock];
        release_startup_info(startup_info);
        return;
    }
    [lock unlock];

    @autoreleasepool
    {
        if (!NSApp)
        {
            [WineApplication sharedApplication];
            created_app = TRUE;
        }

        /* CrossOver hack 12205: Prevent call to NSVersionOfRunTimeLibrary() during app startup.
                                 It can crash if a Wine thread unloads a dylib simultaneously. */
        [[NSUserDefaults standardUserDefaults] registerDefaults:
            [NSDictionary dictionaryWithObject:@"YES" forKey:@"NSUseActiveDisplayForMainScreen"]];

        if ([NSApp respondsToSelector:@selector(setWineController:)])
        {
            WineApplicationController* controller = [WineApplicationController sharedController];
            if (controller)
            {
                [NSApp setWineController:controller];
                [controller computeEventTimeAdjustmentFromTicks:startup_info->tickcount uptime:startup_info->uptime_ns];
                macdrv_main_executor_start_v1();
                success = TRUE;
            }
        }

        [lock lock];
        startup_info->success = success;
        [lock unlockWithCondition:COCOA_APP_RUNNING];
    }

    /* No shared startup state may be accessed after releasing our reference.
       An already-started callback may finish after the caller times out. */
    release_startup_info(startup_info);

    if (created_app && success)
    {
        @autoreleasepool
        {
            /* Never returns */
            [NSApp run];
            macdrv_main_executor_stop_v1();
        }
    }
}


/***********************************************************************
 *              macdrv_start_cocoa_app
 *
 * Tells the main thread to transform itself into a Cocoa application.
 *
 * Returns 0 on success, non-zero on failure.
 */
int macdrv_start_cocoa_app(unsigned long long tickcount)
{
@autoreleasepool
{
    int ret = -1;
    CFRunLoopSourceRef source;
    struct cocoa_app_startup_info* startup_info;
    uint64_t uptime_mach = mach_absolute_time();
    mach_timebase_info_data_t mach_timebase;
    NSDate* timeLimit;
    CFRunLoopSourceContext source_context = { 0 };

    /* Make sure Cocoa is in multi-threading mode by detaching a
       do-nothing thread. */
    [NSThread detachNewThreadSelector:@selector(self)
                             toTarget:[NSThread class]
                           withObject:nil];

    if (!(startup_info = calloc(1, sizeof(*startup_info)))) return -1;
    startup_info->refs = 1;
    startup_info->lock = [[NSConditionLock alloc] initWithCondition:COCOA_APP_NOT_RUNNING];
    startup_info->tickcount = tickcount;
    startup_info->success = FALSE;

    mach_timebase_info(&mach_timebase);
    startup_info->uptime_ns = uptime_mach * mach_timebase.numer / mach_timebase.denom;

    timeLimit = [NSDate dateWithTimeIntervalSinceNow:5];

    source_context.info = startup_info;
    source_context.retain = retain_startup_info;
    source_context.release = release_startup_info;
    source_context.perform = run_cocoa_app;
    source = CFRunLoopSourceCreate(NULL, 0, &source_context);

    if (source && startup_info->lock && timeLimit)
    {
        CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CFRunLoopSourceSignal(source);
        CFRunLoopWakeUp(CFRunLoopGetMain());

        if ([startup_info->lock lockWhenCondition:COCOA_APP_RUNNING beforeDate:timeLimit])
        {
            ret = !startup_info->success;
            [startup_info->lock unlock];
        }
    }

    /* Prevent a source whose waiter timed out from starting Cocoa later.
       If its callback already began, its own reference permits it to finish. */
    [startup_info->lock lock];
    startup_info->cancelled = TRUE;
    [startup_info->lock unlock];
    if (source)
    {
        CFRunLoopSourceInvalidate(source);
        CFRelease(source);
    }
    release_startup_info(startup_info);
    return ret;
}
}
