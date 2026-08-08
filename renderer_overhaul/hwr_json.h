#ifndef HWR_JSON_H
#define HWR_JSON_H

#include <stddef.h>

/* A small, strict JSON reader.
 *
 * It exists so the mod can be described by a file a person writes by hand and
 * every editor already understands, without taking on a dependency to do it.
 * What it reads is JSON and nothing more: objects, arrays, strings, numbers,
 * true, false and null. There is no writer, no streaming, and no extension to
 * the grammar -- a file this refuses is a file every other JSON tool refuses
 * too, which is the whole point of choosing a format that has tools.
 *
 * It knows nothing about what any particular key means. Which names matter and
 * what their values have to be is the business of the layer that walks the
 * result; this only says what the bytes are.
 *
 * The bounds below are the promise that a bad file costs at most itself: a
 * file larger than the cap is refused unread, the values in one are counted
 * against a ceiling, nesting is counted against another, and nothing here
 * recurses, so no file can spend the stack. Everything is built to one side
 * and handed over whole, so a caller that gets a refusal still has whatever it
 * had before.
 */

/* The longest file that will be read, matching the cap the settings file's own
 * keeper uses. A description of one game's lighting is a few kilobytes; a file
 * this size is not one of ours. */
#define HWR_JSON_MAX_BYTES ((size_t)1 << 20)
/* How many values one file may hold, all together and at any depth. */
#define HWR_JSON_MAX_NODES 8192u
/* How deeply one file may nest. Thirty-two is far past anything a person
 * writes and far short of anything that costs memory. */
#define HWR_JSON_MAX_DEPTH 32u
/* The longest single number, in characters. Well past every number that can be
 * told from its neighbour once read. */
#define HWR_JSON_MAX_NUMBER 63u
/* How many remarks about one file are kept to be repeated. A file that draws
 * more than this says so in one closing line rather than in a thousand. */
#define HWR_JSON_MAX_NOTES 8u

enum hwr_json_type {
    HWR_JSON_NONE = 0,
    HWR_JSON_OBJECT,
    HWR_JSON_ARRAY,
    HWR_JSON_STRING,
    HWR_JSON_NUMBER,
    HWR_JSON_BOOL,
    HWR_JSON_NULL
};

/* One value. Children hang off first_child and run along next_sibling, both
 * positions in the document's own array, and -1 for "no such value". The two
 * spans point into the document's copy of the file's bytes: name is what a
 * member of an object was called, text is the value itself for everything that
 * is not a container. Strings arrive with their escapes already undone and a
 * terminator written where their closing quote used to be, so a string's text
 * is a C string; a number or a literal is a span of the file as written and is
 * not terminated, which is what hwr_json_render is for. */
struct hwr_json_node {
    unsigned char type;
    /* Whether this value was written with a name in front of it. It is its own
     * answer rather than a length of nought, because "" is a name a file is
     * allowed to use and a value with no name at all is a different thing. */
    unsigned char named;
    int first_child;
    int next_sibling;
    size_t name_offset;
    size_t name_length;
    size_t text_offset;
    size_t text_length;
};

struct hwr_json_note {
    unsigned line;
    char text[144];
};

/* One file, read. Use the accessors below rather than the fields: the layout
 * is here so the accessors can be small, not because it is the interface. */
struct hwr_json_doc {
    char *text;
    size_t length;
    struct hwr_json_node *nodes;
    unsigned node_count;
    unsigned node_capacity;
    /* The one value the file holds, or -1 when nothing was read. */
    int root;
    /* What the file was called, for anything said about it. */
    char name[260];
    /* What stopped the read. A line of nought means the remark is about the
     * file as a whole rather than a place in it. Empty text means nothing
     * stopped it. */
    unsigned error_line;
    unsigned error_column;
    char error_text[160];
    /* Remarks that did not stop the read, and how many there were in total --
     * which can be more than were kept. */
    struct hwr_json_note notes[HWR_JSON_MAX_NOTES];
    unsigned note_count;
};

/* Reads bytes the caller holds. The document takes its own copy, so the
 * caller's buffer is its own again as soon as this returns. name is what the
 * file should be called in anything said about it.
 *
 * The document handed in must be one that has not been filled yet, or one that
 * has been released: reading into a document that is still holding one would
 * drop what it holds. Releasing a document twice is safe, and releasing one
 * that was refused is safe, so an unwinding caller can release without
 * checking.
 *
 * Returns 1 when the whole file was read, and 0 when it was not -- and a
 * refusal leaves nothing behind: no root, no values, nothing to walk. That is
 * deliberate. JSON has no line-level recovery, because one missing brace moves
 * every following line into a different section, so half a file read is worse
 * than none: it would look like an answer. */
int hwr_json_parse(struct hwr_json_doc *doc, const char *bytes, size_t length,
                   const char *name);
/* The same, reading a file from disc. A file that is not there, cannot be
 * read, or is larger than the cap is a refusal with the reason in it. */
int hwr_json_parse_file(struct hwr_json_doc *doc, const char *path);
/* Releases everything a document holds and leaves it empty and safe to release
 * again. */
void hwr_json_free(struct hwr_json_doc *doc);

/* Puts what the read had to say on the error stream, in the mod's voice, and
 * says it once: either the one thing that stopped it, or the remarks about a
 * file that was read anyway -- never both, because a file that was refused was
 * not read and remarks about its contents would only be noise. Says nothing
 * about a file read without remark. */
void hwr_json_report(const struct hwr_json_doc *doc);

/* The one value the file holds, or -1. */
int hwr_json_root(const struct hwr_json_doc *doc);
/* What a value is. Answers HWR_JSON_NONE for a position that is not one, so a
 * caller can ask about the answer to a lookup that missed without testing it
 * first. */
int hwr_json_type(const struct hwr_json_doc *doc, int node);

/* The member of an object with this name, or -1. Names are matched exactly,
 * case and all: unlike the settings file's keys, a name in here is data, and
 * two names differing only in case are two different names. */
int hwr_json_member(const struct hwr_json_doc *doc, int object,
                    const char *name);
/* The first value inside an object or an array, and the next one after a
 * value: iteration for both, and -1 at the end of either. Anything that is not
 * a container has no first value. */
int hwr_json_first(const struct hwr_json_doc *doc, int container);
int hwr_json_next(const struct hwr_json_doc *doc, int node);
/* How many values are directly inside a container. */
int hwr_json_count(const struct hwr_json_doc *doc, int container);
/* What a member of an object was called, or NULL for a value that is not one
 * (every element of an array, and the root). */
const char *hwr_json_name(const struct hwr_json_doc *doc, int node);

/* The three readings. Each answers 0 and leaves its destination alone when the
 * value is not of that kind, so a caller can ask and be told no rather than
 * having to test the type and then read it. */
int hwr_json_as_number(const struct hwr_json_doc *doc, int node, double *out);
int hwr_json_as_bool(const struct hwr_json_doc *doc, int node, int *out);
/* A string's text, or NULL for anything else. It is a C string and it lives
 * for as long as the document does. */
const char *hwr_json_as_text(const struct hwr_json_doc *doc, int node);

/* Any single value written out as text, which is what a caller hands to a
 * reader that takes the written form of a value -- the settings table's own
 * parser, for one. A string renders as its text with the escapes gone, a
 * number and a literal as they were written. Containers render as nothing.
 * Returns the number of characters written, not counting the terminator, and
 * writes a terminator whenever there is room for one. */
size_t hwr_json_render(const struct hwr_json_doc *doc, int node, char *out,
                       size_t out_size);

#endif
