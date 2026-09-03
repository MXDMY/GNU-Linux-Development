/*
  Standalone functional test for the slim cJSON + cJSON_Utils tree.

  Target: Debian 13 (gcc, glibc). Also builds with any C99 gcc.

  Build and run:
      gcc -std=c99 -Wall -Wextra -O2 -o cJSON_gnul_test \
          cJSON_gnul_test.c cJSON.c cJSON_Utils.c -lm
      ./cJSON_gnul_test

  Exit 0 if every check passed, 1 otherwise.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cJSON.h"
#include "cJSON_Utils.h"

static int g_passed;
static int g_failed;

#define CHECK(cond) do { \
    if (cond) { \
        g_passed++; \
    } else { \
        g_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static cJSON *parse_ok(const char *text)
{
    cJSON *item;

    item = cJSON_Parse(text);
    CHECK(item != NULL);
    if (item == NULL)
    {
        fprintf(stderr, "    parse failed: %s\n    json: %s\n",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "(null)",
                text ? text : "(null)");
    }
    return item;
}

static void expect_unformatted(const cJSON *item, const char *expected)
{
    char *printed;

    printed = cJSON_PrintUnformatted(item);
    CHECK(printed != NULL);
    if (printed != NULL)
    {
        CHECK(strcmp(printed, expected) == 0);
        if (strcmp(printed, expected) != 0)
        {
            fprintf(stderr, "    got:      %s\n    expected: %s\n", printed, expected);
        }
        cJSON_free(printed);
    }
}

/* -------------------------------------------------------------------------- */
/* Core: version, types, create, print                                        */
/* -------------------------------------------------------------------------- */

static void test_version(void)
{
    const char *ver;

    ver = cJSON_Version();
    CHECK(ver != NULL);
    CHECK(strcmp(ver, "1.7.19") == 0);
    CHECK(CJSON_VERSION_MAJOR == 1);
    CHECK(CJSON_VERSION_MINOR == 7);
    CHECK(CJSON_VERSION_PATCH == 19);
}

static void test_create_and_type_checks(void)
{
    cJSON *n;
    cJSON *t;
    cJSON *f;
    cJSON *b;
    cJSON *num;
    cJSON *str;
    cJSON *raw;
    cJSON *arr;
    cJSON *obj;

    CHECK(cJSON_IsInvalid(NULL) == 0);
    CHECK(cJSON_IsFalse(NULL) == 0);
    CHECK(cJSON_IsTrue(NULL) == 0);
    CHECK(cJSON_IsBool(NULL) == 0);
    CHECK(cJSON_IsNull(NULL) == 0);
    CHECK(cJSON_IsNumber(NULL) == 0);
    CHECK(cJSON_IsString(NULL) == 0);
    CHECK(cJSON_IsArray(NULL) == 0);
    CHECK(cJSON_IsObject(NULL) == 0);
    CHECK(cJSON_IsRaw(NULL) == 0);

    n = cJSON_CreateNull();
    t = cJSON_CreateTrue();
    f = cJSON_CreateFalse();
    b = cJSON_CreateBool(1);
    num = cJSON_CreateNumber(3.5);
    str = cJSON_CreateString("hello");
    raw = cJSON_CreateRaw("[1,2]");
    arr = cJSON_CreateArray();
    obj = cJSON_CreateObject();

    CHECK(cJSON_IsNull(n));
    CHECK(cJSON_IsTrue(t));
    CHECK(cJSON_IsFalse(f));
    CHECK(cJSON_IsBool(t) && cJSON_IsBool(f) && cJSON_IsBool(b));
    CHECK(cJSON_IsTrue(b));
    CHECK(cJSON_IsNumber(num));
    CHECK(cJSON_GetNumberValue(num) == 3.5);
    CHECK(cJSON_IsString(str));
    CHECK(strcmp(cJSON_GetStringValue(str), "hello") == 0);
    CHECK(cJSON_GetStringValue(num) == NULL);
    CHECK(isnan(cJSON_GetNumberValue(str)));
    CHECK(cJSON_IsRaw(raw));
    CHECK(cJSON_IsArray(arr));
    CHECK(cJSON_IsObject(obj));
    CHECK(!cJSON_IsInvalid(obj));

    expect_unformatted(n, "null");
    expect_unformatted(t, "true");
    expect_unformatted(f, "false");
    expect_unformatted(num, "3.5");
    expect_unformatted(str, "\"hello\"");
    expect_unformatted(raw, "[1,2]");
    expect_unformatted(arr, "[]");
    expect_unformatted(obj, "{}");

    cJSON_Delete(n);
    cJSON_Delete(t);
    cJSON_Delete(f);
    cJSON_Delete(b);
    cJSON_Delete(num);
    cJSON_Delete(str);
    cJSON_Delete(raw);
    cJSON_Delete(arr);
    cJSON_Delete(obj);
}

