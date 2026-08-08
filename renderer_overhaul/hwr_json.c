/* A small, strict JSON reader. What it is for and what it promises is in
 * hwr_json.h; this is how it keeps the promises.
 *
 * Three of them are worth naming here because they shape the code rather than
 * decorate it:
 *
 * Nothing recurses. A file describes its own nesting, so a reader that
 * recursed over that nesting would let the file choose how much stack to
 * spend. The scan below walks with a stack of its own, thirty-two entries
 * deep and no deeper, and every container it opens is one entry.
 *
 * Values live in one flat array, not one allocation each. A value is a
 * position in that array, children hang off it by position, and the array
 * grows by doubling until it reaches the ceiling. The file's own bytes are
 * copied once, and the strings in it are unescaped where they already are --
 * undoing an escape never makes a string longer -- so the text of every string
 * is a stretch of that one copy rather than a string of its own.
 *
 * A refusal leaves nothing. Everything is built to one side and the caller
 * sees it only when the last byte has been read, so a file with a missing
 * brace at the bottom cannot leave the top of itself standing.
 */

#include "hwr_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a report is prefixed with, which is the prefix everything else the mod
 * says to a person carries. */
#define HWR_JSON_VOICE "[renderer_overhaul] "

struct hwr_json_frame {
    int node;
    /* The last value put inside this container, so the next one is appended
     * without walking what is already there. */
    int last_child;
    int is_object;
    /* Where the container was opened, so a file that ends inside one is
     * complained about at the brace that is missing its partner rather than at
     * the end of the file, which is nowhere near it. */
    unsigned line;
    unsigned column;
};

struct hwr_json_parser {
    struct hwr_json_doc *doc;
    char *text;
    size_t length;
    size_t pos;
    unsigned line;
    unsigned column;
    struct hwr_json_frame stack[HWR_JSON_MAX_DEPTH];
    unsigned depth;
};

/* ------------------------------------------------------- what is said ---- */

/* The one thing that stopped the read. The first caller wins: the scan stops
 * at the first refusal, and this makes sure that even a path that unwinds
 * through another failure cannot overwrite the reason with a later one. */
static int hwr_json_fail(struct hwr_json_parser *parser, unsigned line,
                         unsigned column, const char *format, ...)
{
    va_list args;

    if (parser->doc->error_text[0] != 0)
        return 0;
    va_start(args, format);
    vsnprintf(parser->doc->error_text, sizeof parser->doc->error_text, format,
              args);
    va_end(args);
    if (parser->doc->error_text[0] == 0)
        snprintf(parser->doc->error_text, sizeof parser->doc->error_text,
                 "the file could not be read");
    parser->doc->error_line = line;
    parser->doc->error_column = column;
    return 0;
}

/* Something worth saying about a file that is being read anyway. Counted
 * whether or not there is room to keep it, so the count is the truth even when
 * the list is not the whole of it. */
static void hwr_json_note(struct hwr_json_parser *parser, unsigned line,
                          const char *format, ...)
{
    struct hwr_json_doc *doc = parser->doc;
    va_list args;

    if (doc->note_count < HWR_JSON_MAX_NOTES) {
        struct hwr_json_note *note = &doc->notes[doc->note_count];

        note->line = line;
        va_start(args, format);
        vsnprintf(note->text, sizeof note->text, format, args);
        va_end(args);
    }
    doc->note_count++;
}

