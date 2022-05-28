#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void generate_struct(FILE *stream, const char *array_type)
{
    fprintf(stream, "%c%s", toupper(*array_type), array_type + 1);
}

void generate_sig(FILE *stream, const char *return_type, const char *array_type, const char *function_name, va_list ap)
{
    fprintf(stream, "%s %s_%s(", return_type, array_type, function_name);
    generate_struct(stream, array_type);
    fprintf(stream, " *%s", array_type);

    bool previous_const = false;
    const char *arg = NULL;
    while ((arg = va_arg(ap, const char *))) {
        if (previous_const) {
            previous_const = false;
        } else {
            fputs(", ", stream);
        }

        if (strcmp(arg, "const")) {
            fprintf(stream, "%s %s", arg, va_arg(ap, const char *));
        } else {
            previous_const = true;
            fputs("const ", stream);
        }
    }

    fprintf(stream, ")");
}

void generate_decl(FILE *stream, const char *return_type, const char *array_type, const char *function_name, ...)
{
    va_list ap;
    va_start(ap, function_name);
    generate_sig(stream, return_type, array_type, function_name, ap);
    va_end(ap);

    fprintf(stream, ";\n");
}

#define generate_decl(...) generate_decl(__VA_ARGS__, NULL)

void generate_impl(FILE *stream, const char *body, const char *return_type, const char *array_type, const char *value_type, const char *function_name, ...)
{
    va_list ap;
    va_start(ap, function_name);
    generate_sig(stream, return_type, array_type, function_name, ap);
    va_end(ap);

    fprintf(stream, "\n{\n");
    while (*body) {
        const char ch = *body++;
        if (ch == '?') {
            fputs(array_type, stream);
        } else if (ch == '#') {
            fputs(value_type, stream);
        } else if (ch == '$') {
            for (size_t i = 0; i < strlen(value_type); ++i) {
                fputc(toupper(value_type[i]), stream);
            }
        } else {
            fputc(ch, stream);
        }
    }
    fprintf(stream, "}\n\n");
}

#define generate_impl(...) generate_impl(__VA_ARGS__, NULL)

void generate_guard(FILE *stream, const char *array_type, const char *suffix)
{
    for (size_t i = 0; i < strlen(array_type); ++i) {
        fputc(toupper(array_type[i]), stream);
    }
    fprintf(stream, "_%s\n", suffix);
}

#define IMPL_FREE \
    "    free(?->data);\n" \
    "    memset(?, 0, sizeof(*?));\n"

#define IMPL_RESERVE \
    "    count += ?->count;\n" \
    "    if (count > ?->capacity) {\n" \
    "        ?->capacity += DA_MINIMUM_CAPACITY;\n" \
    "        if (?->capacity < count) ?->capacity = count;\n" \
    "        ?->data = realloc(?->data, ?->capacity * sizeof(#));\n" \
    "        assert(?->data);\n" \
    "    }\n"

#define IMPL_PUSH \
    "    ?_reserve(?, 1);\n" \
    "    ?->data[?->count++] = value;\n"

#define IMPL_INSERT \
    "    ?_reserve(?, 1);\n" \
    "    memmove(?->data + index + 1, ?->data + index, (?->count - index) * sizeof(#));\n" \
    "    ?->data[index] = value;\n" \
    "    ?->count++;\n"

#define IMPL_POP \
    "    assert(?->count);\n" \
    "    return ?->data[--?->count];\n"

#define IMPL_PUSH_MULTI \
    "    ?_reserve(?, count);\n" \
    "    memcpy(?->data + ?->count, src, count * sizeof(#));\n" \
    "    ?->count += count;\n" \

#define IMPL_INSERT_MULTI \
    "    ?_reserve(?, count);\n" \
    "    memmove(?->data + index + count, ?->data + index, (?->count - index) * sizeof(#));\n" \
    "    memcpy(?->data + index, src, count * sizeof(#));\n" \
    "    ?->count += count;\n"

