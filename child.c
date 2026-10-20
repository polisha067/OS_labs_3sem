#define _POSIX_C_SOURCE 200809L

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static int is_vowel(wchar_t character)
{
    return wcschr(L"AEIOUaeiouАЕЁИОУЫЭЮЯаеёиоуыэюя", character) != NULL;
}

int main(void)
{
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        fprintf(stderr, "The C.UTF-8 locale is required.\n");
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    wint_t character;

    while ((character = fgetwc(stdin)) != WEOF) {
        if (character == L'\0') {
            fprintf(stderr, "Input must not contain NUL bytes.\n");
            result = EXIT_FAILURE;
            break;
        }
        if (!is_vowel((wchar_t)character) && fputwc(character, stdout) == WEOF) {
            perror("write output");
            result = EXIT_FAILURE;
            break;
        }
        if (character == L'\n' && fflush(stdout) == EOF) {
            perror("flush output");
            result = EXIT_FAILURE;
            break;
        }
    }
    if (ferror(stdin)) {
        perror("read input");
        result = EXIT_FAILURE;
    }
    if (fclose(stdout) == EOF) {
        perror("close output");
        result = EXIT_FAILURE;
    }
    return result;
}