void hwr_json_report(const struct hwr_json_doc *doc)
{
    unsigned i;
    unsigned kept;

    if (doc == NULL)
        return;
    if (doc->error_text[0] != 0) {
        if (doc->error_line == 0)
            fprintf(stderr, HWR_JSON_VOICE "%s: %s\n", doc->name,
                    doc->error_text);
        else
            fprintf(stderr, HWR_JSON_VOICE "%s:%u:%u: %s\n", doc->name,
                    doc->error_line, doc->error_column, doc->error_text);
        return;
    }
    kept = doc->note_count < HWR_JSON_MAX_NOTES ? doc->note_count
                                                : HWR_JSON_MAX_NOTES;
    for (i = 0; i < kept; i++) {
        if (doc->notes[i].line == 0)
            fprintf(stderr, HWR_JSON_VOICE "%s: %s\n", doc->name,
                    doc->notes[i].text);
        else
            fprintf(stderr, HWR_JSON_VOICE "%s:%u: %s\n", doc->name,
                    doc->notes[i].line, doc->notes[i].text);
    }
    if (doc->note_count > kept)
        fprintf(stderr,
                HWR_JSON_VOICE "%s: and %u more of the same; the rest are not "
                               "listed\n",
                doc->name, doc->note_count - kept);
}

/* ------------------------------------------------------------ the scan ---- */

static int hwr_json_at_end(const struct hwr_json_parser *parser)
{
    return parser->pos >= parser->length;
}

static int hwr_json_peek(const struct hwr_json_parser *parser)
{
    if (parser->pos >= parser->length)
        return -1;
    return (unsigned char)parser->text[parser->pos];
}

/* One byte forward, keeping the place a person would name. Columns are counted
 * in bytes, which is what an editor's own column is for everything a
 * configuration file is likely to hold, and a character written in more than
 * one byte is counted as the bytes it takes rather than pretended away. */
static void hwr_json_advance(struct hwr_json_parser *parser)
{
    if (parser->pos >= parser->length)
        return;
    if (parser->text[parser->pos] == '\n') {
        parser->line++;
        parser->column = 1;
    } else {
        parser->column++;
    }
    parser->pos++;
}

static void hwr_json_skip_space(struct hwr_json_parser *parser)
{
    while (!hwr_json_at_end(parser)) {
        int c = hwr_json_peek(parser);

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            return;
        hwr_json_advance(parser);
    }
}

/* Room for one more value, doubling until the ceiling. */
static int hwr_json_new_node(struct hwr_json_parser *parser, int type)
{
    struct hwr_json_doc *doc = parser->doc;
    struct hwr_json_node *node;
    int index;

    if (doc->node_count >= HWR_JSON_MAX_NODES) {
        hwr_json_fail(parser, parser->line, parser->column,
                      "the file holds more values than can be read (the "
                      "limit is %u)",
                      HWR_JSON_MAX_NODES);
        return -1;
    }
    if (doc->node_count == doc->node_capacity) {
        unsigned capacity = doc->node_capacity == 0 ? 64u
                                                    : doc->node_capacity * 2u;
        struct hwr_json_node *grown;

        if (capacity > HWR_JSON_MAX_NODES)
            capacity = HWR_JSON_MAX_NODES;
        grown = realloc(doc->nodes, (size_t)capacity * sizeof *grown);
        if (grown == NULL) {
            hwr_json_fail(parser, 0, 0,
                          "there was not enough memory to read the file");
            return -1;
        }
        doc->nodes = grown;
        doc->node_capacity = capacity;
    }
    index = (int)doc->node_count++;
    node = &doc->nodes[index];
    node->type = (unsigned char)type;
    node->named = 0;
    node->first_child = -1;
    node->next_sibling = -1;
    node->name_offset = 0;
    node->name_length = 0;
    node->text_offset = 0;
    node->text_length = 0;
    return index;
}

static size_t hwr_json_encode_utf8(unsigned code, char *out)
{
    if (code < 0x80u) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800u) {
        out[0] = (char)(0xc0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3fu));
        return 2;
    }
    if (code < 0x10000u) {
        out[0] = (char)(0xe0u | (code >> 12));
        out[1] = (char)(0x80u | ((code >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (code & 0x3fu));
        return 3;
    }
    out[0] = (char)(0xf0u | (code >> 18));
    out[1] = (char)(0x80u | ((code >> 12) & 0x3fu));
    out[2] = (char)(0x80u | ((code >> 6) & 0x3fu));
    out[3] = (char)(0x80u | (code & 0x3fu));
    return 4;
}

/* The four digits of a character escape, already past the u. */
static int hwr_json_read_hex4(struct hwr_json_parser *parser, unsigned *out)
{
    unsigned value = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = hwr_json_peek(parser);

        if (c >= '0' && c <= '9')
            value = value * 16u + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            value = value * 16u + (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F')
            value = value * 16u + (unsigned)(c - 'A') + 10u;
        else
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a character escape needs four hexadecimal "
                                 "digits after its u");
        hwr_json_advance(parser);
    }
    *out = value;
    return 1;
}

