/* Version 1.5 */

#ifndef __MINIMAL_HEAP_H__
#define __MINIMAL_HEAP_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef MIHP_IMPLEMENTATION
	#define MIHP_IMPLEMENTATION 0
#endif

#define MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_NONE 0
#define MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_MAGIC_NUMBER 1
#define MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM 2

/* Defines to be configured by the library user */
#ifndef MIHP_HEAP_STRUCTURE_VALIDATION_TYPE
	#define MIHP_HEAP_STRUCTURE_VALIDATION_TYPE MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM
#endif

#ifndef MIHP_MIN_TRAVERSELS_BEFORE_EXPANSION
	#define MIHP_MIN_TRAVERSELS_BEFORE_EXPANSION 10000
#endif

#ifndef MIHP_MIN_EXPANSION_TRAVERSEL_RATIO_INT_PERCENT
	#define MIHP_MIN_EXPANSION_TRAVERSEL_RATIO_INT_PERCENT 40
#endif

#ifndef MIHP_ASSERT
	#define MIHP_ASSERT(Condition) while(1) { if (Condition) break; *(volatile char*)(0x0) = 0; }
#endif

#ifndef MIHP_MEMSET
	#define MIHP_MEMSET(Ptr, Size, Val) for(size_t i = 0; i < Size; ++i) *((char*)Ptr + i) = Val;
#endif
/* */

typedef void*(MIHP_PlatformReqeustMemoryFn)(const struct _MIHP_Heap* heap, size_t minRequestedSize, size_t* outActualSize);
typedef bool (MIHP_PlatformFreeMemoryFn)(const struct _MIHP_Heap* heap, void* ptr, size_t actualSize);

#define MIHP_HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH 0
#define MIHP_HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH 1
#define MIHP_HEAP_CORRUPTION_TYPE_DOUBLE_FREE 2

typedef void (MIHP_OnHeapCorruptionDetectedFn)(const struct _MIHP_Heap* heap, struct _MIHP_HeapCorruptionInfo);

typedef void(MIHIP_LockHeapFn)(const struct _MIHP_Heap* heap, void* heapLock);
typedef void(MIHIP_UnlockHeapFn)(const struct _MIHP_Heap* heap, void* heapLock);

typedef struct _MIHP_HeapConfig
{
	MIHP_PlatformReqeustMemoryFn*		PlatformRequestMemoryFn;		// Alignment must at least be alignment of size_t, outActualSize must at least be minRequestedSize
	MIHP_PlatformFreeMemoryFn*			PlatformFreeMemoryFn;			// Can return false if the memory cannot be freed
	MIHP_OnHeapCorruptionDetectedFn*	OnHeapCorruptionDetectedFn;		// Can be NULL

	size_t					MinimalMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	size_t					MaximalMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	
	size_t					AllocationAlignment;		// Must be power of two and at least sizeof(size_t)
	char					AllocationInitialValue;		// Initial value the returned blocks are to be filled with

	void*					HeapLock;			// Does not need to be reentred safe, can be NULL to disable locking
	MIHIP_LockHeapFn*		LockHeapFn;			// Can be NULL if HeapLock is NULL
	MIHIP_LockHeapFn*		UnlockHeapFn;		// Can be NULL if HeapLock is NULL
} MIHP_HeapConfig;

typedef struct _MIHP_HeapStats
{
	size_t 				NumMemoryAreas;
	size_t 				NumTotalSegments;
	size_t 				NumTotalOccupiedSegments;
	size_t 				TotalSize;
} MIHP_HeapStats;

// This structs size must be multiple of 8
typedef struct _MIHP_HeapMemoryAreaHeader
{
	size_t 							AreaSize;
	size_t 							NumSegments;
	size_t 							NumOccupiedSegments;

	struct _MIHP_HeapMemoryAreaHeader* 	NextArea;
	struct _MIHP_HeapMemoryAreaHeader* 	PreviousArea;

	struct _MIHP_Heap*								OwningHeap;
	struct _MIHP_HeapSegmentHeader*					FirstSegment;

	uint32_t 			Checksum; // Checksum must be the last member !
} MIHP_HeapMemoryAreaHeader;

// This structs size must be multiple of 8
typedef struct _MIHP_HeapSegmentHeader
{
	size_t						SegmentSize;
	size_t						OccupiedSize;

	struct _MIHP_HeapSegmentHeader* 		NextSegment;
	struct _MIHP_HeapSegmentHeader* 		PreviousSegment;
	struct _MIHP_HeapMemoryAreaHeader* 		OwningMemoryArea;

	char _padding0[16];

	uint32_t 			Checksum; // Checksum must be the last member !

} MIHP_HeapSegmentHeader;

typedef struct _MIHP_HeapCorruptionInfo
{
	uint8_t 						Type;
	const void* 					Ptr;
	uint32_t 						ExpectedValue;
	uint32_t 						ActualValue;
} MIHP_HeapCorruptionInfo;

typedef struct _MIHP_Heap
{
	MIHP_HeapConfig Config;
	MIHP_HeapStats Stats;
	MIHP_HeapMemoryAreaHeader* FirstArea;
	MIHP_HeapMemoryAreaHeader* LastSuccessfulAllocationArea;
} MIHP_Heap;

/* API Interface */
bool MIHP_InitializeHeap(MIHP_Heap* heap, MIHP_HeapConfig config);
bool MIHP_IsHeapInitialized(const MIHP_Heap* heap);
bool MIHP_UninitializeHeap(MIHP_Heap* heap, bool force);

void* MIHP_Allocate(MIHP_Heap* heap, size_t size);
void* MIHP_Reallocate(MIHP_Heap* heap, void* ptr, size_t newSize);
bool MIHP_Free(MIHP_Heap* heap, void* ptr);

bool MIHP_IsPointerInHeap(MIHP_Heap* heap, void* ptr);
size_t MIHP_GetPtrAllocationSize(MIHP_Heap* heap, void* ptr);
void MIHP_ValidateHeap(MIHP_Heap* heap);
/* */

/* Internals */
void MIHP_LockHeap(MIHP_Heap* heap);
void MIHP_UnlockHeap(MIHP_Heap* heap);

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_Heap* heap, size_t requestedSize);
bool MIHP_DestroyHeapMemoryArea(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* area);

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t size);
bool MIHP_UninitializeHeapSegment(MIHP_HeapSegmentHeader* segment);

bool MIHP_SplitHeapSegment(MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentSize, MIHP_HeapSegmentHeader** outNewSegment);
bool MIHP_MergeHeapSegments(MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb);

void* MIHP_GetSegmentPayloadPtr(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment);
MIHP_HeapSegmentHeader* MIHP_GetSegmentHeaderPtr(MIHP_Heap* heap, void* payloadPtr);

size_t MIHP_GetHeapAlignedSize(const MIHP_Heap* heap, size_t size);

uint32_t MIHP_HashMemoryRegion(const void* data, size_t size);
uint32_t MIHP_GenerateHeapMemoryAreaChecksum(const MIHP_HeapMemoryAreaHeader* area);
uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_HeapSegmentHeader* segment);

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area);
void MIHP_ValidateHeapSegmentHeader(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment);

size_t MIHP_GetMinimalPayloadSize(const MIHP_Heap* heap);
/* */

#endif

#if MIHP_IMPLEMENTATION
	#include "minimal_heap_implementation.inl"
#endif
