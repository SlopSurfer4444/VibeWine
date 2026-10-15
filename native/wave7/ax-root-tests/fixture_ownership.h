/* MRC ownership across AppKit delegate autorelease pools and delayed callbacks. */
#ifndef FIXTURE_OWNERSHIP_H
#define FIXTURE_OWNERSHIP_H
static inline void fixture_replace_windows(NSArray **slot, NSArray *windows)
{
    NSArray *owned = [windows copy];
    [*slot release];
    *slot = owned;
}
#endif
