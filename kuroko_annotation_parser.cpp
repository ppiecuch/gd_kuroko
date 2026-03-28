/**************************************************************************/
/*  kuroko_annotation_parser.cpp                                          */
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

#include "kuroko_annotation_parser.h"

#include "core/print_string.h"

Variant::Type KurokoAnnotationParser::_parse_type_name(const String &p_name) {
	if (p_name == "bool") return Variant::BOOL;
	if (p_name == "int") return Variant::INT;
	if (p_name == "float") return Variant::REAL;
	if (p_name == "String") return Variant::STRING;
	if (p_name == "Vector2") return Variant::VECTOR2;
	if (p_name == "Vector3") return Variant::VECTOR3;
	if (p_name == "Rect2") return Variant::RECT2;
	if (p_name == "Transform2D") return Variant::TRANSFORM2D;
	if (p_name == "Color") return Variant::COLOR;
	if (p_name == "NodePath") return Variant::NODE_PATH;
	if (p_name == "Array") return Variant::ARRAY;
	if (p_name == "Dictionary") return Variant::DICTIONARY;
	if (p_name == "PoolByteArray") return Variant::POOL_BYTE_ARRAY;
	if (p_name == "PoolIntArray") return Variant::POOL_INT_ARRAY;
	if (p_name == "PoolRealArray") return Variant::POOL_REAL_ARRAY;
	if (p_name == "PoolStringArray") return Variant::POOL_STRING_ARRAY;
	if (p_name == "PoolVector2Array") return Variant::POOL_VECTOR2_ARRAY;
	if (p_name == "PoolVector3Array") return Variant::POOL_VECTOR3_ARRAY;
	if (p_name == "PoolColorArray") return Variant::POOL_COLOR_ARRAY;
	return Variant::NIL;
}

PropertyHint KurokoAnnotationParser::_parse_hint_name(const String &p_name) {
	if (p_name == "RANGE") return PROPERTY_HINT_RANGE;
	if (p_name == "ENUM") return PROPERTY_HINT_ENUM;
	if (p_name == "MULTILINE") return PROPERTY_HINT_MULTILINE_TEXT;
	if (p_name == "FILE") return PROPERTY_HINT_FILE;
	if (p_name == "DIR") return PROPERTY_HINT_DIR;
	if (p_name == "EXP_RANGE") return PROPERTY_HINT_EXP_RANGE;
	if (p_name == "FLAGS") return PROPERTY_HINT_FLAGS;
	if (p_name == "LAYERS_2D_RENDER") return PROPERTY_HINT_LAYERS_2D_RENDER;
	if (p_name == "LAYERS_2D_PHYSICS") return PROPERTY_HINT_LAYERS_2D_PHYSICS;
	if (p_name == "LAYERS_3D_RENDER") return PROPERTY_HINT_LAYERS_3D_RENDER;
	if (p_name == "LAYERS_3D_PHYSICS") return PROPERTY_HINT_LAYERS_3D_PHYSICS;
	if (p_name == "COLOR_NO_ALPHA") return PROPERTY_HINT_COLOR_NO_ALPHA;
	return PROPERTY_HINT_NONE;
}

Variant KurokoAnnotationParser::_parse_default_value(const String &p_value, Variant::Type p_type) {
	String v = p_value.strip_edges();
	if (v.empty() || v == "None" || v == "null") {
		return Variant();
	}

	switch (p_type) {
		case Variant::BOOL:
			return Variant(v == "True" || v == "true" || v == "1");
		case Variant::INT:
			return Variant(v.to_int());
		case Variant::REAL:
			return Variant(v.to_double());
		case Variant::STRING: {
			// Strip quotes.
			if ((v.begins_with("\"") && v.ends_with("\"")) ||
					(v.begins_with("'") && v.ends_with("'"))) {
				return Variant(v.substr(1, v.length() - 2));
			}
			return Variant(v);
		}
		default:
			return Variant();
	}
}

