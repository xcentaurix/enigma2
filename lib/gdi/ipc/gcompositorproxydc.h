#pragma once

#ifdef HAVE_E3_COMPD_CLIENT
// Not defined by any build file yet - see this guard's other call sites
// (lib/gdi/grc.h, main/enigma.cpp). This file has no content at all
// outside it.
//
// UI-process stand-in for gMainDC when there is no local gEGLDC (or any
// other real display backend) to be the process's gMainDC singleton -
// gMainDC::getInstance() (SWIG-exposed, used by plugins for geometry
// queries like getSize()) still needs SOMETHING to return once the real
// backend has moved into the compositor process (see the Enigma3
// compositor-split plan). This class supplies just enough to satisfy that
// singleton and gDC::size() - it never actually draws anything: every
// real opcode either goes to gRC's forwarding path (see grc.cpp's
// submit()) before ever reaching a gDC::exec() call on this object, or
// (for whatever this transport doesn't support yet) is dropped there,
// never here.
//
// Deliberately has NO gPixmap (m_pixmap stays null, via gMainDC's
// no-pixmap constructor): gDC::setSpinner() would assert on a non-null
// pixmap+surface to size its CPU save/restore buffers, so this class
// overrides needsSpinnerSetup() (gmaindc.h) to false rather than faking a
// pixmap solely to satisfy that assert, for a feature (the local CPU
// spinner) that doesn't obviously belong in the UI process anymore once
// real rendering happens in e3-compd - main/enigma.cpp's startup checks
// that flag and skips setSpinner()/setSpinnerDC() accordingly.
//
// gMainDC is a strict singleton (ASSERT(m_instance == 0) in its own
// constructor, see gmaindc.cpp) - exactly one gMainDC-constructing
// translation unit may be linked into a given binary. This class's own
// AutoInit registration (gcompositorproxydc.cpp) is therefore ALSO
// wrapped in this same HAVE_E3_COMPD_CLIENT guard, and is only meant to
// be enabled for a build that does NOT also define HAVE_EGL (or link
// gfbdc.cpp/sdl.cpp) - there is no build wiring yet that enforces that
// mutual exclusion at configure time (unlike the existing HAVE_EGL vs
// HAVE_LIBSDL vs plain-fbdev exclusion in lib/gdi/Makefile.inc, which IS
// enforced there). Defining HAVE_E3_COMPD_CLIENT for a build that also
// constructs a real gMainDC will fail loudly at startup via that
// ASSERT - a safe failure mode, not silent misbehavior, but real support
// for a UI build with no local display backend at all needs its own
// configure.ac option; out of scope for this pass.

#include <lib/gdi/gmaindc.h>
#include <lib/gdi/esize.h>

class gCompositorProxyDC : public gMainDC
{
public:
	gCompositorProxyDC() = default;

	void setResolution(int xres, int yres, int bpp = 32) override
	{
		m_size = eSize(xres, yres);
	}

	// Defaults to an invalid size (see eSize's own default constructor)
	// until setResolution() is called - nothing calls it yet. In the real
	// in-process backends, resolution comes from the actual display
	// hardware/config at their own AutoInit time; a compositor-process
	// build needs an equivalent (most naturally, e3-compd reporting its
	// real display's geometry back over the connect handshake gRC::
	// connectToCompositor() doesn't have yet). Until that exists, don't
	// rely on this class's size() being meaningful.
	eSize size() override { return m_size; }

	bool needsSpinnerSetup() const override { return false; }

private:
	eSize m_size;
};

#endif // HAVE_E3_COMPD_CLIENT
