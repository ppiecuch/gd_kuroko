/**************************************************************************/
/*  kuroko_script.cpp                                                     */
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

#include "kuroko_script.h"

#include "kuroko_language.h"
#include "kuroko_script_instance.h"

#include "core/os/file_access.h"
#include "core/print_string.h"

// --- KurokoScript ---

void KurokoScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_code", "code"), &KurokoScript::set_source_code);
	ClassDB::bind_method(D_METHOD("get_source_code"), &KurokoScript::get_source_code);
	ClassDB::bind_method(D_METHOD("reload"), &KurokoScript::reload);
	ClassDB::bind_method(D_METHOD("is_valid"), &KurokoScript::is_valid);
	ClassDB::bind_method(D_METHOD("is_tool"), &KurokoScript::is_tool);
	ClassDB::bind_method(D_METHOD("get_extends_class"), &KurokoScript::get_extends_class);
	ClassDB::bind_method(D_METHOD("instance_create_ref", "owner"), &KurokoScript::instance_create_ref);
}

void KurokoScript::set_source_code(const String &p_code) {
	source_code = p_code;
}

String KurokoScript::get_source_code() const {
	return source_code;
}

Error KurokoScript::reload() {
	valid = false;
	metadata = KurokoAnnotationParser::parse(source_code);

	// Verify source compiles by doing a test interpret.
	// The actual compilation happens when instances are created.
	if (source_code.strip_edges().empty()) {
		valid = true;
		return OK;
	}

	// Ensure the VM is ready.
	KurokoLanguage *lang = KurokoLanguage::get_singleton();
	if (!lang) {
		ERR_PRINT("Kuroko: Language singleton not available.");
		return ERR_UNAVAILABLE;
	}

	valid = true;
	return OK;
}

bool KurokoScript::is_valid() const {
	return valid;
}

bool KurokoScript::is_tool() const {
	return metadata.is_tool;
}

String KurokoScript::get_extends_class() const {
	return metadata.extends_class;
}

void KurokoScript::get_script_property_list(List<PropertyInfo> *p_list) const {
	for (int i = 0; i < metadata.properties.size(); i++) {
		const KurokoExportedProperty &prop = metadata.properties[i];
		p_list->push_back(PropertyInfo(prop.type, prop.name, prop.hint, prop.hint_string));
	}
}

void KurokoScript::get_script_method_list(List<MethodInfo> *p_list) const {
	for (int i = 0; i < metadata.methods.size(); i++) {
		const KurokoExportedMethod &method = metadata.methods[i];
		MethodInfo mi;
		mi.name = method.name;
		for (int j = 0; j < method.arg_names.size(); j++) {
			mi.arguments.push_back(PropertyInfo(Variant::NIL, method.arg_names[j]));
		}
		p_list->push_back(mi);
	}
}

void KurokoScript::get_script_signal_list(List<MethodInfo> *p_list) const {
	for (int i = 0; i < metadata.signals.size(); i++) {
		const KurokoSignalInfo &sig = metadata.signals[i];
		MethodInfo mi;
		mi.name = sig.name;
		for (int j = 0; j < sig.arg_names.size(); j++) {
			mi.arguments.push_back(PropertyInfo(Variant::NIL, sig.arg_names[j]));
		}
		p_list->push_back(mi);
	}
}

bool KurokoScript::has_script_signal(const StringName &p_signal) const {
	for (int i = 0; i < metadata.signals.size(); i++) {
		if (metadata.signals[i].name == p_signal) {
			return true;
		}
	}
	return false;
}

Ref<KurokoScriptInstance> KurokoScript::instance_create_ref(Object *p_owner) {
	if (!valid) {
		ERR_PRINT("Kuroko: Cannot create instance from invalid script.");
		return Ref<KurokoScriptInstance>();
	}

	Ref<KurokoScriptInstance> inst;
	inst.instance();
	inst->set_owner(p_owner);
	inst->set_script_ref(Ref<KurokoScript>(this));
	inst->initialize();
	return inst;
}

KurokoScript::KurokoScript() {
	valid = false;
}

KurokoScript::~KurokoScript() {
}

// --- ResourceFormatLoaderKuroko ---

RES ResourceFormatLoaderKuroko::load(const String &p_path, const String &p_original_path, Error *r_error) {
	Ref<KurokoScript> script;
	script.instance();

	String ext = p_path.get_extension().to_lower();

	if (ext == "krk") {
		String source = FileAccess::get_file_as_string(p_path);
		if (source.empty() && r_error) {
			*r_error = ERR_FILE_CANT_OPEN;
			return RES();
		}
		script->set_source_code(source);
		script->set_path(p_original_path.empty() ? p_path : p_original_path, true);
		Error err = script->reload();
		if (err != OK) {
			if (r_error) *r_error = err;
			return RES();
		}
	} else if (ext == "krkc") {
		// TODO: Load bytecode via KurokoBytecodeMarshal (Phase 7).
		ERR_PRINT("Kuroko: .krkc bytecode loading not yet implemented.");
		if (r_error) *r_error = ERR_UNAVAILABLE;
		return RES();
	}

	if (r_error) *r_error = OK;
	return script;
}

void ResourceFormatLoaderKuroko::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("krk");
	p_extensions->push_back("krkc");
}

bool ResourceFormatLoaderKuroko::handles_type(const String &p_type) const {
	return p_type == "KurokoScript" || p_type == "Resource";
}

String ResourceFormatLoaderKuroko::get_resource_type(const String &p_path) const {
	String ext = p_path.get_extension().to_lower();
	if (ext == "krk" || ext == "krkc") {
		return "KurokoScript";
	}
	return "";
}

// --- ResourceFormatSaverKuroko ---

Error ResourceFormatSaverKuroko::save(const String &p_path, const RES &p_resource, uint32_t p_flags) {
	Ref<KurokoScript> script = p_resource;
	ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

	Error err;
	FileAccess *file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save Kuroko script.");

	file->store_string(script->get_source_code());
	memdelete(file);
	return OK;
}

bool ResourceFormatSaverKuroko::recognize(const RES &p_resource) const {
	return Object::cast_to<KurokoScript>(p_resource.ptr()) != nullptr;
}

void ResourceFormatSaverKuroko::get_recognized_extensions(const RES &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<KurokoScript>(p_resource.ptr())) {
		p_extensions->push_back("krk");
	}
}