static void test_create_arrays(void)
{
    const int ints[3] = {1, 2, 3};
    const float floats[2] = {1.5f, 2.5f};
    const double doubles[2] = {1.25, 2.25};
    const char *strings[3] = {"a", "b", "c"};
    cJSON *ai;
    cJSON *af;
    cJSON *ad;
    cJSON *as;

    ai = cJSON_CreateIntArray(ints, 3);
    af = cJSON_CreateFloatArray(floats, 2);
    ad = cJSON_CreateDoubleArray(doubles, 2);
    as = cJSON_CreateStringArray(strings, 3);

    CHECK(cJSON_GetArraySize(ai) == 3);
    CHECK(cJSON_GetArrayItem(ai, 1)->valueint == 2);
    CHECK(cJSON_GetArraySize(af) == 2);
    CHECK(cJSON_GetNumberValue(cJSON_GetArrayItem(af, 0)) == (double)1.5f);
    CHECK(cJSON_GetArraySize(ad) == 2);
    CHECK(cJSON_GetNumberValue(cJSON_GetArrayItem(ad, 1)) == 2.25);
    CHECK(cJSON_GetArraySize(as) == 3);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(as, 2)), "c") == 0);
    CHECK(cJSON_GetArrayItem(as, 99) == NULL);

    cJSON_Delete(ai);
    cJSON_Delete(af);
    cJSON_Delete(ad);
    cJSON_Delete(as);
}

static void test_print_variants(void)
{
    cJSON *root;
    char *formatted;
    char *buffered;
    char pre[64];
    char too_small[8];

    root = parse_ok("{\"x\":1}");
    if (root == NULL)
    {
        return;
    }

    formatted = cJSON_Print(root);
    CHECK(formatted != NULL);
    CHECK(strstr(formatted, "\"x\"") != NULL);
    CHECK(strchr(formatted, '\n') != NULL);
    cJSON_free(formatted);

    buffered = cJSON_PrintBuffered(root, 16, 0);
    CHECK(buffered != NULL);
    CHECK(strcmp(buffered, "{\"x\":1}") == 0);
    cJSON_free(buffered);

    buffered = cJSON_PrintBuffered(root, 16, 1);
    CHECK(buffered != NULL);
    CHECK(strstr(buffered, "\"x\"") != NULL);
    cJSON_free(buffered);

    memset(pre, 0, sizeof(pre));
    CHECK(cJSON_PrintPreallocated(root, pre, (int)sizeof(pre), 0) == 1);
    CHECK(strcmp(pre, "{\"x\":1}") == 0);

    memset(too_small, 0, sizeof(too_small));
    CHECK(cJSON_PrintPreallocated(root, too_small, (int)sizeof(too_small), 0) == 0);

    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------- */
/* Core: parse                                                                */
/* -------------------------------------------------------------------------- */

static void test_parse_literals_and_numbers(void)
{
    cJSON *item;

    item = parse_ok("true");
    CHECK(cJSON_IsTrue(item));
    cJSON_Delete(item);

    item = parse_ok("false");
    CHECK(cJSON_IsFalse(item));
    cJSON_Delete(item);

    item = parse_ok("null");
    CHECK(cJSON_IsNull(item));
    cJSON_Delete(item);

    item = parse_ok("0");
    CHECK(cJSON_GetNumberValue(item) == 0.0);
    cJSON_Delete(item);

    item = parse_ok("-12");
    CHECK(item->valueint == -12);
    cJSON_Delete(item);

    item = parse_ok("3.1415");
    CHECK(fabs(cJSON_GetNumberValue(item) - 3.1415) < 1e-6);
    cJSON_Delete(item);

    item = parse_ok("1.2e3");
    CHECK(fabs(cJSON_GetNumberValue(item) - 1200.0) < 1e-6);
    cJSON_Delete(item);

    item = parse_ok("\"hello\\nworld\"");
    CHECK(strcmp(cJSON_GetStringValue(item), "hello\nworld") == 0);
    cJSON_Delete(item);

    item = parse_ok("\"\\u0041\\u0042\"");
    CHECK(strcmp(cJSON_GetStringValue(item), "AB") == 0);
    cJSON_Delete(item);
}

static void test_parse_object_array(void)
{
    cJSON *root;
    cJSON *arr;
    cJSON *elem;
    int count;

    root = parse_ok(
        "{"
        "\"name\":\"cJSON\","
        "\"ok\":true,"
        "\"n\":null,"
        "\"nums\":[1,2,3],"
        "\"nested\":{\"k\":false}"
        "}");
    if (root == NULL)
    {
        return;
    }

    CHECK(cJSON_GetArraySize(root) == 5);
    CHECK(cJSON_HasObjectItem(root, "name"));
    CHECK(cJSON_HasObjectItem(root, "NAME")); /* case-insensitive */
    CHECK(!cJSON_HasObjectItem(root, "missing"));

    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(root, "name")), "cJSON") == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "name") != NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "NAME") == NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "ok")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(root, "n")));

    arr = cJSON_GetObjectItem(root, "nums");
    CHECK(cJSON_IsArray(arr));
    CHECK(cJSON_GetArraySize(arr) == 3);
    CHECK(cJSON_GetArrayItem(arr, 0)->valueint == 1);
    CHECK(cJSON_GetArrayItem(arr, 2)->valueint == 3);

    count = 0;
    cJSON_ArrayForEach(elem, arr)
    {
        count++;
        CHECK(cJSON_IsNumber(elem));
    }
    CHECK(count == 3);

    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "nested"), "k")));

    cJSON_Delete(root);
}

