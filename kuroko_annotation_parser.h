/**************************************************************************/
/*  kuroko_annotation_parser.h                                            */
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

#ifndef KUROKO_ANNOTATION_PARSER_H
#define KUROKO_ANNOTATION_PARSER_H

#include "core/object.h"
#include "core/variant.h"

struct KurokoExportedProperty {
	String name;
	Variant::Type type;
	PropertyHint hint;
	String hint_string;
	Variant default_value;
	String setter; // setget setter function name (empty = direct access).
	String getter; // setget getter function name (empty = direct access).
};

struct KurokoExportedMethod {
	String name;
	Vector<String> arg_names;
};

struct KurokoSignalInfo {
	String name;
	Vector<String> arg_names;
};

struct KurokoScriptMetadata {
	String extends_class;
	bool is_tool;
	Vector<KurokoExportedProperty> properties;
	Vector<KurokoExportedMethod> methods;
	Vector<KurokoSignalInfo> signals;

	KurokoScriptMetadata() :
			is_tool(false) {}
};

class KurokoAnnotationParser {
	static Variant::Type _parse_type_name(const String &p_name);
	static PropertyHint _parse_hint_name(const String &p_name);
	static Variant _parse_default_value(const String &p_value, Variant::Type p_type);
	static void _parse_export_annotation(const String &p_content, KurokoScriptMetadata &r_meta);
	static void _parse_signal_annotation(const String &p_content, KurokoScriptMetadata &r_meta);

	static bool _is_lifecycle_method(const String &p_name);
	static void _parse_def_line(const String &p_line, bool p_force_export, KurokoScriptMetadata &r_meta);

public:
	// Parse $ annotations from source, returning metadata.
	static KurokoScriptMetadata parse(const String &p_source);

	// Strip all $ annotation lines from source, producing clean Kuroko code.
	// "$export def foo():" becomes "def foo():" (the def is kept).
	// "$export var speed: float = 200.0" is stripped entirely (vars live in annotations only).
	// "$extends Node2D", "$tool", "$signal ..." are stripped entirely.
	static String strip(const String &p_source);
};

#endif // KUROKO_ANNOTATION_PARSER_H
