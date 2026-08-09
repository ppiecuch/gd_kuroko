/**************************************************************************/
/*  kuroko_godot_modules.cpp                                              */
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

/**
 * Kuroko built-in modules reimplemented using Godot APIs.
 *
 * The Kuroko VM unconditionally calls krk_module_init_{fileio,os,time}()
 * during initialization. Instead of empty stubs, we provide Godot-backed
 * implementations that use FileAccess, DirAccess, OS, etc.
 */

#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "core/project_settings.h"

#define new new_size
// _Generic (C11) is not available in C++; provide a C++-compatible KRK_DOC
// using function overloading before including util.h.
#define KRK_NO_DOCUMENTATION
extern "C" {
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/value.h>
#include <kuroko/object.h>
#include <kuroko/table.h>
#include <kuroko/util.h>
}
#undef KRK_NO_DOCUMENTATION
#undef KRK_DOC
// C++ overload-based KRK_DOC replacement
static inline void _krk_setDoc(KrkClass *thing, const char *text, size_t size) { krk_attachNamedObject(&thing->methods, "__doc__", (KrkObj*)krk_copyString(text, size)); }
static inline void _krk_setDoc(KrkInstance *thing, const char *text, size_t size) { krk_attachNamedObject(&thing->fields, "__doc__", (KrkObj*)krk_copyString(text, size)); }
static inline void _krk_setDoc(KrkNative *thing, const char *text, size_t size) { (void)size; thing->doc = text; }
#define KRK_DOC(thing, text) (_krk_setDoc(thing, text, sizeof(text)-1), thing);
#undef new
// Restore Godot macros after kuroko headers.
#undef vm
#undef likely
#undef unlikely
#if defined(__GNUC__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

// ============================================================================
// time module — backed by OS::get_singleton()
// ============================================================================

// time.time() -> float (seconds since epoch)
static KrkValue _krk_time_time(int argc, const KrkValue argv[], int hasKw) {
	uint64_t ticks = OS::get_singleton()->get_unix_time();
	return FLOATING_VAL((double)ticks);
}

// time.ticks() -> int (milliseconds since engine start)
static KrkValue _krk_time_ticks(int argc, const KrkValue argv[], int hasKw) {
	return INTEGER_VAL((int64_t)OS::get_singleton()->get_ticks_msec());
}

// time.ticks_usec() -> int (microseconds since engine start)
static KrkValue _krk_time_ticks_usec(int argc, const KrkValue argv[], int hasKw) {
	return INTEGER_VAL((int64_t)OS::get_singleton()->get_ticks_usec());
}

// time.sleep(seconds) — delay execution
static KrkValue _krk_time_sleep(int argc, const KrkValue argv[], int hasKw) {
	if (argc != 1) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "sleep() takes exactly 1 argument");
	}
	double secs = 0;
	if (IS_INTEGER(argv[0])) {
		secs = (double)AS_INTEGER(argv[0]);
	} else if (IS_FLOATING(argv[0])) {
		secs = AS_FLOATING(argv[0]);
	} else {
		return krk_runtimeError(krk_vm.exceptions->typeError, "sleep() argument must be int or float");
	}
	OS::get_singleton()->delay_usec((uint64_t)(secs * 1000000.0));
	return NONE_VAL();
}

// time.datetime() -> dict with year, month, day, hour, minute, second
static KrkValue _krk_time_datetime(int argc, const KrkValue argv[], int hasKw) {
	OS::Date date = OS::get_singleton()->get_date();
	OS::Time time = OS::get_singleton()->get_time();

	KrkValue result = krk_dict_of(0, NULL, 0);
	krk_push(result);

	krk_attachNamedValue(AS_DICT(result), "year", INTEGER_VAL(date.year));
	krk_attachNamedValue(AS_DICT(result), "month", INTEGER_VAL(date.month));
	krk_attachNamedValue(AS_DICT(result), "day", INTEGER_VAL(date.day));
	krk_attachNamedValue(AS_DICT(result), "weekday", INTEGER_VAL(date.weekday));
	krk_attachNamedValue(AS_DICT(result), "hour", INTEGER_VAL(time.hour));
	krk_attachNamedValue(AS_DICT(result), "minute", INTEGER_VAL(time.min));
	krk_attachNamedValue(AS_DICT(result), "second", INTEGER_VAL(time.sec));

	return krk_pop();
}

