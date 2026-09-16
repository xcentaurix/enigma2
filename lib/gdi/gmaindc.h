#ifndef __lib_gdi_gmaindc_h
#define __lib_gdi_gmaindc_h

#include "grc.h"

class gMainDC;

SWIG_IGNORE(gMainDC);
class gMainDC: public gDC
{
protected:
	static gMainDC *m_instance;

	gMainDC();
	gMainDC(gPixmap *pixmap);
	virtual ~gMainDC();
public:
	virtual void setResolution(int xres, int yres, int bpp = 32) = 0;

	// True for every real display backend (gEGLDC, gFBDC, gSDLDC): this
	// gMainDC has a local, drawable pixmap, and main/enigma.cpp's startup
	// should set up the local CPU busy-spinner (gDC::setSpinner()) against
	// it as usual. False only for gCompositorProxyDC (lib/gdi/ipc/
	// gcompositorproxydc.h, only ever built under HAVE_E3_COMPD_CLIENT -
	// not defined by any build file yet, see that class's own comment):
	// it has no local pixmap at all, and setSpinner()'s real
	// implementation asserts on one.
	virtual bool needsSpinnerSetup() const { return true; }
#ifndef SWIG
	static int getInstance(ePtr<gMainDC> &ptr) { if (!m_instance) return -1; ptr = m_instance; return 0; }
#endif
};

SWIG_TEMPLATE_TYPEDEF(ePtr<gMainDC>, gMainDC);
SWIG_EXTEND(ePtr<gMainDC>,
	static ePtr<gMainDC> getInstance()
	{
		 extern ePtr<gMainDC> NewgMainDCPtr(void);
		 return NewgMainDCPtr();
	}
);

#endif