/* A string, unescaped where it lies. The place written to never overtakes the
 * place read from, because undoing an escape always shortens the string and
 * copying a plain byte leaves the two together, so this cannot tread on bytes
 * it has not read yet. The terminator goes where the closing quote was, which
 * is a byte the scan has just finished with. */
static int hwr_json_scan_string(struct hwr_json_parser *parser, size_t *offset,
                                size_t *out_length)
{
    unsigned open_line = parser->line;
    unsigned open_column = parser->column;
    size_t write;

    hwr_json_advance(parser); /* the opening quote */
    write = parser->pos;
    *offset = write;
    for (;;) {
        int c = hwr_json_peek(parser);

        if (c < 0)
            return hwr_json_fail(parser, open_line, open_column,
                                 "this text is never closed; the file ends "
                                 "inside it");
        if (c == '"') {
            parser->text[write] = 0;
            *out_length = write - *offset;
            hwr_json_advance(parser);
            return 1;
        }
        if (c < 0x20)
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a line break or other control character "
                                 "inside text has to be written as an escape");
        if (c != '\\') {
            parser->text[write++] = (char)c;
            hwr_json_advance(parser);
            continue;
        }
        hwr_json_advance(parser); /* the backslash */
        c = hwr_json_peek(parser);
        switch (c) {
        case '"':
        case '\\':
        case '/':
            parser->text[write++] = (char)c;
            hwr_json_advance(parser);
            break;
        case 'b':
            parser->text[write++] = '\b';
            hwr_json_advance(parser);
            break;
        case 'f':
            parser->text[write++] = '\f';
            hwr_json_advance(parser);
            break;
        case 'n':
            parser->text[write++] = '\n';
            hwr_json_advance(parser);
            break;
        case 'r':
            parser->text[write++] = '\r';
            hwr_json_advance(parser);
            break;
        case 't':
            parser->text[write++] = '\t';
            hwr_json_advance(parser);
            break;
        case 'u': {
            unsigned code = 0;
            unsigned line = parser->line;
            unsigned column = parser->column;

            hwr_json_advance(parser); /* the u */
            if (!hwr_json_read_hex4(parser, &code))
                return 0;
            if (code == 0)
                return hwr_json_fail(parser, line, column,
                                     "an escape for the zero character cannot "
                                     "be part of text that is read as words");
            /* The two halves of a character written outside the first sixty-five
             * thousand. They only mean anything together, so a half on its own
             * is a mistake rather than a character. */
            if (code >= 0xd800u && code <= 0xdbffu) {
                unsigned low = 0;

                if (hwr_json_peek(parser) != '\\') {
                    return hwr_json_fail(parser, line, column,
                                         "this escape is half of a character "
                                         "and the other half is missing");
                }
                hwr_json_advance(parser);
                if (hwr_json_peek(parser) != 'u') {
                    return hwr_json_fail(parser, line, column,
                                         "this escape is half of a character "
                                         "and the other half is missing");
                }
                hwr_json_advance(parser);
                if (!hwr_json_read_hex4(parser, &low))
                    return 0;
                if (low < 0xdc00u || low > 0xdfffu)
                    return hwr_json_fail(parser, line, column,
                                         "this escape is half of a character "
                                         "and what follows is not the other "
                                         "half");
                code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
            } else if (code >= 0xdc00u && code <= 0xdfffu) {
                return hwr_json_fail(parser, line, column,
                                     "this escape is the second half of a "
                                     "character and the first half is "
                                     "missing");
            }
            write += hwr_json_encode_utf8(code, parser->text + write);
            break;
        }
        default:
            if (c < 0)
                return hwr_json_fail(parser, open_line, open_column,
                                     "this text is never closed; the file "
                                     "ends inside it");
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a backslash inside text has to be followed "
                                 "by one of \" \\ / b f n r t or u");
        }
    }
}