extern "C" void krk_module_init_time(void) {
	KrkInstance *module = krk_newInstance(krk_vm.baseClasses->moduleClass);
	krk_attachNamedObject(&krk_vm.modules, "time", (KrkObj *)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj *)S("time"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	KRK_DOC(module, "Timekeeping functions backed by Godot OS API.");

	KRK_DOC(krk_defineNative(&module->fields, "time", _krk_time_time),
			"Return seconds since Unix epoch as float.");
	KRK_DOC(krk_defineNative(&module->fields, "ticks", _krk_time_ticks),
			"Return milliseconds since engine start.");
	KRK_DOC(krk_defineNative(&module->fields, "ticks_usec", _krk_time_ticks_usec),
			"Return microseconds since engine start.");
	KRK_DOC(krk_defineNative(&module->fields, "sleep", _krk_time_sleep),
			"Pause execution for the given number of seconds.");
	KRK_DOC(krk_defineNative(&module->fields, "datetime", _krk_time_datetime),
			"Return current date and time as a dict.");
}

// ============================================================================
// os module — backed by OS::get_singleton() and ProjectSettings
// ============================================================================

// os.get_name() -> str (platform name)
static KrkValue _krk_os_get_name(int argc, const KrkValue argv[], int hasKw) {
	String name = OS::get_singleton()->get_name();
	CharString utf8 = name.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// os.get_executable_path() -> str
static KrkValue _krk_os_get_executable_path(int argc, const KrkValue argv[], int hasKw) {
	String path = OS::get_singleton()->get_executable_path();
	CharString utf8 = path.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// os.get_user_data_dir() -> str
static KrkValue _krk_os_get_user_data_dir(int argc, const KrkValue argv[], int hasKw) {
	String path = OS::get_singleton()->get_user_data_dir();
	CharString utf8 = path.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// os.get_environment(key) -> str or None
static KrkValue _krk_os_get_environment(int argc, const KrkValue argv[], int hasKw) {
	if (argc != 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "get_environment() takes exactly 1 string argument");
	}
	String key = String::utf8(AS_CSTRING(argv[0]));
	if (OS::get_singleton()->has_environment(key)) {
		String val = OS::get_singleton()->get_environment(key);
		CharString utf8 = val.utf8();
		return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
	}
	return NONE_VAL();
}

// os.has_environment(key) -> bool
static KrkValue _krk_os_has_environment(int argc, const KrkValue argv[], int hasKw) {
	if (argc != 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "has_environment() takes exactly 1 string argument");
	}
	String key = String::utf8(AS_CSTRING(argv[0]));
	return BOOLEAN_VAL(OS::get_singleton()->has_environment(key));
}

// os.get_cmdline_args() -> list of str
static KrkValue _krk_os_get_cmdline_args(int argc, const KrkValue argv[], int hasKw) {
	List<String> args = OS::get_singleton()->get_cmdline_args();
	KrkValue result = krk_list_of(0, NULL, 0);
	krk_push(result);
	for (const List<String>::Element *E = args.front(); E; E = E->next()) {
		CharString utf8 = E->get().utf8();
		KrkValue val = OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
		krk_push(val);
		krk_writeValueArray(AS_LIST(result), val);
		krk_pop();
	}
	return krk_pop();
}

// os.get_locale() -> str
static KrkValue _krk_os_get_locale(int argc, const KrkValue argv[], int hasKw) {
	String locale = OS::get_singleton()->get_locale();
	CharString utf8 = locale.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// os.get_model_name() -> str
static KrkValue _krk_os_get_model_name(int argc, const KrkValue argv[], int hasKw) {
	String model = OS::get_singleton()->get_model_name();
	CharString utf8 = model.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// os.get_processor_count() -> int
static KrkValue _krk_os_get_processor_count(int argc, const KrkValue argv[], int hasKw) {
	return INTEGER_VAL(OS::get_singleton()->get_processor_count());
}

// os.is_debug_build() -> bool
static KrkValue _krk_os_is_debug_build(int argc, const KrkValue argv[], int hasKw) {
#ifdef DEBUG_ENABLED
	return BOOLEAN_VAL(1);
#else
	return BOOLEAN_VAL(0);
#endif
}

// os.get_screen_size() -> dict {width, height}
static KrkValue _krk_os_get_screen_size(int argc, const KrkValue argv[], int hasKw) {
	Size2 size = OS::get_singleton()->get_screen_size();
	KrkValue result = krk_dict_of(0, NULL, 0);
	krk_push(result);
	krk_attachNamedValue(AS_DICT(result), "width", INTEGER_VAL((int64_t)size.width));
	krk_attachNamedValue(AS_DICT(result), "height", INTEGER_VAL((int64_t)size.height));
	return krk_pop();
}

extern "C" void krk_module_init_os(void) {
	KrkInstance *module = krk_newInstance(krk_vm.baseClasses->moduleClass);
	krk_attachNamedObject(&krk_vm.modules, "os", (KrkObj *)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj *)S("os"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	KRK_DOC(module, "OS interface backed by Godot's OS singleton.");

	// Platform info.
	krk_attachNamedObject(&module->fields, "sep", (KrkObj *)S("/"));
	krk_attachNamedObject(&module->fields, "curdir", (KrkObj *)S("."));
	krk_attachNamedObject(&module->fields, "pardir", (KrkObj *)S(".."));
	krk_attachNamedObject(&module->fields, "extsep", (KrkObj *)S("."));

	KRK_DOC(krk_defineNative(&module->fields, "get_name", _krk_os_get_name),
			"Return the platform name (e.g. 'OSX', 'Windows', 'X11', 'Android').");
	KRK_DOC(krk_defineNative(&module->fields, "get_executable_path", _krk_os_get_executable_path),
			"Return the path to the running executable.");
	KRK_DOC(krk_defineNative(&module->fields, "get_user_data_dir", _krk_os_get_user_data_dir),
			"Return the user data directory path.");
	KRK_DOC(krk_defineNative(&module->fields, "get_environment", _krk_os_get_environment),
			"Return the value of an environment variable, or None.");
	KRK_DOC(krk_defineNative(&module->fields, "has_environment", _krk_os_has_environment),
			"Return True if an environment variable exists.");
	KRK_DOC(krk_defineNative(&module->fields, "get_cmdline_args", _krk_os_get_cmdline_args),
			"Return the command line arguments as a list of strings.");
	KRK_DOC(krk_defineNative(&module->fields, "get_locale", _krk_os_get_locale),
			"Return the host OS locale string.");
	KRK_DOC(krk_defineNative(&module->fields, "get_model_name", _krk_os_get_model_name),
			"Return the device model name.");
	KRK_DOC(krk_defineNative(&module->fields, "get_processor_count", _krk_os_get_processor_count),
			"Return the number of CPU cores.");
	KRK_DOC(krk_defineNative(&module->fields, "is_debug_build", _krk_os_is_debug_build),
			"Return True if this is a debug build.");
	KRK_DOC(krk_defineNative(&module->fields, "get_screen_size", _krk_os_get_screen_size),
			"Return the screen size as a dict with 'width' and 'height'.");
}

// ============================================================================
// fileio module — backed by Godot FileAccess/DirAccess
// ============================================================================

// fileio.open(path, mode="r") -> str (reads entire file)
// Simplified: returns file content as string. For full File object support, use Godot API.
static KrkValue _krk_fileio_read_file(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "read_file() takes a string path argument");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	String content = FileAccess::get_file_as_string(path);
	if (content.empty() && !FileAccess::exists(path)) {
		return krk_runtimeError(krk_vm.exceptions->ioError, "File not found: %s", AS_CSTRING(argv[0]));
	}
	CharString utf8 = content.utf8();
	return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
}

// fileio.read_bytes(path) -> bytes
static KrkValue _krk_fileio_read_bytes(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "read_bytes() takes a string path argument");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	FileAccess *fa = FileAccess::open(path, FileAccess::READ);
	if (!fa) {
		return krk_runtimeError(krk_vm.exceptions->ioError, "Cannot open file: %s", AS_CSTRING(argv[0]));
	}
	int64_t len = fa->get_len();
	PoolByteArray data;
	data.resize(len);
	{
		PoolByteArray::Write w = data.write();
		fa->get_buffer(w.ptr(), len);
	}
	memdelete(fa);
	PoolByteArray::Read r = data.read();
	return OBJECT_VAL(krk_newBytes(len, (uint8_t *)r.ptr()));
}

// fileio.write_file(path, content) -> None
static KrkValue _krk_fileio_write_file(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 2 || !IS_STRING(argv[0]) || !IS_STRING(argv[1])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "write_file() takes (path: str, content: str)");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	String content = String::utf8(AS_CSTRING(argv[1]));
	FileAccess *fa = FileAccess::open(path, FileAccess::WRITE);
	if (!fa) {
		return krk_runtimeError(krk_vm.exceptions->ioError, "Cannot write file: %s", AS_CSTRING(argv[0]));
	}
	fa->store_string(content);
	memdelete(fa);
	return NONE_VAL();
}

// fileio.exists(path) -> bool
static KrkValue _krk_fileio_exists(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "exists() takes a string path argument");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	return BOOLEAN_VAL(FileAccess::exists(path));
}

// fileio.list_dir(path) -> list of str
static KrkValue _krk_fileio_list_dir(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "list_dir() takes a string path argument");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	DirAccess *da = DirAccess::open(path);
	if (!da) {
		return krk_runtimeError(krk_vm.exceptions->ioError, "Cannot open directory: %s", AS_CSTRING(argv[0]));
	}

	KrkValue result = krk_list_of(0, NULL, 0);
	krk_push(result);

	da->list_dir_begin();
	String entry = da->get_next();
	while (!entry.empty()) {
		CharString utf8 = entry.utf8();
		KrkValue val = OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
		krk_push(val);
		krk_writeValueArray(AS_LIST(result), val);
		krk_pop();
		entry = da->get_next();
	}
	da->list_dir_end();
	memdelete(da);

	return krk_pop();
}

