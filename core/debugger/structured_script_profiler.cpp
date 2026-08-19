/**************************************************************************/
/*  structured_script_profiler.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "structured_script_profiler.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/templates/hash_set.h"
#include "core/templates/sort_array.h"

namespace {

struct SerializableFunctionRecord {
	String signature;
	uint64_t calls = 0;
	uint64_t self_us = 0;
	uint64_t total_us = 0;
};

struct FunctionRecordSort {
	bool operator()(const SerializableFunctionRecord &p_a, const SerializableFunctionRecord &p_b) const {
		if (p_a.self_us != p_b.self_us) {
			return p_a.self_us > p_b.self_us;
		}
		if (p_a.signature != p_b.signature) {
			return p_a.signature < p_b.signature;
		}
		if (p_a.total_us != p_b.total_us) {
			return p_a.total_us > p_b.total_us;
		}
		return p_a.calls > p_b.calls;
	}
};

} // namespace

Error StructuredScriptProfilerWriter::parse_options(const Array &p_options, Options &r_options, bool &r_structured) {
	r_structured = false;
	r_options = Options();

	int structured_index = -1;
	for (int i = 0; i < p_options.size(); i++) {
		if (p_options[i].get_type() == Variant::DICTIONARY && Dictionary(p_options[i]).has("structured_output_version")) {
			ERR_FAIL_COND_V_MSG(structured_index != -1, ERR_INVALID_PARAMETER, "Structured script profiler arguments contain multiple versioned option dictionaries.");
			structured_index = i;
		}
	}
	if (structured_index == -1) {
		return OK;
	}
	r_structured = true;
	ERR_FAIL_COND_V_MSG(p_options.size() != 1 || structured_index != 0, ERR_INVALID_PARAMETER, "Structured script profiler arguments must contain exactly one versioned options dictionary.");

	const Dictionary options = p_options[0];

	static const char *required_keys[] = {
		"structured_output_version",
		"output_format",
		"output_path",
		"capture_id",
		"periodic_output",
		"include_zero_calls",
		"max_functions",
		"async_write",
	};

	HashSet<String> expected_keys;
	for (const char *key : required_keys) {
		expected_keys.insert(key);
		ERR_FAIL_COND_V_MSG(!options.has(key), ERR_INVALID_PARAMETER, vformat("Structured script profiler option '%s' is required.", key));
	}
	ERR_FAIL_COND_V_MSG(options.size() != int(expected_keys.size()), ERR_INVALID_PARAMETER, "Structured script profiler options contain an unknown key.");
	for (const Variant *key = options.next(nullptr); key; key = options.next(key)) {
		ERR_FAIL_COND_V_MSG(key->get_type() != Variant::STRING && key->get_type() != Variant::STRING_NAME, ERR_INVALID_PARAMETER, "Structured script profiler option keys must be strings.");
		ERR_FAIL_COND_V_MSG(!expected_keys.has(String(*key)), ERR_INVALID_PARAMETER, vformat("Unknown structured script profiler option '%s'.", String(*key)));
	}

	ERR_FAIL_COND_V_MSG(options["structured_output_version"].get_type() != Variant::INT || int64_t(options["structured_output_version"]) != 1, ERR_INVALID_PARAMETER, "'structured_output_version' must be integer 1.");
	ERR_FAIL_COND_V_MSG(options["output_format"].get_type() != Variant::STRING || String(options["output_format"]) != "jsonl", ERR_INVALID_PARAMETER, "'output_format' must be 'jsonl'.");
	ERR_FAIL_COND_V_MSG(options["output_path"].get_type() != Variant::STRING, ERR_INVALID_PARAMETER, "'output_path' must be a string.");
	ERR_FAIL_COND_V_MSG(options["capture_id"].get_type() != Variant::STRING, ERR_INVALID_PARAMETER, "'capture_id' must be a string.");
	ERR_FAIL_COND_V_MSG(options["periodic_output"].get_type() != Variant::BOOL || bool(options["periodic_output"]), ERR_INVALID_PARAMETER, "'periodic_output' must be false.");
	ERR_FAIL_COND_V_MSG(options["include_zero_calls"].get_type() != Variant::BOOL || bool(options["include_zero_calls"]), ERR_INVALID_PARAMETER, "'include_zero_calls' must be false.");
	ERR_FAIL_COND_V_MSG(options["max_functions"].get_type() != Variant::INT, ERR_INVALID_PARAMETER, "'max_functions' must be an integer.");
	ERR_FAIL_COND_V_MSG(options["async_write"].get_type() != Variant::BOOL || !bool(options["async_write"]), ERR_INVALID_PARAMETER, "'async_write' must be true.");

	const int64_t max_functions = options["max_functions"];
	r_options.output_path = options["output_path"];
	r_options.capture_id = options["capture_id"];

	ERR_FAIL_COND_V_MSG(r_options.output_path.is_empty() || !r_options.output_path.is_absolute_path(), ERR_INVALID_PARAMETER, "'output_path' must be a non-empty absolute path.");
	ERR_FAIL_COND_V_MSG(r_options.capture_id.is_empty(), ERR_INVALID_PARAMETER, "'capture_id' must not be empty.");
	ERR_FAIL_COND_V_MSG(max_functions < 1 || max_functions > MAX_PROFILE_FUNCTIONS, ERR_INVALID_PARAMETER, vformat("'max_functions' must be between 1 and %d.", MAX_PROFILE_FUNCTIONS));
	r_options.max_functions = max_functions;

	return OK;
}

Error StructuredScriptProfilerWriter::copy_functions(const ScriptLanguage::ProfilingInfo *p_source, int p_count, Vector<FunctionRecord> &r_functions) {
	ERR_FAIL_NULL_V(p_source, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(p_count < 0 || p_count > MAX_PROFILE_FUNCTIONS, ERR_PARAMETER_RANGE_ERROR, vformat("Structured script profiler snapshot count must be between 0 and %d.", MAX_PROFILE_FUNCTIONS));
	r_functions.resize(p_count);
	for (int i = 0; i < p_count; i++) {
		r_functions.write[i].signature = p_source[i].signature;
		r_functions.write[i].calls = p_source[i].call_count;
		r_functions.write[i].self_us = p_source[i].self_time;
		r_functions.write[i].total_us = p_source[i].total_time;
	}
	return OK;
}

Error StructuredScriptProfilerWriter::serialize_snapshot(const Snapshot &p_snapshot, String &r_jsonl_record) {
	Vector<SerializableFunctionRecord> called_functions;
	called_functions.reserve(p_snapshot.functions.size());
	for (const FunctionRecord &function : p_snapshot.functions) {
		if (function.calls > 0) {
			SerializableFunctionRecord serializable;
			serializable.signature = function.signature;
			serializable.calls = function.calls;
			serializable.self_us = function.self_us;
			serializable.total_us = function.total_us;
			called_functions.push_back(serializable);
		}
	}

	SortArray<SerializableFunctionRecord, FunctionRecordSort> sort;
	sort.sort(called_functions.ptrw(), called_functions.size());

	const int called_function_count = called_functions.size();
	const int reported_function_count = MIN(called_function_count, p_snapshot.options.max_functions);
	Array functions;
	functions.resize(reported_function_count);
	for (int i = 0; i < reported_function_count; i++) {
		Dictionary function;
		function["signature"] = called_functions[i].signature;
		function["calls"] = int64_t(called_functions[i].calls);
		function["self_us"] = int64_t(called_functions[i].self_us);
		function["total_us"] = int64_t(called_functions[i].total_us);
		functions[i] = function;
	}

	Dictionary record;
	record["schema_version"] = 1;
	record["type"] = "script_profile_capture";
	record["capture_id"] = p_snapshot.options.capture_id;
	record["engine_version"] = p_snapshot.engine_version;
	record["started_ticks_usec"] = int64_t(p_snapshot.started_ticks_usec);
	record["stopped_ticks_usec"] = int64_t(p_snapshot.stopped_ticks_usec);
	record["sampled_frames"] = int64_t(p_snapshot.sampled_frames);
	record["called_function_count"] = called_function_count;
	record["reported_function_count"] = reported_function_count;
	record["truncated"] = reported_function_count < called_function_count;
	record["functions"] = functions;

	r_jsonl_record = JSON::stringify(record, "", true, false) + "\n";
	ERR_FAIL_COND_V_MSG(uint64_t(r_jsonl_record.utf8().length()) > MAX_RECORD_BYTES, ERR_OUT_OF_MEMORY, "Structured script profiler record exceeds the 1 MiB limit.");
	return OK;
}

Error StructuredScriptProfilerWriter::preflight(const Options &p_options) const {
	ERR_FAIL_COND_V_MSG(p_options.output_path.is_empty() || !p_options.output_path.is_absolute_path(), ERR_INVALID_PARAMETER, "Structured script profiler output path must be absolute.");

	Error error = OK;
	Ref<FileAccess> file;
	if (FileAccess::exists(p_options.output_path)) {
		file = FileAccess::open(p_options.output_path, FileAccess::READ_WRITE, &error);
	} else {
		file = FileAccess::open(p_options.output_path, FileAccess::WRITE_READ, &error);
	}
	ERR_FAIL_COND_V_MSG(error != OK || file.is_null(), error != OK ? error : ERR_FILE_CANT_OPEN, vformat("Cannot open structured script profiler output '%s'.", p_options.output_path));
	ERR_FAIL_COND_V_MSG(file->get_length() > MAX_SESSION_BYTES, ERR_OUT_OF_MEMORY, "Structured script profiler session file already exceeds the 8 MiB limit.");
	return OK;
}

Error StructuredScriptProfilerWriter::start() {
	MutexLock lock(mutex);
	ERR_FAIL_COND_V_MSG(started, ERR_ALREADY_IN_USE, "Structured script profiler writer is already started.");
	ERR_FAIL_COND_V_MSG(shutdown_requested, ERR_UNCONFIGURED, "Structured script profiler writer has been shut down.");
#ifdef TESTS_ENABLED
	ERR_FAIL_COND_V_MSG(fail_start_for_test, ERR_UNAVAILABLE, "Structured script profiler writer start failure requested by test.");
#endif
	const int pending = queue.size();
	thread.start(_thread_func, this);
	ERR_FAIL_COND_V_MSG(!thread.is_started(), ERR_UNAVAILABLE, "Could not start the structured script profiler writer thread.");
	started = true;
#ifdef TESTS_ENABLED
	successful_starts_for_test++;
#endif
	if (pending > 0) {
		semaphore.post(pending);
	}
	return OK;
}

Error StructuredScriptProfilerWriter::enqueue(const Snapshot &p_snapshot) {
	MutexLock lock(mutex);
	ERR_FAIL_COND_V_MSG(shutdown_requested, ERR_UNCONFIGURED, "Structured script profiler writer has been shut down.");
	ERR_FAIL_COND_V_MSG(queue.size() >= MAX_PENDING_CAPTURES, ERR_BUSY, "Structured script profiler writer queue is full.");
	queue.push_back(p_snapshot);
	if (started) {
		semaphore.post();
	}
	return OK;
}

void StructuredScriptProfilerWriter::_thread_func(void *p_userdata) {
	static_cast<StructuredScriptProfilerWriter *>(p_userdata)->_thread_loop();
}

void StructuredScriptProfilerWriter::_thread_loop() {
	while (true) {
		semaphore.wait();

		Snapshot snapshot;
		bool has_snapshot = false;
		bool should_exit = false;
		{
			MutexLock lock(mutex);
			if (!queue.is_empty()) {
				snapshot = queue.front()->get();
				queue.pop_front();
				has_snapshot = true;
			} else if (shutdown_requested) {
				should_exit = true;
			}
		}

		if (has_snapshot) {
			const Error error = _write_snapshot(snapshot);
			if (error != OK) {
				ERR_PRINT(vformat("Structured script profiler capture '%s' failed to write (error %d).", snapshot.options.capture_id, error));
			}
			continue;
		}
		if (should_exit) {
			return;
		}
	}
}

Error StructuredScriptProfilerWriter::_write_snapshot(const Snapshot &p_snapshot) {
	String record;
	Error error = serialize_snapshot(p_snapshot, record);
	ERR_FAIL_COND_V(error != OK, error);

	Ref<FileAccess> file = FileAccess::open(p_snapshot.options.output_path, FileAccess::READ_WRITE, &error);
	ERR_FAIL_COND_V_MSG(error != OK || file.is_null(), error != OK ? error : ERR_FILE_CANT_OPEN, vformat("Cannot append structured script profile output '%s'.", p_snapshot.options.output_path));

	const CharString utf8 = record.utf8();
	const uint64_t record_bytes = utf8.length();
	const uint64_t existing_bytes = file->get_length();
	ERR_FAIL_COND_V_MSG(existing_bytes > MAX_SESSION_BYTES || record_bytes > MAX_SESSION_BYTES - existing_bytes, ERR_OUT_OF_MEMORY, "Structured script profiler session output exceeds the 8 MiB limit.");

	file->seek_end();
	ERR_FAIL_COND_V_MSG(!file->store_buffer(reinterpret_cast<const uint8_t *>(utf8.get_data()), record_bytes), ERR_FILE_CANT_WRITE, "Could not append the complete structured script profiler record.");
	file->flush();
	ERR_FAIL_COND_V_MSG(file->get_error() != OK, file->get_error(), "Could not flush the structured script profiler record.");
	return OK;
}

void StructuredScriptProfilerWriter::shutdown() {
	{
		MutexLock lock(mutex);
		if (shutdown_requested) {
			return;
		}
		shutdown_requested = true;
		if (!started) {
			queue.clear();
			return;
		}
	}
	semaphore.post();
	thread.wait_to_finish();
	MutexLock lock(mutex);
	started = false;
}

#ifdef TESTS_ENABLED
int StructuredScriptProfilerWriter::pending_count_for_test() const {
	MutexLock lock(mutex);
	return queue.size();
}
#endif

StructuredScriptProfilerWriter::~StructuredScriptProfilerWriter() {
	shutdown();
}
