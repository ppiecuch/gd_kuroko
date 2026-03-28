/**************************************************************************/
/*  kuroko_script_instance.h                                              */
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

#ifndef KUROKO_SCRIPT_INSTANCE_H
#define KUROKO_SCRIPT_INSTANCE_H

#include "kuroko_script.h"
#include "kuroko_type_conversion.h"

#include "core/reference.h"

// Forward declare the Kuroko C type without including headers.
struct KrkInstance;

class KurokoScriptInstance : public Reference {
	GDCLASS(KurokoScriptInstance, Reference);

	Object *owner;
	Ref<KurokoScript> script;
	KrkInstance *module_scope;
	bool initialized;

protected:
	static void _bind_methods();

public:
	void initialize();

	bool set_property(const String &p_name, const Variant &p_value);
	Variant get_property(const String &p_name) const;

	Variant call_method(const String &p_method, const Array &p_args = Array());

	void notification(int p_what);

	Ref<KurokoScript> get_script_ref() const;
	Object *get_owner() const;
	bool is_initialized() const;

	void set_owner(Object *p_owner);
	void set_script_ref(const Ref<KurokoScript> &p_script);

	KurokoScriptInstance();
	~KurokoScriptInstance();
};

#endif // KUROKO_SCRIPT_INSTANCE_H
