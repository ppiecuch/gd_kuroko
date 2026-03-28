/**************************************************************************/
/*  kuroko_type_conversion.cpp                                            */
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

#include "kuroko_type_conversion.h"

#include "core/print_string.h"

Variant KurokoTypeConversion::krk_value_to_variant(KrkValue p_value) {
	// Primitives.
	if (IS_NONE(p_value)) {
		return Variant();
	}
	if (IS_BOOLEAN(p_value)) {
		return Variant((bool)AS_BOOLEAN(p_value));
	}
	if (IS_INTEGER(p_value)) {
		return Variant((int64_t)AS_INTEGER(p_value));
	}
	if (IS_FLOATING(p_value)) {
		return Variant(AS_FLOATING(p_value));
	}

	// String.
	if (IS_STRING(p_value)) {
		KrkString *str = AS_STRING(p_value);
		return Variant(String::utf8(str->chars, str->length));
	}

	// Bytes → PoolByteArray.
	if (IS_OBJECT(p_value) && AS_OBJECT(p_value)->type == KRK_OBJ_BYTES) {
		KrkBytes *bytes = AS_BYTES(p_value);
		PoolByteArray arr;
		arr.resize(bytes->length);
		if (bytes->length > 0) {
			PoolByteArray::Write w = arr.write();
			memcpy(w.ptr(), bytes->bytes, bytes->length);
		}
		return Variant(arr);
	}

	// Tuple → Array (immutability lost).
	if (IS_OBJECT(p_value) && AS_OBJECT(p_value)->type == KRK_OBJ_TUPLE) {
		KrkTuple *tuple = AS_TUPLE(p_value);
		Array arr;
		for (size_t i = 0; i < tuple->values.count; i++) {
			arr.push_back(krk_value_to_variant(tuple->values.values[i]));
		}
		return Variant(arr);
	}

	// List → Array.
	if (IS_INSTANCE(p_value) && AS_INSTANCE(p_value)->_class == krk_vm.baseClasses->listClass) {
		KrkValueArray *list = AS_LIST(p_value);
		Array arr;
		for (size_t i = 0; i < list->count; i++) {
			arr.push_back(krk_value_to_variant(list->values[i]));
		}
		return Variant(arr);
	}

	// Dict → Dictionary.
	if (IS_INSTANCE(p_value) && AS_INSTANCE(p_value)->_class == krk_vm.baseClasses->dictClass) {
		KrkTable *table = AS_DICT(p_value);
		Dictionary dict;
		for (size_t i = 0; i <= table->capacity; i++) {
			KrkTableEntry *entry = &table->entries[i];
			if (IS_KWARGS(entry->key)) {
				continue; // Empty or tombstone slot.
			}
			dict[krk_value_to_variant(entry->key)] = krk_value_to_variant(entry->value);
		}
		return Variant(dict);
	}

	// Fallback: try to get string representation.
	if (IS_OBJECT(p_value)) {
		KrkClass *type = krk_getType(p_value);
		if (type && type->_reprer) {
			krk_push(p_value);
			KrkValue repr = krk_callDirect(type->_reprer, 1);
			if (IS_STRING(repr)) {
				return Variant(String::utf8(AS_CSTRING(repr)));
			}
		}
	}

	return Variant();
}