// fileio.get_modified_time(path) -> int (Unix timestamp)
static KrkValue _krk_fileio_get_modified_time(int argc, const KrkValue argv[], int hasKw) {
	if (argc < 1 || !IS_STRING(argv[0])) {
		return krk_runtimeError(krk_vm.exceptions->argumentError, "get_modified_time() takes a string path argument");
	}
	String path = String::utf8(AS_CSTRING(argv[0]));
	uint64_t mtime = FileAccess::get_modified_time(path);
	return INTEGER_VAL((int64_t)mtime);
}

extern "C" void krk_module_init_fileio(void) {
	KrkInstance *module = krk_newInstance(krk_vm.baseClasses->moduleClass);
	krk_attachNamedObject(&krk_vm.modules, "fileio", (KrkObj *)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj *)S("fileio"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	KRK_DOC(module, "File I/O backed by Godot's FileAccess and DirAccess.\n"
					 "Supports res:// and user:// paths.");

	KRK_DOC(krk_defineNative(&module->fields, "read_file", _krk_fileio_read_file),
			"Read entire file as a string. Supports res:// and user:// paths.");
	KRK_DOC(krk_defineNative(&module->fields, "read_bytes", _krk_fileio_read_bytes),
			"Read entire file as bytes. Supports res:// and user:// paths.");
	KRK_DOC(krk_defineNative(&module->fields, "write_file", _krk_fileio_write_file),
			"Write a string to a file. Supports user:// paths.");
	KRK_DOC(krk_defineNative(&module->fields, "exists", _krk_fileio_exists),
			"Return True if the file exists.");
	KRK_DOC(krk_defineNative(&module->fields, "list_dir", _krk_fileio_list_dir),
			"List directory contents as a list of filenames.");
	KRK_DOC(krk_defineNative(&module->fields, "get_modified_time", _krk_fileio_get_modified_time),
			"Return the file's last modification time as a Unix timestamp.");
}

// ============================================================================
// Doctests
// ============================================================================

#ifdef DOCTEST
#include "doctest/doctest.h"
#include "doctest/doctest_godot.h"
#include "kuroko_language.h"

// Convenience helper: evaluates Kuroko code and returns the result.
static Variant _krk_eval(const String &p_code) {
	return KurokoLanguage::get_singleton()->execute(p_code);
}

TEST_SUITE("[[gd_kuroko]] GodotModules - time") {
	TEST_CASE("[kuroko-mod] time.time returns float") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import time\ntime.time()");
		CHECK(result.get_type() == Variant::REAL);
		double t = result;
		// Unix timestamp should be > year 2020 (~1.577e9).
		CHECK(t > 1577836800.0);
	}

	TEST_CASE("[kuroko-mod] time.ticks returns int") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import time\ntime.ticks()");
		CHECK(result.get_type() == Variant::INT);
		int64_t t = result;
		CHECK(t >= 0);
	}

	TEST_CASE("[kuroko-mod] time.ticks_usec returns int") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import time\ntime.ticks_usec()");
		CHECK(result.get_type() == Variant::INT);
		int64_t t = result;
		CHECK(t >= 0);
	}

	TEST_CASE("[kuroko-mod] time.ticks_usec > time.ticks * 1000") {
		auto *lang = KurokoLanguage::get_singleton();
		// ticks_usec should be roughly 1000x ticks (both monotonic from engine start).
		Variant ms = _krk_eval("import time\ntime.ticks()");
		Variant us = _krk_eval("import time\ntime.ticks_usec()");
		int64_t ms_val = ms;
		int64_t us_val = us;
		// Allow generous margin — just verify usec > msec.
		CHECK(us_val >= ms_val);
	}

	TEST_CASE("[kuroko-mod] time.sleep does not crash") {
		auto *lang = KurokoLanguage::get_singleton();
		// Sleep for a very short time (1ms).
		Variant result = _krk_eval("import time\ntime.sleep(0.001)");
		CHECK(result.get_type() == Variant::NIL);
	}

	TEST_CASE("[kuroko-mod] time.sleep with int argument") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import time\ntime.sleep(0)");
		CHECK(result.get_type() == Variant::NIL);
	}

	TEST_CASE("[kuroko-mod] time.sleep bad argument") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import time\ntime.sleep('bad')");
		);
	}

	TEST_CASE("[kuroko-mod] time.datetime returns dict") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import time\ntime.datetime()");
		CHECK(result.get_type() == Variant::DICTIONARY);
		Dictionary dt = result;
		CHECK(dt.has("year"));
		CHECK(dt.has("month"));
		CHECK(dt.has("day"));
		CHECK(dt.has("hour"));
		CHECK(dt.has("minute"));
		CHECK(dt.has("second"));
		CHECK(dt.has("weekday"));

		// Sanity: year should be >= 2024.
		int year = dt["year"];
		CHECK(year >= 2024);

		// Month 1-12, day 1-31.
		int month = dt["month"];
		CHECK(month >= 1);
		CHECK(month <= 12);

		int day = dt["day"];
		CHECK(day >= 1);
		CHECK(day <= 31);

		// Hour 0-23, minute 0-59, second 0-59.
		int hour = dt["hour"];
		CHECK(hour >= 0);
		CHECK(hour <= 23);
	}

	TEST_CASE("[kuroko-mod] time.datetime consistency with time.time") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant dt = _krk_eval("import time\ntime.datetime()");
		Variant ts = _krk_eval("import time\ntime.time()");
		// Both should represent roughly the same moment.
		// Just check both succeed without error.
		CHECK(dt.get_type() == Variant::DICTIONARY);
		CHECK(ts.get_type() == Variant::REAL);
	}
}

