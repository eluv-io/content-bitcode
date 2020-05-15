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
#include "eluvio/error.h"
#include "eluvio/version-info.h"

extern "C" char*    JPC(char*);
extern "C" char*    Write(char*, char*, char*,int);
extern "C" char*    Read(char*, char*, char*,int);

using namespace std;
using namespace elv_version;

typedef std::pair<nlohmann::json,E> elv_return_type;

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

	typedef elv_return_type (*call_impl)(BitCodeCallContext*, JPCParams&);
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

		elv_return_type make_error(std::string desc, E code){
			nlohmann::json j;
			j["message"] = desc;
			j["data"] = code.getJSON();
			return make_pair(j,code);
		}
		elv_return_type make_success(){
			/* Prepare output */
			nlohmann::json j;
			char *headers = (char *)"application/json";
			j["headers"] = headers;
			j["result"] = 0;
			j["body"] = "SUCCESS";
			return std::make_pair(j,E(false));
		}
		elv_return_type make_success(nlohmann::json j){
			/* Prepare output */
			return std::make_pair(j,E(false));
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
		// Callback overload to easily generate standard json for response
		// encoded in JSON.
		elv_return_type Callback(int status, const char* content_type, int size){
			nlohmann::json j_response = {
				{"http", {
					{"status", status},
					{"headers", {
						{"Content-Type", {content_type}},
						{"Content-Length", {std::to_string(size)}}
						}}}
				}
			};
			std::string method = std::string("Callback");
			return Call(method, j_response, goctx);
		}
		// Callback overload to easily generate standard json for response
		// encoded in JSON.
		elv_return_type CallbackDisposition(int status, const char* content_type, int size, const char* filename){
			auto fileTagPart = string_format("filename=%s;", filename);
			nlohmann::json j_response = {
				{"http", {
					{"status", status},
					{"headers", {
						{"Content-Type", {content_type}},
						{"Content-Length", {std::to_string(size)}},
						{"Content-Disposition: attachment;", {fileTagPart}}
						}}}
				}
			};
			std::string method = std::string("Callback");
			return Call(method, j_response, goctx);
		}
		// Callback calls the callback function for this context with the given args
		// encoded in JSON.
		elv_return_type Callback(nlohmann::json& j){
			//auto jsonCallback = json::parse(args);
			std::string method = std::string("Callback");
			return Call(method, j, goctx);
		}
		// NewStream creates a new stream and returns its ID.
		std::string NewStream(){
			auto s = std::string("NewStream");
			std::string blank("");
			nlohmann::json j;
			auto res = Call( s, j, goctx);
			if (res.first.find("stream_id") == res.first.end() || res.second.IsError()){
				LOG_INFO(this, "NewStream returned empty stream_id");
				return "";
			}
			return res.first["stream_id"].get<string>();
		}
		// LibId returns the QLIBID for the current context
		std::string LibId(){
			auto s = std::string("LibId");
			std::string blank("");
			nlohmann::json j;
			auto res = Call( s, j, goctx);
			if (res.second.IsError()){
				LOG_ERROR(this, "LibId Failed", "json", res.first.dump());
				return "";
			}
			return res.first["qlibid"].get<std::string>();
		}
		// NewFileStream creates a new file and returns its ID.
		nlohmann::json NewFileStream(){
			auto s = std::string("NewFileStream");
			std::string blank("");
			nlohmann::json j;
			auto res = Call( s, j, goctx);
			return res.first;
		}
		elv_return_type WritePartToStream(std::string streamID, std::string qihot, std::string qphash, int offset = 0, int length = -1){
			nlohmann::json j;

			j["stream_id"] = streamID;
			j["off"] = offset;
			j["len"] = length;
			j["qihot"] = qihot;
			j["qphash"] = qphash;

			std::string method = "QWritePartToStream";

			return Call( method, j, gocore);
		}

		elv_return_type WritePartToStream(std::string streamID, std::string qphash,  int offset = 0, int length = -1){
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
		elv_return_type WriteStream(std::string streamToWrite, std::vector<unsigned char>& src, int len=-1){
			if (len == -1) {
			  len = src.size();
			}
			auto res = CHAR_BASED_AUTO_RELEASE(Write((char*)_ID.c_str(), (char*)streamToWrite.c_str(), (char*)src.data(), len));
			if (res.get() != 0){
				auto j_res = nlohmann::json::parse(res.get());
				return make_pair(j_res, E(false));

			}else{
				const char* msg = "ReadStream call failed";
				return make_error(msg,E(msg).Kind(E::Other));
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
		elv_return_type ReadStream(std::string streamToRead, std::vector<unsigned char>&  dst){
			auto res = CHAR_BASED_AUTO_RELEASE(Read((char*)_ID.c_str(), (char*)streamToRead.c_str(), (char*)dst.data(), dst.size()));
			if (res.get() != 0){
				auto j_res = nlohmann::json::parse(res.get());
				return make_pair(j_res, E(false));
			}else{
				const char* msg = "ReadStream call failed";
				return make_error(msg, E(msg).Kind(E::IO));
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
		elv_return_type Call(std::string& functionName, nlohmann::json& j, std::string module){
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
						return make_pair(res["result"], E(false));
					}
					else
						return make_pair(res["error"], E("result not avaliable").Kind(E::Other));
				}else{
					return make_error("JPC call failed", E("JPC call failed").Kind(E::Other));
				}
				// PENDING(LUK): this should return a parsed JSON, not a string, to be consistent
			}catch (nlohmann::json::exception& e){
				return make_pair(e.what(), e.id);
			}
		}
		// Call invokes a function on a provided bitcode module and passes the
		// JSON-encoded arguments. The return value is a JSON text with the result,
		// or an error (e.g. if the module of function cannot be found).
		// The main purpose of this function is to create the Elements of an mpd file
		// that constitute an eluvio channel
		//
		elv_return_type ProcessChannelElement(std::string versionHash, std::string libid, std::string objid, std::string targetDuration, int periodNum, double entryPointSec){
			nlohmann::json j;

			j["targetDuration"] = targetDuration;
			j["versionHash"] = versionHash;
			j["periodNum"] = periodNum;
			j["objectId"] = objid;
			j["libraryId"] = libid;
			j["entryPointSec"] = entryPointSec;

			std::string method = "ProcessChannelElement";

			return Call( method, j, goext);
		}

		elv_return_type ProxyHttp(nlohmann::json& request){
			nlohmann::json j;

			j["request"] = request;

			std::string method = "ProxyHttp";

			return Call( method, j, goext);
		}

		elv_return_type CallAdManager(std::string versionHash, nlohmann::json& queryParams){
			nlohmann::json j;

			j["versionHash"] = versionHash;
			j["queryParams"] = queryParams;

			std::string method = "CallAdManager";

			return Call( method, j, goext);
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

		int KVSet(const char *key, const char *val){
			nlohmann::json j;
			nlohmann::json j_inner;

			j_inner[key] = val;

			j["path"] = key;
			j["meta"] = j_inner;

			std::string method = "SQMDSet";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, "KVset", "key", key, "val", val, "JSON", ret.first.dump());
				return -1;
			}
			return 0;
		}
		bool KVSetMutable(const char* id, const char *key, const char *val){
			nlohmann::json j;
			nlohmann::json j_inner;

			j_inner[key] = val;

			j["path"] = key;
			j["meta"] = j_inner;
			j["qihot"] = id;

			LOG_DEBUG(this, "KVSetMutable",  "json", j.dump());

			std::string method = "SQMDSet";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, "KVsetMutable", "path", key, "JSON", ret.first.dump());
				return false;
			}
			return true;
		}
		std::string KVGet(const char *key){
			nlohmann::json j;

			j["path"] = key;

			std::string method = "SQMDGet";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, ret.first.dump());
				return "";
			}
			LOG_INFO(this,"KVGet", "JSON",ret.first.dump());
			if (ret.first.find(key) != ret.first.end())
				return ret.first[key].get<std::string>();
			else
				return ret.first.get<std::string>();
		}
		const char* KVGetTemp(char *key){
			const char* msg = "KVGetTemp no longer supported";
			LOG_ERROR(this,msg);
			return msg;
		}
		const char* KVRangeTemp(char *key){
			const char* msg = "KVRangeTemp no longer supported";
			LOG_ERROR(this,msg);
			return msg;
		}
		// CheckSumPart calculates a checksum of a given content part.
		// - sum_method:    checksum method ("MD5" or "SHA256")
		// - qphash:        hash of the content part to checksum
		//  Returns the checksum as hex-encoded string
		elv_return_type CheckSumPart(std::string sum_method, std::string qphash){
			nlohmann::json j;

			j["method"] = sum_method;
			j["qphash"] = qphash;

			std::string method = "QCheckSumPart";

			return Call( method, j, gocore);

		}
		// CheckSumFile calculates a checksum of a file in a file bundle
		// - sum_method:    checksum method ("MD5" or "SHA256")
		// - file_path:     the path of the file in the bundle
		//  Returns the checksum as hex-encoded string
		elv_return_type CheckSumFile(std::string sum_method, std::string file_path){
			nlohmann::json j;

			j["method"] = sum_method;
			j["file_path"] = file_path;

			std::string method = "QCheckSumFile";

			return Call( method, j, gocore);

		}
		elv_return_type QListContentFor(std::string qlibid){
			nlohmann::json j;

			j["external_lib"] = qlibid;

			std::string method = "QListContentFor";

			return Call( method, j, gocore);

		}
		std::string QCreatePart(std::string qwt, std::vector<uint8_t>& input_data){
			auto sid = NewStream();
			auto ret_s = WriteStream(sid.c_str(), input_data);
			if (ret_s.second.IsError()){
				const char* msg = "writing part to stream";
				LOG_ERROR(this, msg);
				CloseStream(sid);
				return msg;
			}
			auto written = ret_s.first["written"].get<int>();
			input_data.resize(written);
			nlohmann::json j;
			j["qwtoken"] = qwt;
			j["stream_id"] = sid;
			std::string method = "QCreatePartFromStream";
			auto ret = Call(method, j, gocore);
			CloseStream(sid);
			if (ret.second.IsError()){
				const char* msg = "QCreatePartFromStream";
				LOG_ERROR(this, msg, "inner_error", ret.first.dump());
				return "";
			}
			return ret.first["qphash"].get<string>();
		}
		std::shared_ptr<std::vector<uint8_t>>
		QReadPart(const char * phash, uint32_t start, uint32_t sz, uint32_t *psz){
			// MORE ERROR HANDLING!!! JPF
			auto sid = NewStream();
			auto ret = WritePartToStream(sid, phash);
			LOG_INFO(this, "WritePartToStream", "return_val", ret.first.dump());
			auto written = ret.first["written"].get<int>();
			sz = sz <= -1 ? written : sz;
			std::vector<uint8_t> vec_ret(written > sz ? sz : written);
			auto sread = ReadStream(sid, vec_ret);
			LOG_INFO(this, "ReadStream","read", sread.first.dump());
			*psz = sread.first["read"].get<int>();
			return std::make_shared<std::vector<uint8_t>>(vec_ret);
		}
		std::string SQMDGetString(const char *path){
			return SQMDGetString((char*)path);
		}

		std::string SQMDGetString(char *path){
			nlohmann::json j;

			//j["qihot"] = token;
			j["path"] = path;

			std::string method = "SQMDGet";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, "SQMDGetString", "inner_error", ret.first.dump());
				return "";
			}
			LOG_INFO(this, "SQMDGetString", "json",ret.first.get<string>());
			return ret.first.get<string>();
		}

		elv_return_type SQMDGetJSON(const char *path){
			nlohmann::json j;

			//j["qihot"] = token;
			j["path"] = path;

			std::string method = "SQMDGet";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDGetJSONResolve(const char *path){
			nlohmann::json j;

			//j["qihot"] = token;
			j["path"] = path;

			std::string method = "SQMDGetJSONResolve";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDGetJSON(const char* hash, const char *path){
			nlohmann::json j;

			j["qihot"] = hash;
			j["path"] = path;

			std::string method = "SQMDGet";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDGetJSON(const char* qlibid, const char* qhash, const char *path){
			nlohmann::json j;
 			j["path"] = path;
			j["qlibid"] = qlibid;
			j["qhash"] = qhash;
 			std::string method = "SQMDGetExternal";
			LOG_INFO(this, "About to call", "path", path, "qlibid", qlibid, "qhash", qhash);
 			return Call( method, j, gocore);
		}
		elv_return_type SQMDClearJSON(char *path, char* j_rep){
			nlohmann::json j;

			j["path"] = path;

			std::string method = "SQMDClear";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDDeleteJSON(char *token, char *path){
			nlohmann::json j;

			j["qwtoken"] = token;
			j["path"] = path;

			std::string method = "SQMDDelete";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDSetJSON(const char *path, const char* j_rep){
			nlohmann::json j;

			j["path"] = path;
			j["meta"] = j_rep;

			std::string method = "SQMDSet";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDMergeJSON(char *path, char* j_rep){
			nlohmann::json j;

			j["path"] = path;
			j["meta"] = j_rep;

			std::string method = "SQMDMerge";

			return Call( method, j, gocore);
		}
		elv_return_type SQMDMergeJSONEx(const char *path, nlohmann::json& j_rep){
			nlohmann::json j;

			j["path"] = path;
			j["meta"] = j_rep;

			std::string method = "SQMDMerge";

			return Call( method, j, gocore);
		}
		std::string KVRange(char *key){
			nlohmann::json j;

			j["key"] = key;

			std::string method = "KVRange";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, "KVRange", "inner_error", ret.first.dump());
				return 0;
			}
			return ret.first["result"].dump();
		}
		elv_return_type QPartInfo(char *part_hash_or_token){
			nlohmann::json j;

			j["qphash_or_token"] = part_hash_or_token;
			std::string method = "QPartInfo";

			return Call( method, j, gocore);
		}
		elv_return_type FFMPEGRun(char* cmdline, int sz){
			nlohmann::json j;

			j["stream_params"] = cmdline;
			j["stream_params_size"] = sz;
			std::string method = "FFMPEGRun";

			return Call( method, j, goext);
		}
		elv_return_type  FFMPEGRun(char** cmdline, int argument_count){
			nlohmann::json j;
			nlohmann::json j_args;
			for (int i = 0; i < argument_count; i++){
				j_args.push_back(cmdline[i]);
			}

			j["stream_params"] = j_args;
			std::string method = "FFMPEGRun";

			return  Call( method, j, goext);
		}
		elv_return_type FFMPEGRunEx(char** cmdline, int argument_count, std::string& in_files){
			nlohmann::json j;
			nlohmann::json j_args;

			for (int i = 0; i < argument_count; i++){
				j_args.push_back(cmdline[i]);
			}
			j["stream_params"] = j_args;
			j["stream_files"] = in_files;
			std::string method = "RunEx";

			return Call( method, j, goext);
		}
		elv_return_type FFMPEGRunVideo(char** cmdline, int argument_count, std::string& in_files){
			nlohmann::json j;
			nlohmann::json j_args;

			for (int i = 0; i < argument_count; i++){
				j_args.push_back(cmdline[i]);
			}
			j["stream_params"] = j_args;
			j["stream_files"] = in_files;
			std::string method = "RunVideo";

			return Call( method, j, goext);
		}
		elv_return_type  FFMPEGRunLive(char** cmdline, int argument_count, std::string callback_name){
			nlohmann::json j;
			nlohmann::json j_args;

			for (int i = 0; i < argument_count; i++){
				j_args.push_back(cmdline[i]);
			}
			j["stream_params"] = j_args;

			j["update_callback"] = callback_name;
			std::string method = "RunLive";

			return  Call( method, j, goext);
		}
		void  FFMPEGStopLive(std::string& handle){
			std::string method = "StopLive";
			nlohmann::json j;
			j["handle"] = handle;
			auto ret = Call( method, j, "ext");
			if (ret.second.IsError()){
				LOG_ERROR(this, "FFMPEGStopLive", "handle", handle, "reason", ret.first.dump())
			}
		}
		// PENDING(JAN): REVIEW normalize all return types to elv_return_type
		string QSSGet(const char* id, const char* key){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;

			std::string method = "QSSGet";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_INFO(this, "QSSGet failed", ret.first.dump().c_str());
				return "";
			}
			return ret.first.get<std::string>();
		}
		elv_return_type QSSSet(const char* id, const char* key, const char* val){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;
			j["val"] = val;
			std::string method = "QSSSet";

			return Call( method, j, gocore);
		}
		std::string QCreateQStateStore(){
			nlohmann::json j;

			std::string method = "QCreateQStateStore";

			auto ret = Call( method, j, gocore);
			if (!ret.second.IsError()){
				return ret.first;
			}
			return "";
		}
		elv_return_type QCreateContent(std::string qtype, nlohmann::json jMeta){
			nlohmann::json j;

			j["qtype"] = qtype;
			j["meta"] = jMeta;
			std::string method = "QCreateContent";

			return Call( method, j, gocore);
		}
		elv_return_type QSSDelete(char* id, char* key){
			nlohmann::json j;

			j["qssid"] = id;
			j["key"] = key;
			std::string method = "QSSDelete";

			return Call( method, j, gocore);
		}
		std::string QModifyContent(const char* type){
			nlohmann::json j;

			j["qtype"] = type;
			j["meta"] = nlohmann::json({});
			std::string method = "QModifyContent";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, method, "inner_error", ret.first.dump());
				return "";
			}
			return ret.first["qwtoken"].get<string>();
		}
		std::string QModifyContent(const char* id, const char* type){
			nlohmann::json j;

			j["qihot"] = id;
			j["qtype"] = type;
			j["meta"] = nlohmann::json({});
			std::string method = "QModifyContent";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, ret.first.dump());
				return "";
			}
			return ret.first["qwtoken"].get<string>();
		}
		std::pair<std::string, std::string> QFinalizeContent(std::string token){
			nlohmann::json j;

			j["qwtoken"] = token;
			std::string method = "QFinalizeContent";

			auto ret = Call( method, j, gocore);
			if (ret.second.IsError()){
				LOG_ERROR(this, ret.first.dump());
				return make_pair("","");
			}
			return make_pair(ret.first["qid"].get<string>(), ret.first["qhash"].get<string>());
		}
		char* GetCallingUser(){
			// TODO This needs to point to a Go routine
			return (char*)"Anonymous";
		}
		elv_return_type QCommitContent(std::string hash){
			nlohmann::json j;

			j["qhash"] = hash;
			std::string method = "QCommitContent";

			return Call( method, j, gocore);
		}
		elv_return_type TaggerRun(char* cmdline, int sz){
			nlohmann::json j;

			j["stream_params"] = cmdline;
			j["stream_params_size"] = sz;

			std::string method = "TaggerRun";

			return Call( method, j, gocore);
		}
		elv_return_type TaggerRun(char** cmdline, int argument_count, std::string& in_files){
			nlohmann::json j;
			nlohmann::json j_args;

			for (int i = 0; i < argument_count; i++){
				j_args.push_back(cmdline[i]);
			}
			j["stream_params"] = j_args;
			j["stream_files"] = in_files;
			std::string method = "TaggerRun";

			return Call( method, j, goext);
		}


		elv_return_type WriteOutput(std::vector<unsigned char>& src, int len = -1){
			return WriteStream(Output(), src,len);
		}
		elv_return_type ReadInput(std::vector<unsigned char>& dst){
			return ReadStream(Input(), dst);
		}

		std::pair<std::map<std::string, std::string>, E> QueryParams(JPCParams& j_params){
			std::map<std::string, std::string> queryParams;
			if (j_params.find("http") != j_params.end()){
				auto j_http = j_params["http"];
				auto& q = j_http["query"];

				LOG_DEBUG(this, "HttpParams:", "query", q.dump());
				for (auto& element : q.items()) {
					if (element.value().type() ==  nlohmann::json::value_t::array){
						queryParams[element.key()] = element.value()[0];
					}else{
						queryParams[element.key()] = element.value();
					}
				}
				return make_pair(queryParams, E(false));
			}
			return make_pair(queryParams, E(true).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
		}
		std::pair<std::string, E> HttpParam(JPCParams& j_params, std::string param_name){
			if (j_params.find("http") != j_params.end()){
				auto j_http = j_params["http"];
				if (j_http.find(param_name) != j_http.end())
					return std::make_pair(j_http[param_name].get<std::string>(), E(false));
			}
			return make_pair("", E(true).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
		}
		std::pair<nlohmann::json, E> HttpHeaders(JPCParams& j_params){
			if (j_params.find("http") != j_params.end()){
				auto j_http = j_params["http"];
				if (j_http.find("headers") != j_http.end())
					return std::make_pair(j_http["headers"], E(false));
			}
			return std::make_pair(nlohmann::json({}), E(true).Kind(eluvio_errors::ErrorKinds::BadHttpParams));
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

	elv_return_type GetFullVersionInfo(BitCodeCallContext* ctx){
		time_t commit_date = (time_t)ELV_VERSION::commit_date;
		tm* commit_tm  = gmtime(&commit_date);
		if (commit_tm == nullptr){
			return make_pair(nlohmann::json({}), E("VERSIONINFO", "commit_date", "null"));
		}
		auto date = string_format(
										"%4d-%2.2d-%2.2dT%2.2d:%2.2d:%2.2dZ",
										1900 + commit_tm->tm_year,
										commit_tm->tm_mon,
										commit_tm->tm_mday,
										commit_tm->tm_hour,
										commit_tm->tm_min,
										commit_tm->tm_sec);
		auto full = elv_context::string_format("%s@%.10s %s", strcmp(ELV_VERSION::version,"") == 0 ? ELV_VERSION::branch : ELV_VERSION::version,ELV_VERSION::revision, date.c_str());
    ctx->Callback(200, "application/json", full.length());
    std::vector<std::uint8_t> jsonData(full.c_str(), full.c_str()+full.length());
    auto writeRet = ctx->WriteOutput(jsonData);
    if (writeRet.second.IsError()){
        return ctx->make_error("WriteOutput failed", writeRet.second);
    }

		return make_pair(
			nlohmann::json(
				{
					{"full" ,    full},
					{"version",  ELV_VERSION::version},
					{"branch", 	 ELV_VERSION::branch},
					{"revision", ELV_VERSION::revision},
					{"date" ,    date.c_str()}
				}
			),
			E(false));
	}


	// // TBD json format for init
	// extern "C" int _context_init(int outsz, char* outbuf, char* argbuf){
	// 	ArgumentBuffer args(argbuf);
	// 	auto init_data = json::parse(args[0]);
	// 	return 0;
	// }

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
		if (!res.second.IsError()){
			j_return["result"] = res.first;
		} else {
		    j_return["error"] = res.first;
		}
		std::vector<std::string> vec;
		vec.push_back(j_return.dump());
		call_ret = ArgumentBuffer::argv2buf(vec, out, cb);
	}else{
		std::string error_msg = "unknown method " + method;
		j_return["error"] = E(error_msg.c_str()).Kind(E::Other).getJSON();
		std::vector<std::string> vec;
		vec.push_back(j_return.dump());
		call_ret = ArgumentBuffer::argv2buf(vec, out, cb);
	}
	elv_context::_context_map.erase(new_ctx.first);
	return call_ret;
}catch (nlohmann::json::exception& e){
	std::string ret = "message: ";
	ret += e.what();
	ret += "exception id: ";
	ret +=  e.id;
	j_return["error"] = E(ret).Kind(E::Other).Cause(e.what()).getJSON();
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}catch(std::exception& e){
	j_return["error"] = E("_JPC").Cause(e.what()).getJSON();
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}catch(...){
	std::string ret = "_JPC ELIPSIS handler caught exception";
	j_return["error"] = E(ret).getJSON();
	std::vector<std::string> vec;
	vec.push_back(j_return.dump());
	return ArgumentBuffer::argv2buf(vec, out, cb);
}
}
