#pragma once
#include <windows.h>
#include "../PoolGuardCore.h"
//credits to the creators of https://github.com/MapleStory-Archive/MapleClientEditTemplate
class ZAllocBase
{
public:
	enum BLOCK_SIZE
	{
		BLOCK16 = 0,
		BLOCK32 = 1,
		BLOCK64 = 2,
		BLOCK128 = 3,
	};

	// PoolGuard markers, shared with the native pools (see PoolGuardCore.h).
	static void StampFree(void* block, const void* freedBy)
	{
		auto* words = reinterpret_cast<DWORD*>(block);
		words[1] = PoolGuard::Canary(block);
		words[2] = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(freedBy));
		words[3] = PoolGuard::NextFreeSequence();
		PoolGuard::MarkFree(block);
	}

	static bool StampFreeChain(PVOID* head, UINT expectedSize)
	{
		__try
		{
			for (unsigned i = 0; head && i < 65536; ++i, head = reinterpret_cast<PVOID*>(*head))
			{
				auto* words = reinterpret_cast<DWORD*>(head);
				DWORD header = words[-1];
				if (static_cast<LONG>(header) < 0) header = ~header;
				if (header != expectedSize || *head == head) return false;
				words[1] = PoolGuard::Canary(head);
				words[2] = 0;
				words[3] = 0;
			}
			return head == nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Checks the freelist head and its successor. Returns the corrupt block, if any.
	static const void* CheckFreeHead(PVOID* head, UINT uBlockSize, PoolGuard::Fault* fault)
	{
		PVOID* block = head;
		__try
		{
			for (int i = 0; i < 2 && block; ++i, block = reinterpret_cast<PVOID*>(*block))
			{
				const auto* words = reinterpret_cast<const DWORD*>(block);
				DWORD header = words[-1];
				if (static_cast<LONG>(header) < 0) header = ~header;
				if (header != uBlockSize) { *fault = PoolGuard::Fault::HeaderMismatch; return block; }
				if (*block == block || (i && *block == head)) { *fault = PoolGuard::Fault::FreeListLinkBad; return block; }
				if (words[1] != PoolGuard::Canary(block)) { *fault = PoolGuard::Fault::FreedBlockWritten; return block; }
			}
			return nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*fault = PoolGuard::Fault::FreeListLinkBad;
			return head;
		}
	}

	// Invoked once per replica bucket after opt-in. Pre-install allocations may
	// have left unstamped free blocks; prepare them before enabling validation.
	static const void* PrepareGuardHead(LPVOID& head, UINT size, bool& ready, PoolGuard::Fault* fault)
	{
		if (!ready)
		{
			ready = true;
			if (!StampFreeChain(reinterpret_cast<PVOID*>(head), size))
			{
				*fault = PoolGuard::Fault::FreeListLinkBad;
				return head;
			}
		}
		return CheckFreeHead(reinterpret_cast<PVOID*>(head), size, fault);
	}

	static PVOID* AllocRawBlocks(UINT uBlockSize, UINT uNumberOfBlocks)
	{
		/* TODO make this more legible */

		unsigned int uEnlargedBlockSize = uBlockSize + 4;
		unsigned int uTotalAllocationSize = (uNumberOfBlocks * uEnlargedBlockSize) + 8;

		HANDLE hHeap = GetProcessHeap();
		PVOID* pAlloc = reinterpret_cast<PVOID*>(HeapAlloc(hHeap, NULL, uTotalAllocationSize));

		/* if we deallocate the entire block collection, this first address is where we start */
		*(pAlloc) = reinterpret_cast<PVOID>((uNumberOfBlocks * uEnlargedBlockSize) + 4);

		/* block collection header */
		*(pAlloc + 1) = 0;

		/* size of first block */
		*(pAlloc + 2) = reinterpret_cast<PVOID>(uBlockSize);

		PVOID* pRet = pAlloc + 3;
		DWORD* pdwRet = (DWORD*)(pAlloc + 3);

		for (UINT i = 0; i < uNumberOfBlocks - 1; i++)
		{
			/* initialize each block with a pointer to the next block */
			*pRet = reinterpret_cast<PCHAR>(pRet) + uEnlargedBlockSize;

			/* increase pointer by block size (we divide because the compiler tries to multiply) */
			pRet = reinterpret_cast<PVOID*>(reinterpret_cast<PCHAR>(pRet) + uEnlargedBlockSize);

			/* set the preceding address to equal the size of the block */
			*(pRet - 1) = reinterpret_cast<PVOID>(uBlockSize);
		}

		/* nullptr indicates last block in the linked list */
		*pRet = nullptr;

		/* return address of the first block in the linked list */
		return pAlloc + 3;
	}
};