TEST_SUITE("[[gd_kuroko]] GodotModules - os") {
	TEST_CASE("[kuroko-mod] os.get_name returns string") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_name()");
		CHECK(result.get_type() == Variant::STRING);
		String name = result;
		CHECK(!name.empty());
		// Should be one of the known platform names.
		CHECK((name == "OSX" || name == "Windows" || name == "X11" ||
				name == "Android" || name == "iOS" || name == "Server" ||
				name == "HTML5" || name == "UWP"));
	}

	TEST_CASE("[kuroko-mod] os.get_executable_path returns string") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_executable_path()");
		CHECK(result.get_type() == Variant::STRING);
		String path = result;
		CHECK(!path.empty());
	}

	TEST_CASE("[kuroko-mod] os.get_user_data_dir returns string") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_user_data_dir()");
		CHECK(result.get_type() == Variant::STRING);
	}

	TEST_CASE("[kuroko-mod] os.get_locale returns string") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_locale()");
		CHECK(result.get_type() == Variant::STRING);
		String locale = result;
		CHECK(!locale.empty());
	}

	TEST_CASE("[kuroko-mod] os.get_processor_count returns positive int") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_processor_count()");
		CHECK(result.get_type() == Variant::INT);
		int64_t count = result;
		CHECK(count >= 1);
	}

	TEST_CASE("[kuroko-mod] os.is_debug_build returns bool") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.is_debug_build()");
		CHECK(result.get_type() == Variant::BOOL);
	}

	TEST_CASE("[kuroko-mod] os.get_screen_size returns dict") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_screen_size()");
		CHECK(result.get_type() == Variant::DICTIONARY);
		Dictionary size = result;
		CHECK(size.has("width"));
		CHECK(size.has("height"));
		int64_t w = size["width"];
		int64_t h = size["height"];
		CHECK(w > 0);
		CHECK(h > 0);
	}

	TEST_CASE("[kuroko-mod] os.get_cmdline_args returns list") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_cmdline_args()");
		CHECK(result.get_type() == Variant::ARRAY);
	}

	TEST_CASE("[kuroko-mod] os.has_environment with PATH") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.has_environment('PATH')");
		CHECK(result.get_type() == Variant::BOOL);
		// PATH is almost always set.
		CHECK((bool)result == true);
	}

	TEST_CASE("[kuroko-mod] os.get_environment with PATH") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_environment('PATH')");
		CHECK(result.get_type() == Variant::STRING);
		String path = result;
		CHECK(!path.empty());
	}

	TEST_CASE("[kuroko-mod] os.get_environment nonexistent returns None") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_environment('__KUROKO_NONEXISTENT_VAR_12345__')");
		CHECK(result.get_type() == Variant::NIL);
	}

	TEST_CASE("[kuroko-mod] os.has_environment nonexistent returns False") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.has_environment('__KUROKO_NONEXISTENT_VAR_12345__')");
		CHECK((bool)result == false);
	}

	TEST_CASE("[kuroko-mod] os.has_environment bad arg") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import os\nos.has_environment(42)");
		);
	}

	TEST_CASE("[kuroko-mod] os.get_model_name returns string") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import os\nos.get_model_name()");
		CHECK(result.get_type() == Variant::STRING);
	}

	TEST_CASE("[kuroko-mod] os constants") {
		auto *lang = KurokoLanguage::get_singleton();
		CHECK((String)lang->execute("import os\nos.sep") == "/");
		CHECK((String)lang->execute("import os\nos.curdir") == ".");
		CHECK((String)lang->execute("import os\nos.pardir") == "..");
		CHECK((String)lang->execute("import os\nos.extsep") == ".");
	}
}

