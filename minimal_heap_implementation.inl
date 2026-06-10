#define MIHP_MIN(A, B) (A) < (B) ? (A) : (B)
#define MIHP_MAX(A, B) (A) > (B) ? (A) : (B)

#define MIHP_IS_PO2(Val) (Val != 0 && (Val & (Val - 1)) == 0)

MIHP_HeapConfig MIHP_MakeDefaultConfigPreset()
{
	MIHP_HeapConfig cfg = { 0 };

	cfg.MinMemoryAreaSize = 4096 * 32;
	cfg.MaxMemoryAreaSize = 4096 * 4096 * 128ull;

	cfg.MinTotalTraversalsBeforeHeapExpansion = 10'000;
	cfg.MinHeapTraversalPercentToExpand = 80;

	cfg.AllocationAlignment = 16;
	cfg.AllocationInitialValue = 0xbe;

	cfg.HeapValidationMode = MIHP_HVM_MagicNumber;

	return cfg;
}

bool MIHP_InitializeHeap(MIHP_Heap* heap, MIHP_HeapConfig config)
{
	if (heap == NULL)
		return false;

	if (MIHP_IsHeapInitialized(heap))
		return false;

	if (config.PlatformRequestMemoryFn == NULL)
		return false;

	if (config.PlatformFreeMemoryFn == NULL)
		return false;

	if (config.MinTotalTraversalsBeforeHeapExpansion < 10)
		return false;

	if (config.MinHeapTraversalPercentToExpand > 100)
		return false;

	if (config.AllocationAlignment < sizeof(size_t))
		return false;

	if (!MIHP_IS_PO2(config.AllocationAlignment))
		return false;

	if (config.MinMemoryAreaSize % config.AllocationAlignment != 0)
		return false;

	if (config.MaxMemoryAreaSize % config.AllocationAlignment != 0)
		return false;

	if (config.HeapLock != NULL)
	{
		if (config.LockHeapFn == NULL)
			return false;

		if (config.UnlockHeapFn == NULL)
			return false;
	}

	MIHP_MEMSET(heap, sizeof(MIHP_Heap), 0);

	heap->Config = config;
	heap->FirstArea = MIHP_CreateHeapMemoryArea(heap, config.MinMemoryAreaSize);
	heap->LastSuccessfulAllocationArea = heap->FirstArea;

	heap->FirstArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, heap->FirstArea);
	return true;
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

	if (size > heap->Config.MaxMemoryAreaSize)
		return NULL;

	MIHP_LockHeap(heap);

	MIHP_HeapMemoryAreaHeader* nextArea = heap->LastSuccessfulAllocationArea;
	do
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(heap, currentArea);
		nextArea = currentArea->NextArea;

		if (nextArea == NULL)
			nextArea = heap->FirstArea;

		if (currentArea->NumOccupiedSegments == currentArea->NumSegments)
			continue;

		size_t numTraversalsPerArea = MIHP_MAX(100, heap->Config.MinTotalTraversalsBeforeHeapExpansion * currentArea->NumSegments) / heap->Stats.NumTotalSegments;
		size_t numTraversedAreaSegments = 0;

		MIHP_HeapSegmentHeader* nextSegment = currentArea->LastSuccessfulAllocationSegment;
		do
		{
			if (numTraversedAreaSegments > numTraversalsPerArea)
				if (numTraversedAreaSegments * 100 > currentArea->NumSegments * heap->Config.MinHeapTraversalPercentToExpand)
					break;

			numTraversedAreaSegments++;

			MIHP_HeapSegmentHeader* currentSegment = nextSegment;
			MIHP_ValidateHeapSegmentHeader(heap, currentSegment);
			nextSegment = currentSegment->NextSegment;

			if (nextSegment == NULL)
				nextSegment = currentArea->FirstSegment;

			if (currentSegment->OccupiedSize != 0)
				continue;

			size_t segmentHeaderSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
			MIHP_HeapSegmentHeader* targetSegment = NULL;

			if (!MIHP_SplitHeapSegment(heap, currentSegment, segmentHeaderSize + size, &targetSegment))
			{
				if (currentSegment->SegmentSize >= segmentHeaderSize + size)
					targetSegment = currentSegment;
				else
					continue;
			}

			targetSegment->OccupiedSize = size;
			targetSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, targetSegment);

			// Set it to currentSegment as it may be a large segment we've split targetSegment of
			currentArea->LastSuccessfulAllocationSegment = currentSegment;

			currentArea->NumOccupiedSegments++;
			currentArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, currentArea);

			heap->Stats.NumTotalOccupiedSegments++;
			heap->LastSuccessfulAllocationArea = currentArea;

			MIHP_UnlockHeap(heap);

			void* data = MIHP_GetSegmentPayloadPtr(heap, targetSegment);
			MIHP_MEMSET(data, size, heap->Config.AllocationInitialValue);

			return data;
		} while (nextSegment != currentArea->LastSuccessfulAllocationSegment);
	} while (nextArea != heap->LastSuccessfulAllocationArea);

	/// 
	/// No free segment found (in time), allocate new area
	///
	
	size_t newAreaSize = heap->Stats.TotalSize / 4;  // growth-rate of 1.25

	while (newAreaSize < size)
		newAreaSize = MIHP_MIN(heap->Config.MaxMemoryAreaSize, newAreaSize + newAreaSize / 4);

	MIHP_HeapMemoryAreaHeader* newArea = MIHP_CreateHeapMemoryArea(heap, newAreaSize);
	if (newArea == NULL)
	{
		MIHP_UnlockHeap(heap);
		return NULL;
	}

	// Insert new area one after first area, since the first area is always kept and should be the smallest

	newArea->PreviousArea = heap->FirstArea;
	newArea->NextArea = heap->FirstArea->NextArea;

	if (heap->FirstArea->NextArea)
	{
		heap->FirstArea->NextArea->PreviousArea = newArea;
		heap->FirstArea->NextArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, heap->FirstArea->NextArea);
	}

	heap->FirstArea->NextArea = newArea;
	heap->FirstArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, heap->FirstArea);
	newArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, newArea);

	MIHP_UnlockHeap(heap);
	return MIHP_Allocate(heap, size);
}

