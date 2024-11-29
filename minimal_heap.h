#define USE_HEAP_STRUCTURE_CHECKSUM 1
#define USE_CANARY_VALIDATION 1

#define HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH 0
#define HEAP_CORRUPTION_TYPE_ALLOCATION_CHECKSUM_MISMATCH 1
#define HEAP_CORRUPTION_TYPE_FRONT_CANARY_MISMATCH 2
#define HEAP_CORRUPTION_TYPE_REAR_CANARY_MISMATCH 3

struct heap_memory_area
{
	unsigned long long size;
	unsigned long long num_occupied_allocations;
	heap_memory_area* next_area;
	heap_memory_area* previous_area;

#if USE_HEAP_STRUCTURE_CHECKSUM
	unsigned long long checksum;
#endif
} typedef heap_memory_area;

struct heap_allocation
{
	unsigned long long size;
	bool is_occupied;

	heap_allocation* next_allocation;
	heap_allocation* previous_allocation;
	heap_memory_area* encapsulating_memory_area;

#if USE_HEAP_STRUCTURE_CHECKSUM
	unsigned long long checksum;
#endif
} typedef heap_allocation;

struct heap_corruption_info
{
	char type;
	void* ptr;
	unsigned long long expected_value;
	unsigned long long actual_value;
} typedef heap_corruption_info;

/* Hooks to be implemented by the library user */
void* platform_request_memory_area(unsigned long long size);
bool platform_free_memory_area(void* ptr, unsigned long long size);
char platform_get_required_memory_alingment();
void platform_heap_corruption_detected(heap_corruption_info info);

unsigned long long config_minimal_memory_area_size();
unsigned long long config_minimum_amount_unoccupied_bytes();
unsigned long long config_minimum_amount_unoccupied_continues_bytes();
/* === */