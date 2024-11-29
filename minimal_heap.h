
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define USE_HEAP_STRUCTURE_CHECKSUM 1
#define USE_CANARY_VALIDATION 1

#define HEAP_CORRUPTION_TYPE_MEMORY_AREA_CHECKSUM_MISMATCH 0
#define HEAP_CORRUPTION_TYPE_SEGMENT_CHECKSUM_MISMATCH 1
#define HEAP_CORRUPTION_TYPE_FRONT_CANARY_MISMATCH 2
#define HEAP_CORRUPTION_TYPE_REAR_CANARY_MISMATCH 3

struct heap_stats
{
	size_t 				num_memory_areas;
	size_t 				num_total_segments;
	size_t 				num_total_occupied_segments;
	size_t 				total_size;
} typedef heap_stats;

struct heap_memory_area
{
	size_t 				size;
	size_t 				num_segments;
	size_t 				num_occupied_segments;
	heap_memory_area* 	next_area;
	heap_memory_area* 	previous_area;

#if USE_HEAP_STRUCTURE_CHECKSUM
	int64_t 			checksum;
#endif
} typedef heap_memory_area;

struct heap_segment
{
	size_t 				size;
	bool 				is_occupied;

	heap_segment* 		next_segment;
	heap_segment* 		previous_segment;
	heap_memory_area* 	encapsulating_memory_area;

#if USE_HEAP_STRUCTURE_CHECKSUM
	int64_t 			checksum;
#endif
} typedef heap_segment;

struct heap_corruption_info
{
	uint8_t 			type;
	void* 				ptr;
	int64_t 			expected_value;
	int64_t 			actual_value;
} typedef heap_corruption_info;

/* Hooks to be implemented by the library user */
void* platform_request_memory_area(size_t size);
bool platform_free_memory_area(void* ptr, size_t size);
uint8_t platform_get_required_memory_alingment();
void platform_heap_corruption_detected(heap_corruption_info info);

size_t config_minimal_memory_area_size();
size_t config_minimal_amount_unoccupied_bytes();
size_t config_minimal_amount_unoccupied_continuous_bytes();
/* === */