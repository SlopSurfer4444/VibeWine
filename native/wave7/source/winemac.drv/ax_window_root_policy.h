/* This scopes native AppKit window enumeration, not PE control access or trust.
 * Signed-byte/native-route admission remains outside accessibility getters. */
#ifndef WINE_AX_WINDOW_ROOT_POLICY_H
#define WINE_AX_WINDOW_ROOT_POLICY_H
#include "ax_validation.h"

static inline BOOL wine_ax_native_root_metadata_eligible(NSDictionary *info, NSString *identifier,
                                                        BOOL wine_application, NSInteger policy,
                                                        BOOL file_app_bundle, BOOL file_executable)
{
    if (!wine_application || policy != NSApplicationActivationPolicyRegular ||
        !file_app_bundle || !file_executable || ![identifier isKindOfClass:[NSString class]] ||
        ![identifier length]) return NO;
    /* A plist boolean is required; strings/integers are not opt-in. */
    if ([info objectForKey:@"WineNativeIdentity"] != (id)kCFBooleanTrue) return NO;
    id principal = [info objectForKey:@"NSPrincipalClass"];
    if (![principal isKindOfClass:[NSString class]] || ![principal isEqualToString:@"WineApplication"]) return NO;
    id ui = [info objectForKey:@"LSUIElement"], background = [info objectForKey:@"LSBackgroundOnly"];
    if ((ui && ui != (id)kCFBooleanFalse) || (background && background != (id)kCFBooleanFalse)) return NO;
    return YES;
}

static inline BOOL wine_ax_native_application_root_enabled(void)
{
    NSBundle *bundle = [NSBundle mainBundle];
    NSURL *url = [bundle bundleURL], *executable = [bundle executableURL];
    return wine_ax_native_root_metadata_eligible([bundle infoDictionary], [bundle bundleIdentifier],
        [NSApp isKindOfClass:[WineApplication class]], [NSApp activationPolicy],
        [url isFileURL] && [[[url pathExtension] lowercaseString] isEqualToString:@"app"],
        [executable isFileURL]);
}

static inline BOOL wine_ax_window_root_enabled(void)
{
    /* Preserve the existing explicit PE-bridge behavior. Native bundle opt-in
     * does not change macdrv_ax_bridge_enabled() or start/control its worker. */
    return macdrv_ax_bridge_enabled() || wine_ax_native_application_root_enabled();
}
#endif
