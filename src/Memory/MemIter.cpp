#include "Memory\MemIter.h"
#include "Utilities\Utils.h"
#include <functional>


MemIter* g_pMemIter = new MemIter();


/*
	Set up the iterator by passing portable functions
*/
BOOLEAN MemIter::OnSetup(std::function<BOOLEAN(PVOID, PVOID, ULONG, PVOID)> Callback, std::function<BOOLEAN(uint64_t, DWORD, LPVOID)> ReadPhysicalAddress)
{
	if (!Callback || !ReadPhysicalAddress)
		return false;
	if (!g_pFetch->SFSetup())
		return false;
	if (!g_pFetch->SFGetMemoryInfo(m_MemInfo, m_InfoCount))
		return false;

	this->Callback = Callback;
	this->ReadPhysicalAddress = ReadPhysicalAddress;
	return true;
}


MemIter::~MemIter()
{
}


BOOLEAN MemIter::isInRam(uint64_t address, uint32_t len)
{
	for (int j = 0; j < m_InfoCount; j++)
		if ((m_MemInfo[j].Start <= address) && ((address + len) <= m_MemInfo[j].End))
			return true;
	return false;
}

/*
	Iterate physical memory, scan for pooltag
*/
BOOLEAN MemIter::IterateMemory(const char* Pooltag, PVOID Context)
{
	BOOLEAN bFound = FALSE;
	POOL_HEADER PoolHeader{ 0 };
	uint32_t tag = (
		Pooltag[0] |
		Pooltag[1] << 8 |
		Pooltag[2] << 16 |
		Pooltag[3] << 24
		);

	for (auto i = 0ULL; i < m_MemInfo[m_InfoCount - 1].End; i += 0x1000)
	{
		if (!isInRam(i, 0x1000UL))
			continue;

		uint8_t* lpCursor = (uint8_t*)i;
		uint32_t previousSize = 0;

		while (true)
		{
			if (!ReadPhysicalAddress((uint64_t)lpCursor, sizeof(POOL_HEADER), &PoolHeader))
				return 0;

			auto blockSize = (PoolHeader.BlockSize << 4);
			auto previousBlockSize = (PoolHeader.PreviousSize << 4);

			if (previousBlockSize != previousSize ||
				blockSize == 0 ||
				blockSize >= 0xFFF ||
				!g_pUtils->isPrintable(PoolHeader.PoolTag & 0x7FFFFFFF))
				break;

			previousSize = blockSize;

			if (tag == PoolHeader.PoolTag & 0x7FFFFFFF)
			{
				PVOID block = VirtualAlloc(nullptr, blockSize, MEM_COMMIT, PAGE_READWRITE);	// Alloc mem for whole block
				if (!block)
					break;

				if (!ReadPhysicalAddress((uint64_t)lpCursor, blockSize, block))		// Read whole block
				{
					if (block)
						VirtualFree(block, 0, MEM_RELEASE);
					break;
				}

				bFound = Callback(block, lpCursor, blockSize, Context);	// Callback, passes alloced block and physical address to block and size of block

				if (block)
					VirtualFree(block, 0, MEM_RELEASE);
				break;
			}

			lpCursor += blockSize;
			if (((uint64_t)lpCursor - i) >= 0x1000)
				break;
		}

		if (bFound)
			break;
	}

	return bFound;
}
