#include <lib/base/eerror.h>
#include <lib/gdi/egl/platform/dreambox/dreambox_window_provider.h>
#include <lib/gdi/fb.h>

#ifdef HAVE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#endif

// drm/drm_fourcc.h's DRM_FORMAT_ARGB8888 - matches what gTextureManager (see
// gtexture_manager.cpp) already assumes for enigma2's own 32bpp surfaces.
#define DRM_FORMAT_ARGB8888 0x34325241

DreamboxWindowProvider::DreamboxWindowProvider() {
	memset(&m_pixmap, 0, sizeof(m_pixmap));
}

DreamboxWindowProvider::~DreamboxWindowProvider() {
	cleanup();
}

bool DreamboxWindowProvider::init(int width, int height) {
	fbClass* fb = fbClass::getInstance();
	if (!fb) {
		eDebug("[DreamboxWindowProvider] no fbClass instance available");
		return false;
	}

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
	m_pixmap.format = DRM_FORMAT_ARGB8888;

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
	// The pixmap already IS the live display memory, so there's nothing to
	// copy/composite - just make sure the GPU has actually finished writing
	// into it before the next frame's commands (or the display scanout)
	// might race it.
	glFinish();
}

void DreamboxWindowProvider::cleanup() {
	// Nothing to release: the framebuffer memory is owned by fbClass, not us.
}
