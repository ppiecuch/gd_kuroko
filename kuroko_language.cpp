/**************************************************************************/
/*  kuroko_language.cpp                                                   */
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

#include "kuroko_language.h"

#include "kuroko_type_conversion.h"

#include "core/os/file_access.h"
#include "core/print_string.h"

// Kuroko C headers are included via kuroko_type_conversion.h.
// Re-include only what's needed here that isn't already pulled in.
#define new new_size
extern "C" {
#include <kuroko/compiler.h>
}
#undef new
#undef vm
// Restore likely/unlikely after kuroko headers.
#undef likely
#undef unlikely
#if defined(__GNUC__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

KurokoLanguage *KurokoLanguage::singleton = nullptr;

KurokoLanguage *KurokoLanguage::get_singleton() {
	return singleton;
}

void KurokoLanguage::_bind_methods() {
	ClassDB::bind_method(D_METHOD("execute", "source"), &KurokoLanguage::execute);
	ClassDB::bind_method(D_METHOD("execute_file", "path"), &KurokoLanguage::execute_file);
	ClassDB::bind_method(D_METHOD("compile_to_bytecode", "source_path", "output_path"), &KurokoLanguage::compile_to_bytecode);
}

void KurokoLanguage::_init_vm() {
	if (vm_initialized) {
		return;
	}
	// Use NO_DEFAULT_MODULES to prevent the VM from calling the C-level module
	// inits (time.c, os.c, fileio.c) which are replaced by Godot-backed versions.
	// Then explicitly init the kuroko system module (for module_paths) and the
	// Godot-backed modules.
	krk_initVM(KRK_GLOBAL_CLEAN_OUTPUT | KRK_GLOBAL_NO_DEFAULT_MODULES);

	krk_module_init_kuroko(); // Sets up kuroko.module_paths (required for imports).
	krk_module_init_gc();
	krk_module_init_time();   // Godot-backed (from kuroko_godot_modules.cpp).
	krk_module_init_os();     // Godot-backed.
	krk_module_init_fileio(); // Godot-backed.
#ifndef KRK_DISABLE_DEBUG
	krk_module_init_dis();
#endif

	krk_resetStack(); // Reset stack after module inits (matches krk_initVM behavior).
	krk_startModule("__main__");
	vm_initialized = true;
	print_line("Kuroko: VM initialized (v1.4.0).");
}

void KurokoLanguage::_free_vm() {
	if (!vm_initialized) {
		return;
	}
	krk_freeVM();
	vm_initialized = false;
	print_line("Kuroko: VM freed.");
}

static void _krk_handle_exception() {
	KrkValue exception = krk_currentThread.currentException;
	String err_msg = "Kuroko runtime error";
	if (IS_INSTANCE(exception)) {
		KrkValue msg_val;
		if (krk_tableGet(&AS_INSTANCE(exception)->fields, OBJECT_VAL(krk_copyString("arg", 3)), &msg_val)) {
			if (IS_STRING(msg_val)) {
				err_msg = String::utf8(AS_CSTRING(msg_val));
			}
		}
	}
	krk_currentThread.flags &= ~KRK_THREAD_HAS_EXCEPTION;
	ERR_PRINT("Kuroko: " + err_msg);
	krk_resetStack();
}

