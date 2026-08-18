/**************************************************************************/
/*  test_structured_script_profiler.h                                     */
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

#include "core/debugger/engine_debugger.h"
#include "core/debugger/structured_script_profiler.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestStructuredScriptProfiler {

static Array make_options(const String &p_output_path, const String &p_capture_id = "capture:0", int p_max_functions = 512) {
	Dictionary options;
	options["structured_output_version"] = 1;
	options["output_format"] = "jsonl";
	options["output_path"] = p_output_path;
	options["capture_id"] = p_capture_id;
	options["periodic_output"] = false;
	options["include_zero_calls"] = false;
	options["max_functions"] = p_max_functions;
	options["async_write"] = true;
	Array arguments;
	arguments.push_back(options);
	return arguments;
}

static StructuredScriptProfilerWriter::Snapshot make_snapshot(const String &p_output_path, const String &p_capture_id) {
	StructuredScriptProfilerWriter::Snapshot snapshot;
	snapshot.options.output_path = p_output_path;
	snapshot.options.capture_id = p_capture_id;
	snapshot.options.max_functions = 2;
	snapshot.engine_version = "test-engine";
	snapshot.started_ticks_usec = 100;
	snapshot.stopped_ticks_usec = 200;
	snapshot.sampled_frames = 4;

	StructuredScriptProfilerWriter::FunctionRecord slow;
	slow.signature = "res://slow.gd::slow";
	slow.calls = 2;
	slow.self_us = 80;
	slow.total_us = 100;
	snapshot.functions.push_back(slow);

	StructuredScriptProfilerWriter::FunctionRecord tie_b;
	tie_b.signature = "res://b.gd::tied";
	tie_b.calls = 1;
	tie_b.self_us = 40;
	tie_b.total_us = 50;
	snapshot.functions.push_back(tie_b);

	StructuredScriptProfilerWriter::FunctionRecord tie_a;
	tie_a.signature = "res://a.gd::tied";
	tie_a.calls = 3;
	tie_a.self_us = 40;
	tie_a.total_us = 60;
	snapshot.functions.push_back(tie_a);

	StructuredScriptProfilerWriter::FunctionRecord zero;
	zero.signature = "res://zero.gd::unused";
	zero.calls = 0;
	zero.self_us = 999;
	zero.total_us = 999;
	snapshot.functions.push_back(zero);
	return snapshot;
}

