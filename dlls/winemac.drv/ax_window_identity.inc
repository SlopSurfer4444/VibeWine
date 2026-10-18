/* Native window identity only; no PE controls, provider requests, or focus mutation. */
    - (BOOL) isAccessibilityElement
    {
        if (wine_ax_window_root_enabled() && hwnd) return !self.closing;
        return [super isAccessibilityElement];
    }

    - (NSString*) accessibilityRole
    {
        if (wine_ax_window_root_enabled() && hwnd) return NSAccessibilityWindowRole;
        return [super accessibilityRole];
    }

    - (id) accessibilityParent
    {
        if (wine_ax_window_root_enabled() && hwnd) return self.closing ? nil : NSApp;
        return [super accessibilityParent];
    }

    - (NSString*) accessibilityIdentifier
    {
        /* Window titles may be empty, duplicated, or changed by the application.
           Use the Windows handle to identify the native AX window for its lifetime.
           This does not expose the application's Windows accessibility tree. */
        if (!hwnd) return [super accessibilityIdentifier];
        return [NSString stringWithFormat:@"wine-hwnd-%p", hwnd];
    }

/* An explicit accessibility action may activate this native window's app.
 * Reads remain passive; Windows still approves focus via its existing event path. */
    - (BOOL) accessibilityPerformRaise
    {
        static BOOL raising;
        if (!wine_ax_window_root_enabled() || !hwnd)
            return [super accessibilityPerformRaise];
        if (![NSThread isMainThread] || raising || self.closing || self.disabled ||
            self.noForeground || ![self canBecomeKeyWindow] ||
            ![self presentsVisibleContent]) return NO;

        BOOL submitted = NO;
        raising = YES;
        @try
        {
            [self makeKeyAndOrderFront:self];
            if (!self.closing)
            {
                /* This is NSApplication's instance method, not the deprecated
                 * NSRunningApplication activation option. The generic Wine
                 * cooperative activation helper remains unchanged. */
                [NSApp activateIgnoringOtherApps:YES];
                submitted = YES;
            }
        }
        @finally { raising = NO; }
        /* Accepted request, not a promise that asynchronous OS activation or
         * the Windows foreground handshake has already completed. */
        return submitted;
    }
