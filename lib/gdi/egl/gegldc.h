#pragma once

#include <EGL/egl.h>
#ifdef HAVE_GLES3
#include <GLES3/gl3.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif
#include <lib/gdi/egl/gtexture_manager.h>
#include <lib/gdi/egl/inative_window_provider.h>
#include <lib/gdi/egl/shader/gadvanced_shader.h>
#include <lib/gdi/egl/shader/gshader.h>
#include <lib/gdi/egl/shader/gtext_shader.h>
#include <lib/gdi/egl/shader/gtexture_shader.h>
#include <lib/gdi/gfont_atlas.h>
#include <lib/gdi/gmaindc.h>

class gEGLDCAutoInit;

class gEGLDC : public gMainDC {
private:
	INativeWindowProvider* m_window_provider;
	EGLDisplay m_egl_display;
	EGLConfig m_egl_config;
	EGLSurface m_egl_surface;
	EGLContext m_egl_context;

	int m_width;
	int m_height;
	int m_gles_version; // 2 or 3
	GLint m_max_texture_size;

	gShader m_basic_shader;
	gAdvancedShader m_advanced_shader;
	gTextureShader m_texture_shader;
	gTextShader m_text_shader;

	gFontAtlas m_font_atlas;
	gTextureManager m_texture_manager;

	std::vector<float> m_text_batch_buffer;
	const size_t MAX_BATCH_GLYPHS = 1024;

	// Accumulated across every renderText/renderPara/clear opcode in the
	// current frame instead of uploading+compositing m_pixmap's text overlay
	// once per opcode (see presentTextOverlay()) - the union of every area
	// that touched m_pixmap's CPU buffer since the last present.
	eRect m_dirty_overlay_rect;
	bool m_overlay_dirty = false;

	bool tryInitEGL(int version);
	void cleanupEGL();

	// dedicated opcode handlers
	void executeFill(const gOpcode* op);
	void executeFillRegion(const gOpcode* op);
	void executeRectangle(const gOpcode* op);
	void executeLine(const gOpcode* op);
	void executeBlit(const gOpcode* op);
	void executeDrawGlyph(const gOpcode* op);
	void executeClear(const gOpcode* op);
	void flushTextBatch();
	void setGlScissor(const eRect& rect);

	// Shared by gOpcode::renderText and gOpcode::renderPara: after the
	// existing software eTextPara::blit() path (called via gDC::exec()) has
	// written glyphs into m_pixmap's CPU buffer, upload the affected area as
	// a texture and composite it onto the real GPU surface - m_pixmap has no
	// GPU hook of its own, so without this nothing drawn into it ever
	// reaches the display.
	void compositeTextOverlay(eRect area);

	// Uploads m_pixmap's dirty region (m_dirty_overlay_rect, accumulated by
	// compositeTextOverlay() and executeClear()'s stale-text erase) and
	// composites it onto the GPU surface exactly once per frame, right
	// before flip() - see the comment above compositeTextOverlay() in
	// gegldc.cpp for why deferring this to end-of-frame is safe.
	void presentTextOverlay();

	bool isHardwareAccelerated() const { return true; }
	void renderGlyph(const ePoint& pos, gPixmap* glyph_mask, const gRGB& color);

	static gEGLDC* s_instance;

public:
	// initEGL() performs eglMakeCurrent() and must be called from gRC's own
	// render thread (see gRC::thread() in grc.cpp), NOT from the thread that
	// constructs gEGLDC (eInit/gEGLDCAutoInit) - EGL contexts are per-thread,
	// and gRC::thread() is the only thread that ever issues GL draw calls.
	static gEGLDC* getInstance() { return s_instance; }

	bool initEGL();
	gEGLDC(INativeWindowProvider* window_provider = nullptr, int width = 1280, int height = 720);
	virtual ~gEGLDC();

	virtual void setResolution(int xres, int yres, int bpp = 32) override;
	virtual void exec(const gOpcode* opcode);

	void flip();
	bool isInitialized() const { return m_egl_context != EGL_NO_CONTEXT; }
	int getGLESVersion() const { return m_gles_version; }
};