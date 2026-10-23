/* DS3-only presentation profile retained from the guarded bridge.
 * Shader guards now belong to the matched helper source, not a donor loader. */
#include <crt_externs.h>
#include <stdlib.h>
#include <string.h>

static bool w7ds_ds3_profile_enabled(void) {
  const char *mode = getenv("DXMT_DS3_MARSHAL_FIX");
  if (!mode || strcmp(mode, "1")) return false;
  int argc = *_NSGetArgc();
  char **argv = *_NSGetArgv();
  for (int i = 0; i < argc && i < 2; ++i) {
    const char *base = argv[i];
    for (const char *p = base; *p; ++p)
      if (*p == '/' || *p == '\\') base = p + 1;
    if (!strcasecmp(base, "DarkSoulsIII.exe")) return true;
  }
  return false;
}

static void w7ds_apply_aspect_fit_main(CAMetalLayer *layer) {
  const char *mode = getenv("DXMT_DS3_ASPECT_FIT");
  if (mode && !strcmp(mode, "1") && w7ds_ds3_profile_enabled()) {
    layer.contentsGravity = kCAGravityResizeAspect;
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
  }
}
