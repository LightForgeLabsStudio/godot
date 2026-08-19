/**************************************************************************/
/*  structured_script_profiler.h                                          */
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

#pragma once

#include "core/object/script_language.h"
#include "core/os/mutex.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/string/string_name.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"

class StructuredScriptProfilerWriter {
public:
	static constexpr int MAX_PENDING_CAPTURES = 8;
	static constexpr int MAX_PROFILE_FUNCTIONS = 32768;
	static constexpr uint64_t MAX_RECORD_BYTES = 1024 * 1024;
	static constexpr uint64_t MAX_SESSION_BYTES = 8 * 1024 * 1024;

	struct Options {
		String output_path;
		String capture_id;
		int max_functions = 0;
	};

	struct FunctionRecord {
		StringName signature;
		uint64_t calls = 0;
		uint64_t self_us = 0;
		uint64_t total_us = 0;
	};

	struct Snapshot {
		Options options;
		String engine_version;
		uint64_t started_ticks_usec = 0;
		uint64_t stopped_ticks_usec = 0;
		uint64_t sampled_frames = 0;
		Vector<FunctionRecord> functions;
	};

private:
	Mutex mutex;
	Semaphore semaphore;
	Thread thread;
	List<Snapshot> queue;
	bool started = false;
	bool shutdown_requested = false;
#ifdef TESTS_ENABLED
	bool fail_start_for_test = false;
	int successful_starts_for_test = 0;
#endif

	static void _thread_func(void *p_userdata);
	void _thread_loop();
	Error _write_snapshot(const Snapshot &p_snapshot);

public:
	static Error parse_options(const Array &p_options, Options &r_options, bool &r_structured);
	static Error copy_functions(const ScriptLanguage::ProfilingInfo *p_source, int p_count, Vector<FunctionRecord> &r_functions);
	static Error serialize_snapshot(const Snapshot &p_snapshot, String &r_jsonl_record);

	Error preflight(const Options &p_options) const;
	Error start();
	Error enqueue(const Snapshot &p_snapshot);
	void shutdown();

#ifdef TESTS_ENABLED
	Error write_snapshot_for_test(const Snapshot &p_snapshot) { return _write_snapshot(p_snapshot); }
	int pending_count_for_test() const;
	bool is_started_for_test() const { return started; }
	void set_fail_start_for_test(bool p_fail) { fail_start_for_test = p_fail; }
	int get_successful_starts_for_test() const { return successful_starts_for_test; }
#endif

	~StructuredScriptProfilerWriter();
};
