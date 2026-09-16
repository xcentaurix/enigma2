#include "gshmopcodeclient.h"

namespace e3ipc
{

bool gShmOpcodeClient::connect(const char *shmName, WireDcId dc)
{
	m_dc = dc;
	return m_ring.attach(shmName);
}

bool gShmOpcodeClient::submit(const gOpcode &op)
{
	if (!m_ring.isOpen())
		return false;
	return serializeOpcode(op, m_dc, m_ring);
}

} // namespace e3ipc
