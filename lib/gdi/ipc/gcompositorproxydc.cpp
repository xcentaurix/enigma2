#ifdef HAVE_E3_COMPD_CLIENT
// Not defined by any build file yet - see gcompositorproxydc.h's own
// comment for what this guards and why (mutual exclusion with any real
// gMainDC backend, not yet enforced at configure time).

#include <lib/gdi/ipc/gcompositorproxydc.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>

// Same AutoInit priority gEGLDCAutoInit uses (egl_init.cpp) - one slot
// before gRC (eAutoInitNumbers::graphic), so this exists before gRC's own
// AutoInit runs, mirroring the real backend's ordering exactly even
// though gRC's constructor doesn't itself touch gMainDC::getInstance().
eAutoInitPtr<gCompositorProxyDC> init_gCompositorProxyDC(eAutoInitNumbers::graphic - 1, "gCompositorProxyDC");

#endif // HAVE_E3_COMPD_CLIENT
