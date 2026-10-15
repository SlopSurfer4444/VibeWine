"""Offline tests of extracted production fragments; no AppKit, Wine or GUI launch.

Foundation owns mock layer objects. CoreGraphics builds/queries the actual mask
paths. WindowPosChanged's mask block, conversion functions, wrapper and setMask:
are copied from source at test time, not reimplemented as the system under test.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
PUBLIC_ROOT = HERE.parents[2]
CANDIDATE = HERE.parent / "source/winemac.drv"
BASELINE = PUBLIC_ROOT / "history/wave7-candidate1/native/winemac.drv"


def extract(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    i = opening + 1
    while depth:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    return source[start:i]


PRELUDE = r'''
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
typedef struct { int left, top, right, bottom; } RECT;
static bool retina_on;
static bool EqualRect(const RECT *a, const RECT *b) {
 return a->left==b->left && a->top==b->top && a->right==b->right && a->bottom==b->bottom;
}
static void OffsetRect(RECT *r, int x, int y) {
 r->left+=x; r->right+=x; r->top+=y; r->bottom+=y;
}
static void OnMainThread(void (^block)(void)) { block(); }
@interface MockLayer : NSObject
@property CGRect bounds;
@property CGPoint position;
@property CGPoint anchorPoint;
@property BOOL geometryFlipped;
@property CGColorRef fillColor;
@property (assign) MockLayer *superlayer;
@property (retain) NSMutableArray *children;
@property NSUInteger removes;
@property CGPathRef path;
+ (instancetype)layer;
- (void)addSublayer:(MockLayer *)layer;
- (void)removeFromSuperlayer;
@end
@implementation MockLayer {
 CGPathRef _savedPath;
}
+ (instancetype)layer { return [[[self alloc] init] autorelease]; }
- (instancetype)init { if ((self=[super init])) self.children=[NSMutableArray array]; return self; }
- (CGPathRef)path { return _savedPath; }
- (void)setPath:(CGPathRef)p { if (_savedPath) CGPathRelease(_savedPath); _savedPath=p?CGPathCreateCopy(p):NULL; }
- (void)addSublayer:(MockLayer *)layer { [self.children addObject:layer]; layer.superlayer=self; }
- (void)removeFromSuperlayer {
 MockLayer *parent=self.superlayer;
 if (parent) { parent.removes++; [parent.children removeObjectIdenticalTo:self]; self.superlayer=nil; }
}
- (void)dealloc { if (_savedPath) CGPathRelease(_savedPath); [_children release]; [super dealloc]; }
@end
@interface CAShapeLayer : MockLayer @end
@implementation CAShapeLayer @end
@interface MockView : NSObject
@property (retain) MockLayer *layer;
@end
@implementation MockView @end
@interface MaskBase : NSObject
@property (retain) MockView *contentView;
@property (retain) CAShapeLayer *contentViewMaskLayer;
- (void)setMask:(CGRect)rect;
@end
@implementation MaskBase
'''

AFTER_METHOD = r'''
@end
@interface WineWindow : MaskBase
@property CGRect received;
@property NSUInteger calls;
@end
@implementation WineWindow
- (void)setMask:(CGRect)rect { self.received=rect; self.calls++; [super setMask:rect]; }
@end
typedef WineWindow *macdrv_window;
struct WinData {
 struct { RECT window, visible; } rects;
 macdrv_window cocoa_window;
 bool fullscreen;
};
'''

CASES = r'''
static void require(bool ok, const char *message) {
 if (!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(3); }
}
static bool near(double a,double b) { return std::abs(a-b)<1e-8; }
static WineWindow *makeWindow(double width,double height) {
 WineWindow *w=[[[WineWindow alloc] init] autorelease];
 w.contentView=[[[MockView alloc] init] autorelease];
 w.contentView.layer=[MockLayer layer];
 w.contentView.layer.bounds=CGRectMake(0,0,width,height);
 w.contentView.layer.position=CGPointMake(17,23);
 w.contentView.layer.anchorPoint=CGPointMake(.25,.75);
 w.contentView.layer.geometryFlipped=YES;
 return w;
}
static void checkAperture(WineWindow *w, CGRect expected) {
 require(near(w.received.origin.x,expected.origin.x) && near(w.received.origin.y,expected.origin.y)
         && near(w.received.size.width,expected.size.width) && near(w.received.size.height,expected.size.height),
         "aperture must be local Cocoa points");
 MockLayer *outer=w.contentView.layer, *mask=w.contentViewMaskLayer;
 require(mask && outer.children.count==1, "exactly one attached mask");
 require(CGRectEqualToRect(mask.bounds,outer.bounds), "mask takes current content bounds");
 require(CGPointEqualToPoint(mask.position,outer.position) && CGPointEqualToPoint(mask.anchorPoint,outer.anchorPoint)
         && mask.geometryFlipped==outer.geometryFlipped, "mask layer coordinate properties copied");
 require(CGColorEqualToColor(mask.fillColor,CGColorGetConstantColor(kCGColorBlack)), "mask bars black");
 // Sample on both sides of each aperture edge, plus a broad grid. Avoid path
 // boundary ambiguity; test actual CoreGraphics path coverage, not a fake path.
 double xs[]={.25,expected.origin.x-.25,expected.origin.x+.25,
              CGRectGetMaxX(expected)-.25,CGRectGetMaxX(expected)+.25,outer.bounds.size.width-.25};
 double ys[]={.25,expected.origin.y-.25,expected.origin.y+.25,
              CGRectGetMaxY(expected)-.25,CGRectGetMaxY(expected)+.25,outer.bounds.size.height-.25};
 for (double x:xs) for (double y:ys) {
  CGPoint p=CGPointMake(x,y);
  if (!CGRectContainsPoint(outer.bounds,p)) continue;
  bool outside=!CGRectContainsPoint(expected,p);
  require((bool)CGPathContainsPoint(mask.path,NULL,p,false)==outside, "actual mask path coverage");
 }
 require(!CGPathContainsPoint(mask.path,NULL,CGPointMake(CGRectGetMidX(expected),CGRectGetMidY(expected)),false),
         "center remains visible");
}
static void geometryCase(RECT visible,RECT window,bool retina,CGRect expected) {
 retina_on=retina;
 double scale=retina?2:1;
 WineWindow *w=makeWindow((visible.right-visible.left)/scale,(visible.bottom-visible.top)/scale);
 WinData data={ {window,visible},w,false };
 apply_mask(&data,true);
 require(data.fullscreen && w.calls==1,"fullscreen transition applied");
 checkAperture(w,expected);
 // Same-state fullscreen calls must replace old bars, not accumulate layers.
 NSUInteger removed=w.contentView.layer.removes;
 apply_mask(&data,true);
 require(w.contentView.layer.removes==removed+1,"old mask removed on repeated fullscreen");
 checkAperture(w,expected);
 apply_mask(&data,false);
 require(!data.fullscreen && !w.contentViewMaskLayer && !w.contentView.layer.children.count,"exit clears mask");
 NSUInteger calls=w.calls;
 apply_mask(&data,false);
 require(w.calls==calls,"inactive repeated windowpos has no mask call");
}
static void lifecycle() {
 retina_on=true;
 WineWindow *w=makeWindow(1710,1107);
 WinData d={ {{0,145,3420,2069},{0,0,3420,2214}},w,false };
 apply_mask(&d,true); checkAperture(w,CGRectMake(0,72.5,1710,962));
 d.rects.window={200,400,3200,1800};
 apply_mask(&d,true); checkAperture(w,CGRectMake(100,200,1500,700));
 d.rects.window=d.rects.visible;
 apply_mask(&d,true);
 require(!w.contentViewMaskLayer && !w.contentView.layer.children.count,"equal window/visible clears old mask");
 d.rects.window={0,145,3420,2069};
 apply_mask(&d,true); checkAperture(w,CGRectMake(0,72.5,1710,962));
 d.rects.window={0,0,0,0};
 apply_mask(&d,true);
 require(!w.contentViewMaskLayer && !w.contentView.layer.children.count,"empty aperture clears old mask");
 // Changed bounds must be captured on the next real call.
 w.contentView.layer.bounds=CGRectMake(0,0,2000,1200);
 d.rects.visible={0,0,4000,2400}; d.rects.window={200,400,3800,2000};
 apply_mask(&d,true); checkAperture(w,CGRectMake(100,200,1800,800));
}
int main() { @autoreleasepool {
 geometryCase({0,0,3420,2214},{0,145,3420,2069},false,CGRectMake(0,145,3420,1924));
 geometryCase({0,0,3420,2214},{0,145,3420,2069},true,CGRectMake(0,72.5,1710,962));
 geometryCase({-3420,-2214,0,0},{-3420,-2069,0,-145},true,CGRectMake(0,72.5,1710,962));
 geometryCase({1000,-3000,5000,-1000},{1500,-2850,4500,-1150},false,CGRectMake(500,150,3000,1700));
 geometryCase({1000,-3000,5000,-1000},{1500,-2850,4500,-1150},true,CGRectMake(250,75,1500,850));
 geometryCase({-5000,1000,-1000,3000},{-4500,1150,-1500,2850},false,CGRectMake(500,150,3000,1700));
 lifecycle();
 puts("PASS: 6 geometry cases + replacement/clear/change lifecycle");
 } return 0; }
'''


def build_source(caller_root, wrapper_root):
    caller = extract((caller_root / "window.c").read_text(), "if (fullscreen || fullscreen != data->fullscreen)")
    wrapper = extract((wrapper_root / "cocoa_window.m").read_text(), "void macdrv_set_window_mask(")
    method = extract((CANDIDATE / "cocoa_window.m").read_text(), "- (void) setMask:(CGRect)rect")
    conversions = "\n".join([
        extract((BASELINE / "macdrv.h").read_text(), "static inline CGRect cgrect_from_rect("),
        extract((BASELINE / "macdrv_cocoa.h").read_text(), "static inline CGRect cgrect_mac_from_win("),
    ])
    return PRELUDE + method + AFTER_METHOD + conversions + wrapper + "\nstatic void apply_mask(WinData *data,bool fullscreen) {\n" + caller + "\n}\n" + CASES


class ExtractedMaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="wave7-mask-offline-")
        cls.runs = {}
        for name, caller, wrapper in [
            ("candidate", CANDIDATE, CANDIDATE),
            ("original", BASELINE, BASELINE),
            ("origin_only", CANDIDATE, BASELINE),
            ("scale_only", BASELINE, CANDIDATE),
        ]:
            src = Path(cls.tmp.name) / (name + ".mm")
            exe = src.with_suffix("")
            src.write_text(build_source(caller, wrapper))
            build = subprocess.run(["clang++", "-std=c++17", "-fblocks", "-framework", "Foundation",
                                    "-framework", "CoreGraphics", str(src), "-o", str(exe)],
                                   capture_output=True, text=True, timeout=30)
            if build.returncode:
                raise AssertionError(name + " harness compilation failed:\n" + build.stderr)
            cls.runs[name] = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_candidate_actual_fragments_geometry_and_lifecycle(self):
        r = self.runs["candidate"]
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("6 geometry cases", r.stdout)

    def test_original_fails_same_contract(self):
        self.assertEqual(self.runs["original"].returncode, 3)
        self.assertIn("aperture must be local", self.runs["original"].stderr)

    def test_origin_only_fails_retina_contract(self):
        self.assertEqual(self.runs["origin_only"].returncode, 3)
        self.assertIn("aperture must be local", self.runs["origin_only"].stderr)

    def test_scale_only_fails_translated_origin_contract(self):
        self.assertEqual(self.runs["scale_only"].returncode, 3)
        self.assertIn("aperture must be local", self.runs["scale_only"].stderr)

    def test_source_is_the_narrow_expected_split(self):
        caller = extract((CANDIDATE / "window.c").read_text(), "if (fullscreen || fullscreen != data->fullscreen)")
        wrapper = extract((CANDIDATE / "cocoa_window.m").read_text(), "void macdrv_set_window_mask(")
        self.assertIn("OffsetRect(&mask_rect, -data->rects.visible.left, -data->rects.visible.top)", caller)
        self.assertIn("[window setMask:cgrect_mac_from_win(rect)]", wrapper)
        # The actual bar implementation is intentionally retained unchanged.
        self.assertEqual(extract((CANDIDATE / "cocoa_window.m").read_text(), "- (void) setMask:(CGRect)rect"),
                         extract((BASELINE / "cocoa_window.m").read_text(), "- (void) setMask:(CGRect)rect"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