static void test_parse_options(void)
{
    const char *ok_json = "{\"a\":1}";
    const char *with_junk = "{\"a\":1} trailing";
    char unterminated[7];
    const char *end = NULL;
    cJSON *item;
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF, '{', '}', 0};

    item = cJSON_ParseWithOpts(ok_json, &end, 1);
    CHECK(item != NULL);
    CHECK(end != NULL && *end == '\0');
    cJSON_Delete(item);

    item = cJSON_ParseWithOpts(with_junk, &end, 0);
    CHECK(item != NULL);
    CHECK(end != NULL && *end == ' ');
    cJSON_Delete(item);

    item = cJSON_ParseWithOpts(with_junk, &end, 1);
    CHECK(item == NULL);
    CHECK(cJSON_GetErrorPtr() != NULL);

    memcpy(unterminated, "{\"a\":1}", 7);
    item = cJSON_ParseWithLength(unterminated, 7);
    CHECK(item != NULL);
    CHECK(cJSON_GetObjectItem(item, "a")->valueint == 1);
    cJSON_Delete(item);

    item = cJSON_ParseWithLengthOpts(unterminated, 7, &end, 0);
    CHECK(item != NULL);
    cJSON_Delete(item);

    item = cJSON_Parse((const char *)bom);
    CHECK(item != NULL);
    CHECK(cJSON_IsObject(item));
    CHECK(cJSON_GetArraySize(item) == 0);
    cJSON_Delete(item);

    item = cJSON_Parse("{");
    CHECK(item == NULL);
    CHECK(cJSON_GetErrorPtr() != NULL);

    item = cJSON_Parse("[1,]");
    CHECK(item == NULL);

    item = cJSON_Parse(NULL);
    CHECK(item == NULL);
}

static void test_minify(void)
{
    char json[] = " { \"a\" : 1 ,\n \"b\" : [ 2 , 3 ] } ";
    cJSON *item;

    cJSON_Minify(json);
    CHECK(strcmp(json, "{\"a\":1,\"b\":[2,3]}") == 0);

    item = cJSON_Parse(json);
    CHECK(item != NULL);
    cJSON_Delete(item);
}

/* -------------------------------------------------------------------------- */
/* Core: mutate tree                                                          */
/* -------------------------------------------------------------------------- */

static void test_add_helpers(void)
{
    cJSON *obj;
    cJSON *child;

    obj = cJSON_CreateObject();
    CHECK(cJSON_AddNullToObject(obj, "n") != NULL);
    CHECK(cJSON_AddTrueToObject(obj, "t") != NULL);
    CHECK(cJSON_AddFalseToObject(obj, "f") != NULL);
    CHECK(cJSON_AddBoolToObject(obj, "b", 1) != NULL);
    CHECK(cJSON_AddNumberToObject(obj, "num", 9) != NULL);
    CHECK(cJSON_AddStringToObject(obj, "s", "x") != NULL);
    CHECK(cJSON_AddRawToObject(obj, "r", "[]") != NULL);
    child = cJSON_AddObjectToObject(obj, "o");
    CHECK(child != NULL);
    CHECK(cJSON_AddArrayToObject(obj, "a") != NULL);
    CHECK(cJSON_AddNumberToObject(child, "inner", 1) != NULL);

    CHECK(cJSON_IsNull(cJSON_GetObjectItem(obj, "n")));
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(obj, "t")));
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(obj, "f")));
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(obj, "b")));
    CHECK(cJSON_GetObjectItem(obj, "num")->valueint == 9);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(obj, "s")), "x") == 0);
    CHECK(cJSON_IsRaw(cJSON_GetObjectItem(obj, "r")));
    CHECK(cJSON_IsObject(cJSON_GetObjectItem(obj, "o")));
    CHECK(cJSON_IsArray(cJSON_GetObjectItem(obj, "a")));

    cJSON_Delete(obj);
}

