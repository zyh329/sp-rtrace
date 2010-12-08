#ifndef _TOTALS_GENERATOR_H_
#define _TOTALS_GENERATOR_H_

#include "timeline.h"
#include "report_generator.h"
#include "report_data.h"
#include "event.h"
#include "plotter.h"


/**
 * The TotalsGenerator class generates 'totals' report.
 * 
 * The 'totals' report contains graph displaying the total resource
 * allocation over the time.
 */
class TotalsGenerator: public ReportGenerator {
private:
	/**
	 * Allocation statistics for various snapshots.
	 */
	struct Stats {
		/**
		 * Statistics data for a single snapshot.
		 */
		struct Data {
			unsigned int count;
			unsigned int size;

			/**
			 * Create a new class instance.
			 */
			Data() : count(0), size(0) {
			}
			
			/**
			 * Adds allocated resource to statistics data
			 * 
			 * @param[in] size_new   the size of the allocated resource.
			 */
			void add(unsigned int size_new) {
				count++;
				size += size_new;
			}
			
			/**
			 * Removes freed resource to statistics data
			 * 
			 * @param[in] size_new   the size of the allocated resource.
			 */
			void remove(unsigned int size_new) {
				count--;
				size -= size_new;
			}
		};

		// total unfreed allocation data
		Data end_leaks;
		// unfreed allocation data at the peak time
		Data peak_leaks;
		// total allocation data.
		Data end_totals;
		// total allocation data at unfreed allocation peak time
		Data peak_totals;
		timestamp_t peak_timestamp;

		/**
		 * Creates a new class instance.
		 */
		Stats() : peak_timestamp(0) {
		}
	};

	
	/**
	 * Context related data container
	 */
	class ContextData : public ContextDataBase {
	public:
		// total allocation size data file
		Plotter::DataFile* file_totals;
		// total allocation size
		unsigned int total;

		/**
		 * Creates a new class instance.
		 */
		ContextData()
			: total(0), file_totals(0) {
		}
	};

	/**
	 * Resource related data container
	 */
	class ResourceData: public ResourceDataBase<ContextData> {
	public:
		// resource allocation statistics
		Stats stats;
		// resource allocation overhead
		unsigned int overhead;
		// the allocation overhead data file
		Plotter::DataFile* file_overhead;

		/**
		 * Creates a new class instance.
		 */
		ResourceData() : overhead(0), file_overhead(NULL) {
		}
	};

	// the used resource types
	ReportData<ResourceData> resources;

public:
	// X axis range
	int xrange_min;
	int xrange_max;
	// Y axis range
	int yrange_min;
	int yrange_max;

	/**
	 * Creates a new class instance.
	 */
	TotalsGenerator()
		: ReportGenerator("totals"), xrange_min(-1), xrange_max(0), yrange_min(0), yrange_max(0) {
	}

	/**
	 * @copydoc ReportGenerator::reportAlloc
	 */
	void reportAlloc(const Resource* resource, event_ptr_t& event);
	
	/**
	 * @copydoc ReportGenerator::reportAllocInContext
	 */
	void reportAllocInContext(const Resource* resource, const Context* context, event_ptr_t& event);
	
	/**
	 * @copydoc ReportGenerator::reportFree
	 */
	void reportFree(const Resource* resource, event_ptr_t& event, event_ptr_t& alloc_event);
	
	/**
	 * @copydoc ReportGenerator::reportFreeInContext
	 */
	void reportFreeInContext(const Resource* resource, const Context* context, event_ptr_t& event, event_ptr_t& alloc_event);
	
	/**
	 * @copydoc ReportGenerator::reportUnfreedAlloc
	 */
	void reportUnfreedAlloc(const Resource* resource, event_ptr_t& event) {
	}

	/**
	 * @copydoc ReportGenerator::finalize
	 */
	void finalize();
};


#endif