static int hwr_json_is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* A number, exactly as JSON writes one: an optional minus, then either a lone
 * zero or a digit that is not zero and the digits after it, then an optional
 * fraction, then an optional exponent. Everything else a person might type --
 * a leading plus, a leading zero, a bare dot, a trailing dot, a word for
 * infinity -- is a refusal, because a file that other JSON tools reject is not
 * a file this should quietly accept. */
static int hwr_json_scan_number(struct hwr_json_parser *parser, size_t *offset,
                                size_t *out_length)
{
    unsigned line = parser->line;
    unsigned column = parser->column;
    size_t start = parser->pos;

    if (hwr_json_peek(parser) == '-')
        hwr_json_advance(parser);
    if (hwr_json_peek(parser) == '0') {
        hwr_json_advance(parser);
        if (hwr_json_is_digit(hwr_json_peek(parser)))
            return hwr_json_fail(parser, line, column,
                                 "a number cannot start with a zero followed "
                                 "by more digits");
    } else if (hwr_json_is_digit(hwr_json_peek(parser))) {
        while (hwr_json_is_digit(hwr_json_peek(parser)))
            hwr_json_advance(parser);
    } else {
        return hwr_json_fail(parser, parser->line, parser->column,
                             "a digit was expected here");
    }
    if (hwr_json_peek(parser) == '.') {
        hwr_json_advance(parser);
        if (!hwr_json_is_digit(hwr_json_peek(parser)))
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a number needs at least one digit after its "
                                 "decimal point");
        while (hwr_json_is_digit(hwr_json_peek(parser)))
            hwr_json_advance(parser);
    }
    if (hwr_json_peek(parser) == 'e' || hwr_json_peek(parser) == 'E') {
        hwr_json_advance(parser);
        if (hwr_json_peek(parser) == '+' || hwr_json_peek(parser) == '-')
            hwr_json_advance(parser);
        if (!hwr_json_is_digit(hwr_json_peek(parser)))
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a number needs at least one digit after its "
                                 "exponent");
        while (hwr_json_is_digit(hwr_json_peek(parser)))
            hwr_json_advance(parser);
    }
    *offset = start;
    *out_length = parser->pos - start;
    if (*out_length > HWR_JSON_MAX_NUMBER)
        return hwr_json_fail(parser, line, column,
                             "this number is longer than %u characters, which "
                             "is longer than any number that can be told from "
                             "its neighbour",
                             HWR_JSON_MAX_NUMBER);
    return 1;
}

static int hwr_json_scan_word(struct hwr_json_parser *parser, const char *word,
                              size_t *offset, size_t *out_length)
{
    size_t start = parser->pos;
    size_t i;
    size_t word_length = strlen(word);

    for (i = 0; i < word_length; i++) {
        if (hwr_json_peek(parser) != (unsigned char)word[i])
            return hwr_json_fail(parser, parser->line, parser->column,
                                 "a value was expected here");
        hwr_json_advance(parser);
    }
    *offset = start;
    *out_length = word_length;
    return 1;
}

/* Puts a value inside the container on top of the stack. Where the container
 * is an object and the name is one it already holds, the earlier one is taken
 * out of the chain: a later line in a file a person edited is what they meant,
 * and keeping the earlier one would make an edited file behave as though it
 * had not been. It is said out loud, because a name given twice is nearly
 * always a mistake even when the answer is the obvious one. */