static void test_array_object_edit(void)
{
    cJSON *arr;
    cJSON *obj;
    cJSON *detached;
    cJSON *item;
    const char *cs_key = "constkey";

    arr = cJSON_CreateArray();
    CHECK(cJSON_AddItemToArray(arr, cJSON_CreateNumber(0)));
    CHECK(cJSON_AddItemToArray(arr, cJSON_CreateNumber(2)));
    CHECK(cJSON_InsertItemInArray(arr, 1, cJSON_CreateNumber(1)));
    CHECK(cJSON_GetArraySize(arr) == 3);
    CHECK(cJSON_GetArrayItem(arr, 0)->valueint == 0);
    CHECK(cJSON_GetArrayItem(arr, 1)->valueint == 1);
    CHECK(cJSON_GetArrayItem(arr, 2)->valueint == 2);

    CHECK(cJSON_ReplaceItemInArray(arr, 1, cJSON_CreateNumber(10)));
    CHECK(cJSON_GetArrayItem(arr, 1)->valueint == 10);

    item = cJSON_GetArrayItem(arr, 1);
    CHECK(cJSON_ReplaceItemViaPointer(arr, item, cJSON_CreateNumber(11)));
    CHECK(cJSON_GetArrayItem(arr, 1)->valueint == 11);

    detached = cJSON_DetachItemFromArray(arr, 0);
    CHECK(detached != NULL && detached->valueint == 0);
    CHECK(cJSON_GetArraySize(arr) == 2);
    cJSON_Delete(detached);

    item = cJSON_GetArrayItem(arr, 0);
    detached = cJSON_DetachItemViaPointer(arr, item);
    CHECK(detached != NULL && detached->valueint == 11);
    cJSON_Delete(detached);

    cJSON_DeleteItemFromArray(arr, 0);
    CHECK(cJSON_GetArraySize(arr) == 0);
    cJSON_Delete(arr);

    obj = cJSON_CreateObject();
    CHECK(cJSON_AddItemToObject(obj, "a", cJSON_CreateNumber(1)));
    CHECK(cJSON_AddItemToObjectCS(obj, cs_key, cJSON_CreateNumber(2)));
    CHECK(cJSON_GetObjectItem(obj, "a")->valueint == 1);
    CHECK(cJSON_GetObjectItem(obj, "constkey")->valueint == 2);

    CHECK(cJSON_ReplaceItemInObject(obj, "a", cJSON_CreateNumber(3)));
    CHECK(cJSON_GetObjectItem(obj, "a")->valueint == 3);
    CHECK(cJSON_ReplaceItemInObjectCaseSensitive(obj, "constkey", cJSON_CreateNumber(4)));
    CHECK(cJSON_GetObjectItemCaseSensitive(obj, "constkey")->valueint == 4);

    detached = cJSON_DetachItemFromObject(obj, "A"); /* case-insensitive */
    CHECK(detached != NULL && detached->valueint == 3);
    cJSON_Delete(detached);
    CHECK(!cJSON_HasObjectItem(obj, "a"));

    detached = cJSON_DetachItemFromObjectCaseSensitive(obj, "constkey");
    CHECK(detached != NULL && detached->valueint == 4);
    cJSON_Delete(detached);

    CHECK(cJSON_AddItemToObject(obj, "gone", cJSON_CreateTrue()));
    cJSON_DeleteItemFromObject(obj, "GONE");
    CHECK(!cJSON_HasObjectItem(obj, "gone"));

    CHECK(cJSON_AddItemToObject(obj, "x", cJSON_CreateFalse()));
    cJSON_DeleteItemFromObjectCaseSensitive(obj, "x");
    CHECK(!cJSON_HasObjectItem(obj, "x"));

    cJSON_Delete(obj);
}

