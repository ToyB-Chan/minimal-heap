#ifndef __MINIMAL_HEAP_H__
#define __MINIMAL_HEAP_H__

#define MIHP_VERSION_MAJOR 1
#define MIHP_VERSION_MINOR 9

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef MIHP_IMPLEMENTATION
	#define MIHP_IMPLEMENTATION 0
#endif

/* Defines to be configured by the library user */
#ifndef MIHP_ASSERT
	#define MIHP_ASSERT(Condition) while(1) { if (Condition) break; *(volatile char*)(0x0) = 0; }
#endif

#ifndef MIHP_MEMSET
	#define MIHP_MEMSET(Ptr, Size, Val) do { for(size_t i = 0; i < Size; ++i) *((char*)Ptr + i) = Val; } while(0);
#endif
/* */

typedef uintptr_t MIHP_HeapOpaque;

typedef void*(MIHP_PlatformRequestMemoryFn)(MIHP_HeapOpaque heap, size_t minRequestedSize, size_t* outActualSize);
typedef bool (MIHP_PlatformFreeMemoryFn)(MIHP_HeapOpaque heap, void* ptr, size_t actualSize);

typedef enum _MIHP_HeapCorruptionError
{
	MIHP_HCE_MemoryAreaChecksumMismatch = 0,
	MIHP_HCE_SegmentChecksumMismatch = 1,
	MIHP_HCE_DoubleFree = 2
} MIHP_HeapCorruptionError;

typedef void (MIHP_OnHeapCorruptionDetectedFn)(const struct _MIHP_Heap* heap, struct _MIHP_HeapCorruptionInfo corruptionInfo);

typedef void(MIHP_LockHeapFn)(MIHP_HeapOpaque heap, void* heapLock);
typedef void(MIHP_UnlockHeapFn)(MIHP_HeapOpaque heap, void* heapLock);

typedef enum _MIHP_HeapValidationMode
{
	MIHP_HVM_None = 0,
	MIHP_HVM_MagicNumber = 1,
	MIHP_HVM_Checksum = 2
} MIHP_HeapValidationMode;

typedef struct _MIHP_HeapConfig
{
	MIHP_PlatformRequestMemoryFn*		PlatformRequestMemoryFn;		// Alignment must at least be alignment of size_t, outActualSize must at least be minRequestedSize
	MIHP_PlatformFreeMemoryFn*			PlatformFreeMemoryFn;			// Can return false if the memory cannot be freed
	MIHP_OnHeapCorruptionDetectedFn*	OnHeapCorruptionDetectedFn;		// Can be NULL

	size_t					MinMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	size_t					MaxMemoryAreaSize;		// Must be a multiple of AllocationAlignment
	size_t					MaxHeapSize;			// Maximum size the heap is allowed to grow to. May exceed slightly if PlatformRequestMemory returns a larger area than requested

	size_t					MinTotalTraversalsBeforeHeapExpansion;		// Minimal amounts of traversals during allocation before considering expanding the heap
	uint8_t					MinHeapTraversalPercentToExpand;			// Minimal integer percent of the heap traversed before expanding the heap
	
	size_t					AllocationAlignment;		// Must be power of two and at least sizeof(size_t)
	uint8_t					AllocationInitialValue;		// Initial value the returned blocks are to be filled with

	MIHP_HeapValidationMode	HeapValidationMode;

	void*					HeapLock;			// Does not need to be reentred safe, can be NULL to disable locking
	MIHP_LockHeapFn*		LockHeapFn;			// Can be NULL if HeapLock is NULL
	MIHP_UnlockHeapFn*		UnlockHeapFn;		// Can be NULL if HeapLock is NULL
} MIHP_HeapConfig;

typedef struct _MIHP_HeapStats
{
	size_t 				NumMemoryAreas;
	size_t 				NumTotalSegments;
	size_t 				NumTotalOccupiedSegments;
	size_t 				TotalSize;
} MIHP_HeapStats;

typedef struct _MIHP_HeapMemoryAreaHeader
{
	size_t 							AreaSize;
	size_t 							NumSegments;
	size_t 							NumOccupiedSegments;

	struct _MIHP_HeapMemoryAreaHeader* 	NextArea;
	struct _MIHP_HeapMemoryAreaHeader* 	PreviousArea;

	struct _MIHP_HeapSegmentHeader*		FirstSegment;
	struct _MIHP_HeapSegmentHeader*		LastSuccessfulAllocationSegment;

	// TODO: Change this to an InstigatingHeapUuid (opaque ptr)
	MIHP_HeapOpaque		InstigatingHeap; // Heap that created this area.

	uint32_t 			Checksum; // Checksum must be the last member !
} MIHP_HeapMemoryAreaHeader;

