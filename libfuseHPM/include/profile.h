#ifndef FUSE_PROFILE_H
#define FUSE_PROFILE_H

#include "fuse_types.h"

#include "boost/icl/interval_map.hpp"

namespace Fuse {

	class Trace;
	class Trace_aftermath_legacy;
	class Trace_aftermath;

	class Execution_profile {

		private:
			std::map<Fuse::Symbol, std::vector<Fuse::Instance_p> > instances; // Symbols mapped to instances of that symbol
			std::string tracefile;
			std::string benchmark;

			Fuse::Event_set events;
			Fuse::Event_set filtered_events; // If this is populated, then only these counter-events will be loaded

			// If loaded, each instance maps to [instances that it depends on, instances that depend on it]
			std::map<
					Fuse::Instance_p,
					std::pair<std::set<Fuse::Instance_p>,std::set<Fuse::Instance_p> >
				> instance_dependencies;

			friend class Fuse::Trace;
			friend class Fuse::Trace_aftermath_legacy;
			friend class Fuse::Trace_aftermath;

		public:

			Execution_profile(
				std::string tracefile,
				std::string benchmark_binary,
				Fuse::Event_set filtered_events = Fuse::Event_set()
			);

			~Execution_profile();
			std::string get_tracefile_name();

			void load_from_tracefile(Fuse::Runtime runtime = Fuse::Runtime::ALL, bool load_communication_matrix = false);
			void print_to_file(std::string output_file);
			void dump_instance_dependencies(std::string output_file);
			void dump_instance_dependencies_dot(std::string output_file);

			std::vector<Fuse::Symbol> get_unique_symbols(bool include_runtime);
			Fuse::Event_set get_unique_events();

			std::vector<Fuse::Instance_p> get_instances(
				bool include_runtime,
				const std::vector<Fuse::Symbol> symbols = std::vector<Fuse::Symbol>()
			);

			std::map<std::string, std::vector<std::vector<int64_t> > > get_value_distribution(
				Fuse::Event_set events,
				bool include_runtime,
				const std::vector<Fuse::Symbol> symbols = std::vector<Fuse::Symbol>()
			);

			void add_instance(Fuse::Instance_p instance);
			void add_event(Fuse::Event event);

	};

}

#endif
