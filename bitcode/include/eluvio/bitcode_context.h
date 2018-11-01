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
#include <sys/socket.h>    //socket
#include <arpa/inet.h> //inet_addr
#include <netdb.h> //hostent
#include "eluvio/argutils.h"
#include "eluvio/utils.h"

extern "C" char*    JPC(char*);
extern "C" char*    Write(char*, char*, char*,int);
extern "C" char*    Read(char*, char*, char*,int);

using namespace std;

namespace elv_context{


	std::string string_format(const std::string fmt_str, ...) {
		int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
		std::unique_ptr<char[]> formatted;
		va_list ap;
		while(1) {
			formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
			strcpy(&formatted[0], fmt_str.c_str());
			va_start(ap, fmt_str);
			final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
			va_end(ap);
			if (final_n < 0 || final_n >= n)
				n += abs(final_n - n + 1);
			else
				break;
		}
		return std::string(formatted.get());
	}

	class GlobalCleanup{
	public:
		static bool shouldCleanup() {return true;}
	};

	typedef std::map<std::string,std::string> context_map_type;

	using nlohmann::json;
	class BitCodeCallContext; // forward

	typedef nlohmann::json JPCParams;

	class HttpParams{
	public:
		HttpParams(){}
		std::pair<std::string,int> Init(JPCParams& j_params){
			try{
				if (j_params.find("http") != j_params.end()){
					std::cout << j_params.dump() << std::endl;
					auto j_http = j_params["http"];
					_verb = j_http["verb"];
					_path = j_http["path"];
					_headers = j_http["headers"];
					auto& q = j_http["query"];

					for (auto& element : q.items()) {
						_map[element.key()] = element.value()[0];
					}
					return make_pair("success", 0);
				}
				return make_pair("could not find http in parameters", -1);
			}
			catch (json::exception& e)
			{
				return make_pair(e.what(), e.id);
			}
		}
		std::map<std::string, std::string>	_map;
		std::string _verb;
		std::string _path;
		nlohmann::json _headers;
	};




	typedef std::pair<nlohmann::json,int> (*call_impl)(BitCodeCallContext*, JPCParams&);
	static std::map<std::string, std::unique_ptr<BitCodeCallContext>> _context_map;

	struct ModuleMap{
		const char* external_name;
		call_impl fn;
	};

	static ModuleMap* get_module_map();

	int get_module_map_size(){
		ModuleMap* p = get_module_map();
		int i=0;
		while(p[i].fn != 0) i++;
		return i;
	}

