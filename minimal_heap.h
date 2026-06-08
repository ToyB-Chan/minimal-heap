/* Version 1.4 */

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

#ifndef MIHP_ASSERT
	#define MIHP_ASSERT(Condition) while(1) { if (Condition) break; *(volatile char*)(0x0) = 0; }
#endif

#ifndef MIHP_HEAP_LOCK_TYPE
	#define MIHP_HEAP_LOCK_TYPE volatile bool // Does NOT have to be reentred safe
	#define MIHP_LOCK_HEAP(LockVarPtr) { while (*(LockVarPtr)) {}; *(LockVarPtr) = true; }
	#define MIHP_UNLOCK_HEAP(LockVarPtr) { *(LockVarPtr) = false; }
#endif
/* */

/* Hooks to be implemented by the library user */
void* MIHP_PlatformRequestMemory(const struct _MIHP_Heap* heap, size_t minRequestedSize, size_t* outActualSize); // Alignment must at least be alignment of size_t, outActualSize must at least be minRequestedSize
bool MIHP_PlatformFreeMemory(const struct _MIHP_Heap* heap, void* ptr, size_t actualSize);
void MIHP_PlatformOnHeapCorruptionDetected(const struct _MIHP_Heap* heap, struct _MIHP_HeapCorruptionInfo info);
/* */

#define MIHP_HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH 0
#define MIHP_HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH 1
#define MIHP_HEAP_CORRUPTION_TYPE_DOUBLE_FREE 2

typedef struct _MIHP_HeapConfig
{
	size_t			AllocationAlignment;		// Must be power of two and at least sizeof(size_t)

	size_t			MinimalMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	size_t			MaximalMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	
	char			AllocationInitialValue;
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
	struct _MIHP_HeapSegmentHeaderFreePayload*		FirstFreeSegment;


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

typedef struct _MIHP_HeapSegmentHeaderFreePayload
{
	struct _MIHP_HeapSegmentHeaderFreePayload* PreviousFreeSegment;
	struct _MIHP_HeapSegmentHeaderFreePayload* NextFreeSegment;
} MIHP_HeapSegmentHeaderFreePayload;

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

	MIHP_HEAP_LOCK_TYPE HeapLock;
} MIHP_Heap;

/* API Interface */
bool MIHP_InitializeHeap(MIHP_Heap* heap, MIHP_HeapConfig config);
bool MIHP_IsHeapInitialized(const MIHP_Heap* heap);
bool MIHP_UninitializeHeap(MIHP_Heap* heap, bool force);

void* MIHP_Allocate(MIHP_Heap* heap, size_t size);
void* MIHP_Realloc(MIHP_Heap* heap, void* ptr, size_t newSize);
bool MIHP_Free(MIHP_Heap* heap, void* ptr);

bool MIHP_IsPointerInHeap(MIHP_Heap* heap, void* ptr);
size_t MIHP_GetPtrAllocationSize(MIHP_Heap* heap, void* ptr);
void MIHP_ValidateHeap(MIHP_Heap* heap);
/* */

/* Internals */
MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_Heap* heap, size_t requestedSize);
bool MIHP_DestroyHeapMemoryArea(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* area);

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t size);
bool MIHP_UninitializeHeapSegment(MIHP_HeapSegmentHeader* segment);

bool MIHP_SplitHeapSegment(MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentSize, MIHP_HeapSegmentHeader** outNewSegment);
bool MIHP_MergeHeapSegments(MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb);

void MIHP_AddSegmentToFreeList(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment);
void MIHP_RemoveSegmentFromFreeList(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment);

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
