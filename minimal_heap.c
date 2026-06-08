#include "minimal_heap.h"

#define MIHP_MIN(A, B) (A) < (B) ? (A) : (B)
#define MIHP_MAX(A, B) (A) > (B) ? (A) : (B)

MIHP_HeapInfo* MIHP_CreateHeap(size_t initialSize, size_t allocationAlignment)
{
	MIHP_HeapInfo* heap = (MIHP_HeapInfo*)MIHP_PlatformRequestMemory(sizeof(MIHP_HeapInfo));
	if (heap == NULL)
		return NULL;

	heap->FirstArea = MIHP_CreateHeapMemoryArea(heap, initialSize);
	if (heap->FirstArea == NULL)
	{
		MIHP_PlatformFreeMemory(heap, sizeof(MIHP_HeapInfo));
		return NULL;
	}

	heap->AllocationAlignment = allocationAlignment;
	return heap;
}

bool MIHP_DestroyHeap(MIHP_HeapInfo* heap, bool force)
{
	MIHP_ASSERT(heap);

	if (!force && heap->Stats.NumTotalOccupiedSegments > 0)
		return false;

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		nextArea = currentArea->NextArea;
		MIHP_DestroyHeapMemoryArea(heap, currentArea);
	}

	MIHP_PlatformFreeMemory(heap, sizeof(MIHP_HeapInfo));
	return true;
}

void* MIHP_Allocate(MIHP_HeapInfo* heap, size_t size)
{
	return NULL;
}

void* MIHP_Realloc(MIHP_HeapInfo* heap, void* ptr, size_t newSize)
{
	return NULL;
}

void MIHP_Free(MIHP_HeapInfo* heap, void* ptr)
{
}

bool MIHP_IsPointerInHeap(MIHP_HeapInfo* heap, void* ptr)
{
	MIHP_LOCK_HEAP(&heap->HeapLock);

	size_t searchingPtr = (size_t)ptr;

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		nextArea = currentArea->NextArea;
		
		size_t ptrMin = (size_t)currentArea;
		size_t ptrMax = (size_t)currentArea + currentArea->AreaSize;

		if (searchingPtr >= ptrMin && searchingPtr <= ptrMax)
		{
			MIHP_UNLOCK_HEAP(&heap->HeapLock);
			return true;
		}
	}

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return false;
}

void MIHP_ValidateHeap(MIHP_HeapInfo* heap)
{
}

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_HeapInfo* heap, size_t requestedSize)
{
	MIHP_ASSERT(heap);

	size_t effectiveSize = ((requestedSize + MIHP_MINIMAL_MEMORY_AREA_SIZE - 1) / MIHP_MINIMAL_MEMORY_AREA_SIZE) * MIHP_MINIMAL_MEMORY_AREA_SIZE;

	MIHP_HeapMemoryAreaHeader* area = MIHP_PlatformRequestMemory(effectiveSize);
	if (area == NULL)
		return NULL;

	heap->Stats.NumMemoryAreas++;
	area->NextArea = NULL;
	area->PreviousArea = NULL;
	area->AreaSize = effectiveSize;
	area->NumOccupiedSegments = 0;
	area->NumSegments = 0;
	area->OwningHeap = heap;

	size_t headerSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapMemoryAreaHeader));
	area->FirstSegment = MIHP_InitializeHeapSegment(area, (char*)area + headerSize, area->AreaSize - headerSize);
	
	area->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(area);

	return area;
}

bool MIHP_DestroyHeapMemoryArea(MIHP_HeapInfo* heap, MIHP_HeapMemoryAreaHeader* memoryArea)
{
	MIHP_ASSERT(heap);
	MIHP_ASSERT(memoryArea);

	size_t areaNumSegments = memoryArea->NumSegments;
	size_t areaNumOccupiedSegments = memoryArea->NumOccupiedSegments;

	if (!MIHP_PlatformFreeMemory(memoryArea, memoryArea->AreaSize))
		return false;

	heap->Stats.NumTotalSegments -= areaNumSegments;
	heap->Stats.NumTotalOccupiedSegments -= areaNumOccupiedSegments;
	heap->Stats.NumMemoryAreas--;
	return true;
}

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t segmentSize)
{
	MIHP_ASSERT(area);
	MIHP_ASSERT(segmentStart);

	MIHP_HeapSegmentHeader* segment = (MIHP_HeapSegmentHeader*)segmentStart;
	segment->SegmentSize = segmentSize;
	segment->OccupiedSize = 0;
	segment->NextSegment = NULL;
	segment->PreviousSegment = NULL;
	segment->OwningMemoryArea = area;

	segment->Checksum = MIHP_GenerateHeapSegmentChecksum(segment);

	area->NumSegments++;
	area->OwningHeap->Stats.NumTotalSegments++;
	return segment;
}

bool MIHP_UninitializeHeapSegment(MIHP_HeapSegmentHeader* segment)
{
	MIHP_ASSERT(segment);

	if (segment->OccupiedSize > 0)
		return false;

	segment->OwningMemoryArea->NumSegments--;
	segment->OwningMemoryArea->OwningHeap->Stats.NumTotalSegments--;

#if MIHP_ZERO_OUT_MERGED_HEADERS
	segment->SegmentSize = 0;
	segment->OccupiedSize = 0;
	segment->NextSegment = NULL;
	segment->PreviousSegment = NULL;
	segment->OwningMemoryArea = NULL;
	segment->Checksum = 0;
#endif

	return true;
}

