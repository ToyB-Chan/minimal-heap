#define MIHP_IMPLEMENTATION 1
#include "minimal_heap.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define USE_MALLOC 0

//#define MIHP_ValidateHeap()

void* MIHP_PlatformRequestMemory(size_t size)
{
	return malloc(size);
}

bool MIHP_PlatformFreeMemory(void* ptr, size_t size)
{
	free(ptr);
	return true;
}

void MIHP_PlatformOnHeapCorruptionDetected(MIHP_HeapCorruptionInfo info)
{
	__debugbreak();
}

#define NUM_ALLOCATIONS (4096 * 4)

void* myptrs[NUM_ALLOCATIONS] = { 0 };

int main()
{
	MIHP_HeapConfig config = {0};
	config.AllocationInitialValue = 0xbe;
	config.AllocationAlignment = 16;
	config.MinimalMemoryAreaSize = 4096 * 32;
	config.MaximalMemoryAreaSize = 4096ull * 4096ull * 4096ull;
	MIHP_HeapInfo* myHeap = MIHP_CreateHeap(config);

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		size_t size = 473 + (i % 5 == 0 ? 854 : 0);
#if USE_MALLOC
		myptrs[i] = malloc(size);
#else
		myptrs[i] = MIHP_Allocate(myHeap, size);
		MIHP_ValidateHeap(myHeap);
#endif
	}

	__debugbreak();

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		if (rand() % 3 == 0)
		{
#if USE_MALLOC
			free(myptrs[i]);
#else
			MIHP_Free(myHeap, myptrs[i]);
			MIHP_ValidateHeap(myHeap);
#endif

			myptrs[i] = NULL;
		}
	}

	__debugbreak();

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		volatile int n = i;
		if (myptrs[i])
		{
			size_t size = 300 + rand() % 500;

#if USE_MALLOC
			myptrs[i] = realloc(myptrs[i], size);
#else
			myptrs[i] = MIHP_Realloc(myHeap, myptrs[i], size);
			MIHP_ValidateHeap(myHeap);

			
#endif
		}
	}

	__debugbreak();
	
	for (int z = 15; z >= 0; z--)
		for (int y = 0; y < NUM_ALLOCATIONS / 16; y++)
		{
			int i = z + y * 16;
#if USE_MALLOC
			free(myptrs[i]);
#else
			MIHP_Free(myHeap, myptrs[i]);
			MIHP_ValidateHeap(myHeap);
#endif

			myptrs[i] = NULL;
		}


	__debugbreak();
}