// Extract "setget setter, getter" from the end of a declaration string.
// Modifies p_str in place to remove the setget part.
static void _extract_setget(String &p_str, String &r_setter, String &r_getter) {
	int setget_pos = p_str.find("setget ");
	if (setget_pos == -1) {
		return;
	}

	String setget_part = p_str.substr(setget_pos + 7).strip_edges();
	p_str = p_str.substr(0, setget_pos).strip_edges();

	Vector<String> parts = setget_part.split(",");
	if (parts.size() >= 1) {
		r_setter = parts[0].strip_edges();
	}
	if (parts.size() >= 2) {
		r_getter = parts[1].strip_edges();
	}
}

void KurokoAnnotationParser::_parse_export_annotation(const String &p_content, KurokoScriptMetadata &r_meta) {
	// Forms:
	// 1) export var name: type = default
	// 2) export var name: type = default setget setter, getter
	// 3) export(type, HINT, "hint_string") var name = default
	// 4) export(type, HINT, "hint_string") var name = default setget setter, getter

	String content = p_content.strip_edges();
	KurokoExportedProperty prop;
	prop.type = Variant::NIL;
	prop.hint = PROPERTY_HINT_NONE;

	if (content.begins_with("export(")) {
		// Form 2: export(type, HINT, "hint_string") var name = default
		int paren_end = content.find(")");
		if (paren_end == -1) return;

		String hint_part = content.substr(7, paren_end - 7); // Inside parentheses.
		String rest = content.substr(paren_end + 1).strip_edges();

		// Parse hint arguments.
		Vector<String> hint_args = hint_part.split(",");
		if (hint_args.size() >= 1) {
			prop.type = _parse_type_name(hint_args[0].strip_edges());
		}
		if (hint_args.size() >= 2) {
			prop.hint = _parse_hint_name(hint_args[1].strip_edges());
		}
		if (hint_args.size() >= 3) {
			String hs = hint_args[2].strip_edges();
			// Strip quotes from hint string.
			if ((hs.begins_with("\"") && hs.ends_with("\"")) ||
					(hs.begins_with("'") && hs.ends_with("'"))) {
				hs = hs.substr(1, hs.length() - 2);
			}
			prop.hint_string = hs;
		}

		// Parse "var name = default [setget setter, getter]" from rest.
		if (!rest.begins_with("var ")) return;
		rest = rest.substr(4).strip_edges();

		// Extract setget before parsing name/default.
		_extract_setget(rest, prop.setter, prop.getter);

		int eq_pos = rest.find("=");
		if (eq_pos != -1) {
			prop.name = rest.substr(0, eq_pos).strip_edges();
			prop.default_value = _parse_default_value(rest.substr(eq_pos + 1), prop.type);
		} else {
			prop.name = rest.strip_edges();
		}
	} else if (content.begins_with("export var ")) {
		// Form 1: export var name: type = default [setget setter, getter]
		String rest = content.substr(11).strip_edges();

		// Extract setget before parsing name/type/default.
		_extract_setget(rest, prop.setter, prop.getter);

		int colon_pos = rest.find(":");
		int eq_pos = rest.find("=");

		if (colon_pos != -1) {
			prop.name = rest.substr(0, colon_pos).strip_edges();
			String after_colon = rest.substr(colon_pos + 1).strip_edges();

			int eq_in_after = after_colon.find("=");
			if (eq_in_after != -1) {
				String type_str = after_colon.substr(0, eq_in_after).strip_edges();
				prop.type = _parse_type_name(type_str);
				prop.default_value = _parse_default_value(after_colon.substr(eq_in_after + 1), prop.type);
			} else {
				prop.type = _parse_type_name(after_colon);
			}
		} else if (eq_pos != -1) {
			prop.name = rest.substr(0, eq_pos).strip_edges();
			prop.default_value = _parse_default_value(rest.substr(eq_pos + 1), prop.type);
		} else {
			prop.name = rest;
		}
	} else {
		return;
	}

	if (!prop.name.empty()) {
		r_meta.properties.push_back(prop);
	}
}

void KurokoAnnotationParser::_parse_signal_annotation(const String &p_content, KurokoScriptMetadata &r_meta) {
	// signal name(arg1, arg2, ...)
	String content = p_content.strip_edges();
	if (!content.begins_with("signal ")) return;

	String rest = content.substr(7).strip_edges();
	KurokoSignalInfo sig;

	int paren_start = rest.find("(");
	if (paren_start != -1) {
		sig.name = rest.substr(0, paren_start).strip_edges();
		int paren_end = rest.find(")");
		if (paren_end != -1) {
			String args_str = rest.substr(paren_start + 1, paren_end - paren_start - 1);
			Vector<String> args = args_str.split(",");
			for (int i = 0; i < args.size(); i++) {
				String arg = args[i].strip_edges();
				if (!arg.empty()) {
					sig.arg_names.push_back(arg);
				}
			}
		}
	} else {
		sig.name = rest;
	}

	if (!sig.name.empty()) {
		r_meta.signals.push_back(sig);
	}
}

