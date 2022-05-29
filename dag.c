#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static char *value_name;
static char *array_name;
static char *guard_name;
static char *ident_name;

void generate_sig(FILE *stream, const char *return_type, const char *function_name, va_list ap)
{
    fprintf(stream, "%s %s_%s(%s *%s", return_type, ident_name, function_name, array_name, ident_name);

    const char *arg = NULL;
    bool previous_const = false;

    while ((arg = va_arg(ap, const char *))) {
        if (previous_const) {
            previous_const = false;
        } else {
            fprintf(stream, ", ");
        }

        if (strcmp(arg, "const")) {
            fprintf(stream, "%s %s", arg, va_arg(ap, const char *));
        } else {
            previous_const = true;
            fprintf(stream, "const ");
        }
    }

    fprintf(stream, ")");
}

void generate_decl(FILE *stream, const char *return_type, const char *function_name, ...)
{
    va_list ap;
    va_start(ap, function_name);
    generate_sig(stream, return_type, function_name, ap);
    va_end(ap);

    fprintf(stream, ";\n");
}

#define generate_decl(...) generate_decl(__VA_ARGS__, NULL)

void generate_impl(FILE *stream, const char *body, const char *return_type, const char *function_name, ...)
{
    va_list ap;
    va_start(ap, function_name);
    generate_sig(stream, return_type, function_name, ap);
    va_end(ap);

    fprintf(stream, "\n{\n");
    while (*body) {
        const char ch = *body++;
        if (ch == '$') {
            if (*body != '(') {
                fputc(ch, stream);
                continue;
            }

            body++;

            const char *end = strchr(body, ')');
            if (!end || end != body + 5) {
                fputs("$(", stream);
                continue;
            }

            if (!memcmp(body, "VALUE", 5)) {
                fputs(value_name, stream);
            } else if (!memcmp(body, "ARRAY", 5)) {
                fputs(array_name, stream);
            } else if (!memcmp(body, "GUARD", 5)) {
                fputs(guard_name, stream);
            } else if (!memcmp(body, "IDENT", 5)) {
                fputs(ident_name, stream);
            } else {
                fputs("$(", stream);
                continue;
            }

            body = end + 1;
        } else {
            fputc(ch, stream);
        }
    }
    fprintf(stream, "}\n\n");
}

#define generate_impl(...) generate_impl(__VA_ARGS__, NULL)

#define IMPL_FREE \
    "#ifdef $(VALUE)_free\n" \
    "    for (size_t i = 0; i < $(IDENT)->count; ++i) $(VALUE)_free($(IDENT)->data + i);\n" \
    "#endif\n\n" \
    "    free($(IDENT)->data);\n" \
    "    memset($(IDENT), 0, sizeof(*$(IDENT)));\n"

#define IMPL_RESERVE \
    "    count += $(IDENT)->count;\n" \
    "    if (count > $(IDENT)->capacity) {\n" \
    "        $(IDENT)->capacity += DA_MINIMUM_CAPACITY;\n" \
    "        if ($(IDENT)->capacity < count) $(IDENT)->capacity = count;\n" \
    "        $(IDENT)->data = realloc($(IDENT)->data, $(IDENT)->capacity * sizeof($(VALUE)));\n" \
    "        assert($(IDENT)->data);\n" \
    "    }\n"

#define IMPL_PUSH \
    "    $(IDENT)_reserve($(IDENT), 1);\n" \
    "    $(IDENT)->data[$(IDENT)->count++] = value;\n"

#define IMPL_INSERT \
    "    $(IDENT)_reserve($(IDENT), 1);\n" \
    "    memmove($(IDENT)->data + index + 1, $(IDENT)->data + index, ($(IDENT)->count - index) * sizeof($(VALUE)));\n" \
    "    $(IDENT)->data[index] = value;\n" \
    "    $(IDENT)->count++;\n"

#define IMPL_POP \
    "    assert($(IDENT)->count);\n" \
    "    return $(IDENT)->data[--$(IDENT)->count];\n"

#define IMPL_PUSH_MULTI \
    "    $(IDENT)_reserve($(IDENT), count);\n" \
    "    memcpy($(IDENT)->data + $(IDENT)->count, src, count * sizeof($(VALUE)));\n" \
    "    $(IDENT)->count += count;\n" \

#define IMPL_INSERT_MULTI \
    "    $(IDENT)_reserve($(IDENT), count);\n" \
    "    memmove($(IDENT)->data + index + count, $(IDENT)->data + index, ($(IDENT)->count - index) * sizeof($(VALUE)));\n" \
    "    memcpy($(IDENT)->data + index, src, count * sizeof($(VALUE)));\n" \
    "    $(IDENT)->count += count;\n"

#define IMPL_POP_MULTI \
    "    if (count > $(IDENT)->count) count = $(IDENT)->count;\n" \
    "    if (dst) memcpy(dst, $(IDENT)->data + $(IDENT)->count - count, count * sizeof($(VALUE)));\n" \
    "    $(IDENT)->count -= count;\n" \
    "    return count;\n"