static void test_setters_duplicate_compare(void)
{
    cJSON *obj;
    cJSON *dup_shallow;
    cJSON *dup_deep;
    cJSON *other;
    cJSON *flag;
    char *newstr;

    obj = parse_ok("{\"k\":\"old\",\"n\":1,\"child\":{\"z\":2}}");
    if (obj == NULL)
    {
        return;
    }

    newstr = cJSON_SetValuestring(cJSON_GetObjectItem(obj, "k"), "new");
    CHECK(newstr != NULL);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(obj, "k")), "new") == 0);

    cJSON_SetNumberValue(cJSON_GetObjectItem(obj, "n"), 42.5);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "n")) == 42.5);
    CHECK(cJSON_GetObjectItem(obj, "n")->valueint == 42);

    cJSON_SetIntValue(cJSON_GetObjectItem(obj, "n"), 7);
    CHECK(cJSON_GetObjectItem(obj, "n")->valueint == 7);
    CHECK(cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "n")) == 7.0);

    flag = cJSON_CreateTrue();
    CHECK(cJSON_SetBoolValue(flag, 0) == cJSON_False);
    CHECK(cJSON_IsFalse(flag));
    CHECK(cJSON_SetBoolValue(flag, 1) == cJSON_True);
    CHECK(cJSON_IsTrue(flag));
    CHECK(cJSON_SetBoolValue(obj, 0) == cJSON_Invalid);
    cJSON_Delete(flag);

    dup_deep = cJSON_Duplicate(obj, 1);
    CHECK(dup_deep != NULL);
    CHECK(cJSON_Compare(obj, dup_deep, 1));
    CHECK(cJSON_GetObjectItem(dup_deep, "child") != NULL);

    dup_shallow = cJSON_Duplicate(obj, 0);
    CHECK(dup_shallow != NULL);
    CHECK(cJSON_GetObjectItem(dup_shallow, "k") == NULL);

    other = parse_ok("{\"k\":\"new\",\"n\":7,\"child\":{\"z\":2}}");
    CHECK(cJSON_Compare(obj, other, 1));
    CHECK(cJSON_Compare(obj, other, 0));
    cJSON_Delete(other);

    /* case_sensitive=0 treats object keys as case-insensitive; string values stay exact */
    other = parse_ok("{\"K\":\"new\",\"n\":7,\"child\":{\"z\":2}}");
    CHECK(cJSON_Compare(obj, other, 0));
    CHECK(!cJSON_Compare(obj, other, 1));

    CHECK(!cJSON_Compare(obj, NULL, 1));
    CHECK(!cJSON_Compare(NULL, obj, 1));

    cJSON_Delete(obj);
    cJSON_Delete(dup_deep);
    cJSON_Delete(dup_shallow);
    cJSON_Delete(other);
}

static void test_references(void)
{
    char live[] = "live";
    cJSON *owned;
    cJSON *sref;
    cJSON *arr;
    cJSON *aref;
    cJSON *obj;
    cJSON *oref;
    cJSON *holder;

    sref = cJSON_CreateStringReference(live);
    CHECK(cJSON_IsString(sref));
    CHECK(sref->valuestring == live);
    cJSON_Delete(sref);
    CHECK(strcmp(live, "live") == 0);

    owned = cJSON_CreateString("keep");
    arr = cJSON_CreateArray();
    CHECK(cJSON_AddItemReferenceToArray(arr, owned));
    CHECK(cJSON_GetArraySize(arr) == 1);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetArrayItem(arr, 0)), "keep") == 0);
    cJSON_Delete(arr);
    CHECK(strcmp(cJSON_GetStringValue(owned), "keep") == 0);

    obj = cJSON_CreateObject();
    CHECK(cJSON_AddItemReferenceToObject(obj, "k", owned));
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(obj, "k")), "keep") == 0);
    cJSON_Delete(obj);
    CHECK(strcmp(cJSON_GetStringValue(owned), "keep") == 0);

    holder = cJSON_CreateArray();
    cJSON_AddItemToArray(holder, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(holder, cJSON_CreateNumber(2));
    aref = cJSON_CreateArrayReference(holder->child);
    CHECK(cJSON_IsArray(aref));
    cJSON_Delete(aref);
    CHECK(cJSON_GetArraySize(holder) == 2);

    oref = cJSON_CreateObjectReference(holder);
    CHECK(cJSON_IsObject(oref));
    cJSON_Delete(oref);
    CHECK(cJSON_GetArraySize(holder) == 2);

    cJSON_Delete(owned);
    cJSON_Delete(holder);
}

static void test_default_allocator(void)
{
    void *block;
    cJSON *item;

    block = cJSON_malloc(32);
    CHECK(block != NULL);
    if (block != NULL)
    {
        memset(block, 0x5A, 32);
        cJSON_free(block);
    }

    item = cJSON_CreateString("alloc");
    CHECK(item != NULL);
    cJSON_Delete(item);
}

static int g_hook_allocs;
static int g_hook_frees;

static void *test_malloc(size_t sz)
{
    g_hook_allocs++;
    return malloc(sz);
}

static void test_free(void *p)
{
    g_hook_frees++;
    free(p);
}

