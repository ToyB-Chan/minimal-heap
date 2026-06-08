#include "minimal_heap.h"

#define MIHP_MIN(A, B) (A) < (B) ? (A) : (B)
#define MIHP_MAX(A, B) (A) > (B) ? (A) : (B)

MIHP_HeapInfo* MIHP_CreateHeap(MIHP_HeapConfig config)
{
	MIHP_HeapInfo* heap = (MIHP_HeapInfo*)MIHP_PlatformRequestMemory(sizeof(MIHP_HeapInfo));
	if (heap == NULL)
		return NULL;

	heap->Config = config;
	heap->FirstArea = MIHP_CreateHeapMemoryArea(heap, config.MinimalMemoryAreaSize);
	heap->FirstArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap->FirstArea);

	if (heap->FirstArea == NULL)
	{
		MIHP_PlatformFreeMemory(heap, sizeof(MIHP_HeapInfo));
		return NULL;
	}

	return heap;
}

bool MIHP_DestroyHeap(MIHP_HeapInfo* heap, bool force)
{
	if (heap == NULL)
		return false;

	if (!force && heap->Stats.NumTotalOccupiedSegments > 0)
		return false;

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(currentArea);
		nextArea = currentArea->NextArea;

		MIHP_DestroyHeapMemoryArea(heap, currentArea);
	}

	MIHP_PlatformFreeMemory(heap, sizeof(MIHP_HeapInfo));
	return true;
}

void* MIHP_Allocate(MIHP_HeapInfo* heap, size_t size)
{
	if (heap == NULL)
		return NULL;

	if (size == 0)
		return NULL;

	if (size > heap->Config.MaximalMemoryAreaSize)
		return NULL;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(currentArea);
		nextArea = currentArea->NextArea;
		
		if (currentArea->NumOccupiedSegments == currentArea->NumSegments)
			continue;

		MIHP_HeapSegmentHeader* nextSegment = currentArea->FirstSegment;
		while (nextSegment)
		{
			MIHP_HeapSegmentHeader* currentSegment = nextSegment;
			MIHP_ValidateHeapSegmentHeader(currentSegment);
			nextSegment = currentSegment->NextSegment;

			if (currentSegment->OccupiedSize != 0)
				continue;

			size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
			size_t minimalPayloadSize = heap->Config.AllocationAlignment;
			MIHP_HeapSegmentHeader* targetSegment = NULL;

			if (currentSegment->SegmentSize >= 2 * segmentHeaderSize + size + minimalPayloadSize)
				MIHP_SplitHeapSegment(currentSegment, segmentHeaderSize + size, &targetSegment);
			else if (currentSegment->SegmentSize >= segmentHeaderSize + size)
				targetSegment = currentSegment;
			else
				continue;

			targetSegment->OccupiedSize = size;
			targetSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(targetSegment);

			currentArea->NumOccupiedSegments++;
			heap->Stats.NumTotalOccupiedSegments++;

			MIHP_UNLOCK_HEAP(&heap->HeapLock);

			void* data = (char*)targetSegment + segmentHeaderSize;

#if MIHP_ZERO_OUT_ALLOCATIONS
			for (size_t i = 0; i < size; ++i)
				*(char*)data = 0;
#endif

			return data;
		}
	}

	size_t newAreaSize = heap->Stats.TotalSize;

	while (newAreaSize < size)
		newAreaSize = MIHP_MIN(heap->Config.MaximalMemoryAreaSize, newAreaSize * 2);

	MIHP_HeapMemoryAreaHeader* newArea = MIHP_CreateHeapMemoryArea(heap, newAreaSize);
	if (newArea == NULL)
		return NULL;

	newArea->NextArea = heap->FirstArea;
	heap->FirstArea->PreviousArea = newArea;
	heap->FirstArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap->FirstArea);

	heap->FirstArea = newArea;
	newArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(newArea);

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return MIHP_Allocate(heap, size);
}

void* MIHP_Realloc(MIHP_HeapInfo* heap, void* ptr, size_t newSize)
{
	return NULL;
}

