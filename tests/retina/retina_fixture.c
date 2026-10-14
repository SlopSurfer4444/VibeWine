/* Offline caller-order regression: no CoreGraphics, AppKit or Wine runtime.
 * Production functions are extracted verbatim into this fixture. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef int BOOL;
typedef uint32_t DWORD;
typedef unsigned UINT;
typedef int INT;
typedef uint32_t CGDirectDisplayID;
typedef CFDataRef CGDisplayModeRef;
#define TRUE 1
#define FALSE 0
#define kCGDirectMainDisplay 1
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL 1
#define DM_DISPLAYORIENTATION 1
#define DM_DISPLAYFIXEDOUTPUT 2
#define DM_BITSPERPEL 4
#define DM_PELSWIDTH 8
#define DM_PELSHEIGHT 16
#define DM_DISPLAYFLAGS 32
#define DM_DISPLAYFREQUENCY 64
#define DM_POSITION 128
#define DMDFO_STRETCH 1
#define DMDFO_CENTER 2
#define DM_INTERLACED 1
#define WINE_DM_UNSUPPORTED 0x80000000
#define kDisplayModeValidFlag 1u
#define kDisplayModeSafeFlag 2u
#define kDisplayModeStretchedFlag 4u
#define kDisplayModeInterlacedFlag 8u
#define kDisplayModeTelevisionFlag 16u
#define TRACE(...) do {} while (0)
#define ERR(...) do {} while (0)
typedef struct { int left, top, right, bottom; } RECT;
typedef struct {
 DWORD dmSize, dmFields, dmDisplayOrientation, dmDisplayFixedOutput;
 DWORD dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
 struct { int x, y; } dmPosition;
} DEVMODEW;
struct display_mode_descriptor {
 DWORD width, height, pixel_width, pixel_height, io_flags;
 double refresh;
 CFStringRef pixel_encoding;
};
struct fake_mode { DWORD width, height, pixel_width, pixel_height, flags; double refresh; };
struct macdrv_adapter { CGDirectDisplayID id; UINT state_flags; };
struct macdrv_gpu { uint64_t id; unsigned vendor_id, device_id, subsys_id, revision_id; const char *name; };
struct macdrv_monitor { CGDirectDisplayID id; CGRect rc_monitor, rc_work; size_t edid_len; void *edid; bool hdr_enabled; };
struct pci_id { unsigned vendor, device, subsystem, revision; };
struct gdi_monitor { RECT rc_monitor, rc_work; size_t edid_len; void *edid; bool hdr_enabled; };
struct gdi_device_manager {
 void (*add_gpu)(const char *, const struct pci_id *, void *, void *);
 void (*add_source)(const char *, UINT, UINT, void *);
 void (*add_monitor)(const struct gdi_monitor *, void *);
 void (*add_modes)(const DEVMODEW *, int, const DEVMODEW *, void *);
};
static bool retina_enabled, retina_on;
static CGDisplayModeRef current_mode;
static struct display_mode_descriptor original;
static unsigned setter_calls, monitor_reads, add_monitor_calls, add_modes_calls;
static BOOL provider_fails, descriptor_unavailable;
static struct gdi_monitor captured_monitor;
static DEVMODEW captured_current;
static int assertions, failures;
static const struct fake_mode *metadata(CGDisplayModeRef m) { return (const struct fake_mode *)CFDataGetBytePtr(m); }
static DWORD CGDisplayModeGetWidth(CGDisplayModeRef m) { return metadata(m)->width; }
static DWORD CGDisplayModeGetHeight(CGDisplayModeRef m) { return metadata(m)->height; }
static DWORD CGDisplayModeGetPixelWidth(CGDisplayModeRef m) { return metadata(m)->pixel_width; }
static DWORD CGDisplayModeGetPixelHeight(CGDisplayModeRef m) { return metadata(m)->pixel_height; }
static DWORD CGDisplayModeGetIOFlags(CGDisplayModeRef m) { return metadata(m)->flags; }
static double CGDisplayModeGetRefreshRate(CGDisplayModeRef m) { return metadata(m)->refresh; }
static CFStringRef CGDisplayModeCopyPixelEncoding(CGDisplayModeRef m) { (void)m; return CFRetain(CFSTR("32bit")); }
static CGDisplayModeRef CGDisplayCopyDisplayMode(CGDirectDisplayID id) { (void)id; return current_mode ? CFRetain(current_mode) : NULL; }
static void CGDisplayModeRelease(CGDisplayModeRef m) { CFRelease(m); }
static double CGDisplayRotation(CGDirectDisplayID id) { (void)id; return 0; }
static double CGRectGetMinX(CGRect r) { return r.origin.x; }
static double CGRectGetMinY(CGRect r) { return r.origin.y; }
static int is_apple_silicon(void) { return 1; }
static int display_mode_bits_per_pixel(CGDisplayModeRef m) { (void)m; return 32; }
static struct display_mode_descriptor *create_original_display_mode_descriptor(CGDirectDisplayID id) {
 (void)id; if (descriptor_unavailable) return NULL;
 struct display_mode_descriptor *copy = malloc(sizeof(*copy)); *copy = original; return copy;
}
static void free_display_mode_descriptor(struct display_mode_descriptor *d) { free(d); }
/* Models the synchronous setter's first action, as in Cocoa setRetinaMode. */
static void macdrv_set_cocoa_retina_mode(bool value) { retina_on = value; setter_calls++; }
static UINT NtUserGetSystemDpiForProcess(void *process) { (void)process; return 96; }
static RECT rect_from_cgrect(CGRect r) { return (RECT){r.origin.x, r.origin.y, r.origin.x+r.size.width, r.origin.y+r.size.height}; }
/* EXACT_GEOMETRY_HELPER */
static int macdrv_get_gpus(struct macdrv_gpu **out, INT *count) {
 if (provider_fails) return 1;
 *out = calloc(1, sizeof(**out)); (*out)->id = 1; (*out)->name = "fake GPU"; *count = 1; return 0;
}
static void macdrv_free_gpus(struct macdrv_gpu *out) { free(out); }
static int macdrv_get_adapters(uint64_t id, struct macdrv_adapter **out, INT *count) {
 (void)id; *out = calloc(1, sizeof(**out)); (*out)->id = 1; *count = 1; return 0;
}
static void macdrv_free_adapters(struct macdrv_adapter *out) { free(out); }
static int macdrv_get_monitors(CGDirectDisplayID id, struct macdrv_monitor **out, INT *count) {
 struct macdrv_monitor *m = calloc(1, sizeof(*m));
 m->id = id;
 CGRect frame = {{0, 0}, {CGDisplayModeGetWidth(current_mode), CGDisplayModeGetHeight(current_mode)}};
 CGRect work = frame; work.origin.y = 20; work.size.height -= 20;
 /* Same conversion seam as cocoa_display.m:984-985; fake point-space NSScreen frame. */
 m->rc_monitor = cgrect_win_from_mac(frame); m->rc_work = cgrect_win_from_mac(work);
 *out = m; *count = 1; monitor_reads++; return 0;
}
static void macdrv_free_monitors(struct macdrv_monitor *out, INT count) { (void)count; free(out); }
static DEVMODEW *display_get_modes(CGDirectDisplayID id, INT *count) {
 (void)id; *count = 1; return calloc(1, sizeof(DEVMODEW));
}
/* PRODUCTION_FUNCTIONS */
static void add_gpu(const char *name, const struct pci_id *pci, void *uuid, void *param) { (void)name; (void)pci; (void)uuid; (void)param; }
static void add_source(const char *name, UINT flags, UINT dpi, void *param) { (void)name; (void)flags; (void)dpi; (void)param; }
static void add_monitor(const struct gdi_monitor *m, void *param) { (void)param; captured_monitor = *m; add_monitor_calls++; }
static void add_modes(const DEVMODEW *current, int count, const DEVMODEW *modes, void *param) {
 (void)count; (void)modes; (void)param; captured_current = *current; add_modes_calls++;
}
static const struct gdi_device_manager manager = {add_gpu, add_source, add_monitor, add_modes};
static void check(const char *name, BOOL pass) { assertions++; if (!pass) {failures++; fprintf(stderr, "FAIL: %s\n", name);} }
static void reset(CGDisplayModeRef mode, bool enabled, bool on) {
 current_mode = mode; retina_enabled = enabled; retina_on = on; setter_calls = monitor_reads = add_monitor_calls = add_modes_calls = 0;
 provider_fails = descriptor_unavailable = FALSE; memset(&captured_monitor,0,sizeof(captured_monitor)); memset(&captured_current,0,sizeof(captured_current));
}
static void geometry_check(const char *label, bool expected, unsigned changes) {
 UINT status = macdrv_UpdateDisplayDevices(&manager, NULL);
 unsigned scale = expected ? 2 : 1;
 DWORD w = CGDisplayModeGetWidth(current_mode), h = CGDisplayModeGetHeight(current_mode);
 char name[128];
 #define CHECK_PART(part, cond) do {snprintf(name,sizeof(name),"%s: %s",label,part); check(name,(cond));} while(0)
 CHECK_PART("success", status == STATUS_SUCCESS);
 CHECK_PART("state updated before monitor provider", retina_on == expected && setter_calls == changes);
 CHECK_PART("one complete provider snapshot", monitor_reads == 1 && add_monitor_calls == 1 && add_modes_calls == 1);
 CHECK_PART("monitor width agrees with current mode", captured_monitor.rc_monitor.right == (int)captured_current.dmPelsWidth);
 CHECK_PART("monitor height agrees with current mode", captured_monitor.rc_monitor.bottom == (int)captured_current.dmPelsHeight);
 CHECK_PART("work area uses current scale", captured_monitor.rc_work.right == (int)(w*scale) && captured_monitor.rc_work.bottom == (int)(h*scale) && captured_monitor.rc_work.top == (int)(20*scale));
 CHECK_PART("current mode follows original-only Retina rule", captured_current.dmPelsWidth == w*scale && captured_current.dmPelsHeight == h*scale);
 #undef CHECK_PART
}
int main(int argc, char **argv) {
 struct fake_mode desktop = {1440,900,2880,1800, kDisplayModeValidFlag|kDisplayModeSafeFlag,60};
 struct fake_mode alternate = {1920,1080,1920,1080,kDisplayModeValidFlag|kDisplayModeSafeFlag,60};
 CGDisplayModeRef desktop_ref = CFDataCreate(NULL,(const UInt8*)&desktop,sizeof(desktop));
 CGDisplayModeRef alternate_ref = CFDataCreate(NULL,(const UInt8*)&alternate,sizeof(alternate));
 original = (struct display_mode_descriptor){1440,900,2880,1800,desktop.flags,60,CFSTR("32bit")};
 if (argc == 2) {
  bool on = strstr(argv[1], "-on") != NULL;
  bool unavailable_mode = strstr(argv[1], "mode-") == argv[1];
  reset(desktop_ref, true, on);
  if (unavailable_mode) current_mode = NULL; else descriptor_unavailable = TRUE;
  check_retina_status();
  check("unknown native state preserves prior Retina value", retina_on == on && setter_calls == 0);
  CFRelease(desktop_ref); CFRelease(alternate_ref);
  printf("{\"assertions\":%d,\"failures\":%d,\"displayApisCalled\":false,\"runtimeLaunched\":false}\n", assertions, failures);
  return failures ? 1 : 0;
 }
 reset(alternate_ref, true, true); geometry_check("leave original Retina mode without GDI call", false, 1);
 reset(desktop_ref, true, false); geometry_check("return to original Retina mode without GDI call", true, 1);
 reset(desktop_ref, true, true); geometry_check("already reconciled original", true, 0);
 reset(alternate_ref, true, false); geometry_check("already reconciled alternate", false, 0);
 reset(desktop_ref, false, false); geometry_check("Retina disabled", false, 0);
 reset(alternate_ref, true, true); provider_fails = TRUE;
 check("GPU provider failure preserved", macdrv_UpdateDisplayDevices(&manager,NULL) == STATUS_UNSUCCESSFUL);
 check("failed provider does not emit incomplete monitor or modes", !monitor_reads && !add_monitor_calls && !add_modes_calls);
 CFRelease(desktop_ref); CFRelease(alternate_ref);
 printf("{\"assertions\":%d,\"failures\":%d,\"displayApisCalled\":false,\"runtimeLaunched\":false}\n",assertions,failures);
 return failures ? 1 : 0;
}