#define IMPL_DELETE \
    "    assert($(IDENT)->count > index);\n" \
    "    const $(VALUE) value = $(IDENT)->data[index];\n" \
    "    memmove($(IDENT)->data + index, $(IDENT)->data + index + 1, ($(IDENT)->count - index - 1) * sizeof($(VALUE)));\n" \
    "    $(IDENT)->count--;\n" \
    "    return value;\n" \

#define IMPL_REPLACE \
    "    assert($(IDENT)->count > index);\n" \
    "    $(IDENT)->data[index] = value;\n"

#define IMPL_DELETE_MULTI \
    "    if (index >= $(IDENT)->count) return 0;\n" \
    "    if (index + count > $(IDENT)->count) count = $(IDENT)->count - index;\n" \
    "    if (dst) memcpy(dst, $(IDENT)->data + index, count * sizeof($(VALUE)));\n" \
    "    memmove($(IDENT)->data + index, $(IDENT)->data + index + count, ($(IDENT)->count - index - count) * sizeof($(VALUE)));\n" \
    "    $(IDENT)->count -= count;\n" \
    "    return count;\n"

#define IMPL_REPLACE_MULTI \
    "    if (index >= $(IDENT)->count) return 0;\n" \
    "    if (index + count > $(IDENT)->count) count = $(IDENT)->count - index;\n" \
    "    if (dst) memcpy(dst, $(IDENT)->data + index, count * sizeof($(VALUE)));\n" \
    "    memcpy($(IDENT)->data + index, src, count * sizeof($(VALUE)));\n" \
    "    return count;\n"

#define IMPL_FIND \
    "    for (size_t i = index; i < $(IDENT)->count; ++i) {\n" \
    "        if (!$(IDENT)_compare($(IDENT)->data + i, &pred, 1)) {\n" \
    "            return i;\n" \
    "        }\n" \
    "    }\n" \
    "    return -1;\n"

#define IMPL_FIND_MULTI \
    "    for (size_t i = index; i + count <= $(IDENT)->count; ++i) {\n" \
    "        if (!$(IDENT)_compare($(IDENT)->data + i, pred, count)) {\n" \
    "            return i;\n" \
    "        }\n" \
    "    }\n" \
    "    return -1;\n"

#define IMPL_SPLIT \
    "    $(ARRAY) rhs = {0};\n" \
    "    $(IDENT)_push_multi(&rhs, $(IDENT)->data + index, $(IDENT)->count - index);\n" \
    "    $(IDENT)->count = index;\n" \
    "    return rhs;\n"

char *stralloc(const char *data, size_t *size)
{
    const size_t len = strlen(data);

    char *memory = malloc(len + 1);
    memcpy(memory, data, len);
    memory[len] = '\0';

    *size = len;
    return memory;
}

char *nullalloc(const size_t size)
{
    char *memory = malloc(size + 1);
    memory[size] = '\0';
    return memory;
}

void usage(FILE *stream)
{
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  dag <array-name> <value-type> [FLAGS]\n");
    fprintf(stream, "\nFlags:\n");
    fprintf(stream, "  -i PATH  Include PATH\n");
    fprintf(stream, "  -o PATH  Save the header to PATH\n");
}

char *shift(int *argc, char ***argv, const char *expect)
{
    if (*argc == 0) {
        usage(stderr);
        fprintf(stderr, "\nError: expected %s\n", expect);
        exit(1);
    }

    char *value = **argv;
    *argv += 1;
    *argc -= 1;
    return value;
}

