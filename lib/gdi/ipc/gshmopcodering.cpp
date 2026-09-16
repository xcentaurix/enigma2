#include "gshmopcodering.h"

#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace e3ipc
{

static size_t alignUp(size_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

gShmOpcodeRing::~gShmOpcodeRing()
{
	close();
}

size_t gShmOpcodeRing::segmentSize(uint32_t slotCount, uint32_t arenaBytes)
{
	size_t headerPart = alignUp(sizeof(Header), alignof(WireOpcode));
	return headerPart + size_t(slotCount) * sizeof(WireOpcode) + arenaBytes;
}

bool gShmOpcodeRing::create(const char *name, uint32_t slotCount, uint32_t arenaBytes)
{
	close();
	if (!name || !*name || slotCount < 2 || arenaBytes < 2)
		return false;

	int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0660);
	if (fd < 0)
		// Most likely EEXIST. A segment left behind by a crashed prior
		// instance must be shm_unlink()'d by the caller (the process
		// supervisor, once one exists) before retrying - its bookkeeping
		// could be mid-update if whichever side held the mutex died,
		// so silently reusing it here would be unsafe.
		return false;

	m_mappingSize = segmentSize(slotCount, arenaBytes);
	if (ftruncate(fd, static_cast<off_t>(m_mappingSize)) != 0)
	{
		::close(fd);
		shm_unlink(name);
		return false;
	}

	void *mapping = mmap(nullptr, m_mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	::close(fd); // the mapping keeps the segment alive; the fd itself isn't needed past this point
	if (mapping == MAP_FAILED)
	{
		shm_unlink(name);
		return false;
	}

	m_mapping = mapping;
	m_owner = true;
	std::strncpy(m_name, name, sizeof(m_name) - 1);

	uint8_t *base = static_cast<uint8_t *>(mapping);
	m_header = new (base) Header(); // placement-new: every field gets its in-class default before anything below touches it
	m_header->slotCount = slotCount;
	m_header->arenaSize = arenaBytes;
	m_header->rp = m_header->wp = 0;
	m_header->arenaRp = m_header->arenaWp = 0;
	m_header->shuttingDown = false;

	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&m_header->mutex, &mattr);
	pthread_mutexattr_destroy(&mattr);

	pthread_condattr_t cattr;
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
#if defined(CLOCK_MONOTONIC)
	// Best-effort: pop()'s timed wait uses CLOCK_MONOTONIC if this
	// succeeds (checked again there isn't necessary - a failed
	// setclock() here just leaves the cond on the default REALTIME
	// clock, which still works, only less robustly against wall-clock
	// jumps).
	pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
#endif
	pthread_cond_init(&m_header->notEmpty, &cattr);
	pthread_condattr_destroy(&cattr);

	size_t headerPart = alignUp(sizeof(Header), alignof(WireOpcode));
	m_slots = reinterpret_cast<WireOpcode *>(base + headerPart);
	for (uint32_t i = 0; i < slotCount; ++i)
		new (&m_slots[i]) WireOpcode();
	m_arena = base + headerPart + size_t(slotCount) * sizeof(WireOpcode);

	return true;
}

bool gShmOpcodeRing::mapExisting(const char *name)
{
	int fd = shm_open(name, O_RDWR, 0);
	if (fd < 0)
		return false;

	struct stat st
	{
	};
	if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Header)))
	{
		::close(fd);
		return false;
	}

	void *mapping = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	::close(fd);
	if (mapping == MAP_FAILED)
		return false;

	Header *header = static_cast<Header *>(mapping);

	// Confirm the segment is exactly the size create() would have made
	// it for the slotCount/arenaSize the header itself claims, before
	// trusting those fields to compute m_slots/m_arena - guards against
	// attaching to some unrelated segment that happened to exist under
	// the same name with garbage in its first bytes.
	if (segmentSize(header->slotCount, header->arenaSize) != static_cast<size_t>(st.st_size))
	{
		munmap(mapping, static_cast<size_t>(st.st_size));
		return false;
	}

	m_mapping = mapping;
	m_mappingSize = static_cast<size_t>(st.st_size);
	m_header = header;

	uint8_t *base = static_cast<uint8_t *>(mapping);
	size_t headerPart = alignUp(sizeof(Header), alignof(WireOpcode));
	m_slots = reinterpret_cast<WireOpcode *>(base + headerPart);
	m_arena = base + headerPart + size_t(header->slotCount) * sizeof(WireOpcode);
	m_owner = false;
	std::strncpy(m_name, name, sizeof(m_name) - 1);
	return true;
}