bool KurokoAnnotationParser::_is_lifecycle_method(const String &p_name) {
	return p_name == "_ready" ||
			p_name == "_process" ||
			p_name == "_physics_process" ||
			p_name == "_input" ||
			p_name == "_unhandled_input" ||
			p_name == "_unhandled_key_input" ||
			p_name == "_enter_tree" ||
			p_name == "_exit_tree" ||
			p_name == "_notification" ||
			p_name == "_draw" ||
			p_name == "_init";
}

void KurokoAnnotationParser::_parse_def_line(const String &p_line, bool p_force_export, KurokoScriptMetadata &r_meta) {
	// Parse "def func_name(arg1, arg2, ...):" — extract name and args.
	String func_part = p_line.strip_edges();
	if (!func_part.begins_with("def ")) {
		return;
	}
	func_part = func_part.substr(4).strip_edges();

	int paren_start = func_part.find("(");
	if (paren_start == -1) {
		return;
	}

	String func_name = func_part.substr(0, paren_start).strip_edges();
	int paren_end = func_part.find(")");

	Vector<String> arg_names;
	if (paren_end != -1 && paren_end > paren_start + 1) {
		String args_str = func_part.substr(paren_start + 1, paren_end - paren_start - 1);
		Vector<String> args = args_str.split(",");
		for (int j = 0; j < args.size(); j++) {
			String arg = args[j].strip_edges();
			// Strip type annotations and defaults.
			int colon = arg.find(":");
			if (colon != -1) arg = arg.substr(0, colon).strip_edges();
			int eq = arg.find("=");
			if (eq != -1) arg = arg.substr(0, eq).strip_edges();
			if (!arg.empty() && arg != "self") {
				arg_names.push_back(arg);
			}
		}
	}

	if (p_force_export || _is_lifecycle_method(func_name)) {
		KurokoExportedMethod method;
		method.name = func_name;
		method.arg_names = arg_names;
		r_meta.methods.push_back(method);
	}
}

KurokoScriptMetadata KurokoAnnotationParser::parse(const String &p_source) {
	KurokoScriptMetadata meta;

	Vector<String> lines = p_source.split("\n");

	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();

		// $ annotations: $extends, $tool, $export, $signal.
		if (line.begins_with("$")) {
			String annotation = line.substr(1).strip_edges();

			if (annotation.begins_with("extends ")) {
				meta.extends_class = annotation.substr(8).strip_edges();
			} else if (annotation == "tool") {
				meta.is_tool = true;
			} else if (annotation.begins_with("export def ")) {
				// "$export def func_name(args):" — exported function.
				_parse_def_line(annotation.substr(7), true, meta);
			} else if (annotation.begins_with("export")) {
				// "$export var ..." or "$export(type, HINT) var ..."
				_parse_export_annotation(annotation, meta);
			} else if (annotation.begins_with("signal ")) {
				_parse_signal_annotation(annotation, meta);
			}
			continue;
		}

		// Also support legacy #@ prefix for backward compatibility.
		if (line.begins_with("#@")) {
			String annotation = line.substr(2).strip_edges();

			if (annotation.begins_with("extends ")) {
				meta.extends_class = annotation.substr(8).strip_edges();
			} else if (annotation == "tool") {
				meta.is_tool = true;
			} else if (annotation.begins_with("export def ")) {
				_parse_def_line(annotation.substr(7), true, meta);
			} else if (annotation.begins_with("export")) {
				_parse_export_annotation(annotation, meta);
			} else if (annotation.begins_with("signal ")) {
				_parse_signal_annotation(annotation, meta);
			}
			continue;
		}

		// Plain `def` lines — auto-detect lifecycle methods.
		if (line.begins_with("def ")) {
			_parse_def_line(line, false, meta);
		}
	}

	return meta;
}

