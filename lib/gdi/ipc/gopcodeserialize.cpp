#include "gopcodeserialize.h"

#include <cstring>

#include <lib/gdi/gpixmap.h>
#include <lib/gdi/grc.h>
#include <lib/gdi/region.h>

namespace e3ipc
{

namespace
{

// Shared by setClip/addClip/fillRegion: gRegion's rect list goes into the
// arena as a packed eRect[], with its bounding `extends` carried inline in
// rectB (see gwireopcode.h's field-reuse comments).
bool pushRegion(WireOpcode &wire, gShmOpcodeRing &ring, const gRegion &region)
{
	wire.rectB = region.extends;
	const void *payload = region.rects.empty() ? nullptr : region.rects.data();
	uint32_t payloadLen = static_cast<uint32_t>(region.rects.size() * sizeof(eRect));
	return ring.push(wire, payload, payloadLen);
}

} // namespace

bool serializeOpcode(const gOpcode &in, WireDcId dc, gShmOpcodeRing &ring)
{
	WireOpcode wire;
	wire.dc = dc;

	switch (in.opcode)
	{
	case gOpcode::fill:
		wire.op = WireOp::Fill;
		wire.rectA = in.parm.fill->area;
		return ring.push(wire, nullptr, 0);

	case gOpcode::fillRegion:
		wire.op = WireOp::FillRegion;
		return pushRegion(wire, ring, in.parm.fillRegion->region);

	case gOpcode::clear:
		wire.op = WireOp::Clear;
		return ring.push(wire, nullptr, 0);

	case gOpcode::blit:
	{
		const gPixmap *pixmap = in.parm.blit->pixmap;
		if (!pixmap || !pixmap->surface)
			return false;
		if (pixmap->surface->data_phys != 0)
			// Accel/ION-backed - needs its dma-buf fd passed over the
			// control socket and imported via the existing
			// createTextureFromDmabuf() path (gtexture_manager.cpp)
			// instead of an inline byte copy. Not yet implemented - see
			// gwireopcode.h's file comment.
			return false;

		wire.op = WireOp::Blit;
		wire.rectA = in.parm.blit->position;
		wire.rectB = in.parm.blit->clip;
		wire.flags = in.parm.blit->flags;
		wire.width = pixmap->surface->x;
		wire.height = pixmap->surface->y;
		wire.stride = pixmap->surface->stride;
		wire.bpp = pixmap->surface->bpp;

		uint32_t payloadLen = static_cast<uint32_t>(pixmap->surface->stride * pixmap->surface->y);
		return ring.push(wire, pixmap->surface->data, payloadLen);
	}

	case gOpcode::rectangle:
		wire.op = WireOp::Rectangle;
		wire.rectA = in.parm.rectangle->area;
		wire.intA = in.parm.rectangle->useNew ? 1 : 0;
		return ring.push(wire, nullptr, 0);

	case gOpcode::line:
		wire.op = WireOp::Line;
		wire.pointA = in.parm.line->start;
		wire.pointB = in.parm.line->end;
		return ring.push(wire, nullptr, 0);

	case gOpcode::renderText:
	{
		auto *rt = in.parm.renderText;
		if (rt->offset)
			// Out-parameter caller - see gwireopcode.h's file comment.
			return false;
		wire.op = WireOp::RenderText;
		wire.rectA = rt->area;
		wire.flags = rt->flags;
		wire.intA = rt->border;
		wire.colorRGB = rt->bordercolor;
		wire.intB = rt->markedpos;
		wire.intC = rt->scrollpos;
		const char *text = rt->text ? rt->text : "";
		return ring.push(wire, text, static_cast<uint32_t>(strlen(text)));
	}

	case gOpcode::setBackgroundColor:
		wire.op = WireOp::SetBackgroundColor;
		wire.colorIdx = in.parm.setColor->color;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setForegroundColor:
		wire.op = WireOp::SetForegroundColor;
		wire.colorIdx = in.parm.setColor->color;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setBackgroundColorRGB:
		wire.op = WireOp::SetBackgroundColorRGB;
		wire.colorRGB = in.parm.setColorRGB->color;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setForegroundColorRGB:
		wire.op = WireOp::SetForegroundColorRGB;
		wire.colorRGB = in.parm.setColorRGB->color;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setGradient:
	{
		wire.op = WireOp::SetGradient;
		wire.orientation = in.parm.gradient->orientation;
		wire.alphablend = in.parm.gradient->alphablend;
		wire.fullSize = in.parm.gradient->fullSize;
		const std::vector<gRGB> &colors = in.parm.gradient->colors;
		const void *payload = colors.empty() ? nullptr : colors.data();
		uint32_t payloadLen = static_cast<uint32_t>(colors.size() * sizeof(gRGB));
		return ring.push(wire, payload, payloadLen);
	}

	case gOpcode::setRadius:
		wire.op = WireOp::SetRadius;
		wire.intA = in.parm.radius->radius;
		wire.edges = in.parm.radius->edges;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setBorder:
		wire.op = WireOp::SetBorder;
		wire.colorRGB = in.parm.border->color;
		wire.intA = in.parm.border->width;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setOffset:
		wire.op = WireOp::SetOffset;
		wire.pointA = in.parm.setOffset->value;
		wire.intA = in.parm.setOffset->rel;
		return ring.push(wire, nullptr, 0);

	case gOpcode::setClip:
		wire.op = WireOp::SetClip;
		return pushRegion(wire, ring, in.parm.clip->region);

	case gOpcode::addClip:
		wire.op = WireOp::AddClip;
		return pushRegion(wire, ring, in.parm.clip->region);

	case gOpcode::popClip:
		wire.op = WireOp::PopClip;
		return ring.push(wire, nullptr, 0);

	case gOpcode::flush:
		wire.op = WireOp::Flush;
		return ring.push(wire, nullptr, 0);

	case gOpcode::waitVSync:
		wire.op = WireOp::WaitVSync;
		return ring.push(wire, nullptr, 0);

	case gOpcode::flip:
		wire.op = WireOp::Flip;
		return ring.push(wire, nullptr, 0);

	case gOpcode::notify:
		wire.op = WireOp::Notify;
		return ring.push(wire, nullptr, 0);

	case gOpcode::enableSpinner:
		wire.op = WireOp::EnableSpinner;
		return ring.push(wire, nullptr, 0);

	case gOpcode::disableSpinner:
		wire.op = WireOp::DisableSpinner;
		return ring.push(wire, nullptr, 0);

	case gOpcode::incrementSpinner:
		wire.op = WireOp::IncrementSpinner;
		return ring.push(wire, nullptr, 0);

	case gOpcode::sendShow:
		wire.op = WireOp::SendShow;
		wire.pointA = in.parm.setShowHideInfo->point;
		wire.sizeA = in.parm.setShowHideInfo->size;
		return ring.push(wire, nullptr, 0);

	case gOpcode::sendHide:
		wire.op = WireOp::SendHide;
		wire.pointA = in.parm.setShowHideInfo->point;
		wire.sizeA = in.parm.setShowHideInfo->size;
		return ring.push(wire, nullptr, 0);

	case gOpcode::shutdown:
		wire.op = WireOp::Shutdown;
		return ring.push(wire, nullptr, 0);

	default:
		// renderPara, setCompositing, setPalette, mergePalette, and (under
		// USE_LIBVUGLES2) sendShowItem/setFlush/setView - see
		// gwireopcode.h's file comment for why each is excluded.
		return false;
	}
}

} // namespace e3ipc