void* MIHP_Reallocate(MIHP_Heap* heap, void* ptr, size_t newSize)
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

	MIHP_LockHeap(heap);

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
			segment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, segment);

			MIHP_UnlockHeap(heap);
			return ptr;
		}

		if (segment->NextSegment)
		{
			MIHP_ValidateHeapSegmentHeader(heap, segment->NextSegment);
			if (MIHP_MergeHeapSegments(heap, segment, segment->NextSegment))
			{
				MIHP_UnlockHeap(heap);
				return MIHP_Reallocate(heap, ptr, newSize);
			}
		}

		MIHP_UnlockHeap(heap);

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
		segment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, segment);

		size_t alignedNewSize = MIHP_GetHeapAlignedSize(heap, newSize);
		size_t newSplitSegmentSize = segment->SegmentSize - segmentHeaderSize - alignedNewSize;

		MIHP_HeapSegmentHeader* newSpittedSegment = NULL;
		MIHP_SplitHeapSegment(heap, segment, newSplitSegmentSize, &newSpittedSegment);

		if (newSpittedSegment && newSpittedSegment->NextSegment)
			MIHP_MergeHeapSegments(heap, newSpittedSegment, newSpittedSegment->NextSegment); // Try directly merging with the adjacent segment

		MIHP_UnlockHeap(heap);
		return ptr;
	}

	MIHP_UnlockHeap(heap);
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

	MIHP_LockHeap(heap);

	MIHP_HeapSegmentHeader* segment = MIHP_GetSegmentHeaderPtr(heap, ptr);
	MIHP_ValidateHeapSegmentHeader(heap, segment);

	MIHP_HeapMemoryAreaHeader* area = segment->OwningMemoryArea;
	MIHP_ValidateHeapMemoryAreaHeader(heap, area);

	if (segment->OccupiedSize == 0)
	{
		MIHP_HeapCorruptionInfo corruptionInfo = {0};
		corruptionInfo.Type = MIHP_HCE_DoubleFree;
		corruptionInfo.Ptr = ptr;

		if (heap->Config.OnHeapCorruptionDetectedFn != NULL)
			heap->Config.OnHeapCorruptionDetectedFn(heap, corruptionInfo);
		return false;
	}

	segment->OccupiedSize = 0;
	segment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, segment);
	
	area->NumOccupiedSegments--;
	area->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, area);

	heap->Stats.NumTotalOccupiedSegments--;

	if (area->NumOccupiedSegments == 0 && area != heap->FirstArea)
	{
		MIHP_HeapMemoryAreaHeader* prevArea = area->PreviousArea;
		MIHP_HeapMemoryAreaHeader* nextArea = area->NextArea;

		if (MIHP_DestroyHeapMemoryArea(heap, area))
		{
			if (prevArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(heap, prevArea);
				prevArea->NextArea = nextArea;
				prevArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, prevArea);
			}

			if (nextArea)
			{
				MIHP_ValidateHeapMemoryAreaHeader(heap, nextArea);
				nextArea->PreviousArea = prevArea;
				nextArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, nextArea);
			}

			if (heap->LastSuccessfulAllocationArea == area)
				heap->LastSuccessfulAllocationArea = heap->FirstArea;

			MIHP_UnlockHeap(heap);
			return true;
		}
	}

	if (segment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, segment->NextSegment);
		if (segment->NextSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(heap, segment, segment->NextSegment);
	}

	if (segment->PreviousSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, segment->PreviousSegment);
		if (segment->PreviousSegment->OccupiedSize == 0)
			MIHP_MergeHeapSegments(heap, segment->PreviousSegment, segment);
	}

	MIHP_UnlockHeap(heap);
	return true;
}