String KurokoAnnotationParser::strip(const String &p_source) {
	// Remove $ annotation lines from source, producing clean Kuroko code.
	// - "$export def foo():" → "def foo():" (keep the def, strip $export prefix)
	// - "$export var ...", "$extends ...", "$tool", "$signal ..." → removed entirely
	// - "#@ ..." lines → removed entirely (legacy format, comments anyway)
	// - All other lines → kept as-is

	Vector<String> lines = p_source.split("\n");
	Vector<String> output;
	output.resize(lines.size()); // Pre-allocate, may shrink.
	int out_count = 0;

	for (int i = 0; i < lines.size(); i++) {
		String stripped = lines[i].strip_edges();

		if (stripped.begins_with("$")) {
			String annotation = stripped.substr(1).strip_edges();

			if (annotation.begins_with("export def ")) {
				// Keep the def line, preserving original indentation.
				// Find where "def " starts in the original line.
				int def_pos = lines[i].find("def ");
				if (def_pos != -1) {
					// Reconstruct with original indentation.
					String indent = lines[i].substr(0, lines[i].find("$"));
					output.write[out_count++] = indent + lines[i].substr(def_pos);
				}
			}
			// All other $ lines are stripped (vars, extends, tool, signal).
			continue;
		}

		if (stripped.begins_with("#@")) {
			// Legacy annotation — strip entirely (it's a comment anyway).
			continue;
		}

		// Keep all other lines.
		output.write[out_count++] = lines[i];
	}

	// Join kept lines.
	String result;
	for (int i = 0; i < out_count; i++) {
		if (i > 0) {
			result += "\n";
		}
		result += output[i];
	}
	return result;
}

// --- Doctests ---

#ifdef DOCTEST
#include "doctest/doctest.h"