int main(int argc, char **argv)
{
    shift(&argc, &argv, "program name");

    size_t ident_size;
    ident_name = stralloc(shift(&argc, &argv, "array name"), &ident_size);

    size_t value_size;
    value_name = stralloc(shift(&argc, &argv, "value name"), &value_size);

    array_name = nullalloc(ident_size);
    guard_name = nullalloc(ident_size);

    for (size_t i = 0; i < ident_size; ++i) {
        const char ch = i ? ident_name[i] : toupper(ident_name[i]);
        array_name[i] = ch;
        guard_name[i] = toupper(ch);
    }

    const char **includes = malloc(argc * sizeof(char *));
    size_t includes_count = 0;

    char *output_path = NULL;
    bool output_owned = false;

    while (argc) {
        const char *flag = shift(&argc, &argv, "flag");

        if (!strcmp(flag, "-i")) {
            includes[includes_count++] = shift(&argc, &argv, "include path");
        } else if (!strcmp(flag, "-o")) {
            output_path = shift(&argc, &argv, "output path");
        } else {
            usage(stderr);
            fprintf(stderr, "\nError: invalid flag '%s'\n", flag);
            exit(1);
        }
    }

    if (output_path == NULL) {
        output_path = malloc(ident_size + 3);
        memcpy(output_path, ident_name, ident_size);
        strcpy(output_path + ident_size, ".h");

        output_owned = true;
    }

    FILE *stream = fopen(output_path, "w");
    if (!stream) {
        fprintf(stderr, "Error: could not write to file '%s': %s\n",
                output_path, strerror(errno));
        exit(1);
    }

    {
        fprintf(stream, "#ifndef %s_H\n", guard_name);
        fprintf(stream, "#define %s_H\n\n", guard_name);
    }

    {
        fprintf(stream, "#include <assert.h>\n");
        fprintf(stream, "#include <stdlib.h>\n");
        fprintf(stream, "#include <string.h>\n\n");

        for (size_t i = 0; i < includes_count; ++i) {
            if (*includes[i] == '<') {
                fprintf(stream, "#include %s\n", includes[i]);
            } else {
                fprintf(stream, "#include \"%s\"\n", includes[i]);
            }
        }

        if (includes_count) {
            fputc('\n', stream);
        }
    }

    {
        fprintf(stream, "#ifndef DA_MINIMUM_CAPACITY\n");
        fprintf(stream, "#define DA_MINIMUM_CAPACITY 128\n");
        fprintf(stream, "#endif // DA_MINIMUM_CAPACITY\n\n");
    }

    {
        fprintf(stream, "typedef struct {\n");
        fprintf(stream, "    %s *data;\n", value_name);
        fprintf(stream, "    size_t count;\n");
        fprintf(stream, "    size_t capacity;\n");
        fprintf(stream, "} %s;\n", array_name);
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, "void", "free");
        fprintf(stream, "#define %s_free %s_free\n", ident_name, ident_name);
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, "void", "push", value_name, "value");
        generate_decl(stream, "void", "insert", value_name, "value", "size_t", "index");
        generate_decl(stream, value_name, "pop");
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, "void", "push_multi", "const", value_name, "*src", "size_t", "count");
        generate_decl(stream, "void", "insert_multi", "const", value_name, "*src", "size_t", "count", "size_t", "index");
        generate_decl(stream, "size_t", "pop_multi", value_name, "*dst", "size_t", "count");
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, value_name, "delete", "size_t", "index");
        generate_decl(stream, "void", "replace", "size_t", "index", value_name, "value");
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, "size_t", "delete_multi", "size_t", "index", "size_t", "count", value_name, "*dst");
        generate_decl(stream, "size_t", "replace_multi", "size_t", "index", "size_t", "count", value_name, "*dst", "const", value_name, "*src");
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, "long", "find", "size_t", "index", value_name, "pred");
        generate_decl(stream, "long", "find_multi", "size_t", "index", "const", value_name, "*pred", "size_t", "count");
    }

    fprintf(stream, "\n");
    {
        generate_decl(stream, array_name, "split", "size_t", "index");
    }

    {
        fprintf(stream, "\n#endif // %s_H\n\n", guard_name);
    }

    {
        fprintf(stream, "#ifdef %s_IMPLEMENTATION\n", guard_name);
        fprintf(stream, "#undef %s_IMPLEMENTATION\n\n", guard_name);
    }

    {
        generate_impl(stream, IMPL_RESERVE, "static void", "reserve", "size_t", "count");
        generate_impl(stream, IMPL_FREE, "void", "free");
    }

    {
        fprintf(stream, "#ifndef %s_compare\n", ident_name);
        fprintf(stream, "#define %s_compare(a, b, len) (memcmp(a, b, (len) * sizeof(%s)))\n", ident_name, value_name);
        fprintf(stream, "#endif\n\n");
    }


    {
        generate_impl(stream, IMPL_PUSH, "void", "push", value_name, "value");
        generate_impl(stream, IMPL_INSERT, "void", "insert", value_name, "value", "size_t", "index");
        generate_impl(stream, IMPL_POP, value_name, "pop");
    }

    {
        generate_impl(stream, IMPL_PUSH_MULTI, "void", "push_multi", "const", value_name, "*src", "size_t", "count");
        generate_impl(stream, IMPL_INSERT_MULTI, "void", "insert_multi", "const", value_name, "*src", "size_t", "count", "size_t", "index");
        generate_impl(stream, IMPL_POP_MULTI, "size_t", "pop_multi", value_name, "*dst", "size_t", "count");
    }

    {
        generate_impl(stream, IMPL_DELETE, value_name, "delete", "size_t", "index");
        generate_impl(stream, IMPL_REPLACE, "void", "replace", "size_t", "index", value_name, "value");
    }

    {
        generate_impl(stream, IMPL_DELETE_MULTI, "size_t", "delete_multi", "size_t", "index", "size_t", "count", value_name, "*dst");
        generate_impl(stream, IMPL_REPLACE_MULTI, "size_t", "replace_multi", "size_t", "index", "size_t", "count", value_name, "*dst", "const", value_name, "*src");
    }

    {
        generate_impl(stream, IMPL_FIND, "long", "find", "size_t", "index", value_name, "pred");
        generate_impl(stream, IMPL_FIND_MULTI, "long", "find_multi", "size_t", "index", "const", value_name, "*pred", "size_t", "count");
    }

    {
        generate_impl(stream, IMPL_SPLIT, array_name, "split", "size_t", "index");
    }

    {
        fprintf(stream, "#endif // %s_IMPLEMENTATION\n", guard_name);
    }

    printf("Generated %s\n", output_path);

    fclose(stream);
    if (output_owned) {
        free(output_path);
    }

    free(value_name);
    free(array_name);
    free(guard_name);
    free(ident_name);
    free(includes);
    return 0;
}
