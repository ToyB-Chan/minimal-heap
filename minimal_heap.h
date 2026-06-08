#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MIHP_USE_HEAP_STRUCTURE_CHECKSUM_VALIDATION 0
#define MIHP_USE_CANARY_VALIDATION 1

#define MIHP_HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH 0
#define MIHP_HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH 1
#define MIHP_HEAP_CORRUPTION_TYPE_FRONT_CANARY_MISMATCH 2
#define MIHP_HEAP_CORRUPTION_TYPE_REAR_CANARY_MISMATCH 3

typedef struct _MIHP_HeapConfig
{
	size_t			AllocationAlignment;

	size_t			MinimalMemoryAreaSize;
	size_t			MaximalMemoryAreaSize;
	
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

	struct _MIHP_HeapInfo*				OwningHeap;
	struct _MIHP_HeapSegmentHeader*		FirstSegment;


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
	const struct _MIHP_HeapInfo*	Heap;
	uint8_t 						Type;
	const void* 					Ptr;
	uint32_t 						ExpectedValue;
	uint32_t 						ActualValue;
} MIHP_HeapCorruptionInfo;

/* Hooks to be implemented by the library user */
void* MIHP_PlatformRequestMemory(size_t size); // Alignment must at least be alingment of size_t
bool MIHP_PlatformFreeMemory(void* ptr, size_t size);
void MIHP_PlatformOnHeapCorruptionDetected(MIHP_HeapCorruptionInfo info);
/* === */

/* Defines to be configured by the library user */
#define MIHP_MINIMAL_MEMORY_AREA_SIZE (4096 * 32)

#define MIHP_ASSERT(Condition) while(1) { if (Condition) break; *(volatile char*)(0x0) = 0; }

#define MIHP_HEAP_LOCK_TYPE uint8_t
#define MIHP_LOCK_HEAP(LockVariablePtr) {}
#define MIHP_UNLOCK_HEAP(LockVarialbePtr) {}
/* === */

typedef struct _MIHP_HeapInfo
{
	MIHP_HeapConfig Config;
	MIHP_HeapStats Stats;
	MIHP_HeapMemoryAreaHeader* FirstArea;

	MIHP_HEAP_LOCK_TYPE HeapLock;
} MIHP_HeapInfo;

/* API Interface */
MIHP_HeapInfo* MIHP_CreateHeap(MIHP_HeapConfig config);
bool MIHP_DestroyHeap(MIHP_HeapInfo* heap, bool force);
void* MIHP_Allocate(MIHP_HeapInfo* heap, size_t size);
void* MIHP_Realloc(MIHP_HeapInfo* heap, void* ptr, size_t newSize);
bool MIHP_Free(MIHP_HeapInfo* heap, void* ptr);

bool MIHP_IsPointerInHeap(MIHP_HeapInfo* heap, void* ptr);
size_t MIHP_GetPtrAllocationSize(MIHP_HeapInfo* heap, void* ptr);
void MIHP_ValidateHeap(MIHP_HeapInfo* heap);
/* */

/* Internals */
MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_HeapInfo* heap, size_t requestedSize);
bool MIHP_DestroyHeapMemoryArea(MIHP_HeapInfo* heap, MIHP_HeapMemoryAreaHeader* area);

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t size);
bool MIHP_UninitializeHeapSegment(MIHP_HeapSegmentHeader* segment);

bool MIHP_SplitHeapSegment(MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentSize, MIHP_HeapSegmentHeader** outNewSegment);
bool MIHP_MergeHeapSegments(MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb);

size_t MIHP_GetHeapAlignedSize(const MIHP_HeapInfo* heap, size_t size);

uint32_t MIHP_HashMemoryRegion(const void* data, size_t size);
uint32_t MIHP_GenerateHeapMemoryAreaChecksum(const MIHP_HeapMemoryAreaHeader* area);
uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_HeapSegmentHeader* segment);

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_HeapInfo* heap, const MIHP_HeapMemoryAreaHeader* area);
void MIHP_ValidateHeapSegmentHeader(const MIHP_HeapInfo* heap, const MIHP_HeapSegmentHeader* segment);
/* */