TEST_SUITE("[[gd_kuroko]] AnnotationParser") {

	// --- $ syntax (primary) ---

	TEST_CASE("[kuroko-ann] $ extends") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$extends Node2D\n");
		CHECK(meta.extends_class == "Node2D");
	}

	TEST_CASE("[kuroko-ann] $ tool") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$tool\n$extends Node\n");
		CHECK(meta.is_tool == true);
		CHECK(meta.extends_class == "Node");
	}

	TEST_CASE("[kuroko-ann] $ export var float") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var speed: float = 100.0\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].name == "speed");
		CHECK(meta.properties[0].type == Variant::REAL);
		CHECK((float)meta.properties[0].default_value == doctest::Approx(100.0f));
	}

	TEST_CASE("[kuroko-ann] $ export var int") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var health: int = 50\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].type == Variant::INT);
		CHECK((int)meta.properties[0].default_value == 50);
	}

	TEST_CASE("[kuroko-ann] $ export var String") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var label: String = \"hello\"\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].type == Variant::STRING);
		CHECK((String)meta.properties[0].default_value == "hello");
	}

	TEST_CASE("[kuroko-ann] $ export var bool") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var enabled: bool = True\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].type == Variant::BOOL);
		CHECK((bool)meta.properties[0].default_value == true);
	}

	TEST_CASE("[kuroko-ann] $ export with MULTILINE hint") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export(String, MULTILINE) var desc = \"\"\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].hint == PROPERTY_HINT_MULTILINE_TEXT);
	}

	TEST_CASE("[kuroko-ann] $ export with RANGE hint") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export(int, RANGE, \"1,10\") var level = 1\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].hint == PROPERTY_HINT_RANGE);
		CHECK(meta.properties[0].hint_string == "1,10");
	}

	TEST_CASE("[kuroko-ann] $ export with ENUM hint") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(
				"$export(String, ENUM, \"melee,ranged,magic\") var attack = \"melee\"\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].hint == PROPERTY_HINT_ENUM);
		CHECK(meta.properties[0].hint_string == "melee,ranged,magic");
	}

	TEST_CASE("[kuroko-ann] $ signal no args") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$signal clicked()\n");
		REQUIRE(meta.signals.size() == 1);
		CHECK(meta.signals[0].name == "clicked");
		CHECK(meta.signals[0].arg_names.size() == 0);
	}

	TEST_CASE("[kuroko-ann] $ signal with args") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$signal health_changed(old_value, new_value)\n");
		REQUIRE(meta.signals.size() == 1);
		CHECK(meta.signals[0].name == "health_changed");
		REQUIRE(meta.signals[0].arg_names.size() == 2);
		CHECK(meta.signals[0].arg_names[0] == "old_value");
		CHECK(meta.signals[0].arg_names[1] == "new_value");
	}

	// --- $export def (function export) ---

	TEST_CASE("[kuroko-ann] $ export def") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export def my_method(a, b):\n    return a + b\n");
		REQUIRE(meta.methods.size() == 1);
		CHECK(meta.methods[0].name == "my_method");
		REQUIRE(meta.methods[0].arg_names.size() == 2);
		CHECK(meta.methods[0].arg_names[0] == "a");
		CHECK(meta.methods[0].arg_names[1] == "b");
	}

	TEST_CASE("[kuroko-ann] $ export def no args") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export def get_info():\n    return {}\n");
		REQUIRE(meta.methods.size() == 1);
		CHECK(meta.methods[0].name == "get_info");
		CHECK(meta.methods[0].arg_names.size() == 0);
	}

	TEST_CASE("[kuroko-ann] $ export def with defaults") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export def attack(dmg=10, crit=False):\n    pass\n");
		REQUIRE(meta.methods.size() == 1);
		CHECK(meta.methods[0].name == "attack");
		REQUIRE(meta.methods[0].arg_names.size() == 2);
		CHECK(meta.methods[0].arg_names[0] == "dmg");
		CHECK(meta.methods[0].arg_names[1] == "crit");
	}

	// --- Lifecycle auto-detection ---

	TEST_CASE("[kuroko-ann] auto-detect lifecycle methods") {
		String source =
				"def _ready():\n    pass\n"
				"def _process(delta):\n    pass\n"
				"def _physics_process(delta):\n    pass\n"
				"def helper():\n    pass\n";
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(source);
		CHECK(meta.methods.size() == 3);
	}

	TEST_CASE("[kuroko-ann] non-lifecycle def is hidden") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("def internal(x):\n    return x * 2\n");
		CHECK(meta.methods.size() == 0);
	}

	// --- setget ---

	TEST_CASE("[kuroko-ann] $ setget setter and getter") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var health: int = 100 setget set_health, get_health\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].setter == "set_health");
		CHECK(meta.properties[0].getter == "get_health");
	}

	TEST_CASE("[kuroko-ann] $ setget setter only") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var speed: float = 50.0 setget set_speed\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].setter == "set_speed");
		CHECK(meta.properties[0].getter.empty());
	}

	TEST_CASE("[kuroko-ann] $ setget with hint form") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(
				"$export(int, RANGE, \"0,100\") var hp = 100 setget _set_hp, _get_hp\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].setter == "_set_hp");
		CHECK(meta.properties[0].getter == "_get_hp");
	}

	TEST_CASE("[kuroko-ann] no setget defaults empty") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("$export var x: float = 0.0\n");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].setter.empty());
		CHECK(meta.properties[0].getter.empty());
	}

	// --- strip() ---

	TEST_CASE("[kuroko-ann] strip removes $ var lines") {
		String source = "$extends Node2D\n$export var speed: float = 100.0\ndef _ready():\n    pass\n";
		String stripped = KurokoAnnotationParser::strip(source);
		CHECK(stripped.find("$") == -1);
		CHECK(stripped.find("def _ready") != -1);
	}

	TEST_CASE("[kuroko-ann] strip keeps $export def as def") {
		String source = "$export def custom(a, b):\n    return a + b\n";
		String stripped = KurokoAnnotationParser::strip(source);
		CHECK(stripped.find("$") == -1);
		CHECK(stripped.find("def custom(a, b):") != -1);
	}

	TEST_CASE("[kuroko-ann] strip removes $tool $signal $extends") {
		String source = "$tool\n$extends Sprite\n$signal clicked()\nx = 42\n";
		String stripped = KurokoAnnotationParser::strip(source);
		CHECK(stripped.find("$") == -1);
		CHECK(stripped.find("tool") == -1);
		CHECK(stripped.find("extends") == -1);
		CHECK(stripped.find("signal") == -1);
		CHECK(stripped.find("x = 42") != -1);
	}

	TEST_CASE("[kuroko-ann] strip preserves plain code") {
		String source = "x = 42\ndef compute(a, b):\n    return a + b\n";
		String stripped = KurokoAnnotationParser::strip(source);
		CHECK(stripped == source);
	}

	TEST_CASE("[kuroko-ann] strip removes legacy #@ lines") {
		String source = "#@ extends Node\nx = 1\n";
		String stripped = KurokoAnnotationParser::strip(source);
		CHECK(stripped.find("#@") == -1);
		CHECK(stripped.find("x = 1") != -1);
	}

	// --- Multiple properties ---

	TEST_CASE("[kuroko-ann] $ multiple properties") {
		String source =
				"$export var speed: float = 100.0\n"
				"$export var health: int = 50\n"
				"$export var label: String = \"player\"\n";
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(source);
		REQUIRE(meta.properties.size() == 3);
		CHECK(meta.properties[0].name == "speed");
		CHECK(meta.properties[1].name == "health");
		CHECK(meta.properties[2].name == "label");
	}

	// --- Full script ---

	TEST_CASE("[kuroko-ann] $ full script metadata") {
		String source =
				"$extends KinematicBody2D\n"
				"$tool\n"
				"$export var speed: float = 200.0\n"
				"$export(int, RANGE, \"0,100\") var hp = 100\n"
				"$signal died()\n"
				"$signal damage_taken(amount)\n"
				"def _ready():\n    pass\n"
				"def _process(delta):\n    pass\n"
				"$export def take_hit(dmg):\n    pass\n"
				"def _helper():\n    pass\n";
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(source);
		CHECK(meta.extends_class == "KinematicBody2D");
		CHECK(meta.is_tool == true);
		CHECK(meta.properties.size() == 2);
		CHECK(meta.signals.size() == 2);
		// _ready, _process (auto), take_hit (exported) = 3; _helper excluded
		CHECK(meta.methods.size() == 3);
	}

	TEST_CASE("[kuroko-ann] $ full script strip produces valid kuroko") {
		String source =
				"$extends KinematicBody2D\n"
				"$export var speed: float = 200.0\n"
				"$signal died()\n"
				"\n"
				"def _ready():\n"
				"    print('ready')\n"
				"\n"
				"$export def take_hit(dmg):\n"
				"    return dmg * 2\n"
				"\n"
				"def _internal():\n"
				"    pass\n";
		String stripped = KurokoAnnotationParser::strip(source);
		// Should have no $ lines.
		CHECK(stripped.find("$") == -1);
		// Should keep def lines.
		CHECK(stripped.find("def _ready():") != -1);
		CHECK(stripped.find("def take_hit(dmg):") != -1);
		CHECK(stripped.find("def _internal():") != -1);
		// Should keep print.
		CHECK(stripped.find("print('ready')") != -1);
	}

	// --- Edge cases ---

	TEST_CASE("[kuroko-ann] empty source") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("");
		CHECK(meta.extends_class.empty());
		CHECK(meta.is_tool == false);
		CHECK(meta.properties.size() == 0);
		CHECK(meta.methods.size() == 0);
		CHECK(meta.signals.size() == 0);
	}

	TEST_CASE("[kuroko-ann] pure kuroko no annotations") {
		String source = "x = 42\ndef compute(a, b):\n    return a + b\n";
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(source);
		CHECK(meta.properties.size() == 0);
		CHECK(meta.methods.size() == 0);
	}

	// --- Legacy #@ backward compatibility ---

	TEST_CASE("[kuroko-ann] legacy #@ still works") {
		String source = "#@ extends Sprite\n#@ export var x: int = 5\n#@ signal clicked()\n";
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse(source);
		CHECK(meta.extends_class == "Sprite");
		REQUIRE(meta.properties.size() == 1);
		CHECK(meta.properties[0].name == "x");
		REQUIRE(meta.signals.size() == 1);
		CHECK(meta.signals[0].name == "clicked");
	}

	TEST_CASE("[kuroko-ann] legacy #@ export def") {
		KurokoScriptMetadata meta = KurokoAnnotationParser::parse("#@ export def foo(a):\n    pass\n");
		REQUIRE(meta.methods.size() == 1);
		CHECK(meta.methods[0].name == "foo");
	}
}

#endif // DOCTEST