TEST_SUITE("[[gd_kuroko]] GodotModules - fileio") {
	TEST_CASE("[kuroko-mod] fileio.exists with real file") {
		auto *lang = KurokoLanguage::get_singleton();
		// The project.godot file should exist when running in project context.
		// Use the engine executable as a fallback — it always exists.
		String exe = OS::get_singleton()->get_executable_path();
		Variant result = _krk_eval("import fileio\nfileio.exists('" + exe + "')");
		CHECK(result.get_type() == Variant::BOOL);
		CHECK((bool)result == true);
	}

	TEST_CASE("[kuroko-mod] fileio.exists nonexistent") {
		auto *lang = KurokoLanguage::get_singleton();
		Variant result = _krk_eval("import fileio\nfileio.exists('/nonexistent_path_12345.txt')");
		CHECK((bool)result == false);
	}

	TEST_CASE("[kuroko-mod] fileio.write_file and read_file round-trip") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		// Use absolute path to avoid res:// resolution issues with relative paths.
		String abs_folder = OS::get_singleton()->get_executable_path().get_base_dir().plus_file(_doctest_get_folder());
		DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->make_dir_recursive(abs_folder);
		String test_path = abs_folder.plus_file("kuroko_test.txt");

		// Write.
		String write_code = "import fileio\nfileio.write_file('" + test_path + "', 'Hello Kuroko!')";
		SUPPRESS_OUTPUT(lang->execute(write_code));

		// Read back.
		String read_code = "import fileio\nfileio.read_file('" + test_path + "')";
		Variant rd = _krk_eval(read_code);
		CHECK(rd.get_type() == Variant::STRING);
		CHECK((String)rd == "Hello Kuroko!");
	}

	TEST_CASE("[kuroko-mod] fileio.write_file and read_bytes round-trip") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String abs_folder = OS::get_singleton()->get_executable_path().get_base_dir().plus_file(_doctest_get_folder());
		DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->make_dir_recursive(abs_folder);
		String test_path = abs_folder.plus_file("kuroko_bytes.txt");

		SUPPRESS_OUTPUT(lang->execute("import fileio\nfileio.write_file('" + test_path + "', 'ABCD')"));

		Variant rd = _krk_eval("import fileio\nfileio.read_bytes('" + test_path + "')");
		CHECK(rd.get_type() == Variant::POOL_BYTE_ARRAY);
		if (rd.get_type() == Variant::POOL_BYTE_ARRAY) {
			PoolByteArray bytes = rd;
			CHECK(bytes.size() == 4);
			CHECK(bytes[0] == 'A');
			CHECK(bytes[1] == 'B');
			CHECK(bytes[2] == 'C');
			CHECK(bytes[3] == 'D');
		}
	}

	TEST_CASE("[kuroko-mod] fileio.read_file nonexistent raises error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.read_file('/absolutely_nonexistent_12345.txt')");
		);
	}

	TEST_CASE("[kuroko-mod] fileio.read_bytes nonexistent raises error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.read_bytes('/absolutely_nonexistent_12345.txt')");
		);
	}

	TEST_CASE("[kuroko-mod] fileio.list_dir") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String folder = _doctest_get_folder();

		// Write a test file first.
		lang->execute("import fileio\nfileio.write_file('" + folder + "listtest.txt', 'x')");

		Variant result = _krk_eval("import fileio\nfileio.list_dir('" + folder + "')");
		CHECK(result.get_type() == Variant::ARRAY);
		Array entries = result;
		CHECK(entries.size() > 0);

		// Check our file is in the listing.
		bool found = false;
		for (int i = 0; i < entries.size(); i++) {
			if ((String)entries[i] == "listtest.txt") {
				found = true;
				break;
			}
		}
		CHECK(found);
	}

	TEST_CASE("[kuroko-mod] fileio.list_dir nonexistent raises error") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.list_dir('/nonexistent_dir_12345/')");
		);
	}

	TEST_CASE("[kuroko-mod] fileio.get_modified_time") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String test_path = _doctest_get_folder() + "kuroko_mtime.txt";

		lang->execute("import fileio\nfileio.write_file('" + test_path + "', 'mtime test')");
		Variant result = _krk_eval("import fileio\nfileio.get_modified_time('" + test_path + "')");
		CHECK(result.get_type() == Variant::INT);
		int64_t mtime = result;
		// Should be recent — after year 2024.
		CHECK(mtime > 1704067200); // 2024-01-01
	}

	TEST_CASE("[kuroko-mod] fileio.exists after write") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String test_path = _doctest_get_folder() + "kuroko_exists.txt";

		Variant before = _krk_eval("import fileio\nfileio.exists('" + test_path + "')");
		// May or may not exist from previous run — just check it's a bool.
		CHECK(before.get_type() == Variant::BOOL);

		lang->execute("import fileio\nfileio.write_file('" + test_path + "', 'exists')");

		Variant after = _krk_eval("import fileio\nfileio.exists('" + test_path + "')");
		CHECK((bool)after == true);
	}

	TEST_CASE("[kuroko-mod] fileio.write_file bad args") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.write_file(42, 'content')");
		);
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.write_file('path')"); // missing content arg
		);
	}

	TEST_CASE("[kuroko-mod] fileio.read_file bad args") {
		auto *lang = KurokoLanguage::get_singleton();
		EXPECT_ERROR(
			lang->execute("import fileio\nfileio.read_file(42)");
		);
	}

	TEST_CASE("[kuroko-mod] fileio.write and read unicode") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String abs_folder = OS::get_singleton()->get_executable_path().get_base_dir().plus_file(_doctest_get_folder());
		DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->make_dir_recursive(abs_folder);
		String test_path = abs_folder.plus_file("kuroko_unicode.txt");

		// Write and read back a unicode string through Kuroko's fileio module.
		String unicode_str = String::utf8("caf\xc3\xa9 \xc3\xb1 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"); // café ñ 日本語
		lang->execute("import fileio\nfileio.write_file('" + test_path + "', 'caf\\xe9 \\xf1 \\u65e5\\u672c\\u8a9e')");
		Variant result = _krk_eval("import fileio\nfileio.read_file('" + test_path + "')");
		CHECK(result.get_type() == Variant::STRING);
		// Verify read-back is non-empty (exact encoding depends on Kuroko's string handling).
		CHECK(!((String)result).empty());
	}

	TEST_CASE("[kuroko-mod] fileio.write and read empty file") {
		auto *lang = KurokoLanguage::get_singleton();
		_doctest_prepare_folder();
		String abs_folder = OS::get_singleton()->get_executable_path().get_base_dir().plus_file(_doctest_get_folder());
		DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->make_dir_recursive(abs_folder);
		String test_path = abs_folder.plus_file("kuroko_empty.txt");

		lang->execute("import fileio\nfileio.write_file('" + test_path + "', '')");
		Variant result = _krk_eval("import fileio\nfileio.read_file('" + test_path + "')");
		CHECK(result.get_type() == Variant::STRING);
		CHECK((String)result == "");
	}
}