bool MIHP_Free(MIHP_HeapInfo* heap, void* ptr)
{
	if (heap == NULL)
		return false;

	if (ptr == NULL)
		return false;

	if (!MIHP_IsPointerInHeap(heap, ptr))
		return false;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
	MIHP_HeapSegmentHeader* segment = (char*)ptr - segmentHeaderSize;
	MIHP_ValidateHeapSegmentHeader(segment);

	MIHP_HeapMemoryAreaHeader* area = segment->OwningMemoryArea;
	MIHP_ValidateHeapMemoryAreaHeader(area);

	segment->OccupiedSize = 0;
	area->NumOccupiedSegments--;
	heap->Stats.NumTotalOccupiedSegments--;
	segment->Checksum = MIHP_GenerateHeapSegmentChecksum(segment);

	if (area->NumOccupiedSegments == 0 && heap->Stats.NumMemoryAreas > 1)
	{
		MIHP_HeapMemoryAreaHeader* prevArea = area->PreviousArea;
		MIHP_HeapMemoryAreaHeader* nextArea = area->NextArea;

		if (MIHP_DestroyHeapMemoryArea(heap, area))
		{
			if (prevArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(prevArea);
				prevArea->NextArea = nextArea;
				prevArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(prevArea->NextArea);
			}
			else
				heap->FirstArea = nextArea;

			if (nextArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(nextArea);
				nextArea->PreviousArea = prevArea;
				nextArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(nextArea);
			}

			MIHP_UNLOCK_HEAP(&heap->HeapLock);
			return true;
		}
	}

	if (segment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(segment->NextSegment);
		if (segment->NextSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(segment, segment->NextSegment);
	}

	if (segment->PreviousSegment)
	{
		MIHP_ValidateHeapSegmentHeader(segment->PreviousSegment);
		if (segment->PreviousSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(segment->PreviousSegment, segment);
	}

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return true;
}

bool MIHP_IsPointerInHeap(MIHP_HeapInfo* heap, void* ptr)
{
	if (heap == NULL)
		return false;

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
	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(currentArea);
		nextArea = currentArea->NextArea;

		MIHP_HeapSegmentHeader* nextSegment = currentArea->FirstSegment;
		while (nextSegment)
		{
			MIHP_HeapSegmentHeader* currentSegment = nextSegment;
			MIHP_ValidateHeapSegmentHeader(currentSegment);
			nextSegment = currentSegment->NextSegment;
		}
	}
}

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_HeapInfo* heap, size_t requestedSize)
{
	MIHP_ASSERT(heap);

	size_t effectiveSize = ((requestedSize + MIHP_MINIMAL_MEMORY_AREA_SIZE - 1) / MIHP_MINIMAL_MEMORY_AREA_SIZE) * MIHP_MINIMAL_MEMORY_AREA_SIZE;

	MIHP_HeapMemoryAreaHeader* area = MIHP_PlatformRequestMemory(effectiveSize);
	if (area == NULL)
		return NULL;

	heap->Stats.NumMemoryAreas++;
	heap->Stats.TotalSize += effectiveSize;

	area->NextArea = NULL;
	area->PreviousArea = NULL;
	area->AreaSize = effectiveSize;
	area->NumOccupiedSegments = 0;
	area->NumSegments = 0;
	area->OwningHeap = heap;

	size_t headerSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapMemoryAreaHeader));
	area->FirstSegment = MIHP_InitializeHeapSegment(area, (char*)area + headerSize, area->AreaSize - headerSize);
	area->FirstSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(area->FirstSegment);

	return area;
}

bool MIHP_DestroyHeapMemoryArea(MIHP_HeapInfo* heap, MIHP_HeapMemoryAreaHeader* memoryArea)
{
	MIHP_ASSERT(heap);
	MIHP_ASSERT(memoryArea);

	size_t areaNumSegments = memoryArea->NumSegments;
	size_t areaNumOccupiedSegments = memoryArea->NumOccupiedSegments;
	size_t areaSize = memoryArea->AreaSize;

	if (!MIHP_PlatformFreeMemory(memoryArea, memoryArea->AreaSize))
		return false;

	heap->Stats.NumTotalSegments -= areaNumSegments;
	heap->Stats.NumTotalOccupiedSegments -= areaNumOccupiedSegments;
	heap->Stats.NumMemoryAreas--;
	heap->Stats.TotalSize -= areaSize;

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
	{
		MIHP_ValidateHeapSegmentHeader(sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = newSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment->NextSegment);
	}

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

	if (segmentToAbsorb->OccupiedSize > 0)
		return false;

	sourceSegment->NextSegment = segmentToAbsorb->NextSegment;

	if (sourceSegment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = sourceSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment->NextSegment);
	}

	sourceSegment->SegmentSize += segmentToAbsorb->SegmentSize;
	MIHP_UninitializeHeapSegment(segmentToAbsorb);

	return true;
}

size_t MIHP_GetHeapAlignedSize(MIHP_HeapInfo* heap, size_t size)
{
	return (size + (heap->Config.AllocationAlignment - 1)) & ~(heap->Config.AllocationAlignment - 1);
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

