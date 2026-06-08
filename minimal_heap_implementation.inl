#define MIHP_MIN(A, B) (A) < (B) ? (A) : (B)
#define MIHP_MAX(A, B) (A) > (B) ? (A) : (B)

#define MIHP_MEMSET(Ptr, Size, Val) for(size_t i = 0; i < Size; ++i) *((char*)Ptr + i) = Val;

#define MIHP_IS_PO2(Val) (Val != 0 && (Val & (Val - 1)) == 0)

bool MIHP_InitializeHeap(MIHP_Heap* heap, MIHP_HeapConfig config)
{
	if (heap == NULL)
		return false;

	if (MIHP_IsHeapInitialized(heap))
		return false;

	if (config.AllocationAlignment < sizeof(size_t))
		return false;

	if (!MIHP_IS_PO2(config.AllocationAlignment))
		return false;

	if (config.MinimalMemoryAreaSize % config.AllocationAlignment != 0)
		return false;

	if (config.MaximalMemoryAreaSize % config.AllocationAlignment != 0)
		return false;

	MIHP_MEMSET(heap, sizeof(MIHP_Heap), 0);

	heap->Config = config;
	heap->FirstArea = MIHP_CreateHeapMemoryArea(heap, config.MinimalMemoryAreaSize);
	heap->LastSuccessfulAllocationArea = heap->FirstArea;

	heap->FirstArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap->FirstArea);
	return heap;
}

bool MIHP_IsHeapInitialized(const MIHP_Heap* heap)
{
	if (heap == NULL)
		return false;

	return heap->FirstArea != NULL;
}

bool MIHP_UninitializeHeap(MIHP_Heap* heap, bool force)
{
	if (heap == NULL)
		return false;

	if (!MIHP_IsHeapInitialized(heap))
		return false;

	if (!force && heap->Stats.NumTotalOccupiedSegments > 0)
		return false;

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(heap, currentArea);
		nextArea = currentArea->NextArea;

		MIHP_DestroyHeapMemoryArea(heap, currentArea);
	}

	MIHP_MEMSET(heap, sizeof(MIHP_Heap), 0);
	return true;
}

