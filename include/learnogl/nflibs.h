#pragma once

// nf_memory_tracker.c

struct nfmt_Buffer {
    char *start;
    char *end;
};

void nfmt_init();
void nfmt_record_malloc(void *p, int size, const char *tag, const char *file, int line);
void nfmt_record_free(void *p);
struct nfmt_Buffer nfmt_read();

// nf_json_parser.c

struct nfjp_Settings {
    int unquoted_keys;
    int c_comments;
    int implicit_root_object;
    int optional_commas;
    int equals_for_colon;
    int python_multiline_strings;
};
const char *nfjp_parse(const char *s, struct nfcd_ConfigData **cdp);
const char *nfjp_parse_with_settings(const char *s, struct nfcd_ConfigData **cdp,
                                     struct nfjp_Settings *settings);

// nf_config_data.c

struct nfcd_ConfigData;

enum {
    NFCD_TYPE_NULL,
    NFCD_TYPE_FALSE,
    NFCD_TYPE_TRUE,
    NFCD_TYPE_NUMBER,
    NFCD_TYPE_STRING,
    NFCD_TYPE_ARRAY,
    NFCD_TYPE_OBJECT
};

#define NFCD_TYPE_MASK (0x7)
#define NFCD_TYPE_BITS (3)

typedef int nfcd_loc;
typedef void *(*nfcd_realloc)(void *ud, void *ptr, int osize, int nsize, const char *file, int line);

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size);
void nfcd_free(struct nfcd_ConfigData *cd);

// Represents a stored item in an object block.
struct nfcd_ObjectItem {
    nfcd_loc key;
    nfcd_loc value;
};

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd);
int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc);
struct nfcd_ObjectItem *nfcd_object_item(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc);
const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc);

int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc arr);
nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc arr, int i);

int nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc object);
nfcd_loc nfcd_object_keyloc(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
const char *nfcd_object_key(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key);

nfcd_loc nfcd_null();
nfcd_loc nfcd_false();
nfcd_loc nfcd_true();
nfcd_loc nfcd_add_number(struct nfcd_ConfigData **cd, double n);
nfcd_loc nfcd_add_string(struct nfcd_ConfigData **cd, const char *s);
nfcd_loc nfcd_add_array(struct nfcd_ConfigData **cd, int size);
nfcd_loc nfcd_add_object(struct nfcd_ConfigData **cd, int size);
void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc root);

void nfcd_push(struct nfcd_ConfigData **cd, nfcd_loc array, nfcd_loc item);
void nfcd_set(struct nfcd_ConfigData **cd, nfcd_loc object, const char *key, nfcd_loc value);
void nfcd_set_loc(struct nfcd_ConfigData **cd, nfcd_loc object, nfcd_loc key, nfcd_loc value);

nfcd_realloc nfcd_allocator(struct nfcd_ConfigData *cd, void **user_data);

// nf_string_table.c

#define NFST_STRING_TABLE_FULL (-1)

// @rk - Adding this. 
#define NFST_STRING_TABLE_DUMMY (-2)

// Structure representing a string table. The data for the table is stored
// directly after this header in memory and consists of a hash table
// followed by a string data block.
struct nfst_StringTable
{
    // The total size of the allocated data, including this header.
    int allocated_bytes;

    // The number of strings in the table.
    int count;

    // Does the hash table use 16 bit slots
    int uses_16_bit_hash_slots;

    // Total number of slots in the hash table.
    int num_hash_slots;

    // The current number of bytes used for string data.
    int string_bytes;
};

void nfst_init(struct nfst_StringTable *st, int bytes, int average_string_size);
void nfst_grow(struct nfst_StringTable *st, int bytes);
int nfst_pack(struct nfst_StringTable *st);
int nfst_to_symbol(struct nfst_StringTable *st, const char *s);
int nfst_to_symbol_const(const struct nfst_StringTable *st, const char *s);
const char *nfst_to_string(struct nfst_StringTable *, int symbol);