bool MIHP_MergeHeaps(MIHP_Heap* sourceHeap, MIHP_Heap* heapToAbsorb)
{
	if (sourceHeap == NULL)
		return false;

	if (heapToAbsorb == NULL)
		return false;

	if (!MIHP_IsHeapInitialized(sourceHeap))
		return false;

	if (!MIHP_IsHeapInitialized(heapToAbsorb))
		return false;

	if (sourceHeap->Config.PlatformFreeMemoryFn != heapToAbsorb->Config.PlatformFreeMemoryFn)
		return false;

	if (sourceHeap->Config.AllocationAlignment != heapToAbsorb->Config.AllocationAlignment)
		return false;

	if (sourceHeap->Config.MinMemoryAreaSize > heapToAbsorb->Config.MinMemoryAreaSize)
		return false;

	if (sourceHeap->Config.MaxMemoryAreaSize < heapToAbsorb->Config.MaxMemoryAreaSize)
		return false;

	if (sourceHeap->Config.HeapValidationMode != heapToAbsorb->Config.HeapValidationMode)
		return false;

	MIHP_LockHeap(sourceHeap);
	MIHP_LockHeap(heapToAbsorb);

	MIHP_HeapMemoryAreaHeader* sourceEndArea = sourceHeap->FirstArea;
	while (true)
	{
		MIHP_ValidateHeapMemoryAreaHeader(sourceHeap, sourceEndArea);
		if (sourceEndArea->NextArea == NULL)
			break;

		sourceEndArea = sourceEndArea->NextArea;
	}

	sourceEndArea->NextArea = heapToAbsorb->FirstArea;
	heapToAbsorb->FirstArea->PreviousArea = sourceEndArea;

	heapToAbsorb->FirstArea = NULL;

	sourceHeap->Stats.NumMemoryAreas += heapToAbsorb->Stats.NumMemoryAreas;
	sourceHeap->Stats.NumTotalSegments += heapToAbsorb->Stats.NumTotalSegments;
	sourceHeap->Stats.NumTotalOccupiedSegments += heapToAbsorb->Stats.NumTotalOccupiedSegments;
	sourceHeap->Stats.TotalSize += heapToAbsorb->Stats.TotalSize;

	MIHP_UnlockHeap(heapToAbsorb);
	MIHP_MEMSET(heapToAbsorb, sizeof(MIHP_Heap), 0);

	sourceEndArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(sourceHeap, sourceEndArea);
	sourceEndArea->NextArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(sourceHeap, sourceEndArea->NextArea);
	MIHP_UnlockHeap(sourceHeap);
	return true;
}