void* MIHP_Allocate(MIHP_Heap* heap, size_t size)
{
	if (heap == NULL)
		return NULL;

	if (size == 0)
		return NULL;

	if (size > heap->Config.MaximalMemoryAreaSize)
		return NULL;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	bool triedLastAllocationArea = false;
	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = NULL;

		if (!triedLastAllocationArea)
		{
			currentArea = heap->LastSuccessfulAllocationArea;
			triedLastAllocationArea = true;
		}
		else
		{
			currentArea = nextArea;
			nextArea = currentArea->NextArea;
		}

		MIHP_ValidateHeapMemoryAreaHeader(heap, currentArea);
		
		if (currentArea->NumOccupiedSegments == currentArea->NumSegments)
			continue;

		MIHP_HeapSegmentHeader* nextSegment = currentArea->FirstSegment;
		while (nextSegment)
		{
			MIHP_HeapSegmentHeader* currentSegment = nextSegment;
			MIHP_ValidateHeapSegmentHeader(heap, currentSegment);
			nextSegment = nextSegment->NextSegment;

			if (currentSegment->OccupiedSize != 0)
				continue;

			size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
			MIHP_HeapSegmentHeader* targetSegment = NULL;

			if (currentSegment->SegmentSize >= 2 * segmentHeaderSize + size + MIHP_GetMinimalPayloadSize(heap))
				MIHP_SplitHeapSegment(currentSegment, segmentHeaderSize + size, &targetSegment);
			else if (currentSegment->SegmentSize >= segmentHeaderSize + size)
				targetSegment = currentSegment;
			else
				continue;

			targetSegment->OccupiedSize = size;
			targetSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(targetSegment);

			currentArea->NumOccupiedSegments++;
			currentArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(currentArea);

			heap->Stats.NumTotalOccupiedSegments++;
			heap->LastSuccessfulAllocationArea = currentArea;

			MIHP_UNLOCK_HEAP(&heap->HeapLock);

			void* data = MIHP_GetSegmentPayloadPtr(heap, targetSegment);
			MIHP_MEMSET(data, size, heap->Config.AllocationInitialValue);

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
	heap->LastSuccessfulAllocationArea = newArea;
	newArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(newArea);

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return MIHP_Allocate(heap, size);
}

void* MIHP_Realloc(MIHP_Heap* heap, void* ptr, size_t newSize)
{
	if (newSize == 0)
	{
		MIHP_Free(heap, ptr);
		return NULL;
	}

	if (heap == NULL)
		return NULL;

	if (ptr == NULL)
		return MIHP_Allocate(heap, newSize);

	if (!MIHP_IsPointerInHeap(heap, ptr))
		return NULL;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	MIHP_HeapSegmentHeader* segment = MIHP_GetSegmentHeaderPtr(heap, ptr);
	MIHP_ValidateHeapSegmentHeader(heap, segment);

	size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
	if (newSize > segment->OccupiedSize)
	{
		size_t maxPayloadSize = segment->SegmentSize - segmentHeaderSize;
		if (newSize <= maxPayloadSize)
		{
			MIHP_MEMSET((char*)ptr + segment->OccupiedSize, newSize - segment->OccupiedSize, heap->Config.AllocationInitialValue);
			segment->OccupiedSize = newSize;
			segment->Checksum = MIHP_GenerateHeapSegmentChecksum(segment);

			MIHP_UNLOCK_HEAP(&heap->HeapLock);
			return ptr;
		}

		if (segment->NextSegment && MIHP_MergeHeapSegments(segment, segment->NextSegment))
		{
			MIHP_UNLOCK_HEAP(&heap->HeapLock);
			return MIHP_Realloc(heap, ptr, newSize);
		}

		MIHP_UNLOCK_HEAP(&heap->HeapLock);

		void* newPtr = MIHP_Allocate(heap, newSize);
		if (!newPtr)
			return NULL;

		for (size_t i = 0; i < segment->OccupiedSize; i++)
			((char*)newPtr)[i] = ((char*)ptr)[i];

		MIHP_Free(heap, ptr);
		return newPtr;
	}
	else if (newSize < segment->OccupiedSize)
	{
		segment->OccupiedSize = newSize;
		segment->Checksum = MIHP_GenerateHeapSegmentChecksum(segment);

		size_t alignedNewSize = MIHP_GetHeapAlignedSize(heap, newSize);
		if (segment->SegmentSize > 2 * segmentHeaderSize + alignedNewSize + MIHP_GetMinimalPayloadSize(heap))
		{
			size_t newSplitSegmentSize = segment->SegmentSize - segmentHeaderSize - alignedNewSize;

			MIHP_HeapSegmentHeader* newSpittedSegment = NULL;
			MIHP_SplitHeapSegment(segment, newSplitSegmentSize, &newSpittedSegment);
			if (newSpittedSegment && newSpittedSegment->NextSegment)
				MIHP_MergeHeapSegments(newSpittedSegment, newSpittedSegment->NextSegment);
		}

		MIHP_UNLOCK_HEAP(&heap->HeapLock);
		return ptr;
	}

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return ptr;
}

bool MIHP_Free(MIHP_Heap* heap, void* ptr)
{
	if (heap == NULL)
		return false;

	if (ptr == NULL)
		return false;

	if (!MIHP_IsPointerInHeap(heap, ptr))
		return false;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	MIHP_HeapSegmentHeader* segment = MIHP_GetSegmentHeaderPtr(heap, ptr);
	MIHP_ValidateHeapSegmentHeader(heap, segment);

	MIHP_HeapMemoryAreaHeader* area = segment->OwningMemoryArea;
	MIHP_ValidateHeapMemoryAreaHeader(heap, area);

	if (segment->OccupiedSize == 0)
	{
		MIHP_HeapCorruptionInfo corruptionInfo = {0};
		corruptionInfo.Type = MIHP_HEAP_CORRUPTION_TYPE_DOUBLE_FREE;
		corruptionInfo.Ptr = ptr;

		MIHP_PlatformOnHeapCorruptionDetected(heap, corruptionInfo);
		return false;
	}

	segment->OccupiedSize = 0;
	segment->Checksum = MIHP_GenerateHeapSegmentChecksum(segment);
	
	area->NumOccupiedSegments--;
	area->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(area);

	heap->Stats.NumTotalOccupiedSegments--;

	if (area->NumOccupiedSegments == 0 && heap->Stats.NumMemoryAreas > 1)
	{
		MIHP_HeapMemoryAreaHeader* prevArea = area->PreviousArea;
		MIHP_HeapMemoryAreaHeader* nextArea = area->NextArea;

		if (MIHP_DestroyHeapMemoryArea(heap, area))
		{
			if (prevArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(heap, prevArea);
				prevArea->NextArea = nextArea;
				prevArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(prevArea);
			}
			else
				heap->FirstArea = nextArea;

			if (nextArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(heap, nextArea);
				nextArea->PreviousArea = prevArea;
				nextArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(nextArea);
			}

			MIHP_UNLOCK_HEAP(&heap->HeapLock);
			return true;
		}
	}

	if (segment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, segment->NextSegment);
		if (segment->NextSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(segment, segment->NextSegment);
	}

	if (segment->PreviousSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, segment->PreviousSegment);
		if (segment->PreviousSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(segment->PreviousSegment, segment);
	}

	MIHP_UNLOCK_HEAP(&heap->HeapLock);
	return true;
}

bool MIHP_IsPointerInHeap(MIHP_Heap* heap, void* ptr)
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

size_t MIHP_GetPtrAllocationSize(MIHP_Heap* heap, void* ptr)
{
	if (!MIHP_IsPointerInHeap(heap, ptr))
		return 0;

	MIHP_LOCK_HEAP(&heap->HeapLock);

	size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
	MIHP_HeapSegmentHeader* segment = (MIHP_HeapSegmentHeader*)((char*)ptr - segmentHeaderSize);
	MIHP_ValidateHeapSegmentHeader(heap, segment);

	return segment->OccupiedSize;
}

void MIHP_ValidateHeap(MIHP_Heap* heap)
{
	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(heap, currentArea);
		nextArea = currentArea->NextArea;

		MIHP_HeapSegmentHeader* nextSegment = currentArea->FirstSegment;
		while (nextSegment)
		{
			MIHP_HeapSegmentHeader* currentSegment = nextSegment;
			MIHP_ValidateHeapSegmentHeader(heap, currentSegment);
			nextSegment = currentSegment->NextSegment;
		}
	}
}

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_Heap* heap, size_t requestedSize)
{
	MIHP_ASSERT(heap);

	size_t effectiveSize = ((requestedSize + heap->Config.MinimalMemoryAreaSize - 1) / heap->Config.MinimalMemoryAreaSize) * heap->Config.MinimalMemoryAreaSize;
	effectiveSize = MIHP_MAX(effectiveSize, heap->Config.MinimalMemoryAreaSize);
	effectiveSize = MIHP_MIN(effectiveSize, heap->Config.MaximalMemoryAreaSize);

	size_t actualSize = 0;
	MIHP_HeapMemoryAreaHeader* area = (MIHP_HeapMemoryAreaHeader*)MIHP_PlatformRequestMemory(heap, effectiveSize, &actualSize);
	if (area == NULL)
		return NULL;

	MIHP_ASSERT(actualSize >= effectiveSize);
	MIHP_MEMSET(area, sizeof(MIHP_HeapMemoryAreaHeader), 0);

	area->AreaSize = actualSize;
	area->OwningHeap = heap;
	area->Checksum = ~0;

	size_t headerSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapMemoryAreaHeader));
	area->FirstSegment = MIHP_InitializeHeapSegment(area, (char*)area + headerSize, area->AreaSize - headerSize);
	area->FirstSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(area->FirstSegment);

	heap->Stats.NumMemoryAreas++;
	heap->Stats.TotalSize += area->AreaSize;

	return area;
}

