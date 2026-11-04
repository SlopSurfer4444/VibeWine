/* Main-owned display capabilities, published before Cocoa startup acknowledgement.
 * Readers copy native values without calling AppKit or dispatching to main. */
struct macdrv_display_edr_record
{
    uint32_t display_id;
    double values[3]; /* current, reference, potential */
};
static pthread_mutex_t macdrv_display_edr_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct macdrv_display_edr_record *macdrv_display_edr_records;
static size_t macdrv_display_edr_count;

static BOOL macdrv_refresh_display_edr_main(void)
{
    if (![NSThread isMainThread]) return FALSE;
    NSArray *screens = [NSScreen screens];
    size_t count = [screens count];
    struct macdrv_display_edr_record *records = count ? calloc(count, sizeof(*records)) : NULL;
    if (count && !records)
    {
        /* A failed topology refresh must not keep old physical-output data. */
        pthread_mutex_lock(&macdrv_display_edr_mutex);
        struct macdrv_display_edr_record *retired = macdrv_display_edr_records;
        macdrv_display_edr_records = NULL;
        macdrv_display_edr_count = 0;
        pthread_mutex_unlock(&macdrv_display_edr_mutex);
        free(retired);
        return FALSE;
    }
    size_t i = 0;
    for (NSScreen *screen in screens)
    {
        records[i].display_id = [[[screen deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];
        records[i].values[0] = screen.maximumExtendedDynamicRangeColorComponentValue;
        records[i].values[1] = 0.0;
        records[i].values[2] = records[i].values[0];
        if (@available(macOS 10.15, *))
        {
            records[i].values[1] = screen.maximumReferenceExtendedDynamicRangeColorComponentValue;
            records[i].values[2] = screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
        }
        ++i;
    }
    pthread_mutex_lock(&macdrv_display_edr_mutex);
    struct macdrv_display_edr_record *retired = macdrv_display_edr_records;
    macdrv_display_edr_records = records;
    macdrv_display_edr_count = count;
    pthread_mutex_unlock(&macdrv_display_edr_mutex);
    free(retired);
    return TRUE;
}

/* Optional synchronous copy ABI. 1 = matching published display, 0 = unavailable.
 * A missing display never overwrites caller memory with fabricated capabilities. */
__attribute__((visibility("default")))
int macdrv_copy_display_edr_v1(uint32_t display_id, double values[3])
{
    int found = 0;
    if (!values) return 0;
    pthread_mutex_lock(&macdrv_display_edr_mutex);
    for (size_t i = 0; i < macdrv_display_edr_count; ++i)
        if (macdrv_display_edr_records[i].display_id == display_id)
        {
            memcpy(values, macdrv_display_edr_records[i].values, sizeof(macdrv_display_edr_records[i].values));
            found = 1;
            break;
        }
    pthread_mutex_unlock(&macdrv_display_edr_mutex);
    return found;
}