static void hwr_json_attach(struct hwr_json_parser *parser, int index,
                            unsigned line)
{
    struct hwr_json_doc *doc = parser->doc;
    struct hwr_json_frame *frame;
    struct hwr_json_node *node = &doc->nodes[index];

    if (parser->depth == 0) {
        doc->root = index;
        return;
    }
    frame = &parser->stack[parser->depth - 1];
    if (frame->is_object && node->named) {
        const char *name = doc->text + node->name_offset;
        int previous = -1;
        int child = doc->nodes[frame->node].first_child;

        while (child >= 0) {
            struct hwr_json_node *held = &doc->nodes[child];

            if (strcmp(doc->text + held->name_offset, name) == 0) {
                if (previous < 0)
                    doc->nodes[frame->node].first_child = held->next_sibling;
                else
                    doc->nodes[previous].next_sibling = held->next_sibling;
                if (frame->last_child == child)
                    frame->last_child = previous;
                hwr_json_note(parser, line,
                              "'%s' is given more than once in the same "
                              "place; the last one is the one that counts",
                              name);
                break;
            }
            previous = child;
            child = held->next_sibling;
        }
    }
    if (frame->last_child < 0)
        doc->nodes[frame->node].first_child = index;
    else
        doc->nodes[frame->last_child].next_sibling = index;
    frame->last_child = index;
}

/* The whole file, in one loop. The outer turn reads one value; the inner turn
 * closes as many containers as the file closes at that point and then decides
 * whether another value follows. */
static int hwr_json_scan(struct hwr_json_parser *parser)
{
    hwr_json_skip_space(parser);
    if (hwr_json_at_end(parser))
        return hwr_json_fail(parser, parser->line, parser->column,
                             "a value was expected here and the file ends");
    for (;;) {
        size_t name_offset = 0;
        size_t name_length = 0;
        int named = 0;
        unsigned line;
        int index;
        int c;

        if (parser->depth > 0 && parser->stack[parser->depth - 1].is_object) {
            if (hwr_json_peek(parser) != '"')
                return hwr_json_fail(parser, parser->line, parser->column,
                                     "a name in quotation marks was expected "
                                     "here");
            if (!hwr_json_scan_string(parser, &name_offset, &name_length))
                return 0;
            named = 1;
            hwr_json_skip_space(parser);
            if (hwr_json_peek(parser) != ':')
                return hwr_json_fail(parser, parser->line, parser->column,
                                     "a colon was expected after the name");
            hwr_json_advance(parser);
            hwr_json_skip_space(parser);
        }
        line = parser->line;
        c = hwr_json_peek(parser);
        switch (c) {
        case '{':
        case '[': {
            int is_object = c == '{';
            struct hwr_json_frame *frame;

            index = hwr_json_new_node(parser, is_object ? HWR_JSON_OBJECT
                                                        : HWR_JSON_ARRAY);
            if (index < 0)
                return 0;
            if (named) {
                parser->doc->nodes[index].named = 1;
                parser->doc->nodes[index].name_offset = name_offset;
                parser->doc->nodes[index].name_length = name_length;
            }
            hwr_json_attach(parser, index, line);
            if (parser->depth >= HWR_JSON_MAX_DEPTH)
                return hwr_json_fail(parser, parser->line, parser->column,
                                     "the file nests more than %u levels "
                                     "deep",
                                     HWR_JSON_MAX_DEPTH);
            frame = &parser->stack[parser->depth++];
            frame->node = index;
            frame->last_child = -1;
            frame->is_object = is_object;
            frame->line = parser->line;
            frame->column = parser->column;
            hwr_json_advance(parser);
            hwr_json_skip_space(parser);
            if (hwr_json_peek(parser) == (is_object ? '}' : ']')) {
                hwr_json_advance(parser);
                parser->depth--;
                break;
            }
            continue;
        }
        case '"': {
            size_t offset = 0;
            size_t length = 0;

            index = hwr_json_new_node(parser, HWR_JSON_STRING);
            if (index < 0)
                return 0;
            if (!hwr_json_scan_string(parser, &offset, &length))
                return 0;
            parser->doc->nodes[index].text_offset = offset;
            parser->doc->nodes[index].text_length = length;
            break;
        }
        case 't':
        case 'f':
        case 'n': {
            size_t offset = 0;
            size_t length = 0;
            const char *word = c == 't' ? "true" : (c == 'f' ? "false"
                                                             : "null");

            index = hwr_json_new_node(parser, c == 'n' ? HWR_JSON_NULL
                                                       : HWR_JSON_BOOL);
            if (index < 0)
                return 0;
            if (!hwr_json_scan_word(parser, word, &offset, &length))
                return 0;
            parser->doc->nodes[index].text_offset = offset;
            parser->doc->nodes[index].text_length = length;
            break;
        }
        default: {
            size_t offset = 0;
            size_t length = 0;

            if (c != '-' && !hwr_json_is_digit(c))
                return hwr_json_fail(parser, parser->line, parser->column,
                                     "a value was expected here");
            index = hwr_json_new_node(parser, HWR_JSON_NUMBER);
            if (index < 0)
                return 0;
            if (!hwr_json_scan_number(parser, &offset, &length))
                return 0;
            parser->doc->nodes[index].text_offset = offset;
            parser->doc->nodes[index].text_length = length;
            break;
        }
        }
        if (parser->doc->nodes[index].type != HWR_JSON_OBJECT &&
            parser->doc->nodes[index].type != HWR_JSON_ARRAY) {
            if (named) {
                parser->doc->nodes[index].named = 1;
                parser->doc->nodes[index].name_offset = name_offset;
                parser->doc->nodes[index].name_length = name_length;
            }
            hwr_json_attach(parser, index, line);
        }
        /* One value has been read. Close what the file closes here, and then
         * either another value follows or the file is done. */
        for (;;) {
            struct hwr_json_frame *frame;

            hwr_json_skip_space(parser);
            if (parser->depth == 0) {
                if (!hwr_json_at_end(parser))
                    return hwr_json_fail(parser, parser->line, parser->column,
                                         "a file holds one value, and this is "
                                         "past the end of it");
                return 1;
            }
            frame = &parser->stack[parser->depth - 1];
            c = hwr_json_peek(parser);
            if (c == ',') {
                hwr_json_advance(parser);
                hwr_json_skip_space(parser);
                c = hwr_json_peek(parser);
                if (c == '}' || c == ']')
                    return hwr_json_fail(parser, parser->line, parser->column,
                                         "there is a comma after the last "
                                         "value, which JSON does not allow");
                break;
            }
            if (c == (frame->is_object ? '}' : ']')) {
                hwr_json_advance(parser);
                parser->depth--;
                continue;
            }
            if (c < 0)
                return hwr_json_fail(parser, frame->line, frame->column,
                                     frame->is_object
                                         ? "this brace is never closed; the "
                                           "file ends inside it"
                                         : "this bracket is never closed; the "
                                           "file ends inside it");
            return hwr_json_fail(parser, parser->line, parser->column,
                                 frame->is_object
                                     ? "a comma or a closing brace was "
                                       "expected here"
                                     : "a comma or a closing bracket was "
                                       "expected here");
        }
    }
}

