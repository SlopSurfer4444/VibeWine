/* Application root seam: expose actual AppKit-owned WineWindow objects only.
 * No provider query, fake window, proxy or inferred Windows control enters here. */
#include "ax_validation.h"

static BOOL wine_ax_window_eligible(WineWindow *window)
{
    return macdrv_get_window_hwnd((macdrv_window)window) && !window.closing &&
           ([window isVisible] || [window isMiniaturized]) &&
           [window presentsVisibleContent];
}

static NSArray *wine_ax_merge_application_elements(NSArray *inherited, NSArray *windows)
{
    NSMutableArray *result = [NSMutableArray array];
    for (id element in inherited)
    {
        if ([element isKindOfClass:[WineWindow class]] && !wine_ax_window_eligible(element)) continue;
        [result addObject:element];
    }
    for (WineWindow *window in windows)
        if ([window isKindOfClass:[WineWindow class]] && wine_ax_window_eligible(window) &&
            ![result containsObject:window]) [result addObject:window];
    return [NSArray arrayWithArray:result];
}

@implementation WineApplication (WineAccessibilityRoot)
- (NSArray *)accessibilityWindows
{
    NSArray *inherited = [super accessibilityWindows];
    if (!macdrv_ax_bridge_enabled()) return inherited;
    return wine_ax_merge_application_elements(inherited, [self windows]);
}
- (NSArray *)accessibilityChildren
{
    NSArray *inherited = [super accessibilityChildren];
    if (!macdrv_ax_bridge_enabled()) return inherited;
    return wine_ax_merge_application_elements(inherited, [self windows]);
}
@end