Variant KurokoLanguage::execute(const String &p_source) {
	_init_vm();

	String source = p_source.strip_edges();
	if (source.empty()) {
		return Variant();
	}

	// Strategy for returning expression values from Kuroko:
	// - Single expression (no newline): krk_interpret returns the value directly
	//   via the compiler's TYPE_LAMBDA optimization (maybeSingleExpression).
	// - Multi-line with expression at end: run setup lines at module level via
	//   krk_interpret (variables become module globals via OP_DEFINE_GLOBAL and
	//   persist in module->fields), then evaluate the last expression separately
	//   via a second krk_interpret call (resolves globals via OP_GET_GLOBAL).
	// - Multi-line without expression: run everything, return Variant().

	int last_newline = source.find_last("\n");

	if (last_newline == -1) {
		// Single line — krk_interpret handles this directly.
		CharString utf8 = source.utf8();
		KrkValue result = krk_interpret(utf8.get_data(), (char *)"<godot>");
		if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
			_krk_handle_exception();
			return Variant();
		}
		return KurokoTypeConversion::krk_value_to_variant(result);
	}

	// Multi-line: check if last line is a returnable expression.
	String setup = source.substr(0, last_newline);
	String last_line = source.substr(last_newline + 1).strip_edges();

	bool last_is_expr = !last_line.empty() &&
			!last_line.begins_with("import ") &&
			!last_line.begins_with("def ") &&
			!last_line.begins_with("class ") &&
			!last_line.begins_with("#") &&
			!last_line.begins_with("if ") &&
			!last_line.begins_with("for ") &&
			!last_line.begins_with("while ") &&
			!last_line.ends_with(":");

	if (!last_is_expr) {
		// No returnable expression — run everything at module level.
		CharString utf8 = source.utf8();
		krk_interpret(utf8.get_data(), (char *)"<godot>");
		if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
			_krk_handle_exception();
			return Variant();
		}
		return Variant();
	}

	// Run setup lines at module level (variables become module globals).
	if (!setup.strip_edges().empty()) {
		CharString utf8_setup = setup.utf8();
		krk_interpret(utf8_setup.get_data(), (char *)"<godot>");
		if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
			_krk_handle_exception();
			return Variant();
		}
	}

	// Evaluate last expression — single-line krk_interpret returns the value
	// via TYPE_LAMBDA. Module globals from setup are accessible via OP_GET_GLOBAL.
	CharString utf8_expr = last_line.utf8();
	KrkValue result = krk_interpret(utf8_expr.get_data(), (char *)"<godot>");
	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		_krk_handle_exception();
		return Variant();
	}

	return KurokoTypeConversion::krk_value_to_variant(result);
}

Variant KurokoLanguage::execute_file(const String &p_path) {
	_init_vm();

	String source = FileAccess::get_file_as_string(p_path);
	if (source.empty()) {
		ERR_PRINT("Kuroko: Failed to read file: " + p_path);
		return Variant();
	}

	CharString utf8_source = source.utf8();
	CharString utf8_path = p_path.utf8();
	KrkValue result = krk_interpret(utf8_source.get_data(), (char *)utf8_path.get_data());

	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		krk_currentThread.flags &= ~KRK_THREAD_HAS_EXCEPTION;
		ERR_PRINT("Kuroko: Error executing " + p_path);
		krk_resetStack();
		return Variant();
	}

	return KurokoTypeConversion::krk_value_to_variant(result);
}

Error KurokoLanguage::compile_to_bytecode(const String &p_source_path, const String &p_output_path) {
	// TODO: Implement in Phase 7 (bytecode marshal).
	ERR_PRINT("Kuroko: compile_to_bytecode not yet implemented.");
	return ERR_UNAVAILABLE;
}

KurokoLanguage::KurokoLanguage() {
	singleton = this;
	vm_initialized = false;
}

KurokoLanguage::~KurokoLanguage() {
	_free_vm();
	singleton = nullptr;
}

// --- Doctests ---

#ifdef DOCTEST
#include "doctest/doctest.h"
#include "doctest/doctest_godot.h"