static void test_custom_hooks(void)
{
    cJSON_Hooks hooks;
    cJSON *item;
    char *printed;

    g_hook_allocs = 0;
    g_hook_frees = 0;
    hooks.malloc_fn = test_malloc;
    hooks.free_fn = test_free;
    cJSON_InitHooks(&hooks);

    item = cJSON_Parse("{\"a\":[1,true,null,\"z\"]}");
    CHECK(item != NULL);
    CHECK(g_hook_allocs > 0);

    printed = cJSON_PrintUnformatted(item);
    CHECK(printed != NULL);
    CHECK(strcmp(printed, "{\"a\":[1,true,null,\"z\"]}") == 0);

    cJSON_free(printed);
    cJSON_Delete(item);
    CHECK(g_hook_frees > 0);
    CHECK(g_hook_frees >= 1);

    cJSON_InitHooks(NULL);
}

/* -------------------------------------------------------------------------- */
/* Utils: RFC 6901 / 6902 / 7396                                              */
/* -------------------------------------------------------------------------- */

static void test_json_pointer(void)
{
    cJSON *root;
    cJSON *hit;
    char *ptr;

    root = parse_ok(
        "{"
        "\"foo\":[\"bar\",\"baz\"],"
        "\"\":0,"
        "\"a/b\":1,"
        "\"c%d\":2,"
        "\"e^f\":3,"
        "\"g|h\":4,"
        "\"i\\\\j\":5,"
        "\"k\\\"l\":6,"
        "\" \":7,"
        "\"m~n\":8,"
        "\"Foo\":9"
        "}");
    if (root == NULL)
    {
        return;
    }

    hit = cJSONUtils_GetPointer(root, "");
    CHECK(hit == root);

    hit = cJSONUtils_GetPointer(root, "/foo");
    CHECK(cJSON_IsArray(hit) && cJSON_GetArraySize(hit) == 2);

    hit = cJSONUtils_GetPointer(root, "/foo/0");
    CHECK(hit != NULL && strcmp(cJSON_GetStringValue(hit), "bar") == 0);

    hit = cJSONUtils_GetPointer(root, "/foo/1");
    CHECK(hit != NULL && strcmp(cJSON_GetStringValue(hit), "baz") == 0);

    hit = cJSONUtils_GetPointer(root, "/");
    CHECK(hit != NULL && hit->valueint == 0);

    hit = cJSONUtils_GetPointer(root, "/a~1b");
    CHECK(hit != NULL && hit->valueint == 1);

    hit = cJSONUtils_GetPointer(root, "/m~0n");
    CHECK(hit != NULL && hit->valueint == 8);

    hit = cJSONUtils_GetPointer(root, "/ ");
    CHECK(hit != NULL && hit->valueint == 7);

    hit = cJSONUtils_GetPointer(root, "/missing");
    CHECK(hit == NULL);

    hit = cJSONUtils_GetPointer(root, "/foo");
    CHECK(cJSONUtils_GetPointer(hit, "/0") != NULL);

    /* case folding vs exact match */
    hit = cJSONUtils_GetPointer(root, "/foo");
    CHECK(hit != NULL);
    hit = cJSONUtils_GetPointerCaseSensitive(root, "/Foo");
    CHECK(hit != NULL && hit->valueint == 9);
    hit = cJSONUtils_GetPointerCaseSensitive(root, "/foo");
    CHECK(cJSON_IsArray(hit));
    hit = cJSONUtils_GetPointer(root, "/FOO");
    CHECK(cJSON_IsArray(hit)); /* insensitive */
    hit = cJSONUtils_GetPointerCaseSensitive(root, "/FOO");
    CHECK(hit == NULL);

    ptr = cJSONUtils_FindPointerFromObjectTo(root, cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "foo"), 1));
    CHECK(ptr != NULL);
    if (ptr != NULL)
    {
        CHECK(strcmp(ptr, "/foo/1") == 0);
        cJSON_free(ptr);
    }

    ptr = cJSONUtils_FindPointerFromObjectTo(root, root);
    CHECK(ptr != NULL && strcmp(ptr, "") == 0);
    cJSON_free(ptr);

    ptr = cJSONUtils_FindPointerFromObjectTo(root, (cJSON *)root + 1);
    CHECK(ptr == NULL);

    cJSON_Delete(root);
}