	#define BEGIN_MODULE_MAP() static elv_context::ModuleMap* elv_context::get_module_map() { static elv_context::ModuleMap _map[] = {
	#define MODULE_MAP_ENTRY(pfn) {#pfn, pfn},
	#define END_MODULE_MAP() {"", (elv_context::call_impl)0} }; \
	return _map; \
	}

	/* Log macros allow super-simple, structured logging (see doc/logging.md).
	 *
	 * The varargs in the macro definition are optional and can be of any type:
	 * strings, ints, floats, etc. (they are converted to a JSON array and
	 * passed to Go for actual logging). However, they should always come in
	 * key-value pairs, where the key is a string and the value is of an
	 * arbitrary type.
	 *
	 * Examples:
	 * LOG(ctx, "just a simple message")
	 * LOG(ctx, "invalid ID", "ID", id)
	 * LOG(ctx, "invalid ID", "ID", id, "qhash", hash, "iteration", i)
	 */
	#define LOG_DEBUG(CTX, MSG, ...)\
			{\
				nlohmann::json __fields = { __VA_ARGS__ };\
				CTX->Debug(MSG, __fields);\
			}
	#define LOG_INFO(CTX, MSG, ...)\
			{\
				nlohmann::json __fields = { __VA_ARGS__ };\
				CTX->Info(MSG, __fields);\
			 }
	#define LOG_WARN(CTX, MSG, ...)\
			{\
				nlohmann::json __fields = { __VA_ARGS__ };\
				CTX->Warn(MSG, __fields);\
			 }
	#define LOG_ERROR(CTX, MSG, ...)\
			{\
				nlohmann::json __fields = { __VA_ARGS__ };\
				CTX->Error(MSG, __fields);\
			 }

	#include "utils.h"



	call_impl get_callee(const char* fn){
		ModuleMap* p = get_module_map();
		int c = get_module_map_size();
		for (int i = 0; i < c; i++){
			if (strcmp(p[i].external_name, fn) == 0){
				return p[i].fn;
			}
		}
		return 0;
	}





	using namespace std;



	// std::string goctx = "ctx";
	// bool CBOR = false;
	// int chunk_size = 4000000;

	// Mock of SDK we're implementing in bitcode
	class BitCodeCallContext {
	public:

		bool isCBOR(){
			return false;
		}
		int chunk_size(){
			return 4000000;
		}

		std::pair<nlohmann::json,int> make_error(std::string desc, int code){
			nlohmann::json j;
			j["message"] = desc;
			j["code"] = code;
			return make_pair(j,code);
		}
		char* AllocateMemoryAndCopyString(std::string strToCopy){
			char* ret = (char*)malloc(strToCopy.size()+1);
			if (ret == NULL) return NULL;
			strcpy(ret, strToCopy.c_str());
			return ret;
		}
		uint8_t* AllocateMemoryAndCopyBuffer(uint8_t* buf, int sz){
			uint8_t* ret = (uint8_t*)malloc(sz);
			if (ret == NULL) return NULL;
			memcpy(ret, buf, sz);
			return ret;
		}
		std::pair<nlohmann::json,int> make_success(){
			/* Prepare output */
			nlohmann::json j_ret;
			j_ret["headers"] = "application/json";
			j_ret["body"] = "SUCCESS";
			return std::make_pair(j_ret,0);
		}

		BitCodeCallContext(std::string ID) : _ID(ID){
		}
		// ID returns the ID of this call context
		std::string ID(){
			return _ID;
		}

		std::string goctx = "ctx";
		std::string gocore = "core";
		std::string goext = "ext";

		// Input returns the stream ID of the call's default input stream. Use the
		// "ReadStream" method to read data from that stream.
		std::string Input(){
			return std::string("fis");
		}
		// Output returns the stream ID of the call's default output stream. Use the
		// "WriteStream" method to write data to that stream.
		std::string Output(){
			return std::string("fos");
		}
		// Callback calls the callback function for this context with the given args
		// encoded in JSON.
		std::pair<nlohmann::json, int> Callback(nlohmann::json& j){
			//auto jsonCallback = json::parse(args);
			std::string method = std::string("Callback");
			return Call(method, j, goctx);
		}
		// NewStream creates a new stream and returns its ID.
		nlohmann::json NewStream(){
			auto s = std::string("NewStream");
			std::string blank("");
			nlohmann::json j;
			auto res = Call( s, j, goctx);
			return res.first;
		}
		// NewFileStream creates a new file and returns its ID.
		nlohmann::json NewFileStream(){
			auto s = std::string("NewFileStream");
			std::string blank("");
			nlohmann::json j;
			auto res = Call( s, j, goctx);
			return res.first;
		}
		std::pair<nlohmann::json, int> WritePartToStream(std::string streamID, std::string qihot, std::string qphash, int offset = 0, int length = -1){
			nlohmann::json j;

			j["stream_id"] = streamID;
			j["off"] = offset;
			j["len"] = length;
			j["qihot"] = qihot;
			j["qphash"] = qphash;

			std::string method = "QWritePartToStream";

			return Call( method, j, gocore);
		}

		std::pair<nlohmann::json, int> WritePartToStream(std::string streamID, std::string qphash,  int offset = 0, int length = -1){
			nlohmann::json j;

			j["stream_id"] = streamID;
			j["off"] = offset;
			j["len"] = length;
			j["qphash"] = qphash;

			std::string method = "QWritePartToStream";

			return Call( method, j, gocore);
		}
		// WriteStream writes at most len bytes from the provided byte buffer (src) to the
		// stream with the given stream ID (streamToWrite).
		//
		// The returned string is a JSON structure that looks as follows in case of a
		// successful invocation:
		//
		// 	{ "written": XYZ }
		//
		// XYZ is the number of bytes written to the target buffer.
		//
		// In case of an error:
		//
		// 	{
		//		"written": XYZ,
		//		"error" : {
		//			"code": 1,
		//			"message": "error message",
		//			"data": { ... }
		//		}
		//	}
		//
		std::pair<nlohmann::json, int> WriteStream(std::string streamToWrite, std::vector<unsigned char>& src, int len=-1){
			if (len == -1) {
			  len = src.size();
			}
			auto res = CHAR_BASED_AUTO_RELEASE(Write((char*)_ID.c_str(), (char*)streamToWrite.c_str(), (char*)src.data(), len));
			if (res.get() != 0){
				auto j_res = nlohmann::json::parse(res.get());
				return make_pair(j_res, 0);

			}else{
				return make_error("ReadStream call failed", -1);
			}
		}
		// ReReadStream reads at most dst.size() bytes from the stream with the given stream ID (streamToRead)
		// into the provided byte buffer (dst).
		//
		// The returned string is a JSON structure that looks as follows in case of a
		// successful invocation:
		//
		// 	{ "read": XYZ }
		//
		// XYZ is the number of bytes read into the buffer.
		// XYZ may be 0 if no bytes are available at the current time, but the stream is
		// not yet at its end.
		// XYZ is -1 if the stream has reached its end.
		//
		// In case of an error:
		//
		// 	{
		//		"error" : {
		//			"code": 1,
		//			"message": "error message",
		//			"data": { ... }
		//		}
		//	}
		//
		std::pair<nlohmann::json, int> ReadStream(std::string streamToRead, std::vector<unsigned char>&  dst){
			auto res = CHAR_BASED_AUTO_RELEASE(Read((char*)_ID.c_str(), (char*)streamToRead.c_str(), (char*)dst.data(), dst.size()));
			if (res.get() != 0){
				auto j_res = nlohmann::json::parse(res.get());
				return make_pair(j_res, 0);
			}else{
				return make_error("ReadStream call failed", -1);
			}
		}
		// CloseStream closes the given stream. After closing a stream, any call to
		// write will trigger an error. Any call to read will return the remaining
		// bytes that have been written to the stream, but not yet read. If no bytes
		// are remaining, io.EOF is returned.
		void CloseStream(std::string id){
			json j = id;
			auto s = std::string("CloseStream");
			Call(s, j, goctx);
		}
		// Call invokes a function on a provided bitcode module and passes the
		// JSON-encoded arguments. The return value is a JSON text with the result,
		// or an error (e.g. if the module of function cannot be found).
		std::pair<nlohmann::json, int> Call(std::string& functionName, nlohmann::json& j, std::string module){
			try{
				nlohmann::json j_outer;
				j_outer["jpc"] = "1.0";
				j_outer["params"] = j;
				j_outer["id"] = _ID;
				j_outer["module"] = module;
				j_outer["method"] = functionName;

				auto ret = CHAR_BASED_AUTO_RELEASE(JPC((char*)j_outer.dump().c_str()));
				if (ret.get() != 0){
					auto res = nlohmann::json::parse(ret.get());
					if (res.find("result") != res.end()){
						if (res["result"].empty()){
							nlohmann::json j_result;
							res["result"] = j_result;
						}
						return make_pair(res["result"], 0);
					}
					else
						return make_pair(res["error"], -1);
				}else{
					return make_error("JPC call failed", -1);
				}
				// PENDING(LUK): this should return a parsed JSON, not a string, to be consistent
			}catch (nlohmann::json::exception& e){
				return make_pair(e.what(), e.id);
			}
		}
		// Write a debug message to the log. Use the LOG_DEBUG macro for convenience.
		void Debug(string msg, nlohmann::json fields){
			log("DEBUG", msg, fields);
		}
		// Write an info message to the log. Use the LOG_INFO macro for convenience.
		void Info(string msg, nlohmann::json fields){
			log("INFO", msg, fields);
		}
		// Write a warn message to the log. Use the LOG_WARN macro for convenience.
		void Warn(string msg, nlohmann::json fields){
			log("WARN", msg, fields);
		}
		// Write an error message to the log. Use the LOG_ERROR macro for convenience.
		void Error(string msg, nlohmann::json fields){
			log("ERROR", msg, fields);
		}

		int KVSet(char *key, char *val){
			nlohmann::json j;

			j["key"] = key;
			j["val"] = val;

			std::string method = "KVSet";
			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		char* KVGetTemp(char *key){
			nlohmann::json j;

			j.clear();
			j["key"] = key;

			std::string method = "KVGetTemp";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"].get<std::string>());
		}
		int KVPushBack(char *key, char *val){
			nlohmann::json j;

			j["key"] = key;
			j["val"] = val;

			std::string method = "KVPushBack";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		char* KVRangeTemp(char *key){
			nlohmann::json j;

			j["key"] = key;

			std::string method = "KVRangeTemp";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		char* QCreatePart(char *content, uint32_t sz){
			// nlohmann::json j;

			// j["qlib_id"] = libid;
			// j["qw_token"] = token;
			// j["path"] = path;
		// 	std::string method = "QCreatePart";
		// 	std::string params = json_rep;
		// 	auto ret = Call(_Module, method, params);
		// 	return (char*)std::get<0>(ret).c_str();
		//
			// This cannot use a string return it needs to use the streams
			return 0;
		}
		char* QReadPart(char * phash, uint32_t start, uint32_t sz, uint32_t *psz){
			// MORE ERROR HANDLING!!! JPF
			auto s = NewStream();
			auto sid = s["stream_id"];
			auto ret = WritePartToStream(sid, phash);
			LOG_INFO(this, "return from WritePart=",ret.first.dump());
			auto written = ret.first["written"].get<int>();
			sz = sz <= -1 ? written : sz;
			std::vector<uint8_t> vec_ret(written > sz ? sz : written);
			auto sread = ReadStream(sid, vec_ret);
			LOG_INFO(this, "return from ReadStream=",sread.first.dump());
			*psz = sread.first["read"].get<int>();
			return (char*)AllocateMemoryAndCopyBuffer(vec_ret.data(), sz);
		}

		char* SQMDGetString(char *path){
			nlohmann::json j;

			//j["qihot"] = token;
			j["path"] = path;

			std::string method = "SQMDGet";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			LOG_INFO(this,"json=",ret.first.get<string>());
			return AllocateMemoryAndCopyString(ret.first.get<string>());
		}

		char* SQMDGetJSON(char *path){
			nlohmann::json j;

			//j["qihot"] = token;
			j["path"] = path;

			std::string method = "SQMDGet";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"].dump());
				return 0;
			}
			LOG_INFO(this,"json=",ret.first.dump());
			return AllocateMemoryAndCopyString(ret.first.dump());
		}
		char* SQMDQueryJSON(char *token, char *query){
			nlohmann::json j;

			j["qihot"] = token;
			j["query"] = query;

			std::string method = "SQMDQuery";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		int64_t SQMDClearJSON(char *path, char* j_rep){
			nlohmann::json j;

			j["path"] = path;

			std::string method = "SQMDClear";

			auto ret = Call( method, j, gocore);

			return ret.second;
		}
		int64_t SQMDDeleteJSON(char *token, char *path){
			nlohmann::json j;

			j["qwtoken"] = token;
			j["path"] = path;

			std::string method = "SQMDDelete";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		int64_t SQMDSetJSON(char *path, char* j_rep){
			nlohmann::json j;

			j["path"] = path;
			j["meta"] = j_rep;

			std::string method = "SQMDSet";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		int64_t SQMDMergeJSON(char *path, char* j_rep){
			nlohmann::json j;

			j["path"] = path;
			j["meta"] = j_rep;

			std::string method = "SQMDMerge";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		char* KVGet(char *key){
			return SQMDGetJSON(key);
		}
		char* KVRange(char *key){
			nlohmann::json j;

			j["key"] = key;

			std::string method = "KVRange";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		int QPartInfo(char *part_hash_or_token){
			nlohmann::json j;

			j["qphash_or_token"] = part_hash_or_token;
			std::string method = "QPartInfo";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this,ret.first.dump());
			}
			return ret.second;
		}
		int64_t  FFMPEGRun(char* cmdline, int sz){
			nlohmann::json j;

			j["stream_params"] = cmdline;
			j["stream_params_size"] = sz;
			std::string method = "FFMPEGRun";

			auto ret = Call( method, j, goext);
			return ret.second;
		}
		int64_t  FFMPEGRun(char** cmdline, int argument_count){
			nlohmann::json j;

			char inputArray[65536];
			int arrayLen = MemoryUtils::packStringArray(inputArray, cmdline, argument_count);
			auto enc = base64_encode((const unsigned char*)inputArray, arrayLen);

			j["stream_params"] = enc;
			std::string method = "FFMPEGRun";

			auto ret = Call( method, j, goext);
			return ret.second;
		}
		int64_t FFMPEGRunEx(char** cmdline, int argument_count, std::string& in_files){
			nlohmann::json j;

			char inputArray[65536];
			int arrayLen = MemoryUtils::packStringArray(inputArray, cmdline, argument_count);
			auto enc = base64_encode((const unsigned char*)inputArray, arrayLen);

			j["stream_params"] = enc;
			j["stream_files"] = in_files;
			std::string method = "RunEx";

			auto ret = Call( method, j, goext);
			return ret.second;
		}
		int64_t FFMPEGRunVideo(char** cmdline, int argument_count, std::string& in_files){
			nlohmann::json j;

			char inputArray[65536];
			int arrayLen = MemoryUtils::packStringArray(inputArray, cmdline, argument_count);
			auto enc = base64_encode((const unsigned char*)inputArray, arrayLen);

			j["stream_params"] = enc;
			j["stream_files"] = in_files;
			std::string method = "RunVideo";

			auto ret = Call( method, j, goext);
			return ret.second;
		}
		char* KVList(char * libid, char * hash){
			nlohmann::json j;

			std::string method = "KVList";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		int  FFMPEGRunLive(char* key, int key_sz, char* val, int val_sz, char* port,char* callback,char* libid,char* qwt){
			nlohmann::json j;

			j["stream_key"] = key;
			j["stream_key_size"] = key_sz;
			j["stream_value"] = val;
			j["stream_value_size"] = val_sz;

			j["udp_port"] = port;
			j["callback"] = callback;
			j["qlib_id"] = libid;
			j["qwt"] = qwt;

			std::string method = "FFMPEGRunLive";

			auto ret = Call( method, j, "ext");
			return ret.second;
		}
		void  FFMPEGStopLive(int){
			std::string method = "FFMPEGStopLive";
			nlohmann::json j;
			auto ret = Call( method, j, "ext");
		}
		char* QSSGet(char* id, char* key){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;

			std::string method = "QSSGet";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		int QSSSet(char* id, char* key, char* val){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;
			j["val"] = val;
			std::string method = "QSSSet";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		int QSSDelete(char* id, char* key){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;
			std::string method = "QSSDelete";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		char* QModifyContent(char* id, char* type){
			nlohmann::json j;

			j["qid"] = id;
			j["qtype"] = type;
			std::string method = "QModifyContent";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		char* QFinalizeContent(char* token){
			nlohmann::json j;

			j["qwtoken"] = token;
			std::string method = "QFinalizeContent";

			auto ret = Call( method, j, gocore);
			if (ret.second != 0){
				LOG_ERROR(this, ret.first["error"]["message"]);
				return 0;
			}
			return AllocateMemoryAndCopyString(ret.first["result"]);
		}
		char* GetCallingUser(){
			// TODO This needs to point to a Go routine
			return (char*)"Anonymous";
		}
		int64_t TaggerRun(char* cmdline, int sz){
			nlohmann::json j;

			j["stream_params"] = cmdline;
			j["stream_params_size"] = sz;

			std::string method = "TaggerRun";

			auto ret = Call( method, j, gocore);
			return ret.second;
		}
		int64_t TaggerRun(char** cmdline, int argument_count){
			nlohmann::json j;

			char inputArray[65536];
			int arrayLen = MemoryUtils::packStringArray(inputArray, cmdline, argument_count);
			auto enc = base64_encode((const unsigned char*)inputArray, arrayLen);

			j["stream_params"] = enc;
			std::string method = "TaggerRun";

			auto ret = Call( method, j, goext);
			return ret.second;
		}


		std::pair<nlohmann::json, int> WriteOutput(std::vector<unsigned char>& src, int len = -1){
			return WriteStream(Output(), src,len);
		}
		std::pair<nlohmann::json, int> ReadInput(std::vector<unsigned char>& dst){
			return ReadStream(Input(), dst);
		}

	private:
		std::string _ID;

		void log(const char* level, std::string msg, nlohmann::json& fields)
		{
			nlohmann::json j;
			j["msg"] = msg;
			j["level"] = level;
			j["fields"] = fields;

			std::string method = "Log";
			Call(method, j, goctx);
		}

		std::string format(const std::string format, va_list& args)
		{
			va_list orig;
			va_copy(orig, args);
			size_t len = std::vsnprintf(NULL, 0, format.c_str(), args);
			va_end (args);
			std::vector<char> vec(len + 1);
			std::vsnprintf(&vec[0], len + 1, format.c_str(), orig);
			va_end (orig);
			return &vec[0];
		}


		std::string format(const std::string fmt, ...)
		{
			va_list args;
			va_start (args, fmt);
			return format(fmt, args);
		}

	};

	// // TBD json format for init
	// extern "C" int _context_init(int outsz, char* outbuf, char* argbuf){
	// 	ArgumentBuffer args(argbuf);
	// 	auto init_data = json::parse(args[0]);
	// 	return 0;
	// }


	class FileUtils{
	public:

		static auto loadFromFile(BitCodeCallContext* ctx, const char* filename){
			char targetFilename[1024];
			sprintf(targetFilename, "%s",  filename);
			FILE* targetFile = fopen(targetFilename, "rb");

			if (!targetFile){
				LOG_ERROR(ctx, "Error: Unable to open", targetFilename);
				return CHAR_BASED_AUTO_RELEASE(0);
			}
			fseek(targetFile, 0, SEEK_END);
			long fsize = ftell(targetFile);
			fseek(targetFile, 0, SEEK_SET);  //same as rewind(f);

			auto segData = CHAR_BASED_AUTO_RELEASE((char*)malloc(fsize + 1));
			int cb = fread(segData.get(), 1, fsize, targetFile);
			if (cb != fsize){
				LOG_ERROR(ctx, "Did not read all the data");
				return CHAR_BASED_AUTO_RELEASE(0);
			}
			char* pd = segData.get();
			pd[cb] = 0;
			fclose(targetFile);

			if (GlobalCleanup::shouldCleanup()){
				if (unlink(targetFilename) < 0) {
					LOG_ERROR(ctx, "Failed to remove temporary ffmpeg output file=", targetFilename);
				}
			}

			return segData;
		}
		static std::pair<nlohmann::json, int> loadFromFileAndPackData(BitCodeCallContext* ctx, const char* filename, const char* contenttype, int segno=0, float duration = 0.0){
			return ctx->make_error("NO LONGER SUPPORTED FUNCTION", -1);
		}


		/* Write out a 'resource' as a file for ffmpeg to use as input */
		static int
		resourceToFile(BitCodeCallContext* ctx, char *phash, char *resname, char avtype)
		{
			return -1;
		}
	};
}; // end namespace


extern "C" int _JPC(int cb, char* out, char* in){
nlohmann::json j_return;
elv_context::BitCodeCallContext* ctx;
try{
	ArgumentBuffer args(in);
	if (args[0] == 0){
		std::cout << "_JPC called with EMPTY ARG BUFFER!!!\n FAILING!!!";
		return -1;
	}
	auto j = nlohmann::json::parse(args[0]);

	auto call_data = j;
	auto version = call_data["jpc"];
	auto id = call_data["id"];

	auto method = call_data["method"].get<std::string>();
	auto params = call_data["params"];
	auto module = call_data["module"];

	elv_context::JPCParams jpc_params(params);

	std::pair<std::string, elv_context::BitCodeCallContext*> new_ctx;
	auto mapID = elv_context::_context_map.find(id);
	if (mapID != elv_context::_context_map.end()){
		ctx = mapID->second.get();
	}else{
		ctx = new elv_context::BitCodeCallContext(id.get<std::string>());
		new_ctx.first = id;
		new_ctx.second = ctx;
		elv_context::_context_map.insert(new_ctx);
	}
	j_return["jpc"] = "1.0";
	j_return["id"] = ctx->ID();

	auto callee = elv_context::get_callee(method.c_str());
	int call_ret = 0;
	if (callee != 0){
		auto res = callee(ctx,  jpc_params);
		if (res.second == 0){
			j_return["result"] = res.first;
		} else {
		    j_return["error"] = res.first;
		}
		std::vector<std::string> vec;
		vec.push_back(j_return.dump());
		call_ret = ArgumentBuffer::argv2buf(vec, out, cb);
	}else{
		std::string error_msg = "unknown method " + method;
		j_return["error"] = ctx->make_error(error_msg, -1);
		std::vector<std::string> vec;
		vec.push_back(j_return.dump());
		call_ret = ArgumentBuffer::argv2buf(vec, out, cb);
	}
	elv_context::_context_map.erase(new_ctx.first);
	return call_ret;
}catch (nlohmann::json::exception& e){
	std::string ret = "message: ";
	ret += e.what();
	ret += '\n';
	ret += "exception id: ";
	ret +=  e.id;
	ret += '\n';
	j_return["error"] = ctx->make_error(ret, -1);
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}catch(std::exception& e){
	std::string ret = "message: ";
	ret += e.what();
	ret += '\n';
	j_return["error"] = ctx->make_error(ret, -1);
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}catch(...){
	std::string ret = "ELIPSIS handler caught exception\n";
	j_return["error"] = ctx->make_error(ret, -1);
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}
}
