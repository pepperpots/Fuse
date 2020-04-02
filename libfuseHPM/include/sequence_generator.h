#ifndef FUSE_SEQUENCE_GEN_H
#define FUSE_SEQUENCE_GEN_H

#include "fuse_types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Fuse {

	namespace Sequence_generator {

		class Node;
		typedef std::shared_ptr<Node> Node_p;
		
		std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > generate_bc_sequence(
			Fuse::Target& target,
			unsigned int max_linking_events,
			unsigned int num_repeat_combinations
		);

		std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > >
			compute_best_possible_amis(
				Fuse::Target& target,
				std::vector<Fuse::Event_set> reference_pairs,
				std::map<unsigned int, double> pairwise_mi_values,
				unsigned int max_linking_events
		);

		std::vector<std::pair<Fuse::Event_set, std::string> >
			load_previous_profiled_event_sets(
				std::string filename
		);

		std::vector<std::pair<std::string, std::string> >
			load_previous_combinations(
				std::string filename
		);

		Node_p get_tree_root(
			Fuse::Target& target,
			Fuse::Event_set target_events,
			std::vector<Fuse::Event_set> event_pairs,
			std::map<unsigned int, double> pairwise_mi_values
		);

		std::vector<Node_p> get_child_nodes(
			Node_p parent_node,
			Fuse::Target& target,
			Fuse::Event_set target_events,
			int num_pmc,
			int max_linking_events,
			std::vector<Fuse::Event_set> event_pairs,
			std::map<unsigned int, double> pairwise_mi_values,
			std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event
		);

		std::vector<Node_p> compute_leaf_node(
			Fuse::Target& target,
			Node_p node,
			Fuse::Event_set target_events,
			unsigned int num_pmc,
			unsigned int max_linking_events,
			unsigned int num_repeat_combinations,
			std::vector<Fuse::Event_set> reference_pairs,
			std::map<unsigned int, double> pairwise_mi_values,
			std::map<Fuse::Event, std::vector<std::pair<double, Fuse::Event_set> > > best_possible_amis_per_event,
			std::vector<std::pair<double, Node_p> >& nodes_by_epd,
			std::vector<std::pair<double, Node_p> >& nodes_by_cross_profile_epd,
			std::vector<std::pair<double, Node_p> >& nodes_by_tmd_mse,
			std::vector<std::pair<double, Node_p> >& nodes_by_cross_profile_tmd_mse,
			std::vector<std::pair<Fuse::Event_set, std::string> >& profiled_event_sets,
			std::vector<std::pair<std::string, std::string> >& recorded_combinations
		);

		void prune_priority_list(
			std::vector<std::pair<double, Node_p> >* priority_list,
			std::vector<std::pair<Fuse::Event_set, double> > previously_evaluated_nodes
		);

		void save_previously_profiled_event_sets(
			std::string mappings_filename,
			std::vector<std::pair<Fuse::Event_set, std::string> > profiled_event_sets
		);

		void save_previous_combinations(
			std::string mappings_filename,
			std::vector<std::pair<std::string, std::string> > recorded_combinations
		);

		class Node {

			public:

			Fuse::Event_set sorted_combined_events;

			// TODO the combination spec should be of type Combination_sequence, as used in the rest of libFuseHPM
			std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > combination_spec;

			std::vector<Fuse::Profile_p> execution_profiles;
			std::vector<std::string> filenames;

			std::map<unsigned int, double> tmds; // per reference index
			std::vector<unsigned int> cross_profile_reference_indexes;
			std::vector<unsigned int> within_profile_reference_indexes;

			bool executed;
			bool loaded;
			bool combined;
			bool evaluated;

			double epd;
			double tmd_mse;
			double cross_profile_epd;
			double cross_profile_tmd_mse;
		
			Node(
				Fuse::Event_set all_combined_events,
				std::vector<std::pair<Fuse::Event_set, Fuse::Event_set> > combination_spec,
				std::map<unsigned int, double> tmds
			);

			/* This finds or executes the profiles, so that the node's filenames vector
			*  points to proper tracefiles (if executed but not merged) or merged profiles (if merged)
			*/
			void find_or_execute_profiles(
				Fuse::Target& target,
				unsigned int num_repeat_combinations,
				std::vector<std::pair<Event_set, std::string> >& profiled_event_sets
			);

			// If the node is combined, then this will load the node's result combined profiles
			// If the node is not yet combined, then this will load the node's execution profiles for combination
			void load_node_profiles(
				Fuse::Target& target,
				unsigned int num_repeat_combinations
			);

			unsigned int combine_and_evaluate_node_profiles(
				Node_p parent_node,
				Fuse::Target& target,
				std::vector<std::pair<std::string, std::string> >& recorded_combinations,
				std::vector<Fuse::Event_set> reference_pairs
			);

			void analyse_accuracy_and_compute_metrics(
				Fuse::Target& target,
				Fuse::Event_set previously_combined_events,
				std::vector<Fuse::Event_set> reference_pairs
			);

			std::string get_combination_spec_as_string();
			
			private:

			void update_profile_indexes(
				std::vector<Fuse::Event_set> reference_pairs
			);
		
			void compute_resulting_metrics(
				std::vector<Fuse::Event_set> reference_pairs
			);


		};

	}

}

#endif
