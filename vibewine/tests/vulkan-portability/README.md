# Vulkan portability enumeration contract

This local regression compiles the complete current
`convert_instance_create_info`, the actual structure and helper definitions from
`dlls/win32u/vulkan.c`, and the real Wine Vulkan driver, Vulkan extension-list,
list and tree headers. Production fragments retain their original LGPL notice.
The host allocator and platform driver mapping are bounded test seams. The
loader/device enumeration is an explicit API-contract stand-in, not MoltenVK.

The five-line source adaptation follows [Gcenx/macports-wine's recipe at
850f5330](https://github.com/Gcenx/macports-wine/blob/850f5330ed500a87bdcf8441763ad6cf4109aedc/emulators/wine-stable/files/0001-win32u-Enable-host-Vulkan-portability-enumeration.diff).
The recipe has no original author header; attribution to its checked source
does not establish an unknown original author. Its target Wine source retains
its existing LGPL-2.1-or-later license and Roderick Colenbrander copyright.

[Khronos's contract](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_enumeration.html)
requires both the portability extension and the enumeration flag to expose
portability-subset physical devices. The converter now supplies both when the
host advertises the extension, without replacing existing flags.

Run from the component root:

```sh
python3 vibewine/tests/vulkan-portability/run.py
python3 vibewine/tests/vulkan-portability/run.py --revision d2237a9a82d9960360ce8400242ddeae6646c347 --expect-regression
```

Both runs use ASan and UBSan, a 60-second compile limit and a 15-second execution
limit. Eight cases cover host support present/absent, preservation of existing
flags, extension deduplication, explicit-layer rejection, out-of-memory output
handling, Win32-to-native surface mapping/external-memory controls and real
debug callback-list transfer. The baseline must produce exactly six diagnosed
failures: the missing extension, missing flag and resulting contract rejection
for each of two host-support cases. Unexpected diagnostics, sanitizer reports,
signals, timeouts or additional failures fail the baseline check.

All artifacts live in a temporary directory removed on exit. No GPU, Vulkan
loader, Wine prefix, game, user window or installed runtime is used or changed.
The current tested Wine configuration is `--without-vulkan` with the separate
DXMT route. This patch therefore has no demonstrated effect on current DS3
gameplay or FPS. Actual MoltenVK instance/device creation and a Vulkan-enabled
Wine build remain untested integration boundaries.