/* ------------------------------------------------------- reading a file --- */

void hwr_json_free(struct hwr_json_doc *doc)
{
    if (doc == NULL)
        return;
    free(doc->text);
    free(doc->nodes);
    doc->text = NULL;
    doc->nodes = NULL;
    doc->length = 0;
    doc->node_count = 0;
    doc->node_capacity = 0;
    doc->root = -1;
}

/* Everything a refused file leaves behind goes here, so there is one place
 * that decides what "nothing was read" means. What the refusal said stays: it
 * is the only thing the caller still wants. */
static void hwr_json_discard(struct hwr_json_doc *doc)
{
    free(doc->text);
    free(doc->nodes);
    doc->text = NULL;
    doc->nodes = NULL;
    doc->length = 0;
    doc->node_count = 0;
    doc->node_capacity = 0;
    doc->root = -1;
    doc->note_count = 0;
    memset(doc->notes, 0, sizeof doc->notes);
}

static void hwr_json_begin(struct hwr_json_doc *doc, const char *name)
{
    memset(doc, 0, sizeof *doc);
    doc->root = -1;
    snprintf(doc->name, sizeof doc->name, "%s",
             name != NULL ? name : "the configuration");
}

/* Takes the buffer over, whoever it came from. */
static int hwr_json_parse_owned(struct hwr_json_doc *doc, char *bytes,
                                size_t length)
{
    struct hwr_json_parser parser;

    doc->text = bytes;
    doc->length = length;
    memset(&parser, 0, sizeof parser);
    parser.doc = doc;
    parser.text = doc->text;
    parser.length = length;
    parser.line = 1;
    parser.column = 1;
    /* A mark some editors put at the front of a file to say it is written in
     * UTF-8. It is not part of the text and it is not part of the grammar, so
     * it is stepped over before anything else looks, and it is not counted as
     * a column because nothing shows it as one. */
    if (length >= 3 && (unsigned char)bytes[0] == 0xefu &&
        (unsigned char)bytes[1] == 0xbbu && (unsigned char)bytes[2] == 0xbfu)
        parser.pos = 3;
    if (!hwr_json_scan(&parser)) {
        hwr_json_discard(doc);
        return 0;
    }
    return 1;
}