bool MIHP_SplitHeapSegment(MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentMinSize, MIHP_HeapSegmentHeader** outNewSegment)
{
	MIHP_ASSERT(sourceSegment);
	MIHP_ValidateHeapSegmentHeader(sourceSegment);
	MIHP_ValidateHeapMemoryAreaHeader(sourceSegment->OwningMemoryArea);

	size_t headerSize = MIHP_GetHeapAlignedSize(sourceSegment->OwningMemoryArea->OwningHeap, sizeof(MIHP_HeapSegmentHeader));
	size_t effectiveNewSegmentSize = MIHP_GetHeapAlignedSize(sourceSegment->OwningMemoryArea->OwningHeap, newSegmentMinSize);

	if (sourceSegment->SegmentSize < effectiveNewSegmentSize)
		return false;

	size_t sourceResultingSegmentSize = sourceSegment->SegmentSize - effectiveNewSegmentSize;

	if (sourceResultingSegmentSize < sourceSegment->OccupiedSize + headerSize)
		return false;

	sourceSegment->SegmentSize = sourceResultingSegmentSize;

	MIHP_HeapSegmentHeader* newSegment = MIHP_InitializeHeapSegment(sourceSegment->OwningMemoryArea, (char*)sourceSegment + sourceResultingSegmentSize, effectiveNewSegmentSize);
	newSegment->NextSegment = sourceSegment->NextSegment;
	newSegment->PreviousSegment = sourceSegment;

	if (sourceSegment->NextSegment)
		sourceSegment->NextSegment->PreviousSegment = newSegment;

	sourceSegment->NextSegment = newSegment;

	if (outNewSegment)
		*outNewSegment = newSegment;

	newSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(newSegment);
	sourceSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment);

	return true;
}

bool MIHP_MergeHeapSegments(MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb)
{
	MIHP_ASSERT(sourceSegment);
	MIHP_ASSERT(segmentToAbsorb);
	MIHP_ASSERT(sourceSegment->NextSegment == segmentToAbsorb);

	MIHP_ValidateHeapSegmentHeader(sourceSegment);
	MIHP_ValidateHeapSegmentHeader(segmentToAbsorb);
	MIHP_ValidateHeapMemoryAreaHeader(segmentToAbsorb->OwningMemoryArea);

	if (segmentToAbsorb->OccupiedSize > 0)
		return false;

	sourceSegment->NextSegment = segmentToAbsorb->NextSegment;

	if (sourceSegment->NextSegment)
		sourceSegment->NextSegment->PreviousSegment = sourceSegment;

	sourceSegment->SegmentSize += segmentToAbsorb->SegmentSize;
	MIHP_UninitializeHeapSegment(segmentToAbsorb);

	return true;
}

size_t MIHP_GetHeapAlignedSize(MIHP_HeapInfo* heap, size_t size)
{
	return (size + (heap->AllocationAlignment - 1)) & ~(heap->AllocationAlignment - 1);
}

uint32_t MIHP_HashMemoryRegion(const void* data, size_t size)
{
	uint32_t hash = 2166136261U;
	const unsigned char* bytes = (const unsigned char*)data;
	for (size_t i = 0; i < size; i++)
	{
		hash ^= bytes[i];
		hash *= 16777619U;
	}

	return hash;
}

uint32_t MIHP_GenerateHeapMemoryAreaChecksum(const MIHP_HeapMemoryAreaHeader* area)
{
#if !MIHP_USE_HEAP_STRUCTURE_CHECKSUM_VALIDATION
	return 0;
#endif

	MIHP_ASSERT(area);

	// Do not include the checksum itself into the new checksum
	size_t size = sizeof(MIHP_HeapMemoryAreaHeader) - sizeof(area->Checksum);
	return MIHP_HashMemoryRegion(area, size);
}

uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_HeapSegmentHeader* segment)
{
#if !MIHP_USE_HEAP_STRUCTURE_CHECKSUM_VALIDATION
	return 0;
#endif

	MIHP_ASSERT(segment);

	// Do not include the checksum itself into the new checksum
	size_t size = sizeof(MIHP_HeapSegmentHeader) - sizeof(segment->Checksum);
	return MIHP_HashMemoryRegion(segment, size);
}

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_HeapMemoryAreaHeader* area)
{
#if !MIHP_USE_HEAP_STRUCTURE_CHECKSUM_VALIDATION
	return;
#endif

	size_t newChecksum = MIHP_GenerateHeapMemoryAreaChecksum(area);

	if (area->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH;
		corruptionInfo.Ptr = area;
		corruptionInfo.ExpectedValue = area->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		MIHP_PlatformOnHeapCorruptionDetected(corruptionInfo);
	}
}

void MIHP_ValidateHeapSegmentHeader(const MIHP_HeapSegmentHeader* segment)
{
#if !MIHP_USE_HEAP_STRUCTURE_CHECKSUM_VALIDATION
	return;
#endif

	size_t newChecksum = MIHP_GenerateHeapSegmentChecksum(segment);

	if (segment->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH;
		corruptionInfo.Ptr = segment;
		corruptionInfo.ExpectedValue = segment->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		MIHP_PlatformOnHeapCorruptionDetected(corruptionInfo);
	}
}

