#ifndef _PATH_UTILS_H_
#define _PATH_UTILS_H_

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>

#include "rknn_app.h"

inline int folder_mkdirs(const char *folder_path)
{	
	if(!access(folder_path, F_OK)){
		return 0;
	}

	char path[256];
	char *path_buf = path;
	char temp_path[256];
	char *temp;
	int temp_len;
	
	memset(path, 0, sizeof(path));
	memset(temp_path, 0, sizeof(temp_path));
	strcat(path, folder_path);
	path_buf = path;

	while((temp = strsep(&path_buf, "/")) != NULL){
		temp_len = strlen(temp);	
		if(0 == temp_len){
			continue;
		}
		strcat(temp_path, temp);
		if(-1 == access(temp_path, F_OK)){
			if(-1 == mkdir(temp_path, 0777)){
				return 2;
			}
		}
        strcat(temp_path, "/");
	}
	return 0;
}

inline int replacechar(char *str, char orig, char rep) {
    char *ix = str;
    int n = 0;
    while((ix = strchr(ix, orig)) != NULL) {
        *ix++ = rep;
        n++;
    }
    return n;
}


static char* replace_not_allow_symbol(const char* input_str){
    char* output_str = (char*)malloc(strlen(input_str) + 1);
    memcpy(output_str, input_str, strlen(input_str) + 1);
    replacechar(output_str, '/', '_');
    return output_str;
}


static char* get_output_path(const char* base_name, const char* output_folder_path){
    char* base_name_replace_no_allow_char = replace_not_allow_symbol(base_name);
    char* output_path = (char*)malloc(strlen(output_folder_path) + strlen(base_name_replace_no_allow_char) + 20);
    sprintf(output_path, "%s/%s.npy", output_folder_path, base_name_replace_no_allow_char);
    free(base_name_replace_no_allow_char);
    return output_path;
}


static std::vector<std::string> split(const std::string& str, const std::string& pattern)
{
  std::vector<std::string> res;
  if (str == "")
    return res;
  std::string strs = str + pattern;
  size_t      pos  = strs.find(pattern);
  while (pos != strs.npos) {
    std::string temp = strs.substr(0, pos);
    res.push_back(temp);
    strs = strs.substr(pos + 1, strs.size());
    pos  = strs.find(pattern);
  }
  return res;
}

#endif