int hwr_json_parse(struct hwr_json_doc *doc, const char *bytes, size_t length,
                   const char *name)
{
    char *copy;

    if (doc == NULL)
        return 0;
    hwr_json_begin(doc, name);
    if (bytes == NULL) {
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "there was nothing to read");
        return 0;
    }
    if (length > HWR_JSON_MAX_BYTES) {
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "this is larger than %lu bytes, which is larger than any "
                 "description of a game; nothing was read from it",
                 (unsigned long)HWR_JSON_MAX_BYTES);
        return 0;
    }
    if (length == 0) {
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text, "the file is empty");
        return 0;
    }
    copy = malloc(length + 1);
    if (copy == NULL) {
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "there was not enough memory to read the file");
        return 0;
    }
    memcpy(copy, bytes, length);
    copy[length] = 0;
    return hwr_json_parse_owned(doc, copy, length);
}

int hwr_json_parse_file(struct hwr_json_doc *doc, const char *path)
{
    char *buffer = NULL;
    size_t filled = 0;
    size_t capacity = 0;
    FILE *file;

    if (doc == NULL)
        return 0;
    hwr_json_begin(doc, path);
    if (path == NULL)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "the file could not be opened");
        return 0;
    }
    /* Read in pieces and stop at the cap rather than trusting a size the file
     * reports: a file that grows while it is being read, or that reports no
     * size at all, still cannot spend more than the cap allows. */
    for (;;) {
        size_t got;

        if (filled == capacity) {
            size_t grown = capacity == 0 ? 8192u : capacity * 2u;
            char *bigger;

            if (grown > HWR_JSON_MAX_BYTES + 1u)
                grown = HWR_JSON_MAX_BYTES + 1u;
            if (grown == capacity)
                break;
            bigger = realloc(buffer, grown);
            if (bigger == NULL) {
                free(buffer);
                fclose(file);
                doc->error_line = 0;
                snprintf(doc->error_text, sizeof doc->error_text,
                         "there was not enough memory to read the file");
                return 0;
            }
            buffer = bigger;
            capacity = grown;
        }
        got = fread(buffer + filled, 1, capacity - filled, file);
        filled += got;
        if (got == 0)
            break;
    }
    if (ferror(file)) {
        free(buffer);
        fclose(file);
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "the file could not be read to its end");
        return 0;
    }
    fclose(file);
    if (filled > HWR_JSON_MAX_BYTES) {
        free(buffer);
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text,
                 "this is larger than %lu bytes, which is larger than any "
                 "description of a game; nothing was read from it",
                 (unsigned long)HWR_JSON_MAX_BYTES);
        return 0;
    }
    if (filled == 0) {
        free(buffer);
        doc->error_line = 0;
        snprintf(doc->error_text, sizeof doc->error_text, "the file is empty");
        return 0;
    }
    /* One byte past the end for a terminator, which the growth above always
     * leaves room for. */
    if (filled == capacity) {
        char *bigger = realloc(buffer, capacity + 1u);

        if (bigger == NULL) {
            free(buffer);
            doc->error_line = 0;
            snprintf(doc->error_text, sizeof doc->error_text,
                     "there was not enough memory to read the file");
            return 0;
        }
        buffer = bigger;
    }
    buffer[filled] = 0;
    return hwr_json_parse_owned(doc, buffer, filled);
}