bool MIHP_IsPointerInHeap(MIHP_Heap* heap, void* ptr)
{
	if (heap == NULL)
		return false;

	MIHP_LockHeap(heap);

	uintptr_t searchingPtr = (uintptr_t)ptr;

	MIHP_HeapMemoryAreaHeader* nextArea = heap->FirstArea;
	while (nextArea)
	{
		MIHP_HeapMemoryAreaHeader* currentArea = nextArea;
		MIHP_ValidateHeapMemoryAreaHeader(heap, currentArea);
		nextArea = currentArea->NextArea;
		
		uintptr_t ptrMin = (uintptr_t)currentArea;
		uintptr_t ptrMax = (uintptr_t)currentArea + currentArea->AreaSize;

		if (searchingPtr >= ptrMin && searchingPtr <= ptrMax)
		{
			MIHP_UnlockHeap(heap);
			return true;
		}
	}

	MIHP_UnlockHeap(heap);
	return false;
}

size_t MIHP_GetPtrAllocationSize(MIHP_Heap* heap, void* ptr)
{
	if (!MIHP_IsPointerInHeap(heap, ptr))
		return 0;

	MIHP_LockHeap(heap);

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

void MIHP_LockHeap(MIHP_Heap* heap)
{
	if (heap->Config.HeapLock)
		heap->Config.LockHeapFn(heap, heap->Config.HeapLock);
}

void MIHP_UnlockHeap(MIHP_Heap* heap)
{
	if (heap->Config.HeapLock)
		heap->Config.UnlockHeapFn(heap, heap->Config.HeapLock);
}

MIHP_HeapMemoryAreaHeader* MIHP_CreateHeapMemoryArea(MIHP_Heap* heap, size_t requestedSize)
{
	MIHP_ASSERT(heap);

	size_t effectiveSize = ((requestedSize + heap->Config.MinMemoryAreaSize - 1) / heap->Config.MinMemoryAreaSize) * heap->Config.MinMemoryAreaSize;
	effectiveSize = MIHP_MAX(effectiveSize, heap->Config.MinMemoryAreaSize);
	effectiveSize = MIHP_MIN(effectiveSize, heap->Config.MaxMemoryAreaSize);

	size_t actualSize = 0;
	MIHP_HeapMemoryAreaHeader* area = (MIHP_HeapMemoryAreaHeader*)heap->Config.PlatformRequestMemoryFn(heap, effectiveSize, &actualSize);
	if (area == NULL)
		return NULL;

	MIHP_ASSERT(actualSize >= effectiveSize);
	MIHP_MEMSET(area, sizeof(MIHP_HeapMemoryAreaHeader), 0);

	area->InstigatingHeap = heap;
	area->AreaSize = actualSize;
	area->Checksum = ~0;

	size_t headerSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapMemoryAreaHeader));
	area->FirstSegment = MIHP_InitializeHeapSegment(heap, area, (char*)area + headerSize, area->AreaSize - headerSize);
	area->LastSuccessfulAllocationSegment = area->FirstSegment;
	area->FirstSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, area->FirstSegment);

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

	if (!heap->Config.PlatformFreeMemoryFn(memoryArea->InstigatingHeap, memoryArea, memoryArea->AreaSize))
		return false;

	heap->Stats.NumTotalSegments -= areaNumSegments;
	heap->Stats.NumTotalOccupiedSegments -= areaNumOccupiedSegments;
	heap->Stats.NumMemoryAreas--;
	heap->Stats.TotalSize -= areaSize;

	return true;
}