bool gShmOpcodeRing::attach(const char *name)
{
	close();
	if (!name || !*name)
		return false;
	// Callers must only attach() after the compositor side's create()
	// has completed (via whatever connect handshake sequences the two -
	// not part of this primitive) - mapExisting() only validates the
	// segment's size, not that its header fields have been fully
	// initialized yet.
	return mapExisting(name);
}

void gShmOpcodeRing::close()
{
	if (m_mapping)
	{
		if (m_owner && m_header)
		{
			// Wake anything still blocked in pop()/push() before tearing
			// down the primitives it might be waiting on - best-effort,
			// since a peer that's slow to notice still risks touching a
			// destroyed mutex/cond; full lifecycle coordination belongs
			// in the higher-level connect/reconnect handshake, not here.
			requestShutdown();
			pthread_cond_destroy(&m_header->notEmpty);
			pthread_mutex_destroy(&m_header->mutex);
		}
		munmap(m_mapping, m_mappingSize);
		if (m_owner)
			shm_unlink(m_name);
	}
	m_mapping = nullptr;
	m_header = nullptr;
	m_slots = nullptr;
	m_arena = nullptr;
	m_mappingSize = 0;
	m_owner = false;
	m_name[0] = '\0';
	m_pendingRelease = WireBytes();
}

bool gShmOpcodeRing::push(WireOpcode op, const void *payload, uint32_t payloadLen)
{
	if (!isOpen())
		return false;
	if (payloadLen >= m_header->arenaSize)
		// Can never fit - the arena always keeps at least one byte free
		// to disambiguate full from empty, so nothing this large would
		// ever fit even in a completely empty arena. Fail fast instead
		// of retrying forever below.
		return false;

	const int kMaxRetries = 5000; // ~5s at 1ms/retry - matches gRC::submit()'s existing full-queue backoff (grc.cpp)
	for (int attempt = 0;; ++attempt)
	{
		pthread_mutex_lock(&m_header->mutex);

		if (m_header->shuttingDown)
		{
			pthread_mutex_unlock(&m_header->mutex);
			return false;
		}

		uint32_t nextWp = (m_header->wp + 1) % m_header->slotCount;
		bool slotsFull = (nextWp == m_header->rp);

		uint32_t arenaOffset = 0;
		bool arenaFull = false;
		if (payloadLen > 0)
		{
			uint32_t used = (m_header->arenaWp >= m_header->arenaRp)
				? (m_header->arenaWp - m_header->arenaRp)
				: (m_header->arenaSize - m_header->arenaRp + m_header->arenaWp);
			uint32_t freeBytes = m_header->arenaSize - used - 1; // 1 byte held back, see the guard above

			// Every payload's offset is a multiple of 8, relative to the
			// arena's own base: callers reinterpret arena bytes directly
			// as arrays of POD structs (eRect, gRGB - see
			// gopcodeserialize.cpp/e3-compd.cpp) rather than memcpy-ing
			// them out first, which is only well-defined (and on
			// strict-alignment targets, only non-crashing at all) if
			// each array's first element lands on a suitably aligned
			// address. The arena's base itself is only guaranteed
			// 4-byte aligned (it sits after a WireOpcode[] whose element
			// size/alignment are both derived from int/uint32_t-sized
			// fields - see gwireopcode.h), so this guarantees true
			// 4-byte absolute alignment, not 8 - which is what every
			// payload element type here (eRect, gRGB, plain bytes)
			// actually needs. Aligning to 8 rather than 4 here is just
			// headroom for a future element type that needs more.
			const uint32_t kAlign = 8;
			uint32_t alignedWp = (m_header->arenaWp + (kAlign - 1)) & ~(kAlign - 1);
			if (alignedWp >= m_header->arenaSize)
				alignedWp = 0;

			// Never split a payload across the arena's physical end -
			// arenaPtr() hands the consumer a single contiguous pointer,
			// so a wrapped payload would corrupt whatever reads past the
			// physical end. If it doesn't fit before the end (after
			// alignment), waste the remaining tail and restart the
			// candidate at offset 0 instead (already aligned, being a
			// multiple of everything); see the class comment for why
			// this - and the alignment padding above - stays correct
			// bookkeeping-wise regardless (the reclaim path in pop()
			// retraces the exact same jumps, since it always reclaims up
			// to exactly the aligned offset this call commits to below).
			uint32_t tailSpace = m_header->arenaSize - alignedWp;
			uint32_t candidateOffset = alignedWp;
			uint32_t candidateWaste = alignedWp - m_header->arenaWp; // bytes skipped for alignment, always counted as "used" until reclaimed
			if (payloadLen > tailSpace)
			{
				candidateWaste = m_header->arenaSize - m_header->arenaWp;
				candidateOffset = 0;
			}

			if (payloadLen + candidateWaste > freeBytes)
				arenaFull = true;
			else
				arenaOffset = candidateOffset;
		}

		if (!slotsFull && !arenaFull)
		{
			if (payloadLen > 0)
			{
				std::memcpy(m_arena + arenaOffset, payload, payloadLen);
				m_header->arenaWp = (arenaOffset + payloadLen) % m_header->arenaSize;
				op.bytesA.offset = arenaOffset;
				op.bytesA.length = payloadLen;
			}

			m_slots[m_header->wp] = op;
			m_header->wp = nextWp;
			pthread_cond_signal(&m_header->notEmpty);
			pthread_mutex_unlock(&m_header->mutex);
			return true;
		}

		pthread_mutex_unlock(&m_header->mutex);
		if (attempt >= kMaxRetries)
			return false;
		usleep(1000);
	}
}