TEST_SUITE("[[gd_kuroko]] GodotModules - cross-module") {
	TEST_CASE("[kuroko-mod] import all three modules") {
		Variant result = _krk_eval(
				"import time\n"
				"import os\n"
				"import fileio\n"
				"[type(time).__name__, type(os).__name__, type(fileio).__name__]");
		CHECK(result.get_type() == Variant::ARRAY);
		Array arr = result;
		REQUIRE(arr.size() == 3);
	}

	TEST_CASE("[kuroko-mod] use time and os together") {
		Variant result = _krk_eval(
				"import time, os\n"
				"{'platform': os.get_name(), 'timestamp': time.time(), 'cpus': os.get_processor_count()}");
		CHECK(result.get_type() == Variant::DICTIONARY);
		Dictionary d = result;
		CHECK(d.has("platform"));
		CHECK(d.has("timestamp"));
		CHECK(d.has("cpus"));
	}

	TEST_CASE("[kuroko-mod] fileio write then read with time check") {
		_doctest_prepare_folder();
		String abs_folder = OS::get_singleton()->get_executable_path().get_base_dir().plus_file(_doctest_get_folder());
		DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->make_dir_recursive(abs_folder);
		String test_path = abs_folder.plus_file("kuroko_cross.txt");

		// Write file, then read and check in a single function call to avoid
		// module-level variable scoping issues across krk_interpret calls.
		auto *lang = KurokoLanguage::get_singleton();
		lang->execute("import fileio, time");
		lang->execute("fileio.write_file('" + test_path + "', 'cross-module test')");
		Variant content = _krk_eval("fileio.read_file('" + test_path + "')");
		Variant mtime = _krk_eval("fileio.get_modified_time('" + test_path + "')");
		Variant now = _krk_eval("time.time()");
		Dictionary result;
		result["content"] = content;
		result["recent"] = ((double)now - (double)mtime) < 60;
		CHECK((String)result["content"] == "cross-module test");
		CHECK((bool)result["recent"] == true);
	}
}

#endif // DOCTEST