MIHP_HeapSegmentHeader* MIHP_InitializeHeapSegment(MIHP_Heap* heap, MIHP_HeapMemoryAreaHeader* area, void* segmentStart, size_t segmentSize)
{
	MIHP_ASSERT(area);
	MIHP_ASSERT(segmentStart);

	MIHP_HeapSegmentHeader* segment = (MIHP_HeapSegmentHeader*)segmentStart;

	MIHP_MEMSET(segment, sizeof(MIHP_HeapSegmentHeader), 0);

	segment->SegmentSize = segmentSize;
	segment->OwningMemoryArea = area;
	segment->Checksum = ~0;

	area->NumSegments++;
	area->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, area);

	heap->Stats.NumTotalSegments++;

	return segment;
}

bool MIHP_UninitializeHeapSegment(MIHP_Heap* heap, MIHP_HeapSegmentHeader* segment)
{
	MIHP_ASSERT(segment);

	if (segment->OccupiedSize > 0)
		return false;

	segment->OwningMemoryArea->NumSegments--;
	segment->OwningMemoryArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, segment->OwningMemoryArea);
	heap->Stats.NumTotalSegments--;

	MIHP_MEMSET(segment, sizeof(MIHP_HeapSegmentHeader), 0);

	return true;
}

bool MIHP_SplitHeapSegment(MIHP_Heap* heap, MIHP_HeapSegmentHeader* sourceSegment, size_t newSegmentMinSize, MIHP_HeapSegmentHeader** outNewSegment)
{
	MIHP_ASSERT(sourceSegment);

	size_t headerSize = MIHP_GetHeapAlignedSize(heap, sizeof(MIHP_HeapSegmentHeader));
	newSegmentMinSize = MIHP_MAX(MIHP_GetMinimalPayloadSize(heap) + headerSize, newSegmentMinSize);

	size_t effectiveNewSegmentSize = MIHP_GetHeapAlignedSize(heap, newSegmentMinSize);

	if (sourceSegment->SegmentSize < effectiveNewSegmentSize)
		return false;

	size_t sourceResultingSegmentSize = sourceSegment->SegmentSize - effectiveNewSegmentSize;

	if (sourceResultingSegmentSize < MIHP_GetMinimalPayloadSize(heap) + headerSize)
		return false;
	
	if (sourceResultingSegmentSize < sourceSegment->OccupiedSize + headerSize)
		return false;

	sourceSegment->SegmentSize = sourceResultingSegmentSize;

	MIHP_HeapSegmentHeader* newSegment = MIHP_InitializeHeapSegment(heap, sourceSegment->OwningMemoryArea, (char*)sourceSegment + sourceResultingSegmentSize, effectiveNewSegmentSize);
	newSegment->NextSegment = sourceSegment->NextSegment;
	newSegment->PreviousSegment = sourceSegment;

	if (sourceSegment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = newSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, sourceSegment->NextSegment);
	}

	sourceSegment->NextSegment = newSegment;

	newSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, newSegment);
	sourceSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, sourceSegment);

	*outNewSegment = newSegment;
	return true;
}

bool MIHP_MergeHeapSegments(MIHP_Heap* heap, MIHP_HeapSegmentHeader* sourceSegment, MIHP_HeapSegmentHeader* segmentToAbsorb)
{
	MIHP_ASSERT(sourceSegment);
	MIHP_ASSERT(segmentToAbsorb);
	MIHP_ASSERT(sourceSegment->NextSegment == segmentToAbsorb);

	if (segmentToAbsorb->OccupiedSize > 0)
		return false;

	sourceSegment->NextSegment = segmentToAbsorb->NextSegment;

	if (sourceSegment->NextSegment)
	{
		MIHP_ValidateHeapSegmentHeader(heap, sourceSegment->NextSegment);
		sourceSegment->NextSegment->PreviousSegment = sourceSegment;
		sourceSegment->NextSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, sourceSegment->NextSegment);
	}

	MIHP_ValidateHeapMemoryAreaHeader(heap, sourceSegment->OwningMemoryArea);
	if (sourceSegment->OwningMemoryArea->LastSuccessfulAllocationSegment == segmentToAbsorb)
	{
		sourceSegment->OwningMemoryArea->LastSuccessfulAllocationSegment = sourceSegment;
		sourceSegment->OwningMemoryArea->Checksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, sourceSegment->OwningMemoryArea);
	}

	sourceSegment->SegmentSize += segmentToAbsorb->SegmentSize;
	MIHP_UninitializeHeapSegment(heap, segmentToAbsorb);

	sourceSegment->Checksum = MIHP_GenerateHeapSegmentChecksum(heap, sourceSegment);

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