KrkValue KurokoTypeConversion::variant_to_krk_value(const Variant &p_variant) {
	switch (p_variant.get_type()) {
		case Variant::NIL:
			return NONE_VAL();

		case Variant::BOOL:
			return BOOLEAN_VAL((bool)p_variant);

		case Variant::INT:
			return INTEGER_VAL((int64_t)p_variant);

		case Variant::REAL:
			return FLOATING_VAL((double)p_variant);

		case Variant::STRING: {
			String s = p_variant;
			CharString utf8 = s.utf8();
			return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
		}

		case Variant::POOL_BYTE_ARRAY: {
			PoolByteArray arr = p_variant;
			PoolByteArray::Read r = arr.read();
			return OBJECT_VAL(krk_newBytes(arr.size(), (uint8_t *)r.ptr()));
		}

		case Variant::ARRAY: {
			Array arr = p_variant;
			// Create empty list, then append elements.
			// GC safety: push list on stack during construction.
			KrkValue list = krk_list_of(0, NULL, 0);
			krk_push(list);
			KrkValueArray *values = AS_LIST(list);
			for (int i = 0; i < arr.size(); i++) {
				KrkValue elem = variant_to_krk_value(arr[i]);
				krk_push(elem); // GC protect.
				krk_writeValueArray(values, elem);
				krk_pop(); // elem
			}
			krk_pop(); // list
			return list;
		}

		case Variant::DICTIONARY: {
			Dictionary dict = p_variant;
			// Create empty dict, then insert entries.
			KrkValue krk_dict = krk_dict_of(0, NULL, 0);
			krk_push(krk_dict);
			KrkTable *table = AS_DICT(krk_dict);
			Array keys = dict.keys();
			for (int i = 0; i < keys.size(); i++) {
				KrkValue key = variant_to_krk_value(keys[i]);
				krk_push(key);
				KrkValue val = variant_to_krk_value(dict[keys[i]]);
				krk_push(val);
				krk_tableSet(table, key, val);
				krk_pop(); // val
				krk_pop(); // key
			}
			krk_pop(); // dict
			return krk_dict;
		}

		default:
			// Unsupported type: convert to string representation.
			String s = p_variant;
			CharString utf8 = s.utf8();
			return OBJECT_VAL(krk_copyString(utf8.get_data(), utf8.length()));
	}
}

// --- Doctests ---

#ifdef DOCTEST
#include "doctest/doctest.h"
#include "kuroko_language.h"

