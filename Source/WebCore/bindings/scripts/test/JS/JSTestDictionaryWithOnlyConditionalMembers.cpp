/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestDictionaryWithOnlyConditionalMembers.h"

#include "JSDOMGlobalObject.h"
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/ObjectConstructor.h>

#if ENABLE(TEST_CONDITIONAL)
#include "JSTestDictionary.h"
#endif



namespace WebCore {
using namespace JSC;

template<> ConversionResult<IDLDictionary<TestDictionaryWithOnlyConditionalMembers>> convertDictionary<TestDictionaryWithOnlyConditionalMembers>(JSGlobalObject& lexicalGlobalObject, JSValue value)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(&lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    bool isNullOrUndefined = value.isUndefinedOrNull();
    auto* object = isNullOrUndefined ? nullptr : value.getObject();
    if (!isNullOrUndefined && !object) [[unlikely]] {
        throwTypeError(&lexicalGlobalObject, throwScope);
        return ConversionResultException { };
    }
    TestDictionaryWithOnlyConditionalMembers result;
#if ENABLE(TEST_CONDITIONAL)
    JSValue conditionalMemberValue;
    if (isNullOrUndefined)
        conditionalMemberValue = jsUndefined();
    else {
        conditionalMemberValue = object->get(&lexicalGlobalObject, Identifier::fromString(vm, "conditionalMember"_s));
        RETURN_IF_EXCEPTION(throwScope, ConversionResultException { });
    }
    if (!conditionalMemberValue.isUndefined()) {
        auto conditionalMemberConversionResult = convert<IDLDictionary<TestDictionary>>(lexicalGlobalObject, conditionalMemberValue);
        if (conditionalMemberConversionResult.hasException(throwScope)) [[unlikely]]
            return ConversionResultException { };
        result.conditionalMember = conditionalMemberConversionResult.releaseReturnValue();
    }
#endif
    return result;
}

JSC::JSObject* convertDictionaryToJS(JSC::JSGlobalObject& lexicalGlobalObject, JSDOMGlobalObject& globalObject, const TestDictionaryWithOnlyConditionalMembers& dictionary)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(&lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    auto result = constructEmptyObject(&lexicalGlobalObject, globalObject.objectPrototype());

#if ENABLE(TEST_CONDITIONAL)
    if (!IDLDictionary<TestDictionary>::isNullValue(dictionary.conditionalMember)) {
        auto conditionalMemberValue = toJS<IDLDictionary<TestDictionary>>(lexicalGlobalObject, globalObject, throwScope, IDLDictionary<TestDictionary>::extractValueFromNullable(dictionary.conditionalMember));
        RETURN_IF_EXCEPTION(throwScope, { });
        result->putDirect(vm, JSC::Identifier::fromString(vm, "conditionalMember"_s), conditionalMemberValue);
    }
#endif
    UNUSED_PARAM(dictionary);
    UNUSED_VARIABLE(throwScope);

    return result;
}

} // namespace WebCore

