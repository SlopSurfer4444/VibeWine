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