bool MIHP_DestroyHeapMemoryArea(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* memoryArea)
{
	MIHP_ASSERT(heap);
	MIHP_ASSERT(memoryArea);

	size_t areaNumSegments = memoryArea->NumSegments;
	size_t areaNumOccupiedSegments = memoryArea->NumOccupiedSegments;
	size_t areaSize = memoryArea->AreaSize;

	if (!MIHP_PlatformFreeMemory(heap, memoryArea, memoryArea->AreaSize))
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

	MIHP_MEMSET(segment, sizeof(MIHP_HeapSegmentHeader), 0);

	segment->SegmentSize = segmentSize;
	segment->OwningMemoryArea = area;
	segment->Checksum = ~0;

	area->NumSegments++;
	area->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(area);

	area->OwningHeap->Stats.NumTotalSegments++;

	return segment;
}

bool MIHP_UninitializeHeapSegment(MIHP_HeapSegmentHeader* segment)
{
	MIHP_ASSERT(segment);

	if (segment->OccupiedSize > 0)
		return false;

	segment->OwningMemoryArea->NumSegments--;
	segment->OwningMemoryArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(segment->OwningMemoryArea);
	segment->OwningMemoryArea->OwningHeap->Stats.NumTotalSegments--;

	MIHP_MEMSET(segment, sizeof(MIHP_HeapSegmentHeader), 0);

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
		MIHP_ValidateHeapSegmentHeader(sourceSegment->OwningMemoryArea->OwningHeap, sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = newSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment->NextSegment);
	}

	sourceSegment->NextSegment = newSegment;

	newSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(newSegment);
	sourceSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment);

	*outNewSegment = newSegment;
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
		MIHP_ValidateHeapSegmentHeader(sourceSegment->OwningMemoryArea->OwningHeap, sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = sourceSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment->NextSegment);
	}

	sourceSegment->SegmentSize += segmentToAbsorb->SegmentSize;
	MIHP_UninitializeHeapSegment(segmentToAbsorb);

	sourceSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(sourceSegment);

	return true;
}

