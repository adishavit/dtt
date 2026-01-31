// 1004_union_cve_parser_fail.cpp - Parser/deserializer type confusion patterns
// Common in file format parsers, network protocol handlers, serialization
// Pattern: Trusting type tags from untrusted input

// Binary file format header (common pattern in image/document parsers)
enum ChunkType { CHUNK_TEXT = 1, CHUNK_IMAGE = 2, CHUNK_LINK = 3, CHUNK_EMBED = 4 };

union ChunkData {
    struct { int length; char text[256]; } text;
    struct { int width; int height; void* pixelData; } image;
    struct { char url[256]; void* handler; } link;
    struct { int size; void* embeddedObject; } embed;
};

struct FileChunk {
    ChunkType type;  // From file - UNTRUSTED
    ChunkData data;
};

// Variant 1: Attacker controls type tag, code trusts it
void test_untrusted_type_tag(FileChunk* chunk) {
    // Type tag came from untrusted file
    if (chunk->type == CHUNK_TEXT) {
        char* text = chunk->data.text.text;  // FAIL if type was forged
        (void)text;
    }
}

// Variant 2: Image pixel data pointer from text chunk
void* test_image_from_text(FileChunk* chunk) {
    chunk->type = CHUNK_TEXT;  // Attacker says text
    chunk->data.text.length = 0x41414141;  // But this is image width!
    // Code reads as image
    return chunk->data.image.pixelData;  // FAIL: text bytes as pointer
}

// Variant 3: Embedded object confusion
void test_embed_confusion(FileChunk* chunk) {
    chunk->type = CHUNK_LINK;
    chunk->data.link.url[0] = 'A';
    chunk->data.link.handler = nullptr;
    // Code interprets as embed
    void* obj = chunk->data.embed.embeddedObject;  // FAIL: handler as object
    (void)obj;
}

// Network protocol message (common in RPC, IPC)
enum MsgType { MSG_REQUEST = 1, MSG_RESPONSE = 2, MSG_ERROR = 3, MSG_CALLBACK = 4 };

union MsgPayload {
    struct { int methodId; int argCount; void* args; } request;
    struct { int resultCode; void* resultData; } response;
    struct { int errorCode; char errorMsg[128]; } error;
    struct { void* callbackFunc; void* context; } callback;
};

struct Message {
    MsgType type;  // From network - UNTRUSTED
    MsgPayload payload;
};

// Variant 4: RPC callback confusion (code execution)
void test_rpc_callback_confusion(Message* msg) {
    msg->type = MSG_ERROR;  // Claim error message
    msg->payload.error.errorCode = 0x41414141;
    // Code treats as callback
    void (*func)() = (void(*)())msg->payload.callback.callbackFunc;  // FAIL
    // func();  // Would call attacker-controlled address
    (void)func;
}

// Variant 5: Response data as request args
void test_response_as_request(Message* msg) {
    msg->type = MSG_RESPONSE;
    msg->payload.response.resultCode = 0;
    msg->payload.response.resultData = (void*)0xdeadbeef;
    // Code processes as request
    void* args = msg->payload.request.args;  // FAIL: resultData as args
    (void)args;
}

// JSON/MessagePack value (serialization format)
enum JsonType { JSON_NULL = 0, JSON_BOOL = 1, JSON_INT = 2, JSON_STRING = 3, JSON_ARRAY = 4 };

union JsonValue {
    int boolVal;
    long intVal;
    struct { int length; char* data; } string;
    struct { int count; void* elements; } array;
};

struct JsonNode {
    JsonType type;  // From JSON - UNTRUSTED
    JsonValue value;
};

// Variant 6: JSON string from int
void test_json_string_from_int(JsonNode* node) {
    node->type = JSON_INT;
    node->value.intVal = 0x41414141;
    // Deserializer bug: treats as string
    char* str = node->value.string.data;  // FAIL: int bits as pointer
    int len = node->value.string.length;  // FAIL: high bits of int
    (void)str;
    (void)len;
}

// Variant 7: JSON array elements pointer confusion
void test_json_array_confusion(JsonNode* node) {
    node->type = JSON_STRING;
    node->value.string.length = 100;
    node->value.string.data = (char*)"test";
    // Code iterates as array
    void* elems = node->value.array.elements;  // FAIL: string ptr as array
    int count = node->value.array.count;  // FAIL: length as count
    (void)elems;
    (void)count;
}

// Database record union (common in DB engines)
union DbField {
    int intField;
    double floatField;
    struct { int len; char* data; } varchar;
    struct { int size; void* blob; } blobField;
};

struct DbRecord {
    int fieldType;  // From query - potentially UNTRUSTED
    DbField field;
};

// Variant 8: BLOB as VARCHAR (SQL injection adjacent)
void test_blob_as_varchar(DbRecord* rec) {
    rec->fieldType = 4;  // BLOB type
    rec->field.blobField.size = 1000;
    rec->field.blobField.blob = (void*)0x12345678;
    // Code processes as varchar
    char* str = rec->field.varchar.data;  // FAIL: blob ptr as string
    int len = rec->field.varchar.len;  // FAIL: blob size as len
    (void)str;
    (void)len;
}

int main() {
    FileChunk chunk;
    chunk.type = CHUNK_TEXT;
    test_untrusted_type_tag(&chunk);
    test_image_from_text(&chunk);
    test_embed_confusion(&chunk);

    Message msg;
    msg.type = MSG_REQUEST;
    test_rpc_callback_confusion(&msg);
    test_response_as_request(&msg);

    JsonNode node;
    node.type = JSON_NULL;
    test_json_string_from_int(&node);
    test_json_array_confusion(&node);

    DbRecord rec;
    rec.fieldType = 0;
    test_blob_as_varchar(&rec);
    return 0;
}
