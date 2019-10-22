#ifndef FUSE_EXPERIMENT_H
#define FUSE_EXPERIMENT_H

#include <string>
#include <vector>

namespace Fuse {

	/*
	* TYPES
	*/

	typedef std::string Event;
	typedef std::vector<Event> Event_set;

	struct Sequence_part {
		Event_set overlapping;
		Event_set additional;
	};

	typedef std::vector<Sequence_part> Combination_sequence;

	/*
	* Free functions
	*/

	void initialize(std::string target_dir, bool debug);

	/*
	* Most Fuse functions apply to processing a target benchmark (directory)
	*/

	class Target {

		public:

			Target(std::string target_dir);
			~Target();

			// execute_sequence
			// combine_sequence
			// execute_references

	};

}

#endif