bool gShmOpcodeRing::pop(WireOpcode &outOp, int timeoutMs)
{
	if (!isOpen())
		return false;

	pthread_mutex_lock(&m_header->mutex);

	// Reclaim the previous pop()'s payload bytes now that the caller is
	// asking for the next opcode - see the class comment's lifetime
	// contract. A direct assignment (not an increment), so this stays
	// correct even if m_pendingRelease were ever inspected twice.
	if (m_pendingRelease.length > 0)
	{
		m_header->arenaRp = (m_pendingRelease.offset + m_pendingRelease.length) % m_header->arenaSize;
		m_pendingRelease = WireBytes();
	}

	struct timespec deadline
	{
	};
	if (timeoutMs > 0)
	{
#if defined(CLOCK_MONOTONIC)
		clock_gettime(CLOCK_MONOTONIC, &deadline);
#else
		clock_gettime(CLOCK_REALTIME, &deadline);
#endif
		deadline.tv_sec += timeoutMs / 1000;
		deadline.tv_nsec += (long(timeoutMs) % 1000) * 1000000L;
		if (deadline.tv_nsec >= 1000000000L)
		{
			deadline.tv_sec += 1;
			deadline.tv_nsec -= 1000000000L;
		}
	}

	while (m_header->rp == m_header->wp && !m_header->shuttingDown)
	{
		if (timeoutMs == 0)
		{
			pthread_cond_wait(&m_header->notEmpty, &m_header->mutex);
		}
		else
		{
			int rc = pthread_cond_timedwait(&m_header->notEmpty, &m_header->mutex, &deadline);
			if (rc != 0) // ETIMEDOUT, or any other error - stop waiting either way
			{
				pthread_mutex_unlock(&m_header->mutex);
				return false;
			}
		}
	}

	if (m_header->rp == m_header->wp) // woke up for shutdown, nothing was actually queued
	{
		pthread_mutex_unlock(&m_header->mutex);
		return false;
	}

	outOp = m_slots[m_header->rp];
	m_header->rp = (m_header->rp + 1) % m_header->slotCount;
	m_pendingRelease = outOp.bytesA;

	pthread_mutex_unlock(&m_header->mutex);
	return true;
}

const uint8_t *gShmOpcodeRing::arenaPtr(const WireBytes &ref) const
{
	if (!m_arena || ref.length == 0)
		return nullptr;
	return m_arena + ref.offset;
}

void gShmOpcodeRing::requestShutdown()
{
	if (!isOpen())
		return;
	pthread_mutex_lock(&m_header->mutex);
	m_header->shuttingDown = true;
	pthread_cond_broadcast(&m_header->notEmpty);
	pthread_mutex_unlock(&m_header->mutex);
}

} // namespace e3ipc