TEST_CASE("[StructuredScriptProfiler] Structured options are strict and legacy options remain unchanged") {
	StructuredScriptProfilerWriter::Options parsed;
	bool structured = false;
	const String output_path = TestUtils::get_temp_path("structured-options.jsonl");

	CHECK(StructuredScriptProfilerWriter::parse_options(Array(), parsed, structured) == OK);
	CHECK_FALSE(structured);

	Array legacy;
	legacy.push_back(17);
	CHECK(StructuredScriptProfilerWriter::parse_options(legacy, parsed, structured) == OK);
	CHECK_FALSE(structured);

	CHECK(StructuredScriptProfilerWriter::parse_options(make_options(output_path), parsed, structured) == OK);
	CHECK(structured);
	CHECK(parsed.output_path == output_path);
	CHECK(parsed.capture_id == "capture:0");
	CHECK(parsed.max_functions == 512);

	SUBCASE("Unknown keys are rejected") {
		Array arguments = make_options(output_path);
		Dictionary options = arguments[0];
		options["unknown"] = true;
		arguments[0] = options;
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(arguments, parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
	}

	SUBCASE("Extra arguments cannot fall through to legacy mode") {
		Array arguments = make_options(output_path);
		arguments.push_back("extra");
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(arguments, parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
		CHECK(structured);
	}

	SUBCASE("Missing keys are rejected") {
		Array arguments = make_options(output_path);
		Dictionary options = arguments[0];
		options.erase("capture_id");
		arguments[0] = options;
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(arguments, parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
	}

	SUBCASE("Mistyped values are rejected") {
		Array arguments = make_options(output_path);
		Dictionary options = arguments[0];
		options["async_write"] = "true";
		arguments[0] = options;
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(arguments, parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
	}

	SUBCASE("Out-of-range function caps are rejected") {
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(make_options(output_path, "capture:0", 0), parsed, structured) == ERR_INVALID_PARAMETER);
		CHECK(StructuredScriptProfilerWriter::parse_options(make_options(output_path, "capture:0", 32769), parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
	}

	SUBCASE("Relative paths are rejected") {
		ERR_PRINT_OFF;
		CHECK(StructuredScriptProfilerWriter::parse_options(make_options("relative.jsonl"), parsed, structured) == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
	}
}

TEST_CASE("[StructuredScriptProfiler] Records are deterministic, filtered, and capped") {
	const StructuredScriptProfilerWriter::Snapshot snapshot = make_snapshot(TestUtils::get_temp_path("unused.jsonl"), "capture:deterministic");
	String record;
	CHECK(StructuredScriptProfilerWriter::serialize_snapshot(snapshot, record) == OK);
	CHECK(record.ends_with("\n"));

	const Variant parsed = JSON::parse_string(record.strip_edges());
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary dictionary = parsed;
	CHECK(int64_t(dictionary["schema_version"]) == 1);
	CHECK(String(dictionary["type"]) == "script_profile_capture");
	CHECK(String(dictionary["capture_id"]) == "capture:deterministic");
	CHECK(int64_t(dictionary["called_function_count"]) == 3);
	CHECK(int64_t(dictionary["reported_function_count"]) == 2);
	CHECK(bool(dictionary["truncated"]));

	const Array functions = dictionary["functions"];
	REQUIRE(functions.size() == 2);
	CHECK(String(Dictionary(functions[0])["signature"]) == "res://slow.gd::slow");
	CHECK(String(Dictionary(functions[1])["signature"]) == "res://a.gd::tied");
}

TEST_CASE("[StructuredScriptProfiler] Oversized records fail before writing") {
	StructuredScriptProfilerWriter::Snapshot snapshot = make_snapshot(TestUtils::get_temp_path("oversized-record.jsonl"), String("x").repeat(StructuredScriptProfilerWriter::MAX_RECORD_BYTES));
	String record;
	ERR_PRINT_OFF;
	CHECK(StructuredScriptProfilerWriter::serialize_snapshot(snapshot, record) == ERR_OUT_OF_MEMORY);
	ERR_PRINT_ON;
}

TEST_CASE("[StructuredScriptProfiler] Ceiling snapshot and enqueue stay within the close-path budget") {
	Vector<ScriptLanguage::ProfilingInfo> source;
	source.resize(StructuredScriptProfilerWriter::MAX_PROFILE_FUNCTIONS);
	for (int i = 0; i < source.size(); i++) {
		source.write[i].signature = "res://ceiling.gd::function";
		source.write[i].call_count = i + 1;
		source.write[i].self_time = i;
		source.write[i].total_time = i + 1;
	}

	StructuredScriptProfilerWriter writer;
	StructuredScriptProfilerWriter::Snapshot snapshot;
	snapshot.options.output_path = TestUtils::get_temp_path("structured-ceiling.jsonl");
	snapshot.options.capture_id = "capture:ceiling";
	snapshot.options.max_functions = 512;

	const uint64_t started_usec = OS::get_singleton()->get_ticks_usec();
	CHECK(StructuredScriptProfilerWriter::copy_functions(source.ptr(), source.size(), snapshot.functions) == OK);
	CHECK(writer.enqueue(snapshot) == OK);
	const uint64_t elapsed_usec = OS::get_singleton()->get_ticks_usec() - started_usec;
	INFO("32,768-record structured profiler copy and enqueue took " << elapsed_usec << " microseconds");
	CHECK(elapsed_usec < 10000);
}

TEST_CASE("[StructuredScriptProfiler] Delayed writer preserves FIFO order and drains on shutdown") {
	const String output_path = TestUtils::get_temp_path("structured-fifo.jsonl");
	StructuredScriptProfilerWriter writer;
	StructuredScriptProfilerWriter::Options options;
	options.output_path = output_path;
	CHECK(writer.preflight(options) == OK);

	for (int i = 0; i < 6; i++) {
		CHECK(writer.enqueue(make_snapshot(output_path, "capture:" + itos(i))) == OK);
	}
	CHECK(writer.pending_count_for_test() == 6);
	CHECK(writer.start() == OK);
	writer.shutdown();

	const String output = FileAccess::get_file_as_string(output_path);
	const PackedStringArray lines = output.split("\n", false);
	REQUIRE(lines.size() == 6);
	for (int i = 0; i < lines.size(); i++) {
		const Dictionary record = JSON::parse_string(lines[i]);
		CHECK(String(record["capture_id"]) == "capture:" + itos(i));
	}
}

TEST_CASE("[StructuredScriptProfiler] Pending queue is bounded") {
	const String output_path = TestUtils::get_temp_path("structured-queue-bound.jsonl");
	StructuredScriptProfilerWriter writer;
	StructuredScriptProfilerWriter::Options options;
	options.output_path = output_path;
	CHECK(writer.preflight(options) == OK);

	for (int i = 0; i < StructuredScriptProfilerWriter::MAX_PENDING_CAPTURES; i++) {
		CHECK(writer.enqueue(make_snapshot(output_path, "capture:" + itos(i))) == OK);
	}
	ERR_PRINT_OFF;
	CHECK(writer.enqueue(make_snapshot(output_path, "capture:overflow")) == ERR_BUSY);
	ERR_PRINT_ON;
	CHECK(writer.pending_count_for_test() == StructuredScriptProfilerWriter::MAX_PENDING_CAPTURES);
	CHECK(writer.start() == OK);
	writer.shutdown();
}

TEST_CASE("[StructuredScriptProfiler] Session bound rejects a record without a partial line") {
	const String output_path = TestUtils::get_temp_path("structured-session-bound.jsonl");
	Ref<FileAccess> file = FileAccess::open(output_path, FileAccess::WRITE_READ);
	REQUIRE(file.is_valid());
	REQUIRE(file->resize(StructuredScriptProfilerWriter::MAX_SESSION_BYTES) == OK);
	file.unref();

	StructuredScriptProfilerWriter writer;
	ERR_PRINT_OFF;
	CHECK(writer.write_snapshot_for_test(make_snapshot(output_path, "capture:too-large")) == ERR_OUT_OF_MEMORY);
	ERR_PRINT_ON;
	file = FileAccess::open(output_path, FileAccess::READ);
	REQUIRE(file.is_valid());
	CHECK(file->get_length() == StructuredScriptProfilerWriter::MAX_SESSION_BYTES);
}

class RejectingDebugger : public EngineDebugger {
public:
	void send_message(const String &p_msg, const Array &p_data) override {}
	void send_error(const String &p_func, const String &p_file, int p_line, const String &p_err, const String &p_descr, bool p_editor_notify, ErrorHandlerType p_type) override {}
	void debug(bool p_can_continue, bool p_is_error_breakpoint) override {}
};

TEST_CASE("[StructuredScriptProfiler] Failed toggles do not report an active profiler") {
	static const StringName profiler_name = "structured-test-rejecting-profiler";
	EngineDebugger::Profiler profiler(
			nullptr,
			[](void *p_user, bool p_enable, const Array &p_options) {
				return p_enable ? ERR_INVALID_PARAMETER : ERR_UNCONFIGURED;
			},
			nullptr,
			nullptr);
	EngineDebugger::register_profiler(profiler_name, profiler);
	RejectingDebugger debugger;
	ERR_PRINT_OFF;
	CHECK(debugger.profiler_enable(profiler_name, true) == ERR_INVALID_PARAMETER);
	CHECK_FALSE(EngineDebugger::is_profiling(profiler_name));
	CHECK(debugger.profiler_enable(profiler_name, false) == ERR_UNCONFIGURED);
	CHECK_FALSE(EngineDebugger::is_profiling(profiler_name));
	ERR_PRINT_ON;
	EngineDebugger::unregister_profiler(profiler_name);
}

} // namespace TestStructuredScriptProfiler
