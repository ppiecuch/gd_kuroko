/**************************************************************************/
/*  kuroko_script_instance.cpp                                            */
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

#include "kuroko_script_instance.h"

#include "kuroko_annotation_parser.h"
#include "kuroko_language.h"

#include "core/print_string.h"

#define new new_size
extern "C" {
#include <kuroko/vm.h>
#include <kuroko/compiler.h>
#include <kuroko/object.h>
#include <kuroko/table.h>
#include <kuroko/memory.h>
}
#undef new
#undef vm
#undef likely
#undef unlikely

void KurokoScriptInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_property", "name", "value"), &KurokoScriptInstance::set_property);
	ClassDB::bind_method(D_METHOD("get_property", "name"), &KurokoScriptInstance::get_property);
	ClassDB::bind_method(D_METHOD("call_method", "method", "args"), &KurokoScriptInstance::call_method, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("is_initialized"), &KurokoScriptInstance::is_initialized);
}

void KurokoScriptInstance::initialize() {
	if (initialized || script.is_null() || !script->is_valid()) {
		return;
	}

	// Ensure VM is ready.
	KurokoLanguage *lang = KurokoLanguage::get_singleton();
	if (!lang) {
		ERR_PRINT("Kuroko: Language singleton not available.");
		return;
	}

	String source = script->get_source_code();
	if (source.strip_edges().empty()) {
		initialized = true;
		return;
	}

	// Strip $ annotations from source before passing to VM.
	String clean_source = KurokoAnnotationParser::strip(source);
	CharString utf8_source = clean_source.utf8();
	String path = script->get_path();
	CharString utf8_path = path.empty() ? CharString("<kuroko_instance>") : path.utf8();

	// Start a new module context.
	module_scope = krk_startModule((char *)utf8_path.get_data());

	// Set default property values from annotations.
	const KurokoScriptMetadata &meta = script->get_metadata();
	for (int i = 0; i < meta.properties.size(); i++) {
		const KurokoExportedProperty &prop = meta.properties[i];
		if (prop.default_value.get_type() != Variant::NIL) {
			KrkString *name_str = krk_copyString(prop.name.utf8().get_data(), prop.name.utf8().length());
			KrkValue val = KurokoTypeConversion::variant_to_krk_value(prop.default_value);
			krk_push(val);
			krk_tableSet(&module_scope->fields, OBJECT_VAL(name_str), val);
			krk_pop();
		}
	}

	// Compile and execute the source to populate the module with functions/globals.
	KrkValue result = krk_interpret(utf8_source.get_data(), (char *)utf8_path.get_data());

	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		KrkValue exception = krk_currentThread.currentException;
		String err_msg = "Kuroko instance initialization error";
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
		return;
	}

	initialized = true;
}

// Find the setter/getter for a property from the script metadata.
static const KurokoExportedProperty *_find_exported_property(const Ref<KurokoScript> &p_script, const String &p_name) {
	if (p_script.is_null()) {
		return nullptr;
	}
	const KurokoScriptMetadata &meta = p_script->get_metadata();
	for (int i = 0; i < meta.properties.size(); i++) {
		if (meta.properties[i].name == p_name) {
			return &meta.properties[i];
		}
	}
	return nullptr;
}

bool KurokoScriptInstance::set_property(const String &p_name, const Variant &p_value) {
	if (!initialized || !module_scope) {
		return false;
	}

	// Check for setget setter function.
	const KurokoExportedProperty *prop = _find_exported_property(script, p_name);
	if (prop && !prop->setter.empty()) {
		// Call the setter function instead of direct assignment.
		Array args;
		args.push_back(p_value);
		call_method(prop->setter, args);
		return true;
	}

	// Direct assignment.
	CharString utf8_name = p_name.utf8();
	KrkString *name_str = krk_copyString(utf8_name.get_data(), utf8_name.length());
	KrkValue val = KurokoTypeConversion::variant_to_krk_value(p_value);
	krk_push(val);
	krk_tableSet(&module_scope->fields, OBJECT_VAL(name_str), val);
	krk_pop();
	return true;
}