#define IMPL_POP_MULTI \
    "    if (count > ?->count) count = ?->count;\n" \
    "    if (dst) memcpy(dst, ?->data + ?->count - count, count * sizeof(#));\n" \
    "    ?->count -= count;\n" \
    "    return count;\n"

#define IMPL_DELETE \
    "    assert(?->count > index);\n" \
    "    const # value = ?->data[index];\n" \
    "    memmove(?->data + index, ?->data + index + 1, (?->count - index - 1) * sizeof(#));\n" \
    "    ?->count--;\n" \
    "    return value;\n" \

#define IMPL_REPLACE \
    "    assert(?->count > index);\n" \
    "    ?->data[index] = value;\n"

#define IMPL_DELETE_MULTI \
    "    if (index >= ?->count) return 0;\n" \
    "    if (index + count > ?->count) count = ?->count - index;\n" \
    "    if (dst) memcpy(dst, ?->data + index, count * sizeof(#));\n" \
    "    memmove(?->data + index, ?->data + index + count, (?->count - index - count) * sizeof(#));\n" \
    "    ?->count -= count;\n" \
    "    return count;\n"

#define IMPL_REPLACE_MULTI \
    "    if (index >= ?->count) return 0;\n" \
    "    if (index + count > ?->count) count = ?->count - index;\n" \
    "    if (dst) memcpy(dst, ?->data + index, count * sizeof(#));\n" \
    "    memcpy(?->data + index, src, count * sizeof(#));\n" \
    "    return count;\n"

#define IMPL_FIND \
    "    for (size_t i = index; i < ?->count; ++i) {\n" \
    "        if (!$_CMP(?->data + i, &pred, 1)) {\n" \
    "            return i;\n" \
    "        }\n" \
    "    }\n" \
    "    return -1;\n"