TEST_SUITE("[[gd_kuroko]] TypeConversion") {
	TEST_CASE("[kuroko-conv] None round-trip") {
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant());
		CHECK(IS_NONE(v));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::NIL);
	}

	TEST_CASE("[kuroko-conv] bool round-trip") {
		KrkValue v_true = KurokoTypeConversion::variant_to_krk_value(Variant(true));
		CHECK(IS_BOOLEAN(v_true));
		CHECK(AS_BOOLEAN(v_true) == 1);
		CHECK((bool)KurokoTypeConversion::krk_value_to_variant(v_true) == true);

		KrkValue v_false = KurokoTypeConversion::variant_to_krk_value(Variant(false));
		CHECK(AS_BOOLEAN(v_false) == 0);
		CHECK((bool)KurokoTypeConversion::krk_value_to_variant(v_false) == false);
	}

	TEST_CASE("[kuroko-conv] int round-trip") {
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(42));
		CHECK(IS_INTEGER(v));
		CHECK(AS_INTEGER(v) == 42);
		CHECK((int64_t)KurokoTypeConversion::krk_value_to_variant(v) == 42);

		// Negative
		v = KurokoTypeConversion::variant_to_krk_value(Variant(-100));
		CHECK(AS_INTEGER(v) == -100);

		// Zero
		v = KurokoTypeConversion::variant_to_krk_value(Variant(0));
		CHECK(AS_INTEGER(v) == 0);
	}

	TEST_CASE("[kuroko-conv] float round-trip") {
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(3.14));
		CHECK(IS_FLOATING(v));
		CHECK(AS_FLOATING(v) == doctest::Approx(3.14));
		CHECK((double)KurokoTypeConversion::krk_value_to_variant(v) == doctest::Approx(3.14));

		// Negative float
		v = KurokoTypeConversion::variant_to_krk_value(Variant(-0.5));
		CHECK(AS_FLOATING(v) == doctest::Approx(-0.5));

		// Zero float
		v = KurokoTypeConversion::variant_to_krk_value(Variant(0.0));
		CHECK(AS_FLOATING(v) == doctest::Approx(0.0));
	}

	TEST_CASE("[kuroko-conv] string round-trip") {
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(String("hello")));
		CHECK(IS_STRING(v));
		String result = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(result == "hello");

		// Empty string
		v = KurokoTypeConversion::variant_to_krk_value(Variant(String("")));
		CHECK(IS_STRING(v));
		CHECK((String)KurokoTypeConversion::krk_value_to_variant(v) == "");

		// Unicode
		v = KurokoTypeConversion::variant_to_krk_value(Variant(String::utf8("café")));
		CHECK(IS_STRING(v));
	}

	TEST_CASE("[kuroko-conv] bytes round-trip") {
		PoolByteArray bytes;
		bytes.resize(4);
		{
			PoolByteArray::Write w = bytes.write();
			w[0] = 0xDE;
			w[1] = 0xAD;
			w[2] = 0xBE;
			w[3] = 0xEF;
		}
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(bytes));
		CHECK(IS_OBJECT(v));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::POOL_BYTE_ARRAY);
		PoolByteArray result = r;
		CHECK(result.size() == 4);
		CHECK(result[0] == 0xDE);
		CHECK(result[3] == 0xEF);
	}

	TEST_CASE("[kuroko-conv] array round-trip") {
		Array arr;
		arr.push_back(1);
		arr.push_back("two");
		arr.push_back(3.0);
		arr.push_back(Variant()); // nil

		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(arr));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::ARRAY);
		Array result = r;
		REQUIRE(result.size() == 4);
		CHECK((int64_t)result[0] == 1);
		CHECK((String)result[1] == "two");
		CHECK((double)result[2] == doctest::Approx(3.0));
		CHECK(result[3].get_type() == Variant::NIL);
	}

	TEST_CASE("[kuroko-conv] dictionary round-trip") {
		Dictionary dict;
		dict["name"] = "test";
		dict["value"] = 42;
		dict["pi"] = 3.14;

		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(dict));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::DICTIONARY);
		Dictionary result = r;
		CHECK((String)result["name"] == "test");
		CHECK((int64_t)result["value"] == 42);
		CHECK((double)result["pi"] == doctest::Approx(3.14));
	}

	TEST_CASE("[kuroko-conv] nested array") {
		Array inner;
		inner.push_back(1);
		inner.push_back(2);
		Array outer;
		outer.push_back(inner);
		outer.push_back("end");

		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(outer));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		Array result = r;
		REQUIRE(result.size() == 2);
		CHECK(result[0].get_type() == Variant::ARRAY);
		Array inner_result = result[0];
		CHECK((int64_t)inner_result[0] == 1);
	}

	TEST_CASE("[kuroko-conv] nested dict in array") {
		Dictionary d;
		d["key"] = "val";
		Array arr;
		arr.push_back(d);

		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(arr));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		Array result = r;
		REQUIRE(result.size() == 1);
		CHECK(result[0].get_type() == Variant::DICTIONARY);
		Dictionary result_d = result[0];
		CHECK((String)result_d["key"] == "val");
	}

	TEST_CASE("[kuroko-conv] empty array") {
		Array arr;
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(arr));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::ARRAY);
		CHECK(((Array)r).size() == 0);
	}

	TEST_CASE("[kuroko-conv] empty dictionary") {
		Dictionary dict;
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant(dict));
		Variant r = KurokoTypeConversion::krk_value_to_variant(v);
		CHECK(r.get_type() == Variant::DICTIONARY);
		CHECK(((Dictionary)r).size() == 0);
	}

	TEST_CASE("[kuroko-conv] unsupported type becomes string") {
		// Vector2 is not directly supported, should convert to string.
		Variant vec = Vector2(1, 2);
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(vec);
		CHECK(IS_STRING(v)); // Fell through to string fallback.
	}

	TEST_CASE("[kuroko-conv] large integer") {
		KrkValue v = KurokoTypeConversion::variant_to_krk_value(Variant((int64_t)1000000));
		CHECK(IS_INTEGER(v));
		CHECK(AS_INTEGER(v) == 1000000);
	}
}

#endif // DOCTEST
