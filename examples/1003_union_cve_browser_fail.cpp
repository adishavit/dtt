// 1003_union_cve_browser_fail.cpp - Browser/V8-style type confusion patterns
// Based on CVE-2022-1096, CVE-2022-4174, CVE-2015-0336 (Flash)
// Pattern: JavaScript value representation type confusion

// V8-style tagged pointer/value (simplified)
// In V8, values can be either SMI (small integer) or HeapObject pointer
union JsValue {
    long smi;           // Small integer (tagged)
    void* heapPtr;      // Pointer to heap object
    double number;      // Boxed number
};

struct JsObject {
    int type;  // 0=smi, 1=heapPtr, 2=number
    JsValue value;
};

// Variant 1: SMI interpreted as heap pointer (V8 type confusion pattern)
void test_smi_as_pointer(JsObject* obj) {
    obj->type = 0;  // SMI
    obj->value.smi = 42;
    // Code doesn't check type, assumes heap pointer
    void* ptr = obj->value.heapPtr;  // FAIL: integer as pointer  dtt-error {{inactive union member 'heapPtr'}}
    char* str = (char*)ptr;  // Attempt to read "string"
    (void)str;
}

// Variant 2: Heap pointer as SMI (CVE-2022-1096 pattern)
void* g_object = nullptr;
void test_pointer_as_smi(JsObject* obj) {
    obj->type = 1;  // HeapPtr
    obj->value.heapPtr = g_object;
    // Optimization assumes SMI without re-checking
    long val = obj->value.smi;  // FAIL: pointer bits as integer  dtt-error {{inactive union member 'smi'}}
    if (val < 100) {
        // Conditional based on corrupted value
    }
    (void)val;
}

// Flash ActionScript value (CVE-2015-0336 pattern)
union FlashValue {
    int integer;
    double number;
    void* objectPtr;
    struct { int length; char* data; } string;
};

struct ActionScriptAtom {
    unsigned char tag;  // Type tag
    FlashValue val;
};

// Variant 3: Flash integer as object pointer
void test_flash_int_as_object(ActionScriptAtom* atom) {
    atom->tag = 0;  // Integer type
    atom->val.integer = 0x41414141;
    // VM bug: treats as object without checking tag
    void* obj = atom->val.objectPtr;  // FAIL: int as pointer  dtt-error {{inactive union member 'objectPtr'}}
    (void)obj;
}

// Variant 4: Flash string length confusion
void test_flash_string_confusion(ActionScriptAtom* atom) {
    atom->tag = 3;  // String type
    atom->val.string.length = 10;
    atom->val.string.data = nullptr;
    // Code reads as number - interprets length/data as double bits
    double d = atom->val.number;  // KNOWN GAP (not asserted): string struct as double
    (void)d;
}

// WebKit-style CSS value (property access interceptor pattern)
union CSSValue {
    long intValue;
    double floatValue;
    char* stringValue;
    void* computedRef;
};

struct CSSProperty {
    int valueType;
    CSSValue value;
};

// Variant 5: CSS property interceptor confusion (CVE-2022-1096 style)
void test_css_interceptor(CSSProperty* prop) {
    prop->valueType = 0;  // Integer
    prop->value.intValue = 100;
    // Interceptor callback changes type but code continues
    // assuming original type
    char* str = prop->value.stringValue;  // FAIL: int as string ptr  dtt-error {{inactive union member 'stringValue'}}
    (void)str;
}

// Variant 6: CSS computed value reference leak
void* test_css_computed_leak(CSSProperty* prop) {
    prop->valueType = 3;  // Computed reference
    prop->value.computedRef = (void*)0xdeadbeef;
    // Code returns as int - leaks pointer value
    return (void*)(long)prop->value.intValue;  // FAIL: pointer bits leaked as int  dtt-error {{inactive union member 'intValue'}}
}

// DOM node type union (common in browser engines)
union DOMData {
    struct { char* textContent; int length; } text;
    struct { void* element; int childCount; } element;
    struct { void* document; } doc;
};

struct DOMNode {
    int nodeType;  // 1=element, 3=text, 9=document
    DOMData data;
};

// Variant 7: Text node interpreted as element
void test_dom_text_as_element(DOMNode* node) {
    node->nodeType = 3;  // Text node
    node->data.text.textContent = (char*)"Hello";
    node->data.text.length = 5;
    // Code assumes element without checking
    void* elem = node->data.element.element;  // KNOWN GAP (not asserted): text ptr as element
    int count = node->data.element.childCount;  // KNOWN GAP (not asserted): length as count
    (void)elem;
    (void)count;
}

// Variant 8: Document as text (info leak)
void test_dom_doc_as_text(DOMNode* node) {
    node->nodeType = 9;  // Document
    node->data.doc.document = (void*)0x12345678;
    // Read as text node
    char* text = node->data.text.textContent;  // KNOWN GAP (not asserted): doc ptr as string
    (void)text;
}

int main() {
    JsObject js;
    js.type = 0;
    test_smi_as_pointer(&js);
    test_pointer_as_smi(&js);

    ActionScriptAtom atom;
    atom.tag = 0;
    test_flash_int_as_object(&atom);
    test_flash_string_confusion(&atom);

    CSSProperty css;
    css.valueType = 0;
    test_css_interceptor(&css);
    test_css_computed_leak(&css);

    DOMNode dom;
    dom.nodeType = 1;
    test_dom_text_as_element(&dom);
    test_dom_doc_as_text(&dom);
    return 0;
}
