#include <lib/base/eerror.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/gdi/egl/egl_config.h>
#include <lib/gdi/egl/gegldc.h>
#include <lib/gdi/fb.h>

#ifdef DREAMNEXTGEN
#include <lib/gdi/egl/platform/amlogic/amlogic_window_provider.h>
#endif

#ifdef HAVE_DREAMBOX_EGL
#include <lib/gdi/egl/platform/dreambox/dreambox_window_provider.h>
#endif

class gEGLDCAutoInit : protected eAutoInit
{
	gEGLDC *m_dc;
	void initNow() override
	{
		if (egl_config::disable_egl)
		{
			eDebug("[gEGLDC] EGL disabled via command line");
			return;
		}

		INativeWindowProvider *provider = nullptr;

#ifdef DREAMNEXTGEN
		provider = new AmlogicWindowProvider();
#elif defined(HAVE_DREAMBOX_EGL)
		provider = new DreamboxWindowProvider();
#else
		// Fallback for other platforms (SDL/Wayland) once implemented
		// For now, if not HWDREAMONE/HAVE_DREAMBOX_EGL, we don't have a default provider here
		// unless we add SDLWindowProvider or WaylandWindowProvider detection.
#endif

		if (provider)
		{
			int xres = 1920, yres = 1080, bpp = 32;
			if (fbClass::getInstance())
				fbClass::getInstance()->getMode(xres, yres, bpp);

			if (!provider->init(xres, yres))
			{
				eDebug("[gEGLDC] window provider init failed, falling back...");
				delete provider;
				return;
			}

			eDebug("[eInit] + (%d) gEGLDC", rl);
			m_dc = new gEGLDC(provider, xres, yres);
			if (!m_dc->initEGL())
			{
				eDebug("[gEGLDC] initEGL failed, falling back...");
				delete m_dc;
				m_dc = nullptr;
			}
		}
	}

	void closeNow() override
	{
		if (m_dc)
		{
			delete m_dc;
			m_dc = nullptr;
		}
	}

public:
	gEGLDCAutoInit()
		: eAutoInit(eAutoInitNumbers::graphic - 2, "gEGLDC"), m_dc(nullptr)
	{
		eInit::add(rl, this);
	}

	~gEGLDCAutoInit()
	{
		eInit::remove(rl, this);
	}
};

static gEGLDCAutoInit init_gEGLDC_custom;
