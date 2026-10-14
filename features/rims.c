#include "features.h"
#include "support.h"

#define QUERY_CAPACITY 8192
#define QUERY_CALLBACK_SLOT 0xecf5d8

typedef void (*LoadQuery)(void *, const char *, void *);
typedef void *(*Allocate)(size_t);
typedef void (*Free)(void *);

typedef struct {
    size_t prefix_size, removed_size, source_size;
    const char *replacement;
    const char *order;
} QueryEdit;

static LoadQuery original_load;
static Allocate allocate;
static Free free_query;
static unsigned char callback_pointer[8];
static const unsigned char unused_slot[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_slot[] = {unused_slot, callback_pointer};

static const char category_prefix[] = "SELECT -1 AS id, WheelCategories.Id AS Category,";
static const char category_source[] =
    " FROM WheelCategories LEFT JOIN List_CarMake ON List_CarMake.Id = ";
static const char category_order[] = " ORDER BY DisplayOrder";
static const char extra_category[] =
    " UNION ALL SELECT -1, 5, 0, 5, (SELECT Name FROM UpgradeTypes WHERE "
    "PartEnum = 43), "
    "Description, 0, IconPath, ImagePath, '0', '0', '0' FROM WheelCategories "
    "WHERE Id = 2";
static const char rims_prefix[] = "SELECT PartTable.PartId AS id, PartTable.IsStock,";
static const char rims_filter[] = "AND 5 = WheelCategories.Id JOIN UpgradeTypes";
static const char all_rims_filter[] = "AND WheelCategories.IsStock = 0 JOIN UpgradeTypes";
static const char rims_order[] = " ORDER BY WheelAnnotations.DisplayOrder";
static const char all_rims_order[] =
    " ORDER BY WheelCategories.DisplayOrder, WheelAnnotations.DisplayOrder";

static int plan_query(const char *query, QueryEdit *edit) {
    if (!query) {
        return 0;
    }
    size_t length = 0;
    while (length < QUERY_CAPACITY && query[length]) {
        ++length;
    }
    if (length == QUERY_CAPACITY) {
        return 0;
    }
    *edit = (QueryEdit){.source_size = length};
    size_t prefix = sizeof(category_prefix) - 1;
    size_t order = sizeof(category_order) - 1;
    if (length >= prefix + order && memory_equal(query, category_prefix, prefix) &&
        memory_equal(query + length - order, category_order, order) &&
        text_find(query, length, category_source) && !text_find(query, length, " UNION ")) {
        edit->prefix_size = length - order;
        edit->replacement = extra_category;
        return 1;
    }
    prefix = sizeof(rims_prefix) - 1;
    order = sizeof(rims_order) - 1;
    const char *filter;
    if (length >= prefix + order && memory_equal(query, rims_prefix, prefix) &&
        memory_equal(query + length - order, rims_order, order) &&
        (filter = text_find(query, length - order, rims_filter))) {
        edit->prefix_size = (size_t)(filter - query);
        edit->removed_size = sizeof(rims_filter) - 1;
        edit->replacement = all_rims_filter;
        edit->order = all_rims_order;
        edit->source_size -= order;
        return 1;
    }
    return 0;
}

static size_t edit_query(const char *query, const QueryEdit *edit, char *output, size_t capacity) {
    size_t replacement = text_length(edit->replacement);
    size_t suffix = edit->source_size - edit->prefix_size - edit->removed_size;
    size_t order = edit->order ? text_length(edit->order) : 0;
    size_t length = edit->prefix_size + replacement + suffix + order;
    if (length >= capacity) {
        return 0;
    }
    memory_copy(output, query, edit->prefix_size);
    memory_copy(output + edit->prefix_size, edit->replacement, replacement);
    memory_copy(output + edit->prefix_size + replacement,
                query + edit->prefix_size + edit->removed_size, suffix);
    if (order) {
        memory_copy(output + length - order, edit->order, order);
    }
    output[length] = 0;
    return length;
}

static void load_rim_query(void *collection, const char *query, void *database) {
    QueryEdit edit;
    if (!plan_query(query, &edit)) {
        original_load(collection, query, database);
        return;
    }
    char *combined = allocate(QUERY_CAPACITY);
    if (!combined) {
        original_load(collection, query, database);
        return;
    }
    int changed = edit_query(query, &edit, combined, QUERY_CAPACITY) != 0;
    // The native loader copies the SQL before preparing the statement.
    original_load(collection, changed ? combined : query, database);
    free_query(combined);
}

PatchDefinition rims_hook_patch = {QUERY_CALLBACK_SLOT,
                                   0,
                                   8,
                                   GROUP_RIMS,
                                   "all_rims_query_callback",
                                   unused_slot,
                                   callback_pointer,
                                   callback_pointer,
                                   accepted_slot,
                                   2};

void rims_prepare_hook(unsigned char *image) {
    original_load = (LoadQuery)(image + 0x140c3f0);
    allocate = (Allocate)(image + 0x3229c0);
    free_query = (Free)(image + 0x31c7b0);
    uintptr_t callback = (uintptr_t)load_rim_query;
    memory_copy(callback_pointer, &callback, sizeof(callback));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestRimsHookAddress(void) {
    return (void *)load_rim_query;
}

__declspec(dllexport) size_t Fh2TestRimsQuery(const char *query, char *output, size_t capacity) {
    QueryEdit edit;
    return plan_query(query, &edit) ? edit_query(query, &edit, output, capacity) : 0;
}
#endif