/* ---------------------------------------------------------- the reading --- */

static const struct hwr_json_node *hwr_json_at(const struct hwr_json_doc *doc,
                                               int node)
{
    if (doc == NULL || node < 0 || (unsigned)node >= doc->node_count)
        return NULL;
    return &doc->nodes[node];
}

int hwr_json_root(const struct hwr_json_doc *doc)
{
    if (doc == NULL)
        return -1;
    return doc->root;
}

int hwr_json_type(const struct hwr_json_doc *doc, int node)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);

    return held == NULL ? HWR_JSON_NONE : (int)held->type;
}

int hwr_json_first(const struct hwr_json_doc *doc, int container)
{
    const struct hwr_json_node *held = hwr_json_at(doc, container);

    if (held == NULL)
        return -1;
    if (held->type != HWR_JSON_OBJECT && held->type != HWR_JSON_ARRAY)
        return -1;
    return held->first_child;
}

int hwr_json_next(const struct hwr_json_doc *doc, int node)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);

    return held == NULL ? -1 : held->next_sibling;
}

int hwr_json_count(const struct hwr_json_doc *doc, int container)
{
    int child = hwr_json_first(doc, container);
    int count = 0;

    while (child >= 0) {
        count++;
        child = hwr_json_next(doc, child);
    }
    return count;
}

const char *hwr_json_name(const struct hwr_json_doc *doc, int node)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);

    if (held == NULL || !held->named)
        return NULL;
    return doc->text + held->name_offset;
}

int hwr_json_member(const struct hwr_json_doc *doc, int object,
                    const char *name)
{
    int child;

    if (name == NULL)
        return -1;
    if (hwr_json_type(doc, object) != HWR_JSON_OBJECT)
        return -1;
    for (child = hwr_json_first(doc, object); child >= 0;
         child = hwr_json_next(doc, child)) {
        const char *held = hwr_json_name(doc, child);

        if (held != NULL && strcmp(held, name) == 0)
            return child;
    }
    return -1;
}

int hwr_json_as_number(const struct hwr_json_doc *doc, int node, double *out)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);
    char digits[HWR_JSON_MAX_NUMBER + 1u];

    if (held == NULL || held->type != HWR_JSON_NUMBER)
        return 0;
    if (held->text_length > HWR_JSON_MAX_NUMBER)
        return 0;
    memcpy(digits, doc->text + held->text_offset, held->text_length);
    digits[held->text_length] = 0;
    if (out != NULL)
        *out = strtod(digits, NULL);
    return 1;
}

int hwr_json_as_bool(const struct hwr_json_doc *doc, int node, int *out)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);

    if (held == NULL || held->type != HWR_JSON_BOOL)
        return 0;
    if (out != NULL)
        *out = doc->text[held->text_offset] == 't';
    return 1;
}

const char *hwr_json_as_text(const struct hwr_json_doc *doc, int node)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);

    if (held == NULL || held->type != HWR_JSON_STRING)
        return NULL;
    return doc->text + held->text_offset;
}

size_t hwr_json_render(const struct hwr_json_doc *doc, int node, char *out,
                       size_t out_size)
{
    const struct hwr_json_node *held = hwr_json_at(doc, node);
    size_t length;

    if (out != NULL && out_size > 0)
        out[0] = 0;
    if (held == NULL || held->type == HWR_JSON_OBJECT ||
        held->type == HWR_JSON_ARRAY)
        return 0;
    length = held->text_length;
    if (out == NULL || out_size == 0)
        return length;
    if (length > out_size - 1u)
        length = out_size - 1u;
    memcpy(out, doc->text + held->text_offset, length);
    out[length] = 0;
    return length;
}