TEST_SUITE("[[gd_kuroko]] KurokoLanguage") {
	TEST_CASE("[kuroko] singleton exists") {
		KurokoLanguage *lang = KurokoLanguage::get_singleton();
		REQUIRE(lang != nullptr);
	}

	TEST_CASE("[kuroko] execute integer arithmetic") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("1 + 2");
		CHECK(result.get_type() == Variant::INT);
		CHECK((int64_t)result == 3);

		result = lang->execute("10 * 5 - 3");
		CHECK((int64_t)result == 47);

		result = lang->execute("2 ** 10");
		CHECK((int64_t)result == 1024);

		result = lang->execute("17 % 5");
		CHECK((int64_t)result == 2);

		result = lang->execute("17 // 5");
		CHECK((int64_t)result == 3);
	}

	TEST_CASE("[kuroko] execute float arithmetic") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("1.5 + 2.5");
		CHECK(result.get_type() == Variant::REAL);
		CHECK((double)result == doctest::Approx(4.0));

		result = lang->execute("3.14 * 2");
		CHECK((double)result == doctest::Approx(6.28));
	}

	TEST_CASE("[kuroko] execute string operations") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("'hello' + ' ' + 'world'");
		CHECK(result.get_type() == Variant::STRING);
		CHECK((String)result == "hello world");

		result = lang->execute("'abc' * 3");
		CHECK((String)result == "abcabcabc");

		result = lang->execute("len('test')");
		CHECK((int64_t)result == 4);
	}

	TEST_CASE("[kuroko] execute boolean and None") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("True");
		CHECK(result.get_type() == Variant::BOOL);
		CHECK((bool)result == true);

		result = lang->execute("False");
		CHECK((bool)result == false);

		result = lang->execute("None");
		CHECK(result.get_type() == Variant::NIL);

		result = lang->execute("1 < 2");
		CHECK((bool)result == true);

		result = lang->execute("1 > 2");
		CHECK((bool)result == false);
	}

	TEST_CASE("[kuroko] execute list comprehension") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("[x * x for x in range(5)]");
		CHECK(result.get_type() == Variant::ARRAY);
		Array arr = result;
		REQUIRE(arr.size() == 5);
		CHECK((int64_t)arr[0] == 0);
		CHECK((int64_t)arr[1] == 1);
		CHECK((int64_t)arr[2] == 4);
		CHECK((int64_t)arr[3] == 9);
		CHECK((int64_t)arr[4] == 16);
	}

	TEST_CASE("[kuroko] execute dict literal") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("{'a': 1, 'b': 2, 'c': 3}");
		CHECK(result.get_type() == Variant::DICTIONARY);
		Dictionary dict = result;
		CHECK((int64_t)dict["a"] == 1);
		CHECK((int64_t)dict["b"] == 2);
		CHECK((int64_t)dict["c"] == 3);
	}

	TEST_CASE("[kuroko] execute function definition and call") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute(
				"def add(a, b):\n"
				"    return a + b\n"
				"add(3, 4)\n");
		CHECK((int64_t)result == 7);
	}

	TEST_CASE("[kuroko] execute class definition") {
		auto *lang = KurokoLanguage::get_singleton();
		// Note: class + assignment + method call is wrapped in the same function.
		// Kuroko scoping requires the return expression not to follow a class block
		// at the same level, so we explicitly return from a nested scope.
		Variant result = lang->execute(
				"class Point:\n"
				"    def __init__(self, x, y):\n"
				"        self.x = x\n"
				"        self.y = y\n"
				"    def magnitude(self):\n"
				"        return self.x * self.x + self.y * self.y\n"
				"Point(3, 4).magnitude()");
		CHECK(result.get_type() == Variant::INT);
		CHECK((int64_t)result == 25);
	}

	TEST_CASE("[kuroko] error handling - syntax error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
				Variant result = lang->execute("def incomplete(");
		);
		// Should return NIL on error, not crash.
	}

	TEST_CASE("[kuroko] error handling - runtime error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
				Variant result = lang->execute("1 / 0");
		);
	}

	TEST_CASE("[kuroko] error handling - name error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
				Variant result = lang->execute("undefined_variable");
		);
	}

	TEST_CASE("[kuroko] nested data structures") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("[[1, 2], [3, 4], [5, 6]]");
		CHECK(result.get_type() == Variant::ARRAY);
		Array outer = result;
		REQUIRE(outer.size() == 3);
		Array inner = outer[0];
		CHECK((int64_t)inner[0] == 1);
		CHECK((int64_t)inner[1] == 2);
	}

	TEST_CASE("[kuroko] generator expression") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("list(range(1, 6))");
		Array arr = result;
		REQUIRE(arr.size() == 5);
		CHECK((int64_t)arr[0] == 1);
		CHECK((int64_t)arr[4] == 5);
	}

	TEST_CASE("[kuroko] string formatting") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("f'value is {2 + 3}'");
		CHECK((String)result == "value is 5");
	}

	TEST_CASE("[kuroko] exception handling in script") {
		auto *lang = KurokoLanguage::get_singleton();
		// Kuroko scoping: variables inside try/except are block-local.
		// Use a list to capture the value from the except block.
		Variant result = lang->execute(
				"def test_exc():\n"
				"    let x = 0\n"
				"    try:\n"
				"        let _ = 1 / 0\n"
				"    except ZeroDivisionError:\n"
				"        x = -1\n"
				"    return x\n"
				"test_exc()");
		CHECK((int64_t)result == -1);
	}

	TEST_CASE("[kuroko] lambda") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = lang->execute("(lambda x, y: x + y)(10, 20)");
		CHECK((int64_t)result == 30);
	}
}

#endif // DOCTEST
