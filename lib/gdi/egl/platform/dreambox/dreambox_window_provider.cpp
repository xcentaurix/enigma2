#include <lib/base/eerror.h>
#include <lib/gdi/egl/platform/dreambox/dreambox_window_provider.h>
#include <lib/gdi/fb.h>

#ifdef HAVE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

// drm/drm_fourcc.h's DRM_FORMAT_ABGR8888. A diagnostic pass (comparing
// known-good source-image RGB values against the bytes actually handed to
// glTexImage2D/glTexSubImage2D for both text and picon uploads) proved the
// GPU texture pipeline puts the right byte in the right place every time -
// so the visible R/B swap users saw was never in texture uploads at all.
// This is the one remaining untested link: it tells the vendor's native-
// pixmap EGL surface how to interpret the live scanout framebuffer memory
// that GL renders directly into and the display controller reads from.
// ARGB8888 (memory order B,G,R,A) matched gTextureManager's own BGRA
// assumption for enigma2 surfaces, but that's a fact about *enigma2's* CPU
// pixmaps, not about what this SoC's display plane actually expects -
// several Broadcom-based STB graphics stacks are known to swap R/B in their
// hardware compositor's native order. Try ABGR8888 (memory order R,G,B,A).
#define DRM_FORMAT_ABGR8888 0x34324241

DreamboxWindowProvider::DreamboxWindowProvider() {
	memset(&m_pixmap, 0, sizeof(m_pixmap));
}

DreamboxWindowProvider::~DreamboxWindowProvider() {
	cleanup();
}

bool DreamboxWindowProvider::init(int width, int height) {
	fbClass* fb = fbClass::getInstance();
	if (!fb) {
		// gFBDC (the classic gDC) is not built when EGL is enabled (see
		// lib/gdi/Makefile.inc) - it used to be the sole owner of fbClass's
		// construction (gFBDC::gFBDC() does "fb = new fbClass;"), so we take
		// over that responsibility here since we're now the sole consumer of
		// the live framebuffer.
		fb = new fbClass;
		if (!fb) {
			eDebug("[DreamboxWindowProvider] failed to construct fbClass");
			return false;
		}
	}

	// fbClass only opens the device and reads the *boot-time* mode in its
	// constructor - stride is left uninitialized and lfb unmapped until
	// SetMode() actually programs the mode (this is what gFBDC::setResolution()
	// normally does; gFBDC is not built when EGL is enabled, so we do it here).
	// TEMPORARY DIAGNOSTIC: force single buffering to test whether
	// triple-buffering's yres_virtual=height*3 layout is what's causing the
	// observed horizontal-stripe repetition (i.e. whether the GPU/EGL pixmap
	// surface is treating the whole stacked buffer as its render height
	// instead of just our declared height).
	if (fb->SetMode(width, height, 32, /*forceSingleBuffer=*/true) < 0) {
		eDebug("[DreamboxWindowProvider] fbClass::SetMode(%dx%d) failed", width, height);
		return false;
	}

	// With triple/double buffering, which page is actually scanned out is
	// controlled independently by FBIOPAN_DISPLAY (fbClass::setOffset()) - the
	// mmap base (fb->lfb, offset 0) is just page 0 of that larger buffer. Our
	// pixmap always describes page 0, so pin the scanout to page 0 too,
	// otherwise the GPU can render correctly into memory the display never
	// actually shows (whatever page a prior boot/run last panned to).
	fb->setOffset(0);

	// Describe the *existing* live framebuffer memory as the pixmap - see the
	// class comment for why (no separate allocation, no undocumented present
	// ioctl needed).
	m_pixmap.mem.magic = DMEGL_PIXMAP_MAGIC;
	m_pixmap.mem.length = (unsigned long)fb->Stride() * (unsigned long)height;
	m_pixmap.mem.offset = 0;
	m_pixmap.mem.fd = -1; // no separate fd: addr is already mapped below
	m_pixmap.mem.phys = (uintptr_t)fb->getPhysAddr();
	m_pixmap.mem.addr = fb->lfb;

	m_pixmap.width = (unsigned int)width;
	m_pixmap.height = (unsigned int)height;
	m_pixmap.pitch = fb->Stride();
	m_pixmap.format = DRM_FORMAT_ABGR8888;

	eDebug("[DreamboxWindowProvider] init %dx%d pitch=%u phys=0x%lx", width, height, m_pixmap.pitch, (unsigned long)m_pixmap.mem.phys);
	return true;
}

EGLNativeDisplayType DreamboxWindowProvider::getNativeDisplay() {
	return EGL_DEFAULT_DISPLAY;
}

void* DreamboxWindowProvider::getNativePixmap() {
	return &m_pixmap;
}

void DreamboxWindowProvider::presentPixmap() {
	// TEST: the glReadPixels forced-copy that used to live here was added
	// based on a readback diagnostic captured *before* the gRC-thread EGL
	// context-affinity fix (see grc.cpp/egl_init.cpp) - at that time NOTHING
	// rendered at all (viewport was (0,0,0,0)), so of course the native
	// "driver writes directly into the described pixmap memory" mechanism
	// looked broken. That was a symptom of the real bug, not proof this path
	// is bad. Now that rendering actually happens on the correct thread with
	// a correct viewport, trust the vendor's native pixmap-surface write
	// mechanism again and just wait for completion - glReadPixels may have
	// been misinterpreting this GPU's internal tiled/compressed render
	// target layout as plain linear memory, which would explain banding.
	glFinish();
}

void DreamboxWindowProvider::cleanup() {
	// Nothing to release: the framebuffer memory is owned by fbClass, not us.
}
