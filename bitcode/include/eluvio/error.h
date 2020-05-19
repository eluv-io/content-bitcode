#pragma once
#include <nlohmann/json.hpp>
#include "eluvio/el_constants.h"
#include <string>

namespace eluvio_errors{

    typedef std::string kind;
    typedef std::string error;

    typedef struct ErrorKinds{
        static kind Other;
        static kind NotImplemented;
        static kind Invalid;
        static kind Permission;
        static kind IO;
        static kind Exist;
        static kind NotExist;
        static kind IsDir;
        static kind NotDir;
        static kind Finalized;
        static kind NotFinalized;
        static kind BadHttpParams;
    }ErrorKinds;

    kind ErrorKinds::Other = "unclassified error";                      // Unclassified error. This value is not printed in the error message.
    kind ErrorKinds::NotImplemented = "not implemented";                // The functionality is not yet implemented.
    kind ErrorKinds::Invalid = "invalid";                               // Invalid operation for this type of item.
    kind ErrorKinds::Permission = "permission denied";                  // Permission denied.
    kind ErrorKinds::IO = "I/O error";                                  // External I/O error such as network failure.
    kind ErrorKinds::Exist = "item already exists";                     // Item already exists.
    kind ErrorKinds::NotExist = "item does not exist";                  // Item does not exist.
    kind ErrorKinds::IsDir = "item is a directory";                     // Item is a directory.
    kind ErrorKinds::NotDir = "item is not a directory";                // Item is not a directory.
    kind ErrorKinds::Finalized = "item is already finalized";           // Part or content is already finalized.
    kind ErrorKinds::NotFinalized = "item is not finalized";            // Part or content is not yet finalized.
    kind ErrorKinds::BadHttpParams = "Invalid Http params specified";   // Bitcode call with invalid HttpParams

    class Error : public ErrorKinds{
    public:
        Error(bool is_error) : _is_error(is_error){}
        template <typename... remainder>
        Error(std::string p1, remainder... rem) : _is_error(true){
            Fields["op"] = p1;
            init(rem...);
        }
        template <typename... remainder>
        Error(const char* p1, remainder... rem) : _is_error(true){
            Fields["op"] = p1;
            init(rem...);
        }
        Error(std::string msg) : _is_error(true){
            Fields["op"] = msg;
        }
        Error(const char* msg) : _is_error(true){
            Fields["op"] = msg;
        }
        // template <typename... remainder>
        // Error(int p1, remainder... rem){}
        // Cause sets the given original error and returns this error
        //instance for call chaining.
        Error& Cause(error err){
            if (err != ""){
                Fields["cause"] = err;
            }
            return *this;
        }
        Error& Kind(kind k){
            if (k != ""){
                Fields["kind"] = k;
            }
            return *this;
        }
        bool IsUnmarshalled() { return unmarshalled;}
        bool IsError() { return _is_error;}
        bool Is(kind expected){
            auto k = Fields.find("kind");
            if (k == Fields.end()){
                return false;
            }
            return Fields["kind"] == expected;
        }
        // With adds additional context information in the form of key value pairs
        // and returns this error instance for call chaining.
        template <typename ...Args>
        Error& With(Args&... args){
            init(args...);
            return *this;
        }
        // Get the current error as a string
        nlohmann::json getJSON(){
            nlohmann::json j(Fields);
            return j;
        }
        std::map<std::string, std::string> Fields;

    private:
        template <typename T, typename... remainder>
        void init(std::string key, T value, remainder... rem){
            Fields[key] = std::to_string(value);
            init(rem...);
        }
        template <typename... remainder>
        void init(std::string key, std::string value, remainder... rem){
            Fields[key] = value;
            init(rem...);
        }

        template <typename... remainder>
        void init(const char* key, const char* value, remainder... rem){
            Fields[key] = value;
            init(rem...);
        }

        template <typename... remainder>
        void init(std::string key, int value, remainder... rem){
            Fields[key] = value;
            init(rem...);
        }

        template <typename... remainder>
        void init(const char* key, int value, remainder... rem){
            Fields[key] = std::to_string(value);
            init(rem...);
        }

        void init(std::string key, std::string value){
            Fields[key] = value;
        }

        void init(const char* key, const char* value){
            Fields[key] = value;
        }

        void init(std::string key, int value){
            Fields[key] = value;
        }

        void init(const char* key, int value){
            Fields[key] = std::to_string(value);
        }

        void init(std::string p1){
            Fields[p1] = "<empty>";
        }
        void init(const char* p1){
            Fields[p1] = "<empty>";
        }
        bool unmarshalled;
        bool _is_error;
    };
};

using E = eluvio_errors::Error;
