#include "minimal_heap.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

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

#define NUM_ALLOCATIONS (4096 * 1)

void* myptrs[NUM_ALLOCATIONS] = { 0 };

int main()
{
	MIHP_HeapConfig config = {0};
	config.AllocationAlignment = 128;
	config.MinimalMemoryAreaSize = 4096 * 32;
	config.MaximalMemoryAreaSize = 4096 * 4096;
	MIHP_HeapInfo* myHeap = MIHP_CreateHeap(config);

	{
		clock_t start = clock();

		for (int i = 0; i < NUM_ALLOCATIONS; i++)
		{
			size_t size = 473 + (i % 5 == 0 ? 854 : 0);
			//myptrs[i] = malloc(size);
			myptrs[i] = MIHP_Allocate(myHeap, size);
			MIHP_ValidateHeap(myHeap);
		}

		clock_t end = clock();
		printf("Allocating took %f seconds\n", ((double)(end - start)) / CLOCKS_PER_SEC);
	}


	__debugbreak();

	{
		clock_t start = clock();

		for (int z = 15; z >= 0; z--)
		{
			for (int y = 0; y < NUM_ALLOCATIONS / 16; y++)
			{
				int i = z + y * 16;

				//free(myptrs[i]);
				MIHP_Free(myHeap, myptrs[i]);
				MIHP_ValidateHeap(myHeap);
				myptrs[i] = NULL;
			}
		}

		clock_t end = clock();
		printf("Freeing took %f seconds\n", ((double)(end - start)) / CLOCKS_PER_SEC);
	}


	__debugbreak();
}

