#define MIHP_IMPLEMENTATION 1
#include "minimal_heap.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#include <intrin.h>
#include <string.h>

#define USE_MALLOC 0

#define MIHP_ValidateHeap(x)

void* RequestMemory(MIHP_HeapOpaque heap, size_t minRequestedSize, size_t* outActualSize)
{
	*outActualSize = minRequestedSize;
	return malloc(minRequestedSize);
}

bool FreeMemory(MIHP_HeapOpaque heap, void* ptr, size_t actualSize)
{
	free(ptr);
	return true;
}

void OnHeapCorruptionDetectedCallback(const MIHP_Heap* heap, MIHP_HeapCorruptionInfo info)
{
	__debugbreak();
}

#define NUM_ALLOCATIONS (4096 * 4096)

void* myptrs[NUM_ALLOCATIONS] = { 0 };

void* GetThisAddress()
{
	return _ReturnAddress();
}

int main()
{
	MIHP_HeapConfig config = MIHP_MakeDefaultConfigPreset();

	config.PlatformRequestMemoryFn = RequestMemory;
	config.PlatformFreeMemoryFn = FreeMemory;
	config.OnHeapCorruptionDetectedFn = OnHeapCorruptionDetectedCallback;

	config.HeapValidationMode = MIHP_HVM_MagicNumber;

	MIHP_Heap myHeap = { 0 };
	MIHP_InitializeHeap(&myHeap, config);

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		size_t size = 565 + (i % 5 == 0 ? 854 : 0);
#if USE_MALLOC
		myptrs[i] = malloc(size);
#else
		myptrs[i] = MIHP_Allocate(&myHeap, size);
		MIHP_ASSERT(myptrs[i]);

		MIHP_AllocationCustomMetadata metadata = { 0 };
		void* ptr = GetThisAddress();
		metadata.AsPtr[0] = ptr;

		MIHP_SetPointerCustomMetadata(&myHeap, myptrs[i], metadata);
		MIHP_ValidateHeap(&myHeap);

		MIHP_AllocationCustomMetadata outMetadata = { 0 };
		MIHP_GetPointerCustomMetadata(&myHeap, myptrs[i], &outMetadata);
		void* returnPtr = outMetadata.AsPtr[0];

		MIHP_ASSERT(ptr == returnPtr);
#endif
	}

	__debugbreak();

	//MIHP_MEMSET(myptrs[5], 5000, 0x75);

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		if (rand() % 3 == 0)
		{
#if USE_MALLOC
			free(myptrs[i]);
#else
			MIHP_Free(&myHeap, myptrs[i]);
			MIHP_ValidateHeap(&myHeap);
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
			myptrs[i] = MIHP_Reallocate(&myHeap, myptrs[i], size);
			MIHP_ValidateHeap(&myHeap);

			
#endif
		}
	}

	__debugbreak();

	for (int i = 0; i < NUM_ALLOCATIONS; i++)
	{
		volatile int n = i;
		if (!myptrs[i])
		{
			size_t size = 300 + rand() % 500;

#if USE_MALLOC
			myptrs[i] = malloc(size);
#else
			myptrs[i] = MIHP_Allocate(&myHeap, size);
			MIHP_ValidateHeap(&myHeap);


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
			MIHP_Free(&myHeap, myptrs[i]);
			MIHP_ValidateHeap(&myHeap);
#endif

			myptrs[i] = NULL;
		}


	__debugbreak();
}