typedef struct _MIHP_AllocationCustomMetadata
{
	union
	{
		uint8_t		AsU8	[sizeof(size_t) * 2];
		uint16_t	AsU16	[sizeof(size_t) * 1];
		uint32_t	AsU32	[sizeof(size_t) / 2];
		uint64_t	AsU64	[sizeof(size_t) / 4];
	};
} MIHP_AllocationCustomMetadata;

typedef struct _MIHP_HeapSegmentHeader
{
	size_t						SegmentSize;
	size_t						OccupiedSize;

	struct _MIHP_HeapSegmentHeader* 		NextSegment;
	struct _MIHP_HeapSegmentHeader* 		PreviousSegment;
	struct _MIHP_HeapMemoryAreaHeader* 		OwningMemoryArea;

	struct _MIHP_AllocationCustomMetadata	CustomMetadata;

	uint32_t 		Checksum; // Checksum must be the last member !
} MIHP_HeapSegmentHeader;

typedef struct _MIHP_HeapCorruptionInfo
{
	MIHP_HeapCorruptionError 		Type;
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
MIHP_HeapConfig MIHP_MakeDefaultConfigPreset();

bool MIHP_InitializeHeap(MIHP_Heap* heap, MIHP_HeapConfig config);
bool MIHP_IsHeapInitialized(const MIHP_Heap* heap);
bool MIHP_UninitializeHeap(MIHP_Heap* heap, bool force);

void* MIHP_Allocate(MIHP_Heap* heap, size_t size);
void* MIHP_Reallocate(MIHP_Heap* heap, void* ptr, size_t newSize);
bool MIHP_Free(MIHP_Heap* heap, void* ptr);

bool MIHP_SetPointerCustomMetadata(MIHP_Heap* heap, void* ptr, MIHP_AllocationCustomMetadata metadata);
bool MIHP_GetPointerCustomMetadata(MIHP_Heap* heap, void* ptr, MIHP_AllocationCustomMetadata* outMetadata);

bool MIHP_MergeHeaps(MIHP_Heap* sourceHeap, MIHP_Heap* heapToAbsorb);

bool MIHP_IsAddressInHeap(MIHP_Heap* heap, void* ptr);
size_t MIHP_GetPointerAllocationSize(MIHP_Heap* heap, void* ptr);
bool MIHP_ValidateHeap(MIHP_Heap* heap);

typedef void(MIHP_WalkHeapCallbackFn)(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area, const MIHP_HeapSegmentHeader* segment);

bool MIHP_WalkHeap(MIHP_Heap* heap, MIHP_WalkHeapCallbackFn* callback);
/* */

/* Internals */
void MIHP_LockHeap(MIHP_Heap* heap);
void MIHP_UnlockHeap(MIHP_Heap* heap);

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_Heap* heap, size_t requestedSize);
bool MIHP_DestroyHeapMemoryArea(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* area);

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t size);
bool MIHP_UninitializeHeapSegment(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment);

bool MIHP_SplitHeapSegment(MIHP_Heap* heap, MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentSize, MIHP_HeapSegmentHeader** outNewSegment);
bool MIHP_MergeHeapSegments(MIHP_Heap* heap, MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb);

void* MIHP_GetHeapSegmentPayloadPtr(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment);
MIHP_HeapSegmentHeader* MIHP_GetHeapSegmentHeaderPtr(MIHP_Heap* heap, void* payloadPtr);

size_t MIHP_GetHeapAlignedSize(const MIHP_Heap* heap, size_t size);

uint32_t MIHP_HashMemoryRegion(const void* data, size_t size);
uint32_t MIHP_GenerateHeapMemoryAreaChecksum(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area);
uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment);

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area);
void MIHP_ValidateHeapSegmentHeader(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment);

size_t MIHP_GetMinimalPayloadSize(const MIHP_Heap* heap);
/* */

#if MIHP_IMPLEMENTATION
	#include "minimal_heap_implementation.inl"
#endif

#endif
