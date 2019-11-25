#ifndef FUSE_UTIL_H
#define FUSE_UTIL_H

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace Fuse {
	namespace Util {

		template <typename T>
		inline std::string vector_to_string(std::vector<T> vector, bool enclose = true, std::string delim = ","){
			std::stringstream ss;
			for(typename std::vector<T>::iterator it = vector.begin(); it != vector.end(); it++){
				ss << *it;
				if(std::distance(it,vector.end())>1)
					ss << delim;
			}
			if(enclose)
				return "[" + ss.str() + "]";
			else
				return ss.str();
		}

		template<class Type>
    inline std::vector<std::vector<Type> > get_unique_combinations(std::vector<Type> list, int k){

			std::vector<std::vector<Type> > combinations;
			auto n = list.size();

			/*
			* make a selector array, with combination_size values as true and the rest false
			* then get each permutation of this selector array using std::prev_permutation
			* for each permutation of the selector array (where the true elements will be the selected list),
			* select and combine these elements
			*/
			std::vector<bool> selectors(n);
			std::fill(selectors.begin(), selectors.end() - n + k, true);

			do {
				std::vector<Type> current_combination;
				for(int i = 0; i < n; ++i){
					if(selectors[i]){
						current_combination.push_back(list.at(i));
					}
				}
				combinations.push_back(current_combination);

			} while(std::prev_permutation(selectors.begin(), selectors.end()));

			return combinations;
    }

		bool check_file_existance(std::string filename);
		void check_or_create_directory(std::string directory);
		std::string check_or_create_directory_from_filename(std::string filename);
		std::string get_directory_from_filename(const std::string& filename);
		std::string get_filename_from_full_path(const std::string& fully_qualified);
		std::vector<std::string> split_string_to_vector(const std::string& s, char delim);
		std::string lowercase(const std::string str);
		std::vector<std::string> vector_to_lowercase(const std::vector<std::string> word_list);
		std::string uppercase(const std::string str);
		std::vector<std::string> vector_to_uppercase(const std::vector<std::string> word_list);

	}
}

#endif