void* MIHP_GetSegmentPayloadPtr(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment)
{
	MIHP_ASSERT(heap);
	MIHP_ASSERT(segment);

	return (char*)segment + MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
}

MIHP_HeapSegmentHeader* MIHP_GetSegmentHeaderPtr(MIHP_Heap* heap, void* payloadPtr)
{
	MIHP_ASSERT(heap);
	MIHP_ASSERT(payloadPtr);

	return (MIHP_HeapSegmentHeader*)((char*)payloadPtr - MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader)));
}

size_t MIHP_GetHeapAlignedSize(const MIHP_Heap* heap, size_t size)
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

#if MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_NONE
	return 0;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_MAGIC_NUMBER
	return 0xAADEADAA;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM

	MIHP_ASSERT(area);

	// Do not include the checksum itself into the new checksum
	size_t size = offsetof(MIHP_HeapMemoryAreaHeader, Checksum);
	return MIHP_HashMemoryRegion(area, size);

#else
	#error "Invalid heap structure validation type"
#endif
}

uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_HeapSegmentHeader* segment)
{
#if MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_NONE
	return 0;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_MAGIC_NUMBER
	return 0x55DEAD55;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM

	MIHP_ASSERT(segment);

	// Do not include the checksum itself into the new checksum
	size_t size = offsetof(MIHP_HeapSegmentHeader, Checksum);
	return MIHP_HashMemoryRegion(segment, size);

#else
	#error "Invalid heap structure validation type"
#endif
}

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area)
{
	uint32_t newChecksum;

#if MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_NONE
	return;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_MAGIC_NUMBER
	newChecksum = 0xAADEADAA;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM
	newChecksum = MIHP_GenerateHeapMemoryAreaChecksum(area);
#else
	#error "Invalid heap structure validation type"
#endif

	if (area->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH;
		corruptionInfo.Ptr = area;
		corruptionInfo.ExpectedValue = area->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		MIHP_PlatformOnHeapCorruptionDetected(heap, corruptionInfo);
	}
}

void MIHP_ValidateHeapSegmentHeader(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment)
{
	uint32_t newChecksum;

#if MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_NONE
	return;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_MAGIC_NUMBER
	newChecksum = 0x55DEAD55;
#elif MIHP_HEAP_STRUCTURE_VALIDATION_TYPE == MIHP_HEAP_STRUCTURE_VALIDATION_TYPE_CHECKSUM
	newChecksum = MIHP_GenerateHeapSegmentChecksum(segment);
#else
	#error "Invalid heap structure validation type"
#endif

	if (segment->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH;
		corruptionInfo.Ptr = segment;
		corruptionInfo.ExpectedValue = segment->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		MIHP_PlatformOnHeapCorruptionDetected(heap, corruptionInfo);
	}
}

size_t MIHP_GetMinimalPayloadSize(const MIHP_Heap* heap)
{
	return heap->Config.AllocationAlignment;
}