Variant KurokoScriptInstance::get_property(const String &p_name) const {
	if (!initialized || !module_scope) {
		return Variant();
	}

	// Check for setget getter function.
	const KurokoExportedProperty *prop = _find_exported_property(script, p_name);
	if (prop && !prop->getter.empty()) {
		// Call the getter function instead of direct access.
		// const_cast needed because call_method is non-const (it mutates VM state).
		return const_cast<KurokoScriptInstance *>(this)->call_method(prop->getter);
	}

	// Direct access.
	CharString utf8_name = p_name.utf8();
	KrkString *name_str = krk_copyString(utf8_name.get_data(), utf8_name.length());
	KrkValue val;
	if (krk_tableGet(&module_scope->fields, OBJECT_VAL(name_str), &val)) {
		return KurokoTypeConversion::krk_value_to_variant(val);
	}
	return Variant();
}

Variant KurokoScriptInstance::call_method(const String &p_method, const Array &p_args) {
	if (!initialized || !module_scope) {
		ERR_PRINT("Kuroko: Instance not initialized.");
		return Variant();
	}

	CharString utf8_name = p_method.utf8();
	KrkString *name_str = krk_copyString(utf8_name.get_data(), utf8_name.length());
	KrkValue func;
	if (!krk_tableGet(&module_scope->fields, OBJECT_VAL(name_str), &func)) {
		ERR_PRINT("Kuroko: Method not found: " + p_method);
		return Variant();
	}

	// Push function and arguments onto the stack.
	krk_push(func);
	for (int i = 0; i < p_args.size(); i++) {
		krk_push(KurokoTypeConversion::variant_to_krk_value(p_args[i]));
	}

	// Call: func + args are on the stack. krk_callStack pops args and func.
	KrkValue result = krk_callStack(p_args.size());

	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		krk_currentThread.flags &= ~KRK_THREAD_HAS_EXCEPTION;
		ERR_PRINT("Kuroko: Error calling method: " + p_method);
		krk_resetStack();
		return Variant();
	}

	return KurokoTypeConversion::krk_value_to_variant(result);
}

void KurokoScriptInstance::notification(int p_what) {
	if (!initialized || !module_scope) {
		return;
	}

	// Map notification codes to lifecycle method names.
	const char *method_name = nullptr;
	switch (p_what) {
		case 10: // NOTIFICATION_READY
			method_name = "_ready";
			break;
		case 17: // NOTIFICATION_PROCESS
			method_name = "_process";
			break;
		case 18: // NOTIFICATION_PHYSICS_PROCESS
			method_name = "_physics_process";
			break;
		case 11: // NOTIFICATION_ENTER_TREE
			method_name = "_enter_tree";
			break;
		case 12: // NOTIFICATION_EXIT_TREE
			method_name = "_exit_tree";
			break;
		case 30: // NOTIFICATION_DRAW
			method_name = "_draw";
			break;
		default:
			return;
	}

	// Check if the method exists.
	KrkString *name_str = krk_copyString(method_name, strlen(method_name));
	KrkValue func;
	if (!krk_tableGet(&module_scope->fields, OBJECT_VAL(name_str), &func)) {
		return; // Method not defined, silently skip.
	}

	// Call with delta argument for process methods.
	krk_push(func);
	if (p_what == 17 || p_what == 18) {
		// TODO: Get actual delta from owner node.
		krk_push(FLOATING_VAL(0.016));
		krk_callStack(1);
	} else {
		krk_callStack(0);
	}

	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		krk_currentThread.flags &= ~KRK_THREAD_HAS_EXCEPTION;
		ERR_PRINT(String("Kuroko: Error in ") + method_name);
		krk_resetStack();
	}
}

Ref<KurokoScript> KurokoScriptInstance::get_script_ref() const {
	return script;
}

Object *KurokoScriptInstance::get_owner() const {
	return owner;
}

bool KurokoScriptInstance::is_initialized() const {
	return initialized;
}

void KurokoScriptInstance::set_owner(Object *p_owner) {
	owner = p_owner;
}

void KurokoScriptInstance::set_script_ref(const Ref<KurokoScript> &p_script) {
	script = p_script;
}

KurokoScriptInstance::KurokoScriptInstance() {
	owner = nullptr;
	module_scope = nullptr;
	initialized = false;
}

KurokoScriptInstance::~KurokoScriptInstance() {
	// The module_scope is owned by the Kuroko GC.
	// It will be collected when no longer referenced.
	module_scope = nullptr;
}