#define IMPL_FIND_MULTI \
    "    for (size_t i = index; i + count <= ?->count; ++i) {\n" \
    "        if (!$_CMP(?->data + i, pred, count)) {\n" \
    "            return i;\n" \
    "        }\n" \
    "    }\n" \
    "    return -1;\n"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: dag ARRAY_TYPE VALUE_TYPE\n");
        fprintf(stderr, "Error: wrong number of arguments: %d\n", argc);
        exit(1);
    }

    const char *array_type = argv[1];
    const char *value_type = argv[2];

    {
        fprintf(stdout, "#ifndef ");
        generate_guard(stdout, array_type, "H");
        fprintf(stdout, "#define ");
        generate_guard(stdout, array_type, "H\n");
    }

    {
        fprintf(stdout, "#include <assert.h>\n");
        fprintf(stdout, "#include <stdlib.h>\n");
        fprintf(stdout, "#include <string.h>\n\n");
    }

    {
        fprintf(stdout, "#ifndef DA_MINIMUM_CAPACITY\n");
        fprintf(stdout, "#define DA_MINIMUM_CAPACITY 128\n");
        fprintf(stdout, "#endif // DA_MINIMUM_CAPACITY\n\n");
    }

    {
        fprintf(stdout, "typedef struct {\n");
        fprintf(stdout, "    %s *data;\n", value_type);
        fprintf(stdout, "    size_t count;\n");
        fprintf(stdout, "    size_t capacity;\n");
        fprintf(stdout, "} ");
        generate_struct(stdout, array_type);
        fprintf(stdout, ";\n");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, "void", array_type, "free");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, "void", array_type, "push", value_type, "value");
        generate_decl(stdout, "void", array_type, "insert", value_type, "value", "size_t", "index");
        generate_decl(stdout, value_type, array_type, "pop");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, "void", array_type, "push_multi", "const", value_type, "*src", "size_t", "count");
        generate_decl(stdout, "void", array_type, "insert_multi", "const", value_type, "*src", "size_t", "count", "size_t", "index");
        generate_decl(stdout, "size_t", array_type, "pop_multi", value_type, "*dst", "size_t", "count");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, value_type, array_type, "delete", "size_t", "index");
        generate_decl(stdout, "void", array_type, "replace", "size_t", "index", value_type, "value");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, "size_t", array_type, "delete_multi", "size_t", "index", "size_t", "count", value_type, "*dst");
        generate_decl(stdout, "size_t", array_type, "replace_multi", "size_t", "index", "size_t", "count", value_type, "*dst", "const", value_type, "*src");
    }

    fprintf(stdout, "\n");
    {
        generate_decl(stdout, "long", array_type, "find", "size_t", "index", value_type, "pred");
        generate_decl(stdout, "long", array_type, "find_multi", "size_t", "index", "const", value_type, "*pred", "size_t", "count");
    }

    {
        fprintf(stdout, "\n#endif // ");
        generate_guard(stdout, array_type, "H");
    }

    {
        fprintf(stdout, "\n#ifdef ");
        generate_guard(stdout, array_type, "IMPLEMENTATION");
        fprintf(stdout, "#undef ");
        generate_guard(stdout, array_type, "IMPLEMENTATION\n");
    }

    {
        generate_impl(stdout, IMPL_RESERVE, "static void", array_type, value_type, "reserve", "size_t", "count");
        generate_impl(stdout, IMPL_FREE, "void", array_type, value_type, "free");
    }

    {
        fprintf(stdout, "#ifndef ");
        generate_guard(stdout, value_type, "CMP");

        fprintf(stdout, "#define ");
        for (size_t i = 0; i < strlen(value_type); ++i) {
            fputc(toupper(value_type[i]), stdout);
        }
        fprintf(stdout, "_CMP(a, b, len) (memcmp(a, b, (len) * sizeof(%s)))\n", value_type);

        fprintf(stdout, "#endif // ");
        generate_guard(stdout, value_type, "CMP\n");
    }


    {
        generate_impl(stdout, IMPL_PUSH, "void", array_type, value_type, "push", value_type, "value");
        generate_impl(stdout, IMPL_INSERT, "void", array_type, value_type, "insert", value_type, "value", "size_t", "index");
        generate_impl(stdout, IMPL_POP, value_type, array_type, value_type, "pop");
    }

    {
        generate_impl(stdout, IMPL_PUSH_MULTI, "void", array_type, value_type, "push_multi", "const", value_type, "*src", "size_t", "count");
        generate_impl(stdout, IMPL_INSERT_MULTI, "void", array_type, value_type, "insert_multi", "const", value_type, "*src", "size_t", "count", "size_t", "index");
        generate_impl(stdout, IMPL_POP_MULTI, "size_t", array_type, value_type, "pop_multi", value_type, "*dst", "size_t", "count");
    }

    {
        generate_impl(stdout, IMPL_DELETE, value_type, array_type, value_type, "delete", "size_t", "index");
        generate_impl(stdout, IMPL_REPLACE, "void", array_type, value_type, "replace", "size_t", "index", value_type, "value");
    }

    {
        generate_impl(stdout, IMPL_DELETE_MULTI, "size_t", array_type, value_type, "delete_multi", "size_t", "index", "size_t", "count", value_type, "*dst");
        generate_impl(stdout, IMPL_REPLACE_MULTI, "size_t", array_type, value_type, "replace_multi", "size_t", "index", "size_t", "count", value_type, "*dst", "const", value_type, "*src");
    }

    {
        generate_impl(stdout, IMPL_FIND, "long", array_type, value_type, "find", "size_t", "index", value_type, "pred");
        generate_impl(stdout, IMPL_FIND_MULTI, "long", array_type, value_type, "find_multi", "size_t", "index", "const", value_type, "*pred", "size_t", "count");
    }

    {
        fprintf(stdout, "#endif // ");
        generate_guard(stdout, array_type, "IMPLEMENTATION");
    }

    return 0;
}
