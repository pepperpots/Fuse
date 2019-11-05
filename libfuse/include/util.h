#ifndef FUSE_UTIL_H
#define FUSE_UTIL_H

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace Fuse {
	namespace Util {
			
		template <typename T>
		inline std::string vector_to_string(std::vector<T> vector, std::string delim=","){
			std::stringstream ss;
			for(typename std::vector<T>::iterator it = vector.begin(); it != vector.end(); it++){
				ss << *it;
				if(std::distance(it,vector.end())>1)
					ss << delim;
			}
			return "[" + ss.str()+ "]";
		}

		void check_or_create_directory(std::string directory); 
		std::string check_or_create_directory_from_filename(std::string filename); 
		std::string get_directory_from_filename(const std::string& filename);
		std::vector<std::string> split_string_to_vector(const std::string& s, char delim);
	}
}

#endif