uint32_t MIHP_GenerateHeapMemoryAreaChecksum(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area)
{
	if (heap->Config.HeapValidationMode == MIHP_HVM_None)
		return 0;

	if (heap->Config.HeapValidationMode == MIHP_HVM_MagicNumber)
		return 0xAADEADAA;

	MIHP_ASSERT(area);

	// Do not include the checksum itself into the new checksum
	size_t size = offsetof(MIHP_HeapMemoryAreaHeader, Checksum);
	return MIHP_HashMemoryRegion(area, size);
}

uint32_t MIHP_GenerateHeapSegmentChecksum(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment)
{
	if (heap->Config.HeapValidationMode == MIHP_HVM_None)
		return 0;

	if (heap->Config.HeapValidationMode == MIHP_HVM_MagicNumber)
		return 0x55DEAD55;

	MIHP_ASSERT(segment);

	// Do not include the checksum itself into the new checksum
	size_t size = offsetof(MIHP_HeapSegmentHeader, Checksum);
	return MIHP_HashMemoryRegion(segment, size);
}

void MIHP_ValidateHeapMemoryAreaHeader(const MIHP_Heap* heap, const MIHP_HeapMemoryAreaHeader* area)
{
	if (heap->Config.OnHeapCorruptionDetectedFn == NULL)
		return;

	uint32_t newChecksum;

	if (heap->Config.HeapValidationMode == MIHP_HVM_None)
		return;

	if (heap->Config.HeapValidationMode == MIHP_HVM_MagicNumber)
		newChecksum = 0xAADEADAA;
	else if (heap->Config.HeapValidationMode == MIHP_HVM_Checksum)
		newChecksum = MIHP_GenerateHeapMemoryAreaChecksum(heap, area);
	else
		MIHP_ASSERT(false);

	if (area->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HCE_MemoryAreaChecksumMismatch;
		corruptionInfo.Ptr = area;
		corruptionInfo.ExpectedValue = area->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		heap->Config.OnHeapCorruptionDetectedFn(heap, corruptionInfo);
	}
}

void MIHP_ValidateHeapSegmentHeader(const MIHP_Heap* heap, const MIHP_HeapSegmentHeader* segment)
{
	if (heap->Config.OnHeapCorruptionDetectedFn == NULL)
		return;

	uint32_t newChecksum;

	if (heap->Config.HeapValidationMode == MIHP_HVM_None)
		return;

	if (heap->Config.HeapValidationMode == MIHP_HVM_MagicNumber)
		newChecksum = 0x55DEAD55;
	else if (heap->Config.HeapValidationMode == MIHP_HVM_Checksum)
		newChecksum = MIHP_GenerateHeapSegmentChecksum(heap, segment);
	else
		MIHP_ASSERT(false);

	if (segment->Checksum != newChecksum)
	{
		MIHP_HeapCorruptionInfo corruptionInfo;
		corruptionInfo.Type = MIHP_HCE_SegmentChecksumMismatch;
		corruptionInfo.Ptr = segment;
		corruptionInfo.ExpectedValue = segment->Checksum;
		corruptionInfo.ActualValue = newChecksum;

		heap->Config.OnHeapCorruptionDetectedFn(heap, corruptionInfo);
	}
}

size_t MIHP_GetMinimalPayloadSize(const MIHP_Heap* heap)
{
	return MIHP_MAX(sizeof(MIHP_HeapSegmentHeader), heap->Config.AllocationAlignment);
}
