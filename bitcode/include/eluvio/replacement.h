#pragma once
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <ftw.h>

#include <string>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

typedef std::map<std::string, std::string> replacement_map;
class replacer{
public:
    static std::string& replace_all(std::string& buf, replacement_map& replacements, replacement_map& missing){
        for (auto el : replacements){
            auto real_key = "${" + el.first + "}";
            replace_all_in_place(buf, real_key, el.second, missing);
        }
        return buf;
    }
protected:
    static void replace_all_in_place(std::string& subject, const std::string& search,
                            const std::string& replace, replacement_map& missing) {
        size_t pos = 0;
        while ((pos = subject.find(search, pos)) != std::string::npos) {
            subject.replace(pos, search.length(), replace);
            pos += replace.length();
        }
        if (pos == 0){
            missing[search] = replace;
        }

    }
};