static void test_json_patch(void)
{
    cJSON *doc;
    cJSON *patch;
    cJSON *expected;
    cJSON *generated;
    cJSON *from;
    cJSON *to;
    cJSON *value;

    /* add */
    doc = parse_ok("{\"foo\":\"bar\"}");
    patch = parse_ok("[{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    expected = parse_ok("{\"foo\":\"bar\",\"baz\":\"qux\"}");
    CHECK(cJSON_Compare(doc, expected, 1));
    cJSON_Delete(doc);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* remove */
    doc = parse_ok("{\"foo\":[\"bar\",\"baz\"]}");
    patch = parse_ok("[{\"op\":\"remove\",\"path\":\"/foo/1\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    expected = parse_ok("{\"foo\":[\"bar\"]}");
    CHECK(cJSON_Compare(doc, expected, 1));
    cJSON_Delete(doc);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* replace */
    doc = parse_ok("{\"foo\":\"bar\",\"baz\":\"qux\"}");
    patch = parse_ok("[{\"op\":\"replace\",\"path\":\"/baz\",\"value\":\"boo\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    expected = parse_ok("{\"foo\":\"bar\",\"baz\":\"boo\"}");
    CHECK(cJSON_Compare(doc, expected, 1));
    cJSON_Delete(doc);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* move */
    doc = parse_ok("{\"foo\":{\"bar\":\"baz\",\"waldo\":\"fred\"},\"qux\":{\"corge\":\"grault\"}}");
    patch = parse_ok("[{\"op\":\"move\",\"from\":\"/foo/waldo\",\"path\":\"/qux/thud\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    expected = parse_ok("{\"foo\":{\"bar\":\"baz\"},\"qux\":{\"corge\":\"grault\",\"thud\":\"fred\"}}");
    CHECK(cJSON_Compare(doc, expected, 1));
    cJSON_Delete(doc);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* copy */
    doc = parse_ok("{\"foo\":[\"bar\",\"baz\"],\"qux\":{\"thud\":\"grault\"}}");
    patch = parse_ok("[{\"op\":\"copy\",\"from\":\"/foo/0\",\"path\":\"/qux/thud\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    expected = parse_ok("{\"foo\":[\"bar\",\"baz\"],\"qux\":{\"thud\":\"bar\"}}");
    CHECK(cJSON_Compare(doc, expected, 1));
    cJSON_Delete(doc);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* test success / failure */
    doc = parse_ok("{\"foo\":\"bar\"}");
    patch = parse_ok("[{\"op\":\"test\",\"path\":\"/foo\",\"value\":\"bar\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    cJSON_Delete(patch);
    patch = parse_ok("[{\"op\":\"test\",\"path\":\"/foo\",\"value\":\"nope\"}]");
    CHECK(cJSONUtils_ApplyPatches(doc, patch) != 0);
    cJSON_Delete(doc);
    cJSON_Delete(patch);

    /* AddPatchToArray */
    doc = parse_ok("{\"n\":1}");
    patch = cJSON_CreateArray();
    value = cJSON_CreateNumber(2);
    cJSONUtils_AddPatchToArray(patch, "replace", "/n", value);
    cJSON_Delete(value);
    CHECK(cJSONUtils_ApplyPatches(doc, patch) == 0);
    CHECK(cJSON_GetObjectItem(doc, "n")->valueint == 2);
    cJSON_Delete(doc);
    cJSON_Delete(patch);

    /* GeneratePatches + apply */
    from = parse_ok("{\"a\":1,\"b\":2}");
    to = parse_ok("{\"a\":1,\"b\":3,\"c\":true}");
    generated = cJSONUtils_GeneratePatches(from, to);
    CHECK(generated != NULL);
    CHECK(cJSON_IsArray(generated));
    doc = parse_ok("{\"a\":1,\"b\":2}");
    CHECK(cJSONUtils_ApplyPatches(doc, generated) == 0);
    CHECK(cJSON_Compare(doc, to, 1));
    cJSON_Delete(from);
    cJSON_Delete(to);
    cJSON_Delete(generated);
    cJSON_Delete(doc);

    /* case-sensitive generate / apply */
    from = parse_ok("{\"Foo\":1}");
    to = parse_ok("{\"Foo\":2}");
    generated = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    CHECK(generated != NULL);
    doc = parse_ok("{\"Foo\":1}");
    CHECK(cJSONUtils_ApplyPatchesCaseSensitive(doc, generated) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(doc, "Foo")->valueint == 2);
    cJSON_Delete(from);
    cJSON_Delete(to);
    cJSON_Delete(generated);
    cJSON_Delete(doc);
}

static void test_merge_patch(void)
{
    cJSON *target;
    cJSON *patch;
    cJSON *expected;
    cJSON *generated;
    cJSON *from;
    cJSON *to;

    /* RFC 7396 example */
    target = parse_ok("{\"a\":\"b\",\"c\":{\"d\":\"e\",\"f\":\"g\"}}");
    patch = parse_ok("{\"a\":\"z\",\"c\":{\"f\":null}}");
    target = cJSONUtils_MergePatch(target, patch);
    CHECK(target != NULL);
    expected = parse_ok("{\"a\":\"z\",\"c\":{\"d\":\"e\"}}");
    CHECK(cJSON_Compare(target, expected, 1));
    cJSON_Delete(target);
    cJSON_Delete(patch);
    cJSON_Delete(expected);

    /* replace whole document with a scalar */
    target = parse_ok("{\"a\":1}");
    patch = parse_ok("\"hello\"");
    target = cJSONUtils_MergePatch(target, patch);
    CHECK(cJSON_IsString(target));
    CHECK(strcmp(cJSON_GetStringValue(target), "hello") == 0);
    cJSON_Delete(target);
    cJSON_Delete(patch);

    /* case-sensitive merge: Foo vs foo are distinct */
    target = parse_ok("{\"Foo\":1,\"bar\":2}");
    patch = parse_ok("{\"foo\":null}");
    target = cJSONUtils_MergePatchCaseSensitive(target, patch);
    CHECK(cJSON_GetObjectItemCaseSensitive(target, "Foo") != NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(target, "foo") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(target, "bar") != NULL);
    cJSON_Delete(target);
    cJSON_Delete(patch);

    /* GenerateMergePatch then apply */
    from = parse_ok("{\"a\":1,\"b\":2}");
    to = parse_ok("{\"a\":1,\"c\":3}");
    generated = cJSONUtils_GenerateMergePatch(from, to);
    CHECK(generated != NULL);
    target = parse_ok("{\"a\":1,\"b\":2}");
    target = cJSONUtils_MergePatch(target, generated);
    CHECK(cJSON_Compare(target, to, 1));
    cJSON_Delete(from);
    cJSON_Delete(to);
    cJSON_Delete(generated);
    cJSON_Delete(target);

    from = parse_ok("{\"Key\":1}");
    to = parse_ok("{\"Key\":2}");
    generated = cJSONUtils_GenerateMergePatchCaseSensitive(from, to);
    CHECK(generated != NULL);
    target = parse_ok("{\"Key\":1}");
    target = cJSONUtils_MergePatchCaseSensitive(target, generated);
    CHECK(cJSON_GetObjectItemCaseSensitive(target, "Key")->valueint == 2);
    cJSON_Delete(from);
    cJSON_Delete(to);
    cJSON_Delete(generated);
    cJSON_Delete(target);
}

static void test_sort_object(void)
{
    cJSON *obj;
    cJSON *elem;
    const char *expect_ci[] = {"a", "B", "c"};
    const char *expect_cs[] = {"B", "a", "c"};
    int i;

    obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "c", 3);
    cJSON_AddNumberToObject(obj, "a", 1);
    cJSON_AddNumberToObject(obj, "B", 2);

    cJSONUtils_SortObject(obj);
    i = 0;
    cJSON_ArrayForEach(elem, obj)
    {
        CHECK(i < 3);
        if (i < 3)
        {
            CHECK(strcmp(elem->string, expect_ci[i]) == 0);
        }
        i++;
    }
    CHECK(i == 3);
    cJSON_Delete(obj);

    obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "c", 3);
    cJSON_AddNumberToObject(obj, "a", 1);
    cJSON_AddNumberToObject(obj, "B", 2);
    cJSONUtils_SortObjectCaseSensitive(obj);
    i = 0;
    cJSON_ArrayForEach(elem, obj)
    {
        CHECK(i < 3);
        if (i < 3)
        {
            CHECK(strcmp(elem->string, expect_cs[i]) == 0);
        }
        i++;
    }
    CHECK(i == 3);
    cJSON_Delete(obj);
}

static void test_round_trip(void)
{
    const char *src =
        "{"
        "\"version\":1,"
        "\"enabled\":true,"
        "\"empty\":null,"
        "\"tags\":[\"gnu\",\"debian\",13],"
        "\"meta\":{\"os\":\"linux\",\"ptr\":\"/usr/bin\"}"
        "}";
    cJSON *a;
    cJSON *b;
    char *printed;

    a = parse_ok(src);
    if (a == NULL)
    {
        return;
    }
    printed = cJSON_PrintUnformatted(a);
    CHECK(printed != NULL);
    b = cJSON_Parse(printed);
    CHECK(b != NULL);
    CHECK(cJSON_Compare(a, b, 1));
    cJSON_free(printed);
    cJSON_Delete(a);
    cJSON_Delete(b);
}

int main(void)
{
    test_version();
    test_create_and_type_checks();
    test_create_arrays();
    test_print_variants();
    test_parse_literals_and_numbers();
    test_parse_object_array();
    test_parse_options();
    test_minify();
    test_add_helpers();
    test_array_object_edit();
    test_setters_duplicate_compare();
    test_references();
    test_default_allocator();
    test_custom_hooks();
    test_json_pointer();
    test_json_patch();
    test_merge_patch();
    test_sort_object();
    test_round_trip();

    printf("cJSON_gnul_test